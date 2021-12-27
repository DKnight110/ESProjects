/* Helper macros for macros */
#ifndef MACRO_HELPERS_H
#define MACRO_HELPERS_H

#include "led_helpers.h"

#define TOKENPASTE(x, y)	x ## y
#define TOKENPASTE2(x, y)	TOKENPASTE(x, y)
#define CONCAT_LINE(x)	TOKENPASTE2(x, __LINE__)

#define LED_PROGRAM_END	{ 0, 0, {0} }

struct led_program_entry {
	uint16_t time;	/* How long to keep this before switching to the next */
	uint32_t leds[NUM_LEDS_IN_STRIP];	/* leds array for this step */
};

#define NUM_LED_PROGRAMS		2

#define NUM_STEPS_IN_PROGRAM	(2 * NUM_LEDS_IN_STRIP - 1)

struct led_programs {
	struct led_program_entry led_program_entry[NUM_STEPS_IN_PROGRAM];
};

#endif /* MACRO_HELPERS_H */