#include <FS.h>

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>

#define CHAR_ARRAY_LEN            32
#define MAX_WIFI_CONNECT_RETRY    20
#define USE_ADC_ADS111x           1

//#define MAX_NUM_VCC_READS    5
#define ADS1015_PGA_DEFAULT       ADS1015_PGA_2048
#define ADS1015_PGA_6144          0b000
#define ADS1015_PGA_4096          0b001
#define ADS1015_PGA_2048          0b010
#define ADS1015_PGA_SHIFT         1

#define ADS101x_OS                0b1
#define ADS101x_OS_SHIFT          7

#define ADS1015_MUX_AIN0_AIN1     0b000
#define ADS1015_MUX_AIN0_AIN3     0b001
#define ADS1015_MUX_AIN1_AIN3     0b010
#define ADS1015_MUX_AIN2_AIN3     0b011
#define ADS1015_MUX_AIN0_GND      0b100
#define ADS1015_MUX_AIN1_GND      0b101
#define ADS1015_MUX_AIN2_GND      0b110
#define ADS1015_MUX_AIN3_GND      0b111
#define ADS1015_MUX_SHIFT         4

#define ADS101x_MODE_CONT         0b0
#define ADS101x_MODE_ONESHOT      0b1
#define ADS101x_MODE_SHIFT        0

#define ADS101x_DR_1K6_SPS        0b100 // default
#define ADS101x_DR_SHIFT          5


#define ADS1015_COMP_MODE_WIN     0b1
#define ADS1015_COMP_MODE_LT      0b0
#define ADS1015_COMP_MODE_SHIFT   4

#define ADS1015_COMP_POL_HI       0b1
#define ADS1015_COMP_POL_LO       0b0
#define ADS1015_COMP_POL_SHIFT    3

#define ADS1015_COMP_LATCH        0b1
#define ADS1015_COMP_LATCH_SHIFT  2

#define ADS1015_COMP_CFG_DIS      0b11
#define ADS1015_COMP_CFG_TRG_1    0b00
#define ADS1015_COMP_CFG_TRG_2    0b01
#define ADS1015_COMP_CFG_TRG_4    0b10
#define ADS1015_COMP_CFG_SHIFT    0

#define ADS101x_PTR_CFG_REG       0b01
#define ADS101x_PTR_CONV_REG      0b00
#define ADS1015_PTR_CFG_THR_LO    0b10
#define ADS1015_PTR_CFG_THR_HI    0b11
#define ADS101x_CONV_LEN          2

#define ADS101x_ADDR_GND          0b1001000
#define ADS101x_ADDR_VDD          0b1001001

#define ADS101x_CFG_REG_START_CONV            (ADS101x_OS << ADS101x_OS_SHIFT)
#define ADS101x_CFG_REG_CONV_RDY              ADS101x_CFG_REG_START_CONV // same bit, when read means conversion done

#define ADS1015_CFG_REG_MSB_MUX(mux)          (mux << ADS1015_MUX_SHIFT)
#define ADS1015_CFG_REG_MSB_PGA(pga)          (pga << ADS1015_PGA_SHIFT)
#define ADS1015_CFG_REG_MSB_MUX(mux)          (mux << ADS1015_MUX_SHIFT)
#define ADS101x_CFG_REG_MSB_MODE(m)           (m << ADS101x_MODE_SHIFT)
#define ADS101x_CFG_REG_LSB_DR(dr)            (dr << ADS101x_DR_SHIFT)
#define ADS1015_CFG_REG_LSB_COMP_MODE(cmode)  (cmode << ADS1015_COMP_MODE_SHIFT)
#define ADS1015_CFG_REG_LSB_COMP_POL(pol)     (pol << ADS1015_COMP_POL_SHIFT)
#define ADS1015_CFG_REG_LSB_COMP_LATCH(latch) (latch << ADS1015_COMP_LATCH_SHIFT)
#define ADS1015_CFG_REG_LSB_COMP(ccfg)        (ccfg << ADS1015_COMP_CFG_SHIFT)


#if (USE_ADS111x == 1)
#define ADS101x_CODE_SHIFT      0
#else
#define ADS101x_CODE_SHIFT      4
#endif

#define ADC_PGA_SETTING         ADS1015_PGA_6144
#define LOW_VOLTAGE_THRES       2835 /* mV */

#define WATER_ALARM_PIN         12
#define RST_ACK_PIN             13

#if (USE_ADS111x == 1)
char ads101x_lsb_size[][2] = {
  {3, 4}, /* 6144 */
  {1, 3}, /* 4096 */
  {1, 4}, /* 2048 */
  {1, 5}, /* 1024 */
  {1, 6}, /* 512 */
  {1, 7}, /* 256 */
};
#else
char ads101x_lsb_size[][2] = {
  {3, 0 /* 1 */}, /* 6144 */
  {2, 0 /* 1 */}, /* 4096 */
  {1, 0 /* 1 */}, /* 2048 */
  {1, 1 /* 2 */}, /* 1024 */
  {1, 2 /* 4 */}, /* 512 */
  {1, 3 /* 8 */}, /* 256 */
};
#endif

#define CODE_TO_MV(code) \
      (((code) >> ADS101x_CODE_SHIFT) * ads101x_lsb_size[ADC_PGA_SETTING][0] >> ads101x_lsb_size[ADC_PGA_SETTING][1])

#define MV_TO_CODE(mv)  \
      (((mv) * ads101x_lsb_size[ADC_PGA_SETTING][1] >> ads101x_lsb_size[ADC_PGA_SETTING][0]) << ADS101x_CODE_SHIFT)

static char good_to_go = 0;
static char water_alarm = 0;

#define CENTRAL_URL "http://192.168.1.80/logger/test.pl?voltage=%d&water=%d"

//ADC_MODE(ADC_VCC);
 
#if 0
int h20 = 0;

void setup() {
  pinMode(h20,  INPUT);
  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);
  WiFi.forceSleepBegin();
}

void loop() {
  int i = analogRead(h20);

  Serial.println(i);

  //delay(1000);
  ESP.deepSleep(10e6,   );

}
#endif

uint16_t read_measurement()
{
  uint32_t ret;
  uint16_t vcc;

  Wire.beginTransmission(ADS101x_ADDR_GND);
  Wire.write(ADS101x_PTR_CONV_REG);
  ret = Wire.endTransmission();
#ifdef DEBUG
  if (!ret) {
    Serial.println("Sucesfully read conversion result");
  } else {
    Serial.printf("I2C failed : %d\n", ret);
  }
#endif

  Wire.requestFrom(ADS101x_ADDR_GND, ADS101x_CONV_LEN);
  vcc = Wire.read() << 8;
  vcc |= Wire.read();
#ifdef DEBUG
  Serial.print("Raw voltage is: "); Serial.println(vcc);
#endif
  vcc = CODE_TO_MV(vcc);
  return vcc;
}
void start_measurement()
{
  uint32_t ret;

  Wire.beginTransmission(ADS101x_ADDR_GND);
#ifdef DEBUG
  Serial.printf("0x%02x\n", ADS101x_PTR_CFG_REG);
#endif
  Wire.write(ADS101x_PTR_CFG_REG);

//  Serial.printf("0x%02x\n", ADS101x_CFG_REG_START_CONV | ADS1015_CFG_REG_MSB_PGA(ADC_PGA_SETTING) |
//             ADS101x_CFG_REG_MSB_MODE(ADS101x_MODE_ONESHOT));
//  Wire.write(ADS101x_CFG_REG_START_CONV | ADS1015_CFG_REG_MSB_PGA(ADC_PGA_SETTING) |
//             ADS101x_CFG_REG_MSB_MODE(ADS101x_MODE_ONESHOT));
#ifdef DEBUG
  Serial.printf("0x%02x\n", ADS101x_CFG_REG_START_CONV | 
                            ADS1015_CFG_REG_MSB_MUX(ADS1015_MUX_AIN0_GND) |
                            ADS1015_CFG_REG_MSB_PGA(ADC_PGA_SETTING) |
                            ADS101x_CFG_REG_MSB_MODE(ADS101x_MODE_ONESHOT));
#endif
  Wire.write(ADS101x_CFG_REG_START_CONV | 
                            ADS1015_CFG_REG_MSB_MUX(ADS1015_MUX_AIN0_GND) |
                            ADS1015_CFG_REG_MSB_PGA(ADC_PGA_SETTING) |
                            ADS101x_CFG_REG_MSB_MODE(ADS101x_MODE_ONESHOT));
#ifdef DEBUG
  Serial.printf("0x%02x\n", ADS101x_CFG_REG_LSB_DR(ADS101x_DR_1K6_SPS) |
             ADS1015_CFG_REG_LSB_COMP_MODE(ADS1015_COMP_MODE_WIN) |
             //ADS1015_CFG_REG_LSB_COMP_POL(ADS1015_COMP_POL_HI) |
             ADS1015_CFG_REG_LSB_COMP_LATCH(ADS1015_COMP_LATCH) |
             ADS1015_CFG_REG_LSB_COMP(ADS1015_COMP_CFG_TRG_4));
#endif
  Wire.write(ADS101x_CFG_REG_LSB_DR(ADS101x_DR_1K6_SPS) |
             ADS1015_CFG_REG_LSB_COMP_MODE(ADS1015_COMP_MODE_WIN) |
             //ADS1015_CFG_REG_LSB_COMP_POL(ADS1015_COMP_POL_HI) |
             ADS1015_CFG_REG_LSB_COMP_LATCH(ADS1015_COMP_LATCH) |
             ADS1015_CFG_REG_LSB_COMP(ADS1015_COMP_CFG_TRG_4));
  ret = Wire.endTransmission();
#ifdef DEBUG
  if (!ret) {
    Serial.println("Sucesfully configured ADC Config Register");
  } else {
    Serial.printf("I2C failed : %d\n", ret);
  }
#endif
}
void setup() {
  char ssid[CHAR_ARRAY_LEN], password[CHAR_ARRAY_LEN],
       str[CHAR_ARRAY_LEN], file_name[CHAR_ARRAY_LEN],
       c, pos = 0, line = 0;
  IPAddress ip_addr, gw, dns;
  byte my_mac[6], ret;
  uint32_t adc_cfg_done;

  File config_file;

  pinMode(WATER_ALARM_PIN, INPUT);
  pinMode(RST_ACK_PIN, OUTPUT);
  
  digitalWrite(RST_ACK_PIN, HIGH);
  
  Serial.begin(115200);

  /* Reset source handling code */
  if (digitalRead(WATER_ALARM_PIN)) {
    water_alarm = 1;
    Serial.printf("*** Got WATER! ***\n");
  } else {
    water_alarm = 0;
  }

  /* End of reset source handling code */
  WiFi.macAddress(my_mac);
  sprintf(file_name,"/%02x_%02x_%02x_config.txt", my_mac[3], my_mac[4], my_mac[5]);

  SPIFFS.begin();

  config_file = SPIFFS.open(file_name, "r");
  if (!config_file) {
    Serial.printf("Failed to open file %s\n", file_name);
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
        case 2:
          ip_addr.fromString(str);
          Serial.println(ip_addr);
          break;
        case 3:
          gw.fromString(str);
          Serial.println(gw);
          break;
        case 4:
          dns.fromString(str);
          Serial.println(dns);
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
  
  WiFi.forceSleepWake();

  WiFi.config(ip_addr, dns, gw);

  // Bring up the WiFi connection
  WiFi.mode(WIFI_STA);

  Serial.printf("**** %s ****\n**** %s ****\n", ssid, password);
  WiFi.begin(ssid, password);

  Wire.begin(4, 5);

  ESP.rtcUserMemoryRead(0, &adc_cfg_done, sizeof(adc_cfg_done));
  if (adc_cfg_done == 0xA5A55A5A) {
    goto skip_threshold_config;
  }
  
  adc_cfg_done = 0xA5A55A5A;
  ESP.rtcUserMemoryWrite(0, &adc_cfg_done, sizeof(adc_cfg_done));

  Serial.printf("0x%02x\n",ADS101x_ADDR_GND);

  Wire.beginTransmission(ADS101x_ADDR_GND);

  Serial.printf("0x%02x\n", ADS1015_PTR_CFG_THR_HI);
  Wire.write(ADS1015_PTR_CFG_THR_HI);

  Serial.printf("0x%02x\n", 0x7F);
  Wire.write(0x7F);

  Serial.printf("0x%02x\n", 0xFF);
  Wire.write(0xFF);
  
  ret = Wire.endTransmission();
  if (!ret) {
    Serial.println("Sucesfully configured ADC high threshold Register");
  } else {
    Serial.printf("I2C failed : %d\n", ret);
  }

  Wire.beginTransmission(ADS101x_ADDR_GND);

  Serial.printf("0x%02x\n", ADS1015_PTR_CFG_THR_LO);
  Wire.write(ADS1015_PTR_CFG_THR_LO);

  Serial.printf("0x%02x\n", MV_TO_CODE(LOW_VOLTAGE_THRES) >> 8);
  Wire.write(MV_TO_CODE(LOW_VOLTAGE_THRES) >> 8);

  Serial.printf("0x%02x\n", MV_TO_CODE(LOW_VOLTAGE_THRES) & 0xFF);
  Wire.write(MV_TO_CODE(LOW_VOLTAGE_THRES) & 0xFF);

  //Serial.printf("0x%02x\n", 0x30);
  //Wire.write(0x30);

  //Serial.printf("0x%02x\n", 0x00);
  //Wire.write(0x00);

  ret = Wire.endTransmission();
  if (!ret) {
    Serial.println("Sucesfully configured ADC low threshold Register");
  } else {
    Serial.printf("I2C failed : %d\n", ret);
  }
skip_threshold_config:
  good_to_go = 1;
}

void loop() {
  uint32_t vcc = 0, count = 0, ret = 0, update_success = 0;
  HTTPClient http;
  char url[255];

  if (!good_to_go) {
    Serial.println("No config, going to sleep!");
    goto deep_sleep;
  }

  do {
    start_measurement();
    delay(500);
    vcc += read_measurement();
    Serial.print(".");
  } while ((WiFi.status() != WL_CONNECTED) && (count++ < MAX_WIFI_CONNECT_RETRY));

  if (count == MAX_WIFI_CONNECT_RETRY) {
    Serial.println("");
    Serial.println("WiFi connection failed, going to sleep");
    goto deep_sleep;
  }

  Serial.println("");
  Serial.println("WiFi connected");

#if 0
  Wire.beginTransmission(ADS101x_ADDR_GND);

  Serial.printf("0x%02x\n", ADS101x_PTR_CFG_REG);
  Wire.write(ADS101x_PTR_CFG_REG);

  Serial.printf("0x%02x\n", ADS101x_CFG_REG_START_CONV | 
                            ADS1015_CFG_REG_MSB_MUX(ADS1015_MUX_AIN0_GND) |
                            ADS1015_CFG_REG_MSB_PGA(ADC_PGA_SETTING) |
                            ADS101x_CFG_REG_MSB_MODE(ADS101x_MODE_ONESHOT));
  Wire.write(ADS101x_CFG_REG_START_CONV | 
             ADS1015_CFG_REG_MSB_MUX(ADS1015_MUX_AIN0_GND) | 
             ADS1015_CFG_REG_MSB_PGA(ADC_PGA_SETTING) |
             ADS101x_CFG_REG_MSB_MODE(ADS101x_MODE_ONESHOT));
#if 0
  Serial.printf("0x%02x\n", ADS1015_CFG_REG_MSB_PGA(ADC_PGA_SETTING));
  Wire.write(ADS1015_CFG_REG_MSB_PGA(ADC_PGA_SETTING));
#endif
  Serial.printf("0x%02x\n", ADS101x_CFG_REG_LSB_DR(ADS101x_DR_1K6_SPS) |
             ADS1015_CFG_REG_LSB_COMP_MODE(ADS1015_COMP_MODE_WIN) |
             //ADS1015_CFG_REG_LSB_COMP_POL(ADS1015_COMP_POL_HI) |
             ADS1015_CFG_REG_LSB_COMP_LATCH(ADS1015_COMP_LATCH) |
             ADS1015_CFG_REG_LSB_COMP(ADS1015_COMP_CFG_TRG_4));

  Wire.write(ADS101x_CFG_REG_LSB_DR(ADS101x_DR_1K6_SPS) |
             ADS1015_CFG_REG_LSB_COMP_MODE(ADS1015_COMP_MODE_WIN) |
             //ADS1015_CFG_REG_LSB_COMP_POL(ADS1015_COMP_POL_HI) |
             ADS1015_CFG_REG_LSB_COMP_LATCH(ADS1015_COMP_LATCH) |
             ADS1015_CFG_REG_LSB_COMP(ADS1015_COMP_CFG_TRG_4));
  ret = Wire.endTransmission();
  if (!ret) {
    Serial.println("Sucesfully configured ADC Config Register");
  } else {
    Serial.printf("I2C failed : %d\n", ret);
  }
#if 1
  Wire.beginTransmission(ADS101x_ADDR_GND);
  Wire.write(ADS101x_PTR_CFG_REG);
  ret = Wire.endTransmission();
  if (!ret) {
    Serial.println("Sucesfully read cfg reg");
  } else {
    Serial.printf("I2C failed : %d\n", ret);
  }
#endif
  do {
    Wire.requestFrom(ADS101x_ADDR_GND, ADS101x_CONV_LEN);
    ret = Wire.read();
    Wire.read(); /* discard LSB */
  } while (!(ret & ADS101x_CFG_REG_CONV_RDY));
  Serial.println("Conversion is done");
#endif
  
  vcc = vcc / (count + 1);
 
  if (water_alarm) {
    sprintf(url, CENTRAL_URL, vcc, 1);
  } else {
    sprintf(url, CENTRAL_URL, vcc, 0);
  }

  Serial.println(url);

  if (http.begin(url)) {
    int httpCode=http.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        Serial.println("Update success!");
        update_success = 1;
        // we sent the message, now there's
        // not much we can do, so inform
        // the controller about it and sleep.
      } else {
        Serial.printf("Update failed, got code: %d\n", httpCode);
      }
    } else {
      Serial.printf("GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.println("Unable to connect");
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();

  Serial.print("Sleeping");

  if (update_success)
    digitalWrite(RST_ACK_PIN, HIGH);

deep_sleep:
  ESP.deepSleep(0);
}
