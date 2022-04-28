#define DEBUG_LEVEL   0

#if DEBUG_LEVEL == 0
#define SERIAL_PRINT(x)
#define SERIAL_PRINTLN(x)
#elif DEBUG_LEVEL == 1
#include <SendOnlySoftwareSerial.h>
#define SERIAL_TX                 PB1
SendOnlySoftwareSerial Serial(SERIAL_TX);
#define SERIAL_PRINT(x) Serial.print(x)
#define SERIAL_PRINTLN(x) Serial.println(x)
#else
#error "You need to define a debug level"
#endif

#include <avr/sleep.h>
#include <avr/wdt.h>

#define ESP_CONVERTER_PIN         PB2
#define ESP_BATT_ALERT_PIN        PB0
#define ESP_RESET_PIN             PB3

#define AT_TINY_SLEEP_TIME        8UL   /* seconds */
#define WARN_BATT_LEVEL           2870 /* mV */
//#define WARN_BATT_LEVEL           4500 /* mV */
#define WAKEUP_BATT_LEVEL_INT     (24 * 60 * 60UL) /* seconds */
//#define WAKEUP_BATT_LEVEL_INT     (16) /* seconds */
#define MEASUREMENT_INT           (1 * 60 * 60UL) /* seconds */
//#define MEASUREMENT_INT           (120) /* seconds */
#define ESP_ON_CYCLES             2 /* x AT_TINY_SLEEP_TIME = how long the ESP will be on*/

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

#define INT_TO_SLEEP_TIME(x)  ((x) / AT_TINY_SLEEP_TIME)

volatile int watchdog_counter = 0;
uint8_t switch_wakeup = 0;
uint8_t shdn_esp_next_wakeup = 0;
uint8_t esp_cycles = ESP_ON_CYCLES;
int batt_level = WARN_BATT_LEVEL + 1;

//This runs each time the watch dog wakes us up from sleep
ISR(WDT_vect) {
  watchdog_counter++;
}

void setup() {
  // put your setup code here, to run once:
#if DEBUG_LEVEL > 0
  Serial.begin(9600);
#endif
  SERIAL_PRINTLN("In setup()");

  pinMode(ESP_CONVERTER_PIN, OUTPUT);
  pinMode(ESP_BATT_ALERT_PIN, OUTPUT);

  digitalWrite(ESP_CONVERTER_PIN, LOW);
  digitalWrite(ESP_BATT_ALERT_PIN, LOW);

  ADCSRA &= ~(1<<ADEN); //Disable ADC, saves ~230uA
  //Power down various bits of hardware to lower power usage  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //Power down everything, wake up from WDT
  sleep_enable();
}

void loop(void)
{
  setup_watchdog(TIMER_PRESCALER_SETTING);
  SERIAL_PRINTLN("In loop");

  if (shdn_esp_next_wakeup) {
    if (shdn_esp_next_wakeup++ == ESP_ON_CYCLES) {
      digitalWrite(ESP_CONVERTER_PIN, LOW);
      digitalWrite(ESP_BATT_ALERT_PIN, LOW);
      shdn_esp_next_wakeup = 0;
    } else {
      SERIAL_PRINT("Remaining cycles: "); SERIAL_PRINTLN(ESP_ON_CYCLES - shdn_esp_next_wakeup);
    }
  }
 
  if (watchdog_counter == INT_TO_SLEEP_TIME(WAKEUP_BATT_LEVEL_INT)) {
    watchdog_counter = 0;
    batt_level = readVcc();
    SERIAL_PRINT("VCC = "); SERIAL_PRINTLN(batt_level);
    if (batt_level <= WARN_BATT_LEVEL) {     
      digitalWrite(ESP_BATT_ALERT_PIN, HIGH);

      // step 1: set rst pin 0
      pinMode(ESP_RESET_PIN, OUTPUT);
      digitalWrite(ESP_RESET_PIN, LOW);

      // step 2: enable power
      digitalWrite(ESP_CONVERTER_PIN, HIGH);

      // step 3: delay ~1ms (1.3)
      delay(1);

      // step 4: RST is now HiZ
      pinMode(ESP_RESET_PIN, INPUT);

      shdn_esp_next_wakeup = 1;
 
      SERIAL_PRINTLN("send_low_batt_level_warning");
    }
  } else {
    if (watchdog_counter == INT_TO_SLEEP_TIME(MEASUREMENT_INT)) {
      watchdog_counter = 0;
      shdn_esp_next_wakeup = 1;

      if (batt_level <= WARN_BATT_LEVEL) {
        digitalWrite(ESP_BATT_ALERT_PIN, HIGH);        
      }

      // step 1: set rst pin 0
      pinMode(ESP_RESET_PIN, OUTPUT);
      digitalWrite(ESP_RESET_PIN, LOW);

      // step 2: enable power
      digitalWrite(ESP_CONVERTER_PIN, HIGH);

      // step 3: delay ~1ms (1.3)
      delay(1);

      // step 4: RST is now HiZ
      pinMode(ESP_RESET_PIN, INPUT);
      

    } else {
      SERIAL_PRINTLN("???");
    }
  }
  SERIAL_PRINT("watchdog_counter = "); SERIAL_PRINTLN(watchdog_counter);
  SERIAL_PRINT("switch_wakeup = "); SERIAL_PRINTLN(switch_wakeup);

  sleep_mode();
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

long readVcc()
{
  ADCSRA |= (1<<ADEN); //Enable ADC

  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both

  long result = (high<<8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000

  ADCSRA &= ~(1<<ADEN); //Disable ADC, saves ~230uA
  return result; // Vcc in millivolts
}
