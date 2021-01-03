#include <FS.h>

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

#define CHAR_ARRAY_LEN            32
#define MAX_WIFI_CONNECT_RETRY    20

#define CFG_FILE_NAME     "/net_config.txt"

#define GW_ADDR           169,254,1,1
#define SUBNET_ADDR       255,255,0,0
#define IP_ADDR_TEMPLATE  "169.254.%d.%d"

#define MQTT_SERVER       169,254,1,1
#define MQTT_PORT         1883

#define MQTT_CLIENT_ID        "esp8266-pumpctrl-client"
#define MQTT_TOPIC_PUB1       "pumpctrl/button"
#define MQTT_TOPIC_PUB2       "pumpctrl/state"

#define MQTT_TOPIC_PUB1_STR1   "PRESS"
#define MQTT_TOPIC_PUB2_STR1   "LOW_BAT"
#define MQTT_TOPIC_PUB2_STR2   "HIGH_BAT"

#define LOW_BAT_PIN 4
#define LED_RED     5
#define LED_GREEN   14

#define DEBUG_LEVEL   0

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
static char good_to_go = 0;

void setup() {
  char ssid[CHAR_ARRAY_LEN], password[CHAR_ARRAY_LEN],
       str[CHAR_ARRAY_LEN], file_name[CHAR_ARRAY_LEN],
       c, pos = 0, line = 0;
  IPAddress ip_addr, gw(GW_ADDR), subnet(SUBNET_ADDR);
  File config_file;
 
  Serial.begin(115200);

  randomSeed(analogRead(16));

  SPIFFS.begin();

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

  WiFi.persistent(false);
  //WiFi.mode(WIFI_OFF);
  
  //  WiFi.forceSleepWake();

  sprintf(str,IP_ADDR_TEMPLATE, (int)random(1,254), (int)random(1,254));

  ip_addr.fromString(str);

  WiFi.config(ip_addr, gw, subnet);

  // Bring up the WiFi connection
  WiFi.mode(WIFI_STA);

  SERIAL_PRINTF("**** SSID = %s ****\n**** Pass = %s ****\n", ssid, password);

  WiFi.begin(ssid, password);

  good_to_go = 1;

  pinMode(LOW_BAT_PIN, INPUT);

  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, 0);

  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, 0);  
}

int publish_cmd_mqtt(bool low_bat)
{
  IPAddress mqtt_broker(MQTT_SERVER);
  WiFiClient wifi_client;
  PubSubClient client(wifi_client);

  SERIAL_PRINT("MQTT broker address: "); SERIAL_PRINTLN(mqtt_broker);

  client.setServer(mqtt_broker, MQTT_PORT);

  while (!client.connected()) {
   SERIAL_PRINTLN("Connecting to mqtt broker.....");
   if (client.connect(MQTT_CLIENT_ID)) {
       SERIAL_PRINTLN("mqtt broker connected");
   } else {
       SERIAL_PRINT("failed with state ");
       SERIAL_PRINTLN(client.state());
       return -2;
   }
  }

  if (low_bat == true)
  {
    client.publish(MQTT_TOPIC_PUB2, (const uint8_t*)MQTT_TOPIC_PUB2_STR1, strlen(MQTT_TOPIC_PUB2_STR1), true);
  } else {
    /* Publish button press */
    client.publish(MQTT_TOPIC_PUB1, (const uint8_t*)MQTT_TOPIC_PUB1_STR1, strlen(MQTT_TOPIC_PUB1_STR1), false);

    /* Publish batt state */
    client.publish(MQTT_TOPIC_PUB2, (const uint8_t*)MQTT_TOPIC_PUB2_STR2, strlen(MQTT_TOPIC_PUB2_STR2), true);
  }

  client.disconnect();

  return 0;
}

void toggle_led(uint8_t pin, uint8_t times, uint16_t delta)
{
  int i, state = 0;
  for (i = 0; i < times; i++)
  {
    if (state == 0)
    {
      digitalWrite(pin, 1);
      state = 1;
    } else {
      digitalWrite(pin, 0);
      state = 0;
    }
    delay(delta);
  }
}
void loop() {
  uint32_t count = 0;
  IPAddress ip_addr;
  bool low_bat;
  int ret;

  if (!good_to_go) {
    SERIAL_PRINTLN("No config, going to sleep!");
    goto deep_sleep;
  }

  do {
    delay(500);
    SERIAL_PRINT(".");
  } while ((WiFi.status() != WL_CONNECTED) && (++count < MAX_WIFI_CONNECT_RETRY));

  if (count == MAX_WIFI_CONNECT_RETRY) {
    SERIAL_PRINTLN("");
    SERIAL_PRINTLN("WiFi connection failed, going to sleep");

    toggle_led(LED_RED, 5, 500);
    
    goto deep_sleep;
  }

  SERIAL_PRINTLN("");
  SERIAL_PRINTLN("WiFi connected");

  ip_addr = WiFi.localIP();
  SERIAL_PRINT("**** IP = "); SERIAL_PRINT(ip_addr); SERIAL_PRINT(" ***\n");

  low_bat = (bool)(digitalRead(LOW_BAT_PIN) == HIGH);

  ret = publish_cmd_mqtt(low_bat);
  if (ret) {
    toggle_led(LED_RED, 3, 100);
  } else {
    if (low_bat) {
      digitalWrite(LED_RED, 1);
      toggle_led(LED_GREEN, 3, 300);
      digitalWrite(LED_RED, 0);
    } else {
      toggle_led(LED_GREEN, 3, 100);
    }
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();

  SERIAL_PRINT("Sleeping");

deep_sleep:
  ESP.deepSleep(0);

}
