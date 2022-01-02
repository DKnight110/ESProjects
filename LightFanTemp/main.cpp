#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "ws2812.pio.h"
#include "serial_comms.h"

#include "macro_helpers.h"

#include "hardware/uart.h"
#include "hardware/irq.h"

#include "one_wire.h"

/* PIN assignments
 * PIN_LED = GP2 (pin 4)
 * PIN_TMP = GP3 (pin 5)
 * PIN_PWM_0 = GP4 (pin 6)
 * PIN_PWM_1 = GP5 (pin 7)
 * PIN_PWM_2 = GP6 (pin 9)
 * PIN_PWM_3 = GP7 (pin 10)
 * PIN_UART1_TX = GP8 (pin 11)
 * PIN_UART1_RX = GP9 (pin 12)
 * PIN_PWM_4 = GP10 (pin 14)
 * PIN_PWM_5 = GP11 (pin 15)
 * PIN_PWM_6 = GP12 (pin 16)
 * PIN_TACH_0 = GP13 (pin 17)
 * PIN_TACH_1 = GP14 (pin 19)
 * PIN_TACH_2 = GP15 (pin 20)
 * PIN_TACH_3 = GP16 (pin 21)
 * PIN_TACH_4 = GP17 (pin 22)
 * PIN_TACH_5 = GP18 (pin 24)
 * PIN_TACH_6 = GP19 (pin 25)
 * PIN_FAN_ON_1 = GP20 (pin 26)
 * PIN_FAN_ON_2 = GP21 (pin 27)
 * ESP8266_RST = GP22 (pin 29)
*/

#define SERIAL_COMMS_UART_ID	uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

/* pins defines */
#define SERIAL_COMMS_TX_PIN	8
#define SERIAL_COMMS_RX_PIN	9
#define TEMP_MEAS_PIN		3

/* timer intervals defines */
#define REPORTING_INT_MS		5000
#define TEMP_READ_INT_MS		1000
#define FAN_SPEED_UPDATE_INT_MS		10000
#define LED_DISPLAY_UPDATE_INT_MS	10

#define PWM_LOW_THRESHOLD	20

#define INVALID_TEMPERATURE	0x0bad
#define INVALID_SPEED		0xbeef


const uint LEDPIN = 25;

#ifdef PICO_DEFAULT_WS2812_PIN
#define WS2812_PIN PICO_DEFAULT_WS2812_PIN
#else
// default to pin 2 if the board doesn't have a default WS2812 pin defined
#define WS2812_PIN 4
#endif

#define IS_RGBW 0
#define NUM_LEDS 49

int chars_rxed;

/* One wire */
One_wire one_wire(TEMP_MEAS_PIN);
rom_address_t address{};

/* Timers */
repeating_timer_t timer;
repeating_timer_t temp_read_timer;
repeating_timer_t fans_adjust_timer;
repeating_timer_t leds_timer;

/* Timer helpers */
bool reporting_callback(repeating_timer_t *rt);
bool read_temp_callback(repeating_timer_t *rt);
bool set_fan_speeds(repeating_timer_t *rt);

volatile bool do_read_temps;

char double_rx_buf[CMD_LEN*NUM_ENTRIES];

int16_t temperatures[NUM_TEMP_SENSORS] = {
	INVALID_TEMPERATURE,	
	INVALID_TEMPERATURE,
	INVALID_TEMPERATURE,
	INVALID_TEMPERATURE,
	INVALID_TEMPERATURE,
	INVALID_TEMPERATURE,
	INVALID_TEMPERATURE
};

extern bool wifi_connected;
extern bool mqtt_connected;

struct fans {
	bool auto_speed[NUM_FANS];
	uint16_t speed[NUM_FANS];
	uint8_t pwm[NUM_FANS];
};

struct fans fans = {
	/* default all are auto speed */
	{ 1, 1, 1, 1, 1, 1 },
	{
		INVALID_SPEED,
		INVALID_SPEED,
		INVALID_SPEED,
		INVALID_SPEED,
		INVALID_SPEED,
		INVALID_SPEED,
		INVALID_SPEED 
	},
	{ 
		PWM_LOW_THRESHOLD,
		PWM_LOW_THRESHOLD,
		PWM_LOW_THRESHOLD,
		PWM_LOW_THRESHOLD,
		PWM_LOW_THRESHOLD,
		PWM_LOW_THRESHOLD,
		PWM_LOW_THRESHOLD
	}
};

/* LEDs PIO */
PIO pio;

bool do_display;

extern volatile struct led_programs *cur_prg;
extern volatile struct led_programs *shadow_prg;
extern volatile struct led_programs led_programs[NUM_LED_PROGRAMS];

absolute_time_t next_display_step_time;

uint8_t cur_step;

static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}

void put_char(unsigned char ch)
{
	do {} while (!uart_is_writable(SERIAL_COMMS_UART_ID));
	uart_putc(SERIAL_COMMS_UART_ID, ch);
}

void serial_comms_uart_rx() {
    while (uart_is_readable(SERIAL_COMMS_UART_ID)) {
        uint8_t ch = uart_getc(SERIAL_COMMS_UART_ID);
	uart_rx(ch);
	// for looping back what's received from modem, uncomment below
	//uart_putc(uart0, ch);
        chars_rxed++;
    }
}

void setup_serial_comms_uart()
{
    // Set up our UART with a basic baud rate.
    uart_init(SERIAL_COMMS_UART_ID, 2400);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
//    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(SERIAL_COMMS_TX_PIN, GPIO_FUNC_UART);
//    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(SERIAL_COMMS_RX_PIN, GPIO_FUNC_UART);

    // Actually, we want a different speed
    // The call will return the actual baud rate selected, which will be as close as
    // possible to that requested
    int __unused actual = uart_set_baudrate(SERIAL_COMMS_UART_ID, BAUD_RATE);

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow(SERIAL_COMMS_UART_ID, false, false);

    // Set our data format
    uart_set_format(SERIAL_COMMS_UART_ID, DATA_BITS, STOP_BITS, PARITY);

    // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled(SERIAL_COMMS_UART_ID, false);

    // Set up a RX interrupt
    // We need to set up the handler first
    // Select correct interrut for the UART we are using
    int UART_IRQ = SERIAL_COMMS_UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, serial_comms_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(SERIAL_COMMS_UART_ID, true, false);

    // OK, all set up.
    // Lets send a basic string out, and then run a loop and wait for RX interrupts
    // The handler will count them, but also reflect the incoming data back with a slight change!
    DEBUG("Done setting up comms interrupts\n");
}

void setup_timers()
{
    // negative timeout means exact delay (rather than delay between callbacks)
    if (!add_repeating_timer_ms(REPORTING_INT_MS, reporting_callback, NULL, &timer)) {
 	panic("Failed to add reporting callback timer\n");
    }

    if (!add_repeating_timer_ms(TEMP_READ_INT_MS, read_temp_callback, NULL, &temp_read_timer)) {
 	panic("Failed to add temperature read callback timer\n");
    }

    if (!add_repeating_timer_ms(FAN_SPEED_UPDATE_INT_MS, set_fan_speeds, NULL, &fans_adjust_timer)) {
	panic("Failed to add fan speed update callback timer\n");
    }
}

void setup_onewire()
{
	one_wire.init();

        one_wire.single_device_read_rom(address);
        printf("Device Address: %02x%02x%02x%02x%02x%02x%02x%02x\n", address.rom[0], address.rom[1], address.rom[2], address.rom[3], address.rom[4], address.rom[5], address.rom[6], address.rom[7]);

}

void setup_pwm()
{
// TBD
}

void setup_leds()
{
   	int sm = 0, i;
	uint offset;

	pio = pio0;
	offset = pio_add_program(pio, &ws2812_program);

    	ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);
}

void core1_entry()
{
	while(1)
	{
		if (do_read_temps)
		{
		        one_wire.convert_temperature(address, true, false);
			//printf("Temperature: %3.1foC\n", one_wire.temperature(address));
			temperatures[0] = (int16_t)(one_wire.temperature(address) * 100.0f);
			do_read_temps = false;
		}
	}
}

int main()
{
	int i;
	stdio_init_all();
	
	gpio_init(LEDPIN);
	gpio_set_dir(LEDPIN, GPIO_OUT);

    // todo get free sm

	uint32_t color = 0xFF0000, cur_pixel = 0;
	uint32_t data[NUM_LEDS];
	
	for (i = 0 ; i < NUM_LEDS; i++) {
		data[i] = 0x0000FF;
	}

	multicore_launch_core1(core1_entry);
	setup_serial_comms_uart();
	setup_timers();
	setup_onewire();
	setup_leds();
#if 0	
	while (1)
	{
		gpio_put(LEDPIN, 1);
		sleep_ms(500);
		gpio_put(LEDPIN, 0);
		puts("Hello, World!\n");
		sleep_ms(500);
#if 1
		data[cur_pixel++] = color;

		if (cur_pixel == NUM_LEDS) {
			cur_pixel = 0;
			color >>= 8;
			
			if (!color) {
				color = 0xFF0000;
			}
		}
#endif	
		for (uint i = 0; i < NUM_LEDS; i++) {
			put_pixel(data[i]);
		}
	}
#endif
	
	while(1)
	{
		if (serial_buf_pidx != serial_buf_cidx)
		{
			process_message(&double_rx_buf[CMD_LEN * serial_buf_cidx]);
			serial_buf_cidx = (serial_buf_cidx + 1) % NUM_ENTRIES;
		}
#if 0
		if (do_read_temps)
		{
		        one_wire.convert_temperature(address, true, false);
			//printf("Temperature: %3.1foC\n", one_wire.temperature(address));
			temperatures[0] = (int16_t)(one_wire.temperature(address) * 100.0f);
			do_read_temps = false;
		}
#endif
#if 1
		if (do_display)
		{
			if (get_absolute_time() >= next_display_step_time)
			{
				for (i = 0; i < NUM_LEDS_IN_STRIP; i++) 
				{
					put_pixel(cur_prg->led_program_entry[cur_step].leds[i]);
				}

				if (++cur_step == cur_prg->num_steps)
				{
					cur_step = 0;
				}

				next_display_step_time = delayed_by_ms(get_absolute_time(),
					cur_prg->led_program_entry[cur_step].time);
			}
		}
#endif
	        tight_loop_contents();

	}
}

bool reporting_callback(repeating_timer_t *rt)
{
	if (wifi_connected && mqtt_connected)
	{
		// Code to actually read the temperatures...
		send_temperature(temperatures);
		// Code to actually read the fan speed..
		send_tacho(fans.speed);
	}
}

bool read_temp_callback(repeating_timer_t *rt)
{
	do_read_temps = true;
}

void set_fans_power_state(uint8_t state)
{
	if (state)
	{
		// set gpios to 1
	}
	else
	{
		// set gpios to 0
	}
}



void set_fan_pwm(uint8_t fan, uint8_t pwm)
{
	if (pwm > 100)
	{
		fans.auto_speed[fan] = true;
	}
	else
	{
		fans.auto_speed[fan] = false;
		if (pwm < PWM_LOW_THRESHOLD)
		{
			pwm = PWM_LOW_THRESHOLD;
		}
		fans.pwm[fan] = pwm;
		// set pwm for fan
	}
}

/* 
 * < 24 => 20
 * 24 <= x < 26 => 30
 * 26 <= x < 28 => 40
 * 28 <= x < 30 => 50
 * 30 <= x < 31 => 60
 * 31 <= x < 32 => 70
 * 32 <= x < 33 => 80
 * 33 <= x < 34 => 90
 * >= 34 => 100
 * N.B. Should this be user-updatable?
 */
#define NUM_POINTS_TEMP_VS_PWM	9
const uint8_t temp_vs_pwm_curve[][3] = {
	{0, 24, 20},
	{24, 26, 30},
	{26, 28, 40},
	{28, 30, 50},
	{30, 31, 60},
	{31, 32, 70},
	{32, 33, 80},
	{33, 34, 90},
	{34, 100, 100},
	{0, 0, 0}
};

bool set_fan_speeds(repeating_timer_t *rt)
{
	int i, row;
	for (i = 0; i < NUM_FANS; i++)
	{
		if (fans.auto_speed[i])
		{
			for (row = 0; !(temp_vs_pwm_curve[row][0] | temp_vs_pwm_curve[row][1] | temp_vs_pwm_curve[row][2]); row++)
			{
				if ((temp_vs_pwm_curve[row][0] < temperatures[i]) &&
				    (temperatures[i] <= temp_vs_pwm_curve[row][1]))
				{
					ERROR("Setting fan %d PWM to %d\n", temp_vs_pwm_curve[row][2]);
					set_fan_pwm(i, temp_vs_pwm_curve[row][2]);
				}
			}
		}
		else
		{
			set_fan_pwm(i, fans.pwm[i]);
		}
	}
}

void clear_strip()
{
	int i;
	for (i = 0; i < NUM_LEDS_IN_STRIP; i++) 
	{
		put_pixel(0);
		sleep_ms(10);
	}
}

void set_strip_intensity(uint32_t color)
{
	do_display = false;
	
	int i;
	for (i = 0; i < NUM_LEDS_IN_STRIP; i++) 
	{
		put_pixel(color);
	}
}

void switch_programs()
{
	do_display = false;
	
	clear_strip();

	if (cur_prg == &led_programs[0])
	{
		cur_prg = &led_programs[1];
		shadow_prg = &led_programs[0];
	}
	else
	{
		cur_prg = &led_programs[0];
		shadow_prg = &led_programs[1];
	}

	next_display_step_time = 0;
	cur_step = 0;
	do_display = true;
}

void resume_animation()
{
	do_display = true;
}
