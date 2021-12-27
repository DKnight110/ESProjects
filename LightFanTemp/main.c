#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "ws2812.pio.h"

const uint LEDPIN = 25;

#ifdef PICO_DEFAULT_WS2812_PIN
#define WS2812_PIN PICO_DEFAULT_WS2812_PIN
#else
// default to pin 2 if the board doesn't have a default WS2812 pin defined
#define WS2812_PIN 4
#endif

#define IS_RGBW 0
#define NUM_LEDS 49

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
}
