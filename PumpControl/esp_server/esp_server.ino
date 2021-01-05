/*
    This sketch demonstrates how to set up a simple HTTP-like server.
    The server will set a GPIO pin depending on the request
      http://server_ip/gpio/0 will set the GPIO2 low,
      http://server_ip/gpio/1 will set the GPIO2 high
    server_ip is the IP address of the ESP8266 module, will be
    printed to Serial when the module is connected.
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>

#define CFG_FILE_NAME     "/net_config.txt"

#define PUSH_BUTTON             12
#define BUTTON_DEBOUNCE_LIMIT   5
#define LED_RED                 5
#define LED_GREEN               4

#define LOOP_DELAY            200 /* ms */
#define COOL_OFF_PERIOD       20 /* s */

#define GW_ADDR           169,254,1,1
#define SUBNET_ADDR       255,255,0,0
#define IP_ADDR_TEMPLATE  "169.254.%d.%d"

#define MQTT_SERVER           169,254,1,1
#define MQTT_PORT             1883
#define MQTT_CLIENT_ID        "esp8266-pumpctrl-relay"
#define MQTT_TOPIC_SUB1       "pumpctrl/button"
#define MQTT_TOPIC_SUB1_STR1  "PRESS"
#define MQTT_TOPIC_SUB1_STR2  "RESET"

#define MQTT_TOPIC_PUB1       "pumpctrl/status"
#define MQTT_TOPIC_PUB1_STR0  "UP"
#define MQTT_TOPIC_PUB1_STR1  "PUMP_ON"
#define MQTT_TOPIC_PUB1_STR2  "PUMP_OFF"
#define MQTT_TOPIC_PUB1_STR3  "RST"

#define PUB_QUEUE_DEPTH       4
#define CHAR_ARRAY_LEN  32

#define DEBUG_LEVEL   1

#if DEBUG_LEVEL == 0
#define SERIAL_PRINT(x)
#define SERIAL_PRINTLN(x)
#define SERIAL_PRINTF(x,...)
#elif DEBUG_LEVEL == 1
#define SERIAL_PRINT(x) Serial.print(x)
#define SERIAL_PRINTLN(x) Serial.println(x)
#define SERIAL_PRINTF(x, ...)  Serial.printf(x,  __VA_ARGS__)
#else
#error "You need to define a debug level"
#endif

struct mqtt_queue {
  char topic[CHAR_ARRAY_LEN];
  char str[CHAR_ARRAY_LEN];
  unsigned int len;
  boolean retained;
};

WiFiClient wifi_client;
PubSubClient client(wifi_client);

int relay_state = 0;
bool all_ok = false;
int last_button_state = LOW;
int debounce = 0;
uint32_t cool_off = 0;

struct mqtt_queue mqtt_queue[PUB_QUEUE_DEPTH];
unsigned char pub_cidx = 0, pub_pidx = 0;

void toggle_led(uint8_t pin, uint8_t times, uint16_t delta)
{
  int i, state = 0;

  for (i = 0; i < times * 2; i++)
  {
    if (state == 0)
    {
      digitalWrite(pin, 1);
      state = 1;
    } else {
      digitalWrite(pin, 0);
      state = 0;
    }
    delay(delta / 2);
  }
}

void publish_msg(bool all)
{
  do {
    if (pub_cidx != pub_pidx) {
      SERIAL_PRINTF("Publishing from index %d\n", pub_cidx);
      client.publish((const char *)mqtt_queue[pub_cidx].topic,
                     (const uint8_t *)mqtt_queue[pub_cidx].str,
                     mqtt_queue[pub_cidx].len,
                     mqtt_queue[pub_cidx].retained);
      client.loop();
      pub_cidx = (pub_cidx + 1) % PUB_QUEUE_DEPTH;
    } else {
      break;
    }
  } while(all);
}
int queue_publish(const char *topic, const uint8_t *payload, unsigned int len, bool retained)
{
  if (((pub_pidx + 1) % PUB_QUEUE_DEPTH) == pub_cidx) {
    SERIAL_PRINTLN("Queue full, skipping send!");
    return -1;
  }

  strcpy(mqtt_queue[pub_pidx].topic,topic);
  strncpy(mqtt_queue[pub_pidx].str, (const char *)payload, len);
  mqtt_queue[pub_pidx].len = len;
  mqtt_queue[pub_pidx].retained = retained;

  pub_pidx = (pub_pidx + 1) % PUB_QUEUE_DEPTH;

  return 0;
}
void callback(char *topic, byte *payload, unsigned int length) {
  int ret;

  if (strcmp(topic, MQTT_TOPIC_SUB1) == 0)
  {
    SERIAL_PRINTLN("Got message:");
    if (strncmp((char *)payload, MQTT_TOPIC_SUB1_STR1, length) == 0) {
      if (relay_state == 0) {
        SERIAL_PRINT("Turning pump ON\n");
        relay_state = 1;

        digitalWrite(LED_RED, 1);

        ret = queue_publish(MQTT_TOPIC_PUB1, (const uint8_t *)MQTT_TOPIC_PUB1_STR1, strlen(MQTT_TOPIC_PUB1_STR1), true);
        if (ret) {
          SERIAL_PRINTF("Failed to publish : %d\n", ret);
        }
      } else {
        SERIAL_PRINT("Turning pump OFF\n");
        relay_state = 0;
        
        digitalWrite(LED_RED, 0);

        ret = queue_publish(MQTT_TOPIC_PUB1, (const uint8_t *)MQTT_TOPIC_PUB1_STR2, strlen(MQTT_TOPIC_PUB1_STR2), true);
        if (ret) {
          SERIAL_PRINTF("Failed to publish : %d\n", ret);
        }
      }
    } else {
      if (strncmp((char *)payload, MQTT_TOPIC_SUB1_STR2, length) == 0) {
        SERIAL_PRINT("Resetting in 5s\n");
        client.publish(MQTT_TOPIC_PUB1, (const uint8_t *)MQTT_TOPIC_PUB1_STR3, strlen(MQTT_TOPIC_PUB1_STR3), true);

        client.disconnect();
        delay(5000);
        ESP.restart();
      }
    }
  } 
  
  SERIAL_PRINT("Message arrived in topic: ");
  SERIAL_PRINTLN(topic);
  SERIAL_PRINT("Message:");
  for (int i = 0; i < length; i++) {
    SERIAL_PRINT((char) payload[i]);
  }
  SERIAL_PRINTLN(" ");
  SERIAL_PRINTLN("-----------------------");

}

IPAddress ip_addr, gw(GW_ADDR), subnet(SUBNET_ADDR);
char ssid[CHAR_ARRAY_LEN], password[CHAR_ARRAY_LEN];

void connect_to_wifi()
{
  int ret;

  // Bring up the WiFi connection
  WiFi.mode(WIFI_STA);

  while ((ret = WiFi.config(ip_addr, gw, subnet)) == 0) {
    SERIAL_PRINTLN("Applying IP config failed, retrying");
    delay(500);
  }
  
  SERIAL_PRINTF("**** %s ****\n**** %s ****\n", ssid, password);

  WiFi.begin(ssid, password);
  
  do {
    toggle_led(LED_GREEN, 1, 500);
    SERIAL_PRINT(".");
  } while (WiFi.status() != WL_CONNECTED);

  SERIAL_PRINTLN("");
  SERIAL_PRINTLN("WiFi connected");

  SERIAL_PRINT("**** IP = "); SERIAL_PRINT(ip_addr); SERIAL_PRINT(" ***\n");  
}

void connect_to_mqtt()
{
  IPAddress mqtt_broker(MQTT_SERVER);

  client.setServer(mqtt_broker, MQTT_PORT);
  client.setCallback(callback);

  while (!client.connected()) {
    SERIAL_PRINTLN("Connecting to mqtt broker.....");
    if (client.connect(MQTT_CLIENT_ID)) {
      SERIAL_PRINTLN("mqtt broker connected");
    } else {
      SERIAL_PRINT("failed with state ");
      SERIAL_PRINTLN(client.state());

      toggle_led(LED_GREEN, 1, 300);
    }
  }

  client.subscribe(MQTT_TOPIC_SUB1);
  client.publish(MQTT_TOPIC_PUB1, (const uint8_t*)MQTT_TOPIC_PUB1_STR0, strlen(MQTT_TOPIC_PUB1_STR0), true);
}

void setup() {
  char str[CHAR_ARRAY_LEN], file_name[CHAR_ARRAY_LEN],
       c, pos = 0, line = 0;  
  File config_file;

  Serial.begin(115200);

  SPIFFS.begin();

  randomSeed(millis());
  
  config_file = SPIFFS.open(CFG_FILE_NAME, "r");
  if (!config_file) {
    SERIAL_PRINTF("Failed to open file %s\n", file_name);
    return;
  }

  while(config_file.position() < config_file.size()) {
    c = config_file.read();
    if (c != '\n') {
      str[pos++] = c;
    } else {
      str[pos] = '\0';
      switch (line) {
        case 0:
          strcpy(ssid, str);
          break;
        case 1:
          strcpy(password, str);
          break;
        default:
          break;
      }
      pos = 0;
      line++;
    }
  }
  config_file.close();
  SPIFFS.end();

  sprintf(str,IP_ADDR_TEMPLATE, (int)random(1,254), (int)random(1,254));

  ip_addr.fromString(str);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(PUSH_BUTTON, INPUT_PULLUP);

  connect_to_wifi();
  connect_to_mqtt();

  digitalWrite(LED_RED, 0);
  digitalWrite(LED_GREEN, 1);
}

void loop() {
  if (!client.connected())
  {
      digitalWrite(LED_GREEN, 0);
      connect_to_mqtt();
      digitalWrite(LED_GREEN, 1);
  }

  client.loop();

  if (cool_off == 0) {
    if (digitalRead(PUSH_BUTTON) == HIGH) {
      debounce++;
    } else {
      debounce = 0;
    }
  } else {
    cool_off--;
  }

  if (debounce >= BUTTON_DEBOUNCE_LIMIT) {
    SERIAL_PRINTLN("Button pressed");
    callback(MQTT_TOPIC_SUB1, (byte *)MQTT_TOPIC_SUB1_STR1, strlen(MQTT_TOPIC_SUB1_STR1));

    debounce = 0;
    cool_off = COOL_OFF_PERIOD * 1000 / LOOP_DELAY;
  }

  /* Check if there's something to send */
  publish_msg(false);

  delay(LOOP_DELAY);
}
