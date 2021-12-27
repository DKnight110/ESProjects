/* Helper macros for macros */
#ifndef MACRO_HELPERS_H
#define MACRO_HELPERS_H

#include "led_helpers.h"

#define TOKENPASTE(x, y)	x ## y
#define TOKENPASTE2(x, y)	TOKENPASTE(x, y)
#define CONCAT_LINE(x)	TOKENPASTE2(x, __LINE__)

#define LED_PROGRAM_END	{ 0, 0, {0} }

#endif /* MACRO_HELPERS_H */