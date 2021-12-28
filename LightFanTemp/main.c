#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "ws2812.pio.h"
#include "serial_comms.h"

#include "hardware/uart.h"
#include "hardware/irq.h"

#define SERIAL_COMMS_UART_ID	uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

#define SERIAL_COMMS_TX_PIN	8
#define SERIAL_COMMS_RX_PIN	9

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
 * PIN_FAN_ON = GP20 (pin 26)
 * ESP8266_RST = GP21 (pin 27)
*/

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
    uart_set_fifo_enabled(SERIAL_COMMS_UART_ID, true);

    // Set up a RX interrupt
    // We need to set up the handler first
    // Select correct interrupt for the UART we are using
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
	        tight_loop_contents();
	}
}
