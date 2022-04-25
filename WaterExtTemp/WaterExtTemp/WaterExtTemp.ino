// Include the libraries we need
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>

#define NETCFG_FILE_NAME        "/net_config.txt"
#define LED_RED                 14
#define LED_GREEN               16
#define ONE_WIRE_BUS            4

#define LOOP_DELAY              1000 /* ms */
#define PUBLISH_INTERVAL        10000 /* ms */

#define MAX_TRIES_MQTT          20 /* After 20 retries, reconnect wifi & close valve */
#define MAX_TRIES_WIFI          20 /* After 20 retires, reset the board */

#define GW_ADDR           169,254,1,1
#define SUBNET_ADDR       255,255,0,0
#define IP_ADDR_TEMPLATE  "169.254.%d.%d"

#define MQTT_SERVER           169,254,1,1
#define MQTT_PORT             1883
#define MQTT_CLIENT_ID        "water-tank-temp"

#define MQTT_TOPIC_PUB1       "water-tank-temp/water"
#define MQTT_TOPIC_PUB2       "water-tank-temp/ext"

#define MQTT_TOPIC_PUB3       "water-tank-temp/state"
#define MQTT_TOPIC_PUB3_STR1  "connected"
#define MQTT_TOPIC_PUB3_STR2  "reset"

#define MQTT_TOPIC_SUB1       "button"
#define MQTT_TOPIC_SUB1_STR1  "reset"

#define PUB_QUEUE_DEPTH       4
#define CHAR_ARRAY_LEN        32

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

#define OFF 0
#define ON  1

struct mqtt_queue {
  char topic[CHAR_ARRAY_LEN];
  char str[CHAR_ARRAY_LEN];
  unsigned int len;
  boolean retained;
};

IPAddress ip_addr, gw(GW_ADDR), subnet(SUBNET_ADDR);
char ssid[CHAR_ARRAY_LEN], password[CHAR_ARRAY_LEN];

WiFiClient wifi_client;
PubSubClient client(wifi_client);

bool post_passed = false;
int mqtt_tries;
int wifi_tries;

struct mqtt_queue mqtt_queue[PUB_QUEUE_DEPTH];
unsigned char pub_cidx = 0, pub_pidx = 0;

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

/* address for water termometer */
DeviceAddress insideThermometer = {0x28, 0x67, 0x6F, 0xB5, 0x5C, 0x21, 0x01, 0xAD};
/* address for env termometer */
DeviceAddress outsideThermometer = {0x28, 0x60, 0x79, 0xD5, 0x5C, 0x21, 0x01, 0x58};
//DeviceAddress outsideThermometer = {0x28, 0x53, 0xBA, 0xE5, 0x0C, 0x00, 0x00, 0xE8};

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

void publish_msg(bool all)
{
  bool ret;
  SERIAL_PRINTF("publish_msg: cidx = %d, pidx = %d\n", pub_cidx, pub_pidx);
  do {
    if (pub_cidx != pub_pidx) {
      SERIAL_PRINTF("Publishing from index %d\n", pub_cidx);
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
  SERIAL_PRINTF("queue_publish: cidx = %d, pidx = %d\n", pub_cidx, pub_pidx);
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
  if (strcmp(topic, MQTT_TOPIC_SUB1) == 0) {
    if (strncmp((char *)payload, MQTT_TOPIC_SUB1_STR1, length) == 0) {
      SERIAL_PRINT("Resetting in 5s\n");
      /* Don't care things might crash here... */
      client.publish(MQTT_TOPIC_PUB3, (const uint8_t *)MQTT_TOPIC_PUB3_STR2, strlen(MQTT_TOPIC_PUB3_STR2), true);

      client.disconnect();
      delay(5000);
      ESP.restart();
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

  SERIAL_PRINT("**** IP = "); SERIAL_PRINT(ip_addr); SERIAL_PRINT(" ***\n");  
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

    if (tries == MAX_TRIES_MQTT && !client.connected()) {
      tries = 0;
      /* Force a reconnection. This will reset the board after some tries. */
      connect_to_wifi();
    }
  }

  client.subscribe(MQTT_TOPIC_SUB1);
  queue_publish(MQTT_TOPIC_PUB3, (const uint8_t*)MQTT_TOPIC_PUB3_STR1, strlen(MQTT_TOPIC_PUB3_STR1), true);
}

void assign_ip_addr()
{
  char str[CHAR_ARRAY_LEN];

  sprintf(str,IP_ADDR_TEMPLATE, (int)random(1,254), (int)random(1,254));
  ip_addr.fromString(str);
}

/*
 * The setup function. We only start the sensors here
 */
void setup(void)
{
    char str[CHAR_ARRAY_LEN],
       c, pos = 0, line = 0;
  File config_file;

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

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

  
  post_passed = true;
  // Start up the library
  sensors.begin();

  // report parasite power requirements
  SERIAL_PRINTF("Parasite power is: %s\n", sensors.isParasitePowerMode() ? "ON" : "OFF");
  
  SPIFFS.end();

  if (post_passed) {
      connect_to_wifi();
      connect_to_mqtt();
  }
}

/*
 * Main function, get and show the temperature
 */

void publishTemperature(const char *topic, DeviceAddress deviceAddress)
{
  char temp_pub[] = "-127.00";
  float tempC = sensors.getTempC(deviceAddress);
  int ret;

  if(tempC == DEVICE_DISCONNECTED_C) 
  {
    SERIAL_PRINTF("Failed to connect to device 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
      deviceAddress[0],deviceAddress[1],deviceAddress[2],deviceAddress[3],deviceAddress[4],deviceAddress[5]);
  } else {
    SERIAL_PRINTF("%.02f\n", tempC);
    sprintf(temp_pub,"%.02f",tempC);
  }
  ret = queue_publish(topic, (const uint8_t *)temp_pub, strlen(temp_pub), true);
  if (ret) {
    SERIAL_PRINTF("Failed to publish : %d\n", ret);
  }
}

void loop() {

  static uint8_t publish_interval = 0;

  if (!post_passed)
    goto sleep;

  if (!client.connected()) {      
      connect_to_mqtt();
  }

  sensors.requestTemperatures(); // Send the command to get temperatures

  if (publish_interval == PUBLISH_INTERVAL / LOOP_DELAY) {
    SERIAL_PRINTLN("Publishing temperatures");

    publishTemperature(MQTT_TOPIC_PUB1, insideThermometer);
    publishTemperature(MQTT_TOPIC_PUB2, outsideThermometer);
    publish_interval = 0;
  } else {
    publish_interval++;
  }

  client.loop();
  
  /* Check if there's something to send */
  publish_msg(true);
sleep:
  delay(LOOP_DELAY);
}
