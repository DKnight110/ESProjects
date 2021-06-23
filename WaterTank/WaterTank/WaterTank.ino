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

#define NETCFG_FILE_NAME     "/net_config.txt"
#define CFG_FILE_NAME     "/limits.bin"

#define SAFE_LVL_BTN            3
#define PUSH_BUTTON             12
#define LED_RED                 14
#define LED_GREEN               16

#define RELAY_ON_PIN            4
#define RELAY_OFF_PIN           5

#define TRIG_ECHO_PIN           13

//#define LOOP_DELAY            200 /* ms */
#define LOOP_DELAY              1000 /* ms */
//#define BUTTON_DEBOUNCE_LIMIT   5
#define BUTTON_DEBOUNCE_LIMIT   2
#define COOL_OFF_PERIOD         20 /* s */
#define VALVE_MOVE_TIME_MS      20000 /* ms */
#define MAX_SAFE_MEASUREMENTS   20
#define MAX_LEVEL_WARNINGS      10
#define WATER_PUBLISH_INTERVAL  5000 /* ms */

#define MAX_TRIES_MQTT          20 /* After 20 retries, reconnect wifi & close valve */
#define MAX_TRIES_WIFI          20 /* After 20 retires, reset the board */

#define GW_ADDR           169,254,1,1
#define SUBNET_ADDR       255,255,0,0
#define IP_ADDR_TEMPLATE  "169.254.%d.%d"

#define MQTT_SERVER           169,254,1,1
#define MQTT_PORT             1883
#define MQTT_CLIENT_ID        "water-tank"

#define MQTT_TOPIC_SUB1       "valve/button"
#define MQTT_TOPIC_SUB1_STR1  "ON"
#define MQTT_TOPIC_SUB1_STR2  "OFF"
#define MQTT_TOPIC_SUB1_STR3  "RST"

#define MQTT_TOPIC_PUB2       "water/level"

#define MQTT_TOPIC_PUB1       "valve/state"
#define MQTT_TOPIC_PUB1_STR0  "UP"
#define MQTT_TOPIC_PUB1_STR1  "WATER_ON"
#define MQTT_TOPIC_PUB1_STR2  "WATER_OFF"
#define MQTT_TOPIC_PUB1_STR3  "RESET"

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
char current_state = OFF;
char current_action = 0xFF;
short water_safe_limit = 0;
char water_warning_level = 0;
char water_interval = 0;
int debounce = 0;
uint32_t cool_off = 0;
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
  int ret;

  if (strcmp(topic, MQTT_TOPIC_SUB1) == 0)
  {
    SERIAL_PRINTLN("Got message:");
    if (strncmp((char *)payload, MQTT_TOPIC_SUB1_STR1, length) == 0) {
      if (current_action == 0xFF) {
        current_action = 1;
        SERIAL_PRINTLN("Opening valve\n");
      } else {
        SERIAL_PRINTF("Can't open valve: action = %d\n", current_action);
      }
    } else {
      if (strncmp((char *)payload, MQTT_TOPIC_SUB1_STR2, length) == 0) {
        if (current_action == 0xFF) {
          current_action = 0;
          SERIAL_PRINTLN("Closing valve\n");
        } else {
          SERIAL_PRINTF("Can't close valve: action = %d\n", current_action);
        }   
      } else {
        if (strncmp((char *)payload, MQTT_TOPIC_SUB1_STR3, length) == 0) {
          SERIAL_PRINT("Resetting in 5s\n");
          /* Don't care things might crash here... */
          client.publish(MQTT_TOPIC_PUB1, (const uint8_t *)MQTT_TOPIC_PUB1_STR3, strlen(MQTT_TOPIC_PUB1_STR3), true);

          client.disconnect();
          delay(5000);
          ESP.restart();
        }
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

void connect_to_wifi()
{
  int ret, tries;

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
  int tries;

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
      /* Things have gone awry: 
       * no connection to MQTT => goto safe state = valve closed 
       */
      digitalWrite(LED_RED, 0);
      water_valve_change_state(OFF);
      digitalWrite(LED_GREEN, 1);
      /* Try and publish. Don't check the ret code, you can't do much anyway */
      queue_publish(MQTT_TOPIC_PUB1, (const uint8_t *)MQTT_TOPIC_PUB1_STR2, strlen(MQTT_TOPIC_PUB1_STR2), true);

      tries = 0;
      /* Force a reconnection */
      connect_to_wifi();
    }
  }

  client.subscribe(MQTT_TOPIC_SUB1);
  queue_publish(MQTT_TOPIC_PUB1, (const uint8_t*)MQTT_TOPIC_PUB1_STR0, strlen(MQTT_TOPIC_PUB1_STR0), true);
}

void assign_ip_addr()
{
  char str[CHAR_ARRAY_LEN];

  sprintf(str,IP_ADDR_TEMPLATE, (int)random(1,254), (int)random(1,254));
  ip_addr.fromString(str);
}

void save_settings(short water_level)
{
  File config_file;
  
  SPIFFS.begin();
  config_file = SPIFFS.open(CFG_FILE_NAME, "w");
  if (!config_file) {
    SERIAL_PRINTF("Failed to open file %s\n", CFG_FILE_NAME);
    return;
  }

  config_file.write(water_level >> 8);
  config_file.write(water_level & 0xFF);

  config_file.close();
  SPIFFS.end();
}

void setup() {
  char str[CHAR_ARRAY_LEN],
       c, pos = 0, line = 0;
  short avg_water_height = 0;
  File config_file;
  int i;

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

  SPIFFS.begin();

  randomSeed(millis());
  
  config_file = SPIFFS.open(NETCFG_FILE_NAME, "r");
  if (!config_file) {
    SERIAL_PRINTF("Failed to open file %s\n", CFG_FILE_NAME);
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

  config_file = SPIFFS.open(CFG_FILE_NAME, "r");
  if (!config_file) {
    SERIAL_PRINTF("Failed to open file %s\n", CFG_FILE_NAME);
    return;
  }

  water_safe_limit = (config_file.read() << 8) | config_file.read();

  SERIAL_PRINTF("Recovered settings: water limit %d\n",
                water_safe_limit);

  config_file.close();

  SPIFFS.end();

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(PUSH_BUTTON, INPUT);
  pinMode(SAFE_LVL_BTN, INPUT);
  pinMode(RELAY_ON_PIN, OUTPUT);
  pinMode(RELAY_OFF_PIN, OUTPUT);

  /* If the set "safe distance" button is pressed, do
   * the calibration => this can be done only after (P)OR */
  if (digitalRead(SAFE_LVL_BTN) == HIGH) {
    SERIAL_PRINTLN("Setting water safe level");

    for (i = 0; i < MAX_SAFE_MEASUREMENTS; i++) {
      avg_water_height += get_water_height();
      toggle_led(LED_RED, 1, 250);
    }
    avg_water_height = avg_water_height / MAX_SAFE_MEASUREMENTS;

    SERIAL_PRINTF("Saving settings: water safe level %d",
                  avg_water_height);

    save_settings(avg_water_height);

    SERIAL_PRINTLN("Saved settings, now resetting in 5s");
    
    delay(5000);

    ESP.restart();
  }

  if (water_safe_limit != 0) {
    post_passed = true;

    /* Let's make sure we're in a proper state (i.e. closed)
     * We first move all the way to OPEN, then CLOSED
     */
     water_valve_change_state(ON);
     delay(1000);
     water_valve_change_state(OFF);
  }

  if (post_passed) {
      connect_to_wifi();
      connect_to_mqtt();
  }

  /* 
   *  Let the user know the valve is OFF
   * This is done here since connect to mqtt publishes
   * and I want the last status to be this one
   */
  queue_publish(MQTT_TOPIC_PUB1, (const uint8_t *)MQTT_TOPIC_PUB1_STR2, strlen(MQTT_TOPIC_PUB1_STR2), true);
}

long get_water_height()
{
  long height = 0;
  long duration;

  pinMode(TRIG_ECHO_PIN, OUTPUT);

  digitalWrite(TRIG_ECHO_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(TRIG_ECHO_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_ECHO_PIN, LOW);
 
  // Read the signal from the sensor: a HIGH pulse whose
  // duration is the time (in microseconds) from the sending
  // of the ping to the reception of its echo off of an object.
  pinMode(TRIG_ECHO_PIN, INPUT);
  duration = pulseIn(TRIG_ECHO_PIN, HIGH);
 
  // Convert the time into a distance
  height = (duration/2) / 29.1;     // Divide by 29.1 or multiply by 0.0343

  SERIAL_PRINTF("Water height : %d\n", height);
  return height;
}

void water_valve_control(char pin_led, char relay_pin)
{
  int i;

  SERIAL_PRINTF("Setting %d pin to 1\n", relay_pin);
  digitalWrite(relay_pin, 1);

  for (i = 0; i < VALVE_MOVE_TIME_MS / 1000; i++)
  {
    if (client.connected()) {
      client.loop();
    }
    toggle_led(pin_led, 1, 1000);
  }

  digitalWrite(relay_pin, 0);
  SERIAL_PRINTF("Setting %d pin to 0\n", relay_pin);
}

void water_valve_change_state(char state)
{
  if (state == OFF) {
    digitalWrite(LED_RED, 0);
    water_valve_control(LED_GREEN, RELAY_OFF_PIN);
    /* Let the user know the valve is closed */
    digitalWrite(LED_GREEN,1);
  } else {
    digitalWrite(LED_GREEN, 0);
    water_valve_control(LED_RED, RELAY_ON_PIN);
    digitalWrite(LED_RED,1);
  }
    current_state = state;
}

void loop() {
  char water_level_str[7] = {0, 0, 0, 0, 0, 0, 0};
  short cur_water_level;
  int ret;

  /* If water check level failed => don't do anything */
  if (!post_passed) {
    SERIAL_PRINTLN("POST failed, sleeping");

    toggle_led(LED_RED, 10, 1000);

    goto sleep;
  }

  if (!client.connected()) {      
      connect_to_mqtt();
  }

  /* We read water level every LOOP_INTERVAL */
  cur_water_level = get_water_height();
  if (current_state == ON) {
      if (cur_water_level < water_safe_limit) {
        water_warning_level++;
 
        if (water_warning_level > MAX_LEVEL_WARNINGS) {
          /* this will be taken care of below */
          current_action = OFF;

          water_warning_level = 0;
        }
    } else {
      water_warning_level = 0;
    }
  }

  if (water_interval == WATER_PUBLISH_INTERVAL / LOOP_DELAY) {
    SERIAL_PRINTLN("Publishing water level");
  
    sprintf(water_level_str, "%d", cur_water_level);
    ret = queue_publish(MQTT_TOPIC_PUB2, (const uint8_t *)water_level_str, strlen(water_level_str), true);
    if (ret) {
      SERIAL_PRINTF("Failed to publish : %d\n", ret);
    }
    water_interval = 0;
  } else {
    water_interval++;
  }

  client.loop();

  SERIAL_PRINTF("PUSH button = %d\n", digitalRead(PUSH_BUTTON) == HIGH ? 1 : 0);

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
    int ret;
    SERIAL_PRINTLN("Button pressed");

    if (current_action == 0xFF) {
      if (current_state == OFF) {
        current_action = ON;
      } else {
        current_action = OFF;
      }
    } else {
      SERIAL_PRINTLN("Can't change state, pending change in progress");
    }
    debounce = 0;
    cool_off = COOL_OFF_PERIOD * 1000 / LOOP_DELAY;
  }

  /* Somebody wants something... */
  switch (current_action) {
    case OFF:
      digitalWrite(LED_RED, 0);
      water_valve_change_state(OFF);
      digitalWrite(LED_GREEN, 1);

      ret = queue_publish(MQTT_TOPIC_PUB1, (const uint8_t *)MQTT_TOPIC_PUB1_STR2, strlen(MQTT_TOPIC_PUB1_STR2), true);
      if (ret) {
        SERIAL_PRINTF("Failed to publish : %d\n", ret);
      }
      current_action = 0xFF;
      break;
 
    case ON:
      digitalWrite(LED_GREEN, 0);
      water_valve_change_state(ON);
      digitalWrite(LED_RED, 1);

      ret = queue_publish(MQTT_TOPIC_PUB1, (const uint8_t *)MQTT_TOPIC_PUB1_STR1, strlen(MQTT_TOPIC_PUB1_STR1), true);
      if (ret) {
        SERIAL_PRINTF("Failed to publish : %d\n", ret);
      }
      current_action = 0xFF;
      break;

    case 0xFF:
      break;
  }

  
  /* Check if there's something to send */
  /* We send everything: when a button is pressed => 2 publish cmds */
  publish_msg(true);

sleep:
  delay(LOOP_DELAY);
}
