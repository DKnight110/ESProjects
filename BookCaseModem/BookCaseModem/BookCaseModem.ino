  /*
    This sketch demonstrates how to set up a simple HTTP-like server.
    The server will set a GPIO pin depending on the request
      http://server_ip/gpio/0 will set the GPIO2 low,
      http://server_ip/gpio/1 will set the GPIO2 high
    server_ip is the IP address of the ESP8266 module, will be
    printed to Serial when the module is connected.
*/
#include <serial_comms.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>

#define ESP_AS_MODEM

#define NETCFG_FILE_NAME     "/net_config.txt"

#define MAX_TRIES_MQTT          20 /* After 20 retries, reconnect wifi & close valve */
#define MAX_TRIES_WIFI          20 /* After 20 retires, reset the board */

#define GW_ADDR           169,254,1,1
#define SUBNET_ADDR       255,255,0,0
#define IP_ADDR_TEMPLATE  "169.254.%d.%d"

#define MQTT_SERVER           169,254,1,1
#define MQTT_PORT             1883
#define MQTT_CLIENT_ID        "bookcase"

#define MQTT_TOPIC_SUB1       "bookcase/control"
#define MQTT_TOPIC_SUB1_STR1  "RESET"

#define MQTT_TOPIC_SUB2       "bookcase/ledstrip"
#define MQTT_TOPIC_SUB2_STR1  "SET_COLOR"
#define MQTT_TOPIC_SUB2_STR2  "SET_COLOR_BULK"
#define MQTT_TOPIC_SUB2_STR3  "SET_TIME"
#define MQTT_TOPIC_SUB2_STR4  "SET_TIME_BULK"
#define MQTT_TOPIC_SUB2_STR5  "SWITCH_PRGS"
#define MQTT_TOPIC_SUB2_STR6  "RESET_COLOR"

#define MQTT_TOPIC_SUB3       "bookcase/fan"
#define MQTT_TOPIC_SUB3_STR1  "ON"
#define MQTT_TOPIC_SUB3_STR2  "OFF"
#define MQTT_TOPIC_SUB3_STR3  "SET_PWM"

#define MQTT_TOPIC_PUB1       "bookcase/debug"
#define MQTT_TOPIC_PUB1_STR1  "RST"
#define MQTT_TOPIC_PUB1_STR2  "DBGPRINT"

#define MQTT_TOPIC_PUB2       "bookcase/temp"
#define MQTT_TOPIC_PUB2_STR0  "TEMP"

#define MQTT_TOPIC_PUB3       "bookcase/fan"
#define MQTT_TOPIC_PUB3_STR0  "SPEED"

#define PUB_QUEUE_DEPTH       4
#define CHAR_ARRAY_LEN  32

#define LED_GREEN             4

#define LOOP_DELAY            200 /* ms */

#define DEBUG_LEVEL   1

#ifndef ESP_AS_MODEM
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
#else
#define SERIAL_PRINT(x) send_log("%s", x)
#define SERIAL_PRINTLN(x) send_log("%s\n", x)
#define SERIAL_PRINTF(...)  send_log(__VA_ARGS__)
#endif

struct mqtt_queue {
  char topic[CHAR_ARRAY_LEN];
  char str[CHAR_ARRAY_LEN];
  unsigned int len;
  boolean retained;
};

IPAddress ip_addr, gw(GW_ADDR), subnet(SUBNET_ADDR);
char ssid[CHAR_ARRAY_LEN], password[CHAR_ARRAY_LEN];
char double_rx_buf[CMD_LEN*NUM_ENTRIES];

WiFiClient wifi_client;
PubSubClient client(wifi_client);

bool post_passed = false;
int mqtt_tries;
int wifi_tries;

struct mqtt_queue mqtt_queue[PUB_QUEUE_DEPTH];
unsigned char pub_cidx = 0, pub_pidx = 0;

void toggle_led(uint8_t pin, uint8_t times, uint16_t delta)
{
  int i, state = 0;
  int initial_state = digitalRead(pin);

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

  digitalWrite(pin, initial_state);
}

void modem_reset()
{
  client.disconnect();
  delay(5000);
  ESP.restart();
}

void put_char(unsigned char ch)
{
  Serial.write(ch);
}
void publish_msg(bool all)
{
  bool ret;
  DEBUG("publish_msg: cidx = %d, pidx = %d\n", pub_cidx, pub_pidx);
  do {
    if (pub_cidx != pub_pidx) {
      DEBUG("Publishing from index %d\n", pub_cidx);
      ret = client.publish((const char *)mqtt_queue[pub_cidx].topic,
                     (const uint8_t *)mqtt_queue[pub_cidx].str,
                     mqtt_queue[pub_cidx].len,
                     mqtt_queue[pub_cidx].retained);
      client.loop();
      if (!ret) {
        /* Failed to publish => conn dropped. leave it in queue */
        break;
      }
      pub_cidx = (pub_cidx + 1) % PUB_QUEUE_DEPTH;
    } else {
      break;
    }
  } while(all);
}

int queue_publish(const char *topic, const uint8_t *payload, unsigned int len, bool retained)
{
  DEBUG("queue_publish: cidx = %d, pidx = %d\n", pub_cidx, pub_pidx);
  if (((pub_pidx + 1) % PUB_QUEUE_DEPTH) == pub_cidx) {
    ERROR("Queue full, skipping send!");
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

  SERIAL_PRINTLN("Got message:");
  if (!strcmp(topic, MQTT_TOPIC_SUB1))
  {
    if (!strncmp((char *)payload, MQTT_TOPIC_SUB1_STR1, length))
    {
      client.disconnect();
      delay(1000);
      ESP.restart();
    }
  }

  if (!strcmp(topic, MQTT_TOPIC_SUB2))
  {
    if (!strncmp((char *)payload, MQTT_TOPIC_SUB2_STR1, length))
    {
      
    }
    else if (!strncmp((char *)payload, MQTT_TOPIC_SUB2_STR2, length))
    {
    }
    else if (!strncmp((char *)payload, MQTT_TOPIC_SUB2_STR3, length))
    {
    }
    else if (!strncmp((char *)payload, MQTT_TOPIC_SUB2_STR4, length))
    {
    }
    else if (!strncmp((char *)payload, MQTT_TOPIC_SUB2_STR5, length))
    {
    }
    else if (!strncmp((char *)payload, MQTT_TOPIC_SUB2_STR6, length))
    {
    }
    else
    {
      SERIAL_PRINTLN("Got unknown!");
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

void connect_to_wifi()
{
  int ret, tries = 0;

  assign_ip_addr();

  // Bring up the WiFi connection
  WiFi.mode(WIFI_STA);

  while ((ret = WiFi.config(ip_addr, gw, subnet)) == 0) {
    SERIAL_PRINTLN("Applying IP config failed, resetting");
    delay(500);
    ESP.restart();
  }
  
  SERIAL_PRINTF("**** %s ****\n**** %s ****\n", ssid, password);

  WiFi.begin(ssid, password);
  
  do {
    toggle_led(LED_GREEN, 1, 500);
    SERIAL_PRINT(".");
  } while (WiFi.status() != WL_CONNECTED && (tries++ < MAX_TRIES_WIFI));

  if (WiFi.status() != WL_CONNECTED) {
    SERIAL_PRINTLN("Failed to connect to network, resetting");
    delay(500);
    ESP.restart();
  }
  SERIAL_PRINTLN("");
  SERIAL_PRINTLN("WiFi connected");

  SERIAL_PRINT("**** IP = "); SERIAL_PRINT(ip_addr.toString().c_str()); SERIAL_PRINT(" ***\n");  
}

void connect_to_mqtt()
{
  IPAddress mqtt_broker(MQTT_SERVER);
  int tries = 0;

  client.setServer(mqtt_broker, MQTT_PORT);
  client.setCallback(callback);

  while (!client.connected()) {
    SERIAL_PRINTLN("Connecting to mqtt broker.....");
    if (client.connect(MQTT_CLIENT_ID)) {
      SERIAL_PRINTLN("mqtt broker connected");
    } else {
      SERIAL_PRINT("failed with state ");
      SERIAL_PRINTLN(client.state());

      toggle_led(LED_GREEN, 1, 1000);

      tries++;
    }

    if (tries == MAX_TRIES_MQTT) {
      tries = 0;
      
      /* Force a reconnection */
      connect_to_wifi();
    }
  }
}

void assign_ip_addr()
{
  char str[CHAR_ARRAY_LEN];

  sprintf(str,IP_ADDR_TEMPLATE, (int)random(1,254), (int)random(1,254));
  ip_addr.fromString(str);
}

void setup() {
  char str[CHAR_ARRAY_LEN],
       c, pos = 0, line = 0;
  File config_file;

  Serial.begin(115200, SERIAL_8N1);

  SPIFFS.begin();

  randomSeed(millis());
  
  config_file = SPIFFS.open(NETCFG_FILE_NAME, "r");
  if (!config_file) {
    SERIAL_PRINTF("Failed to open file %s\n", NETCFG_FILE_NAME);
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

  pinMode(LED_GREEN, OUTPUT);

  connect_to_wifi();
  connect_to_mqtt();

  client.subscribe(MQTT_TOPIC_SUB1);
  queue_publish(MQTT_TOPIC_PUB1, (const uint8_t*)MQTT_TOPIC_PUB1_STR1, strlen(MQTT_TOPIC_PUB1_STR1), true);

  post_passed = true;
}


void loop() {
  int ret;

  if (!post_passed) {
    SERIAL_PRINTLN("POST failed, sleeping");

    goto sleep;
  }

  if (!client.connected()) {      
      connect_to_mqtt();
  }

  client.loop();
  
  /* Check if there's something to send */
  /* We send everything: when a button is pressed => 2 publish cmds */
  publish_msg(true);

  while (Serial.available()) {
        char x = Serial.read();
        uart_rx(x);
  }

  if (serial_buf_pidx != serial_buf_cidx)
  {
    process_message(&double_rx_buf[CMD_LEN * serial_buf_cidx]);
    serial_buf_cidx = (serial_buf_cidx + 1) % NUM_ENTRIES;
  }

sleep:
  delay(LOOP_DELAY);
}

void publish_mqtt_fan_pwm(uint8_t len, uint8_t *cmd)
{
  char payload[64] = {0}, tmp[4];
  int i;
 
  for (i = 0; i < len; i+=2)
  {
    sprintf(tmp,"%d,", (cmd[i] << 8) | cmd[i + 1]);
    strcat(payload, tmp);
  }
  payload[strlen(payload) - 1] = 0;
  queue_publish(MQTT_TOPIC_PUB3, (const uint8_t *)payload, strlen(payload), true);
}

void publish_mqtt_temp(uint8_t len, uint8_t *cmd)
{
  char payload[64] = {0}, tmp[4];
  int i;

  for (i = 0; i < len; i++)
  {
    sprintf(tmp,"%d,", cmd[i]);
    strcat(payload, tmp);
  }

  payload[strlen(payload) - 1] = 0;
  queue_publish(MQTT_TOPIC_PUB2, (const uint8_t *)payload, strlen(payload), true);
}
