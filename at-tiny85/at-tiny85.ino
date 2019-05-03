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
#define LED_BUILTIN             0
#define WATER_SENSOR_PIN        A1
#define ESP8266_RST_PIN         1 // assert this pin for reset of wifi module ...
#define ESP8266_ACK_PIN         3 // ... wait the ack from the module
#define ESP8266_WATER_ALARM_PIN 4 // when water is detected, this is logic 1

#define WATCHDOG_MAX  5
#define MAX_WATER_DIFF  100

uint8_t watchdog_counter = 0;
int water_avg = 0;
 
//This runs each time the watch dog wakes us up from sleep
ISR(WDT_vect) {
  watchdog_counter++;
}

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);

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

void check_water()
{
  int water_diff = abs(analogRead(WATER_SENSOR_PIN) - water_avg);
  if (water_diff < MAX_WATER_DIFF)
          return;

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
}

// the loop function runs over and over again forever
void loop() {
    if (watchdog_counter > WATCHDOG_MAX) {
        watchdog_counter = 0;

        ADCSRA |= (1<<ADEN); //Enable ADC
        check_water();
        ADCSRA &= ~(1<<ADEN); //Disable ADC, saves ~230uA
 
//        digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
//        delay(1000);                       // wait for a second
//        digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
    }

    setup_watchdog(6); //Setup watchdog to go off after 1sec
    sleep_mode(); //Go to sleep! Wake up 1sec later and check water
    
}
