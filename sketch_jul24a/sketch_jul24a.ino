#include <FS.h>

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>

#define CHAR_ARRAY_LEN        32

//#define MAX_NUM_VCC_READS    5
#define ADS1015_PGA_DEFAULT       0b010
#define ADS1015_PGA_SHIFT         1

#define ADS101x_MODE_CONT         0b0
#define ADS101x_MODE_ONESHOT      0b1
#define ADS101x_MODE_SHIFT        0

#define ADS101x_DR_1K6_SPS        0b100 // default
#define ADS101x_DR_SHIFT          5

#define ADS1015_COMP_CFG_DIS      0b11
#define ADS1015_COMP_CFG_SHIFT    0

#define ADS101x_PTR_CFG_REG       0b01
#define ADS101x_PTR_CONV_REG      0b00

#define ADS101x_CONV_LEN          2

#define ADS101x_ADDR_GND          0b1001000
#define ADS101x_ADDR_VDD          0b1001001

#define ADS1015_CFG_REG_MSB_PGA(pga)          (pga << ADS1015_PGA_SHIFT)
#define ADS101x_CFG_REG_MSB_MODE(m)           (m << ADS101x_MODE_SHIFT)

#define ADS101x_CFG_REG_LSB_DR(dr)            (dr << ADS101x_DR_SHIFT)
#define ADS1015_CFG_REG_LSB_COMP(ccfg)        (ccfg << ADS1015_COMP_CFG_SHIFT)

#define ADS101x_MAX_CODE                      0x7FF0
float ads101x_pga_to_fs(uint32_t pga)
{
    switch(pga) {
      case 0b000:
        return 6144.0;
      case 0b001:
        return 4096.0;
      case 0b010:
        return 2048.0;
      case 0b011:
        return 1024.0;
      case 0b100:
        return 512.0;
      case 0b101:
      case 0b110:
      case 0b111:
        return 256.0;      
    }
}
#define EXTERNAL_RES_DIV_COEF    2

static char good_to_go = 0;

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

void setup() {
  char ssid[CHAR_ARRAY_LEN], password[CHAR_ARRAY_LEN],
       str[CHAR_ARRAY_LEN], file_name[CHAR_ARRAY_LEN],
       c, pos = 0, line = 0;
  IPAddress ip_addr, gw, dns;
  byte my_mac[6], ret;
  
  File config_file;

  Serial.begin(115200);

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

  Serial.printf("0x%02x\n",ADS101x_ADDR_GND);

  Wire.beginTransmission(ADS101x_ADDR_GND);

  Serial.printf("0x%02x\n", ADS101x_PTR_CFG_REG);
  Wire.write(ADS101x_PTR_CFG_REG);

  Serial.printf("0x%02x\n", ADS1015_CFG_REG_MSB_PGA(ADS1015_PGA_DEFAULT) |
             ADS101x_CFG_REG_MSB_MODE(ADS101x_MODE_CONT));
  Wire.write(ADS1015_CFG_REG_MSB_PGA(ADS1015_PGA_DEFAULT) |
             ADS101x_CFG_REG_MSB_MODE(ADS101x_MODE_CONT));

  Serial.printf("0x%02x\n", ADS101x_CFG_REG_LSB_DR(ADS101x_DR_1K6_SPS) |
             ADS1015_CFG_REG_LSB_COMP(ADS1015_COMP_CFG_DIS));

  Wire.write(ADS101x_CFG_REG_LSB_DR(ADS101x_DR_1K6_SPS) |
             ADS1015_CFG_REG_LSB_COMP(ADS1015_COMP_CFG_DIS));
  
  ret = Wire.endTransmission();
  if (!ret) {
    Serial.println("Sucesfully configured ADC Config Register");
  } else {
    Serial.printf("I2C failed : %d\n", ret);
  }

  Wire.beginTransmission(ADS101x_ADDR_GND);
  Wire.write(ADS101x_PTR_CONV_REG);
  ret = Wire.endTransmission();
  if (!ret) {
    Serial.println("Sucesfully performed conversion");
  } else {
    Serial.printf("I2C failed : %d\n", ret);
  }

  good_to_go = 1;

}

void loop() {
  uint32_t vcc = 0, count = 20, ret = 0;
  HTTPClient http;

  if (!good_to_go) {
    Serial.println("No config, going to sleep!");
    goto deep_sleep;
  }
  while ((WiFi.status() != WL_CONNECTED) && (--count)) {
    delay(500);
    Serial.print(".");
  }

  if (!count) {
    Serial.println("");
    Serial.println("WiFi connection failed, going to sleep");
  } else {
    Serial.println("");
    Serial.println("WiFi connected");

#if 0
    for (count = 0; count < MAX_NUM_VCC_READS; count++) { 
      vcc += ESP.getVcc();
      delay(10);
    }

    vcc /= MAX_NUM_VCC_READS;
#else
  Wire.requestFrom(ADS101x_ADDR_GND, ADS101x_CONV_LEN);
  vcc = Wire.read() << 8;
  vcc |= Wire.read();
  Serial.print("Raw voltage is: "); Serial.println(vcc);
  vcc = vcc * ads101x_pga_to_fs(ADS1015_PGA_DEFAULT) / ADS101x_MAX_CODE * 2.0;
#endif
    char url[255];
    
    sprintf(url,"http://192.168.1.30/logger/test.pl?voltage=%d", vcc);
    Serial.println(url);
#if 1
    if (http.begin(url)) {
      int httpCode=http.GET();
      if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
          Serial.println("Update success!");
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
#endif
  }

  count = 20;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();

  Serial.print("Wait for disconnect");
  while ((WiFi.status() == WL_CONNECTED) && (--count)) {
    delay(100);
    Serial.print(".");
  }
  
deep_sleep:

  ESP.deepSleep(20e6);
}
