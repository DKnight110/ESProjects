#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "ws2812.pio.h"
#include "serial_comms.h"

#include "macro_helpers.h"

#include "hardware/uart.h"
#include "hardware/irq.h"

#include "one_wire.h"

#define SERIAL_COMMS_UART_ID	uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

#define SERIAL_COMMS_TX_PIN	8
#define SERIAL_COMMS_RX_PIN	9

#define TEMP_MEAS_PIN		3

char double_rx_buf[CMD_LEN*NUM_ENTRIES];
int16_t temperatures[NUM_TEMP_SENSORS] = {0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000};

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
bool timer_callback(repeating_timer_t *rt);
bool read_temp_callback(repeating_timer_t *rt);
bool do_read_temps;

One_wire one_wire(TEMP_MEAS_PIN);
rom_address_t address{};

static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}
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

int main()
{
	stdio_init_all();
	
	gpio_init(LEDPIN);
	gpio_set_dir(LEDPIN, GPIO_OUT);

    // todo get free sm
    PIO pio = pio0;
    int sm = 0, i = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
	uint32_t color = 0xFF0000, cur_pixel = 0;
	uint32_t data[NUM_LEDS];
	
	for (i = 0 ; i < NUM_LEDS; i++) {
		data[i] = 0x0000FF;
	}

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

	setup_serial_comms_uart();

   repeating_timer_t timer;
   repeating_timer_t temp_read_timer;

    // negative timeout means exact delay (rather than delay between callbacks)
    if (!add_repeating_timer_ms(5000, timer_callback, NULL, &timer)) {
        printf("Failed to add timer\n");
        return 1;
    }

    if (!add_repeating_timer_ms(1000, read_temp_callback, NULL, &temp_read_timer)) {
        printf("Failed to add timer\n");
        return 1;
    }

	one_wire.init();

        one_wire.single_device_read_rom(address);
        printf("Device Address: %02x%02x%02x%02x%02x%02x%02x%02x\n", address.rom[0], address.rom[1], address.rom[2], address.rom[3], address.rom[4], address.rom[5], address.rom[6], address.rom[7]);
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

		if (do_read_temps)
		{
		        one_wire.convert_temperature(address, true, false);
			//printf("Temperature: %3.1foC\n", one_wire.temperature(address));
			temperatures[0] = (int16_t)(one_wire.temperature(address) * 10.0);
			do_read_temps = false;
		}

	        tight_loop_contents();

	}
}

bool timer_callback(repeating_timer_t *rt)
{
	uint16_t fan_speed[NUM_FANS] = {0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700};

	// Code to actually read the temperatures...
	send_temperature(temperatures);
	// Code to actually read the fan speed..
	send_tacho(fan_speed);

}

bool read_temp_callback(repeating_timer_t *rt)
{
	do_read_temps = true;
}

