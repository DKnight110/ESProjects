/*
  Blink

  Turns an LED on for one second, then off for one second, repeatedly.

  Most Arduinos have an on-board LED you can control. On the UNO, MEGA and ZERO
  it is attached to digital pin 13, on MKR1000 on pin 6. LED_BUILTIN is set to
  the correct LED pin independent of which board is used.
  If you want to know what pin the on-board LED is connected to on your Arduino
  model, check the Technical Specs of your board at:
  https://www.arduino.cc/en/Main/Products

  modified 8 May 2014
  by Scott Fitzgerald
  modified 2 Sep 2016
  by Arturo Guadalupi
  modified 8 Sep 2016
  by Colby Newman

  This example code is in the public domain.

  http://www.arduino.cc/en/Tutorial/Blink
*/
#include <avr/sleep.h>
#include <avr/wdt.h>

/* HW defines */
#define LED_BUILTIN               0
#define WATER_SENSOR_PIN          A1
#define ESP8266_RST_PIN           1 // assert this pin for reset of wifi module ...
#define ESP8266_ACK_PIN           3 // ... wait the ack from the module, as logic 0
#define ESP8266_WATER_ALARM_PIN   4 // when water is detected, this is logic 1

#define MAX_WATER_DIFF            100
#define AT_TINY_SLEEP_TIME        1   /* seconds */
#define WATER_CHECK_TIME          10  /* seconds */
#define ESP_RESET_TIME            60  /* seconds, multiple of WATER_CHECK_TIME */
#define ESP_WATER_ALARM_TIME      20  /* seconds, multiple of WATER_CHECK_TIME */

#define ESP_RUN_ALLOWED_INTERVAL      10500 /* msec, total time to wait for ACK */
#define ESP_ACK_POLL_TIME             300 /* msec */
#define ESP_ACK_LOOPS                 (ESP_RUN_ALLOWED_INTERVAL / ESP_ACK_POLL_TIME)

int water_avg = 0;
uint8_t watchdog_counter = WATER_CHECK_TIME - 1;
uint32_t esp_counter = ESP_RESET_TIME / WATER_CHECK_TIME - 1;
uint32_t esp_wakeup_time = ESP_RESET_TIME / WATER_CHECK_TIME;

#if 0
/* Persistent variable signalling water is still present */
int water_detected = 0;
#else
/* Persistent variable storing if the alarm was sent upstream */
int water_alarm_sent = 0;
#endif

#if (AT_TINY_SLEEP_TIME == 8)
#define TIMER_PRESCALER_SETTING 9
#elif (AT_TINY_SLEEP_TIME == 4)
#define TIMER_PRESCALER_SETTING 8
#elif (AT_TINY_SLEEP_TIME == 2)
#define TIMER_PRESCALER_SETTING 7
#elif (AT_TINY_SLEEP_TIME == 1)
#define TIMER_PRESCALER_SETTING 6
#else
#error "Invalid AT Tiny sleep interval"
#endif

//This runs each time the watch dog wakes us up from sleep
ISR(WDT_vect) {
  watchdog_counter++;
}

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  
  /* do an inital reset of the ESP at boot */
  pinMode(ESP8266_RST_PIN, OUTPUT);

  digitalWrite(ESP8266_RST_PIN, LOW);

  /* configure the water alarm pin & set it low */
  pinMode(ESP8266_WATER_ALARM_PIN, OUTPUT);
  digitalWrite(ESP8266_WATER_ALARM_PIN, LOW);

  for(int x = 0 ; x < 8 ; x++)
  {
    water_avg += analogRead(WATER_SENSOR_PIN);

    //During power up, blink the LED to let the world know we're alive
    if(digitalRead(LED_BUILTIN) == LOW)
      digitalWrite(LED_BUILTIN, HIGH);
    else
      digitalWrite(LED_BUILTIN, LOW);

    delay(50);
  }
  water_avg /= 8;

  ADCSRA &= ~(1<<ADEN); //Disable ADC, saves ~230uA

  //Power down various bits of hardware to lower power usage  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
}

//Sets the watchdog timer to wake us up, but not reset
//0=16ms, 1=32ms, 2=64ms, 3=128ms, 4=250ms, 5=500ms
//6=1sec, 7=2sec, 8=4sec, 9=8sec
//From: http://interface.khm.de/index.php/lab/experiments/sleep_watchdog_battery/
void setup_watchdog(int timerPrescaler) {

  if (timerPrescaler > 9 ) timerPrescaler = 9; //Limit incoming amount to legal settings

  byte bb = timerPrescaler & 7; 
  if (timerPrescaler > 7) bb |= (1<<5); //Set the special 5th bit if necessary

  //This order of commands is important and cannot be combined
  MCUSR &= ~(1<<WDRF); //Clear the watch dog reset
  WDTCR |= (1<<WDCE) | (1<<WDE); //Set WD_change enable, set WD enable
  WDTCR = bb; //Set new watchdog timeout value
  WDTCR |= _BV(WDIE); //Set the interrupt enable, this will keep unit from resetting after each int
}

int check_water()
{
  int water_diff = abs(analogRead(WATER_SENSOR_PIN) - water_avg);
  if (water_diff < MAX_WATER_DIFF)
    return 0;
  else
    return 1;
#if 0
  wdt_disable();

  long startTime = millis(); //Record the current time
  long timeSinceBlink = millis(); //Record the current time for blinking
  digitalWrite(LED_BUILTIN, HIGH); //Start out with the uh-oh LED on

  while((water_diff > MAX_WATER_DIFF) || (millis() - startTime) < 2000)
  {
      if(millis() - timeSinceBlink > 100) //Toggle the LED every 100ms
      {
        timeSinceBlink = millis();

        if(digitalRead(LED_BUILTIN) == LOW) 
          digitalWrite(LED_BUILTIN, HIGH);
        else
          digitalWrite(LED_BUILTIN, LOW);
      }

      water_diff = abs(analogRead(WATER_SENSOR_PIN) - water_avg); //Take a new reading

    } //Loop until we don't detect water AND 2 seconds of alarm have completed

    digitalWrite(LED_BUILTIN, LOW); // ensure the led is off
#endif
}

// the loop function runs over and over again forever
void loop() {
  int ret = 0, count = 0;

  if (watchdog_counter == WATER_CHECK_TIME) {
    ADCSRA |= (1<<ADEN); //Enable ADC
    ret = check_water();
    ADCSRA &= ~(1<<ADEN); //Disable ADC, saves ~230uA

    if (ret) {
      if (!water_alarm_sent) {
        digitalWrite(ESP8266_WATER_ALARM_PIN, HIGH);

        /* optionally signal LED */
        digitalWrite(LED_BUILTIN, HIGH);
        
        esp_counter = esp_wakeup_time - 1;
      }
    } else {
      water_alarm_sent = 0;
      /* optionally signal LED */
      digitalWrite(LED_BUILTIN, LOW);
    }

    watchdog_counter = 0;
    esp_counter++;
  }

  if (esp_counter == esp_wakeup_time) {
    digitalWrite(ESP8266_RST_PIN, HIGH);
#if 0
        if (ret != water_detected) {
          if (ret) {
            /* assert ESP alarm line */
            digitalWrite(ESP8266_WATER_ALARM_PIN, HIGH);

            /* optionally signal LED */
            digitalWrite(LED_BUILTIN, HIGH);

            wakeup_esp = 1;
          } else {
            /* turn off signal LED */
            digitalWrite(LED_BUILTIN, LOW);
            /* Put pin down, ESP must've read it, since the reset is ack'ed */
            digitalWrite(ESP8266_WATER_ALARM_PIN, LOW);
          }
          water_detected = ret;
        }

        /* If water is still detected, then wakeup ESP again */

        if (water_detected && 
            (esp_counter == ESP_WATER_ALARM_INTERVAL / WATCHDOG_MAX)) {
          /* assert ESP alarm line */
          digitalWrite(ESP8266_WATER_ALARM_PIN, HIGH);

          /* optionally signal LED */
          digitalWrite(LED_BUILTIN, HIGH);
 
          wakeup_esp = 1;
        }
#endif
#if 0
    if (ret) {
      if (!water_alarm_sent) {
        /* 
         * if water is detected and the ESP hasn't sent it upstream,
         * then assert the alarm pin 
         */
        /* assert ESP alarm line */
        digitalWrite(ESP8266_WATER_ALARM_PIN, HIGH);

        /* optionally signal LED */
        digitalWrite(LED_BUILTIN, HIGH);
      }
    } else {
        /* water is not detected anymore , then reset the sent signal */
        if (water_alarm_sent)
          water_alarm_sent = 0; 
    }
#endif
    /*
     * Wakeup ESP if either it's time to wakeup
     * or water is detected and the alarm wasn't
     * sent upstream
     */
    wdt_disable();
#if 0
          do {            /* Toggle reset line to ESP */
            //pinMode(ESP8266_RST_PIN, OUTPUT);
            digitalWrite(ESP8266_RST_PIN, LOW);
            delay(10);
            digitalWrite(ESP8266_RST_PIN, HIGH);
            //pinMode(ESP8266_RST_PIN, INPUT);
            delay(300);
          } while(digitalRead(ESP8266_ACK_PIN));
#else
    for (count = 0; count < ESP_ACK_LOOPS; count++) {
      delay(ESP_ACK_POLL_TIME);
      if (!digitalRead(ESP8266_ACK_PIN)) {
        if (ret && !water_alarm_sent) {
          water_alarm_sent = 1;

          /* Put pin down, ESP read it */
          digitalWrite(ESP8266_WATER_ALARM_PIN, LOW);

          esp_wakeup_time = ESP_RESET_TIME / WATER_CHECK_TIME;
        }
        break;
      }
    }
    digitalWrite(ESP8266_RST_PIN, LOW);

    if ((count == ESP_ACK_LOOPS) && ret && !water_alarm_sent) {
      /* 
       * Got water, but the message didn't get through, so
       * try again, but faster
       */
      esp_wakeup_time =  ESP_WATER_ALARM_TIME / WATER_CHECK_TIME;
    }
#endif
    esp_counter = 0;
  }
  setup_watchdog(TIMER_PRESCALER_SETTING); //Setup watchdog to go off after 1sec
  sleep_mode(); //Go to sleep! Wake up 1sec later and check water
}
