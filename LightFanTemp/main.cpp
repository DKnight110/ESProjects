#if 1
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "ws2812.pio.h"
#include "serial_comms.h"
#include "pin_defines.h"

#include "macro_helpers.h"

#include "hardware/uart.h"
#include "hardware/irq.h"

#include "one_wire.h"


#define SERIAL_COMMS_UART_ID	uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

/* FAN PWM */
#define PWM_TOP	4999 // 125 MHz / 25 kHz - 1
#define TACHO_SPEED_MEAS_INTERVAL 5 // s

void fanspeed_callback(uint gpio, uint32_t events);

/* timer intervals defines */
#define REPORTING_INT_MS			5000
#define TEMP_READ_INT_MS			1000
#define FAN_SPEED_UPDATE_INT_MS		10000
#define LED_DISPLAY_UPDATE_INT_MS	10

#define PWM_LOW_THRESHOLD	20

#define INVALID_TEMPERATURE	0x0bad
#define INVALID_SPEED		0xbeef

int chars_rxed;

/* One wire */
One_wire one_wire(PIN_TEMP_MEAS);
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
	uint16_t  fan_count[NUM_FANS];
	uint8_t pwm[NUM_FANS];
	uint8_t pins[NUM_FANS];

};

struct fans fans = {
	/* default all are auto speed */
	{ 1, 1, 1, 1, 1, 1, 1 },
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
	},
	{
		PIN_PWM_0,
		PIN_PWM_1,
		PIN_PWM_2,
		PIN_PWM_3,
		PIN_PWM_4,
		PIN_PWM_5,
		PIN_PWM_6
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
    gpio_set_function(PIN_MDM_TX, GPIO_FUNC_UART);
//    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(PIN_MDM_RX, GPIO_FUNC_UART);

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
#if 0
        one_wire.single_device_read_rom(address);
        printf("Device Address: %02x%02x%02x%02x%02x%02x%02x%02x\n", address.rom[0], address.rom[1], address.rom[2], address.rom[3], address.rom[4], address.rom[5], address.rom[6], address.rom[7]);
#endif
}

void setup_pwm()
{
	pwm_config cfg = pwm_get_default_config();
	pwm_config_set_wrap(&cfg, PWM_TOP);

	/* link pins to fans */
	fans.pins[0] = PIN_PWM_0;
	fans.pins[1] = PIN_PWM_1;
	fans.pins[2] = PIN_PWM_2;
	fans.pins[3] = PIN_PWM_3;
	fans.pins[4] = PIN_PWM_4;
	fans.pins[5] = PIN_PWM_5;
	fans.pins[6] = PIN_PWM_6;

	pwm_init(pwm_gpio_to_slice_num(PIN_PWM_0), &cfg, true);
	pwm_set_gpio_level(PIN_PWM_0, 0.5f * (PWM_TOP + 1));
	gpio_set_function(PIN_PWM_0, GPIO_FUNC_PWM);

	pwm_init(pwm_gpio_to_slice_num(PIN_PWM_1), &cfg, true);
	pwm_set_gpio_level(PIN_PWM_1, 0.5f * (PWM_TOP + 1));
	gpio_set_function(PIN_PWM_1, GPIO_FUNC_PWM);

	pwm_init(pwm_gpio_to_slice_num(PIN_PWM_2), &cfg, true);
	pwm_set_gpio_level(PIN_PWM_2, 0.5f * (PWM_TOP + 1));
	gpio_set_function(PIN_PWM_2, GPIO_FUNC_PWM);

	pwm_init(pwm_gpio_to_slice_num(PIN_PWM_3), &cfg, true);
	pwm_set_gpio_level(PIN_PWM_3, 0.5f * (PWM_TOP + 1));
	gpio_set_function(PIN_PWM_3, GPIO_FUNC_PWM);

	pwm_init(pwm_gpio_to_slice_num(PIN_PWM_4), &cfg, true);
	pwm_set_gpio_level(PIN_PWM_4, 0.5f * (PWM_TOP + 1));
	gpio_set_function(PIN_PWM_4, GPIO_FUNC_PWM);

	pwm_init(pwm_gpio_to_slice_num(PIN_PWM_5), &cfg, true);
	pwm_set_gpio_level(PIN_PWM_5, 0.5f * (PWM_TOP + 1));
	gpio_set_function(PIN_PWM_5, GPIO_FUNC_PWM);

	pwm_init(pwm_gpio_to_slice_num(PIN_PWM_6), &cfg, true);
	pwm_set_gpio_level(PIN_PWM_6, 0.5f * (PWM_TOP + 1));
	gpio_set_function(PIN_PWM_6, GPIO_FUNC_PWM);
}

void setup_gpios()
{
	gpio_init(PIN_FAN_ON_1);
	gpio_set_dir(PIN_FAN_ON_1, GPIO_OUT);

	gpio_init(PIN_FAN_ON_2);
	gpio_set_dir(PIN_FAN_ON_2, GPIO_OUT);

	gpio_clr_mask(PIN_FANS_MASK);

	gpio_init(ESP8266_RST);
	gpio_set_dir(ESP8266_RST, GPIO_OUT);
	gpio_set_pulls(ESP8266_RST, true, false);

	gpio_init(PIN_TACHO_0);
	gpio_set_dir(PIN_TACHO_0, GPIO_IN);
	gpio_set_pulls(PIN_TACHO_0, true, false);

	gpio_init(PIN_TACHO_1);
	gpio_set_dir(PIN_TACHO_1, GPIO_IN);
	gpio_set_pulls(PIN_TACHO_1, true, false);

	gpio_init(PIN_TACHO_2);
	gpio_set_dir(PIN_TACHO_2, GPIO_IN);
	gpio_set_pulls(PIN_TACHO_2, true, false);

	gpio_init(PIN_TACHO_3);
	gpio_set_dir(PIN_TACHO_3, GPIO_IN);
	gpio_set_pulls(PIN_TACHO_3, true, false);

	gpio_init(PIN_TACHO_4);
	gpio_set_dir(PIN_TACHO_4, GPIO_IN);
	gpio_set_pulls(PIN_TACHO_4, true, false);

	gpio_init(PIN_TACHO_5);
	gpio_set_dir(PIN_TACHO_5, GPIO_IN);
	gpio_set_pulls(PIN_TACHO_5, true, false);

	gpio_init(PIN_TACHO_6);
	gpio_set_dir(PIN_TACHO_6, GPIO_IN);
	gpio_set_pulls(PIN_TACHO_6, true, false);
}

/* Done in a separate function, the IRQ handler needs to be
 * on core1
 */
void setup_fan_speed_gpios(bool enabled)
{
	gpio_set_irq_enabled_with_callback(PIN_TACHO_0, GPIO_IRQ_EDGE_RISE, enabled, &fanspeed_callback);

	gpio_set_irq_enabled_with_callback(PIN_TACHO_1, GPIO_IRQ_EDGE_RISE, enabled, &fanspeed_callback);

	gpio_set_irq_enabled_with_callback(PIN_TACHO_2, GPIO_IRQ_EDGE_RISE, enabled, &fanspeed_callback);

	gpio_set_irq_enabled_with_callback(PIN_TACHO_3, GPIO_IRQ_EDGE_RISE, enabled, &fanspeed_callback);

	gpio_set_irq_enabled_with_callback(PIN_TACHO_4, GPIO_IRQ_EDGE_RISE, enabled, &fanspeed_callback);

	gpio_set_irq_enabled_with_callback(PIN_TACHO_5, GPIO_IRQ_EDGE_RISE, enabled, &fanspeed_callback);

	gpio_set_irq_enabled_with_callback(PIN_TACHO_6, GPIO_IRQ_EDGE_RISE, enabled, &fanspeed_callback);
}

void setup_leds()
{
   	int sm = 0, i;
	uint offset;

	gpio_init(PIN_LED);
	gpio_set_dir(PIN_LED, GPIO_OUT);

    // todo get free sm
	pio = pio0;
	offset = pio_add_program(pio, &ws2812_program);

    ws2812_program_init(pio, sm, offset, PIN_LED, 800000, IS_RGBW);
}

/*
 * Full MAC addresses for sensors. Will use last byte only
	{0x28, 0x44, 0xE0, 0xE4, 0x0C, 0x00, 0x00, 0x91},
	{0x28, 0x58, 0x83, 0xE4, 0x0C, 0x00, 0x00, 0xE8},
	{0x28, 0x55, 0x96, 0xE4, 0x0C, 0x00, 0x00, 0x0C},
	{0x28, 0xDE, 0x8D, 0xE5, 0x0C, 0x00, 0x00, 0x9D},
	{0x28, 0xC7, 0x1B, 0xE5, 0x0C, 0x00, 0x00, 0x7B},
	{0x28, 0x79, 0xC6, 0xE5, 0x0C, 0x00, 0x00, 0xDD},
	{0x28, 0x4A, 0xE3, 0xE4, 0x0C, 0x00, 0x00, 0xCC}
*/
/* 0xFF means not populated */
uint8_t sensor_adresses[] = {
	0x91, 0xE8, 0x0C, 
	0x9D, 0x7B, 0xDD,
	0xFF, 0xFF, 0xCC
};

void fanspeed_callback(uint gpio, uint32_t events)
{
	switch (gpio)
	{
		case PIN_TACHO_0:
			fans.fan_count[0]++;
			break;
		case PIN_TACHO_1:
			fans.fan_count[1]++;
			break;

		case PIN_TACHO_2:
			fans.fan_count[2]++;
			break;

		case PIN_TACHO_3:
			fans.fan_count[3]++;
			break;

		case PIN_TACHO_4:
			fans.fan_count[4]++;
			break;

		case PIN_TACHO_5:
			fans.fan_count[5]++;
			break;

		case PIN_TACHO_6:
			fans.fan_count[6]++;
			break;
	}
}

void core1_entry()
{
	absolute_time_t start_meas_time = get_absolute_time(),
					next_fan_speed_measurement_time =
						delayed_by_ms(get_absolute_time(), TACHO_SPEED_MEAS_INTERVAL * 1000);

	/* Enable interrupts for TACHO */
	setup_fan_speed_gpios(true);

	while(1)
	{
		if (get_absolute_time() >= next_fan_speed_measurement_time)
		{
			setup_fan_speed_gpios(false);
			for (int i = 0; i < NUM_FANS; i++)
			{
				fans.speed[i] = fans.fan_count[i] * 6; // = 60 /  2 / TACHO_SPEED_MEAS_INTERVAL; // 2 ticks per revoluion
				ERROR("FAN %d count %d\n", i, fans.fan_count[i]);
				fans.fan_count[i] = 0;
			}
			next_fan_speed_measurement_time = delayed_by_ms(get_absolute_time(), TACHO_SPEED_MEAS_INTERVAL * 1000);
			setup_fan_speed_gpios(true);
		}

		if (do_read_temps)
		{
#if 0
		        one_wire.convert_temperature(address, true, false);
			//printf("Temperature: %3.1foC\n", one_wire.temperature(address));
			temperatures[0] = (int16_t)(one_wire.temperature(address) * 100.0f);
#endif
			int count = one_wire.find_and_count_devices_on_bus();
			rom_address_t null_address{};
			one_wire.convert_temperature(null_address, true, true);
			for (int i = 0; i < count; i++) {
				auto address = One_wire::get_address(i);
//				printf("Address: %02x%02x%02x%02x%02x%02x%02x%02x\r\n", address.rom[0], address.rom[1], address.rom[2],
//					   address.rom[3], address.rom[4], address.rom[5], address.rom[6], address.rom[7]);
				//printf("Temperature: %3.1foC\n", one_wire.temperature(address));
				for (int j = 0; j < NUM_TEMP_SENSORS; j++)		
				{
					if (sensor_adresses[j] == address.rom[7])
					{
						temperatures[i] = (int16_t)(one_wire.temperature(address) * 100.0f);
					}
				}
			}
			do_read_temps = false;
		}

	}
}

void reset_modem(void)
{
	gpio_clr_mask(1 << ESP8266_RST);
	sleep_ms(500);
	gpio_set_mask(1 << ESP8266_RST);
}
int main()
{
	int i;
	stdio_init_all();
	setup_serial_comms_uart();

	setup_gpios();
	
	reset_modem();

	setup_timers();
	setup_onewire();
	setup_pwm();
	setup_leds();
	multicore_launch_core1(core1_entry);
	
	while(1)
	{
		if (serial_buf_pidx != serial_buf_cidx)
		{
			process_message(&double_rx_buf[CMD_LEN * serial_buf_cidx]);
			serial_buf_cidx = (serial_buf_cidx + 1) % NUM_ENTRIES;
		}

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
		gpio_set_mask(PIN_FANS_MASK);
	}
	else
	{
		gpio_clr_mask(PIN_FANS_MASK);
	}
}

void set_fan_pwm(uint8_t fan, uint8_t pwm)
{
	if (pwm > 100)
	{
		fans.auto_speed[fan] = true;
		ERROR("Setting fan %d PWM to auto\n", fan);
	}
	else
	{
		fans.auto_speed[fan] = false;
		if (pwm < PWM_LOW_THRESHOLD)
		{
			pwm = PWM_LOW_THRESHOLD;
		}
		fans.pwm[fan] = pwm;
		ERROR("Setting fan %d PWM to %d\n", fan, pwm);
		pwm_set_gpio_level(fans.pins[fan], (pwm / 100.f) * (PWM_TOP + 1));
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
#define PWM_CURVE_INVALID	0xdead

const uint16_t temp_vs_pwm_curve[][3] = {
	{0, 2400, 20},
	{2400, 2600, 30},
	{2600, 2800, 40},
	{2800, 3000, 50},
	{3000, 3100, 60},
	{3100, 3200, 70},
	{3200, 3300, 80},
	{3300, 3400, 90},
	{3400, 10000, 100},
	{PWM_CURVE_INVALID, PWM_CURVE_INVALID, PWM_CURVE_INVALID}
};

bool set_fan_speeds(repeating_timer_t *rt)
{
	int i, row;
	for (i = 0; i < NUM_FANS; i++)
	{
		if (fans.auto_speed[i])
		{
			for (row = 0; temp_vs_pwm_curve[row][0] != PWM_CURVE_INVALID; row++)
			{
				if ((temp_vs_pwm_curve[row][0] < temperatures[i]) &&
				    (temperatures[i] <= temp_vs_pwm_curve[row][1]))
				{
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

void light_drawer(uint8_t drawer, uint32_t color)
{
	int i;
	do_display = false;
	
	clear_strip();

	for (i = 0; i < NUM_LEDS_IN_STRIP; i++)
	{
		if ((i >= LED_START_OFFSET(drawer)) && (i < LED_END_OFFSET(drawer)))
		{
			put_pixel(color);
		}
		else
		{
			put_pixel(0);
		}
	}
}

#else
#if PWM_BLABLA
/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

// This example drives a PWM output at a range of duty cycles, and uses
// another PWM slice in input mode to measure the duty cycle. You'll need to
// connect these two pins with a jumper wire:
const uint OUTPUT_PIN = 2;
const uint MEASURE_PIN = 5;

#if 0
float measure_duty_cycle(uint gpio) {
    // Only the PWM B pins can be used as inputs.
    assert(pwm_gpio_to_channel(gpio) == PWM_CHAN_B);
    uint slice_num = pwm_gpio_to_slice_num(gpio);

    // Count once for every 100 cycles the PWM B input is high
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_HIGH);
    pwm_config_set_clkdiv(&cfg, 100);
    pwm_init(slice_num, &cfg, false);
    gpio_set_function(gpio, GPIO_FUNC_PWM);

    pwm_set_enabled(slice_num, true);
    sleep_ms(10);
    pwm_set_enabled(slice_num, false);
    float counting_rate = clock_get_hz(clk_sys) / 100;
    float max_possible_count = counting_rate * 0.01;
    return pwm_get_counter(slice_num) / max_possible_count;
}
#endif
const float test_duty_cycles[] = {
        0.f,
        0.1f,
        0.5f,
        0.9f,
        1.f
};

int main() {
    stdio_init_all();
    printf("\nPWM duty cycle measurement example\n");

    // Configure PWM slice and set it running
    const uint count_top = 1000;
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, count_top);
    pwm_init(pwm_gpio_to_slice_num(OUTPUT_PIN), &cfg, true);

    // Note we aren't touching the other pin yet -- PWM pins are outputs by
    // default, but change to inputs once the divider mode is changed from
    // free-running. It's not wise to connect two outputs directly together!
    gpio_set_function(OUTPUT_PIN, GPIO_FUNC_PWM);

	gpio_set_pulls(MEASURE_PIN,true, false);
	while(1) {
    // For each of our test duty cycles, drive the output pin at that level,
    // and read back the actual output duty cycle using the other pin. The two
    // values should be very close!
    for (int i = 0; i < count_of(test_duty_cycles); ++i) {
        float output_duty_cycle = test_duty_cycles[i];
        pwm_set_gpio_level(OUTPUT_PIN, output_duty_cycle * (count_top + 1));
//        float measured_duty_cycle = measure_duty_cycle(MEASURE_PIN);
//        printf("Output duty cycle = %.1f%%, measured input duty cycle = %.1f%%\n",
//               output_duty_cycle * 100.f, measured_duty_cycle * 100.f);

	sleep_ms(5000);
    }
	tight_loop_contents();
	}
}
#endif
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "one_wire.h"

#define TEMP_SENSE_GPIO_PIN 2
//#define EXIT_GPIO_PIN 22

int main() {
	stdio_init_all();
	One_wire one_wire(TEMP_SENSE_GPIO_PIN);
	one_wire.init();
	//gpio_init(EXIT_GPIO_PIN);
	//gpio_set_dir(EXIT_GPIO_PIN, GPIO_IN);
	//gpio_pull_up(EXIT_GPIO_PIN);
	sleep_ms(1);
	while (1) {
		int count = one_wire.find_and_count_devices_on_bus();
		rom_address_t null_address{};
		one_wire.convert_temperature(null_address, true, true);
		for (int i = 0; i < count; i++) {
			auto address = One_wire::get_address(i);
			printf("Address: %02x%02x%02x%02x%02x%02x%02x%02x\r\n", address.rom[0], address.rom[1], address.rom[2],
				   address.rom[3], address.rom[4], address.rom[5], address.rom[6], address.rom[7]);
			printf("Temperature: %3.1foC\n", one_wire.temperature(address));
		}
		sleep_ms(1000);
	}
	return 0;
}
#endif
