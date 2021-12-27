#ifndef LED_STRIP_HELPERS_H
#define LED_STRIP_HELPERS_H

#include "macro_helpers.h"

/* 
 * +---------------+----------------+----------------+
 * | L0 - 60 - L17 | L18 - 60 - L35 | L36 - 60 - L53 |
 * | IC0 - IC5     | IC6 - IC11     | IC12 - IC17    |
 * +---------------+----------------+----------------+
 * | L54 - 60 - L71| L72 - 60 - L89 | L90 - 60 - L107|
 * | IC18 - IC23   | IC24 - IC29    | IC30 - IC35    |
 * +---------------+----------------+----------------+
 * | L108 -60 -L125|                |                |
 * | IC36 - IC41   |                |                |
 * +---------------+----------------+----------------+
*/

#define NUM_LEDS_IN_STRIP	42	/* all the IC leds in the strip */

/* Counting is done from top to bottom left to right
   i.e. LED 0 is in the leftmost top drawer
 */
#define MAX_DRAWERS			7	/* number of drawers with LEDs */

/* Fixed number of LEDs per drawer */
#define NUM_LEDS_PER_DRAWER	18

/* Each IC controls this many LEDs */
#define NUM_LEDS_PER_IC		3

/* Helper to keep macros compact */
#define NUM_ICS_PER_DRAWER \
	(NUM_LEDS_PER_DRAWER / NUM_LEDS_PER_IC)

/* Offset of the first LED IC in a drawer */
#define LED_START_OFFSET(drawer)	\
	((drawer) * NUM_ICS_PER_DRAWER)

/* 
 * Offset of the first LED IC in the next drawer. Use
 * in for loops with "<"
 */
#define LED_END_OFFSET(drawer)	\
	LED_START_OFFSET((drawer) + 1)

/* Set all LEDs in a drawer to a color */
#define SET_COLOR_DRAWER(arry, x,color)	\
	for (int CONCAT_LINE(i) =  LED_START_OFFSET(x); CONCAT_LINE(i) < LED_END_OFFSET(x); CONCAT_LINE(i) ++) { \
		arry[CONCAT_LINE(i)] = color; \
	}

/* Clear all LEDs in a strip */
#define CLEAR_STRIP(arry)	\
	for (int CONCAT_LINE(i) =  LED_START_OFFSET(0); CONCAT_LINE(i) < LED_START_OFFSET(MAX_DRAWERS); CONCAT_LINE(i) ++) { \
		arry[CONCAT_LINE(i)] = 0; \
	}

/* Clear all LEDs in a drawer */
#define CLEAR_DRAWER(arry, x)	\
	for (int CONCAT_LINE(i) =  LED_START_OFFSET(x); CONCAT_LINE(i) < LED_END_OFFSET(x); CONCAT_LINE(i) ++) { \
		arry[CONCAT_LINE(i)] = 0; \
	}

/* Defines for led colors */
#define COLOR_RED	0x0000FF
#define COLOR_GREEN	0x00FF00
#define COLOR_BLUE	0xFF0000

#endif /* LED_STRIP_HELPERS_H */