#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>

#define POWER_PIN                 4
#define LOW_BAT_PIN               12
#define ONE_WIRE_BUS              5

#define LED_RED                   15 /* NC */
#define LED_GREEN                 14 /* NC */

#define CHAR_ARRAY_LEN            32
#define MAX_WIFI_CONNECT_RETRY    20

#define CFG_FILE_NAME     "/net_config.txt"

#define GW_ADDR           169,254,1,1
#define SUBNET_ADDR       255,255,0,0
#define IP_ADDR_TEMPLATE  "169.254.%d.%d"

#define MQTT_SERVER       169,254,1,1
#define MQTT_PORT         1883

#define MQTT_CLIENT_ID        "humidity_sensor1"
#define MQTT_TOPIC_PUB1       "humidity_sensor1/humidity"
#define MQTT_TOPIC_PUB2       "humidity_sensor1/temperature"
#define MQTT_TOPIC_PUB3       "humidity_sensor1/batt_state"

#define MQTT_TOPIC_PUB3_STR1   "LOW_BAT"
#define MQTT_TOPIC_PUB3_STR2   "HIGH_BAT"

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
static char good_to_go = 0;

const int AirValue = 781;   // indication from sensor when placed in air
const int WaterValue = 441;  // indication from sensor when placed in water

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

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

  // Start up the library
  sensors.begin();

  // report parasite power requirements
  SERIAL_PRINTF("Parasite power is: %s\n", sensors.isParasitePowerMode() ? "ON" : "OFF");

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

  pinMode(POWER_PIN, OUTPUT);
}

int publish_cmd_mqtt(bool low_bat, int humidity, float temp)
{
  IPAddress mqtt_broker(MQTT_SERVER);
  WiFiClient wifi_client;
  PubSubClient client(wifi_client);
  char humidity_str[] = "-100";
  char temp_str[]="-127.00";

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
    client.publish(MQTT_TOPIC_PUB3, (const uint8_t*)MQTT_TOPIC_PUB3_STR1, strlen(MQTT_TOPIC_PUB3_STR1), true);
  } else {
    /* Publish batt state */
    client.publish(MQTT_TOPIC_PUB3, (const uint8_t*)MQTT_TOPIC_PUB3_STR2, strlen(MQTT_TOPIC_PUB3_STR2), true);
  }

  sprintf(humidity_str,"%d", humidity);
  client.publish(MQTT_TOPIC_PUB1, (const uint8_t*)humidity_str, strlen(humidity_str), true);

  sprintf(temp_str,"%.02f",temp);
  client.publish(MQTT_TOPIC_PUB2, (const uint8_t*)temp_str, strlen(temp_str), true);

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
  int voltage, humidity;
  float tempC;
  
  delay(1000);
  if (!good_to_go) {
    SERIAL_PRINTLN("No config, going to sleep!");
    goto deep_sleep;
  }

  digitalWrite(POWER_PIN, 1);
  sensors.requestTemperatures();

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

  /* Read the humidity now */
  voltage = analogRead(A0);
  digitalWrite(POWER_PIN, 0);

  SERIAL_PRINTLN("");
  SERIAL_PRINTLN("WiFi connected");

  ip_addr = WiFi.localIP();
  SERIAL_PRINT("**** IP = "); SERIAL_PRINT(ip_addr); SERIAL_PRINT(" ***\n");

  low_bat = (bool)(digitalRead(LOW_BAT_PIN) == HIGH);

  humidity = map(voltage, AirValue, WaterValue, 0, 100);
  SERIAL_PRINTF("ADC: %d\tHumidity = %d\n", voltage, humidity);

  tempC = sensors.getTempCByIndex(0);
  // Check if reading was successful
  if(tempC == DEVICE_DISCONNECTED_C) 
  {
    tempC = -127.00;
  } 
  SERIAL_PRINTF("Temp: %.02f\n", tempC);
  ret = publish_cmd_mqtt(low_bat, humidity, tempC);

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
