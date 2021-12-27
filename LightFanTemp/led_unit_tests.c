#include "led_helpers.h"

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#define MAKE_TEST(func)	\
	if (func()) { 				\
		fprintf(stderr, "Test " # func " failed\n"); \
		exit(-1); \
	}

int test1()
{
#undef TEST_DRAWER
#undef TEST_COLOR
#define TEST_DRAWER	0
#define TEST_COLOR	0x12345678
	if (LED_START_OFFSET(TEST_DRAWER) != 0) {
		fprintf(stderr,"Start test offset failed: expected %d, got %d\n", 0, LED_START_OFFSET(TEST_DRAWER));
		return -1;
	}

	if (LED_END_OFFSET(TEST_DRAWER) != 6) {
		fprintf(stderr,"End test offset failed: expected %d, got %d\n", 0, LED_END_OFFSET(TEST_DRAWER));
		return -2;
	}

	if (LED_END_OFFSET(MAX_DRAWERS - 1) != 42) {
		fprintf(stderr,"End test NUM_DRAWERS offset failed: expected %d, got %d\n", 42, LED_END_OFFSET(MAX_DRAWERS));
		return -3;
	}
	
	return 0;
}

int test2()
{
	uint32_t arry[NUM_LEDS_IN_STRIP];
	int i;

	CLEAR_STRIP(arry);

	for (i = 0; i < NUM_LEDS_IN_STRIP; i++) {
		if (arry[i] != 0) {
			fprintf(stderr,"Error: LED at pos %d doesn't match initial value %d\n", i, 0);
			return -1;
		}
	}

	return 0;
}

int test3()
{
#undef TEST_DRAWER
#undef TEST_COLOR
#define TEST_DRAWER	0
#define TEST_COLOR	0x12345678

	uint32_t arry[NUM_LEDS_IN_STRIP];
	int i;

	CLEAR_STRIP(arry);
	SET_COLOR_DRAWER(arry, TEST_DRAWER, TEST_COLOR);
	
	for(i = LED_START_OFFSET(TEST_DRAWER); i < LED_END_OFFSET(TEST_DRAWER); i++) {
		if (arry[i] != TEST_COLOR) {
			fprintf(stderr,"Error: LED at pos %d doesn't match color %d\n", i, TEST_COLOR);
			return -1;
		}
	}
	
	for(i = LED_END_OFFSET(TEST_DRAWER); i < LED_START_OFFSET(MAX_DRAWERS); i++) {
		if (arry[i] != 0) {
			fprintf(stderr,"Error: LED at pos %d doesn't match initial value %d\n", i, 0);
			return -2;
		}
	}
	return 0;
}

int test4()
{
#undef TEST_DRAWER
#undef TEST_COLOR
#define TEST_DRAWER	2
#define TEST_COLOR	0x12345678

	uint32_t arry[NUM_LEDS_IN_STRIP];
	int i;

	CLEAR_STRIP(arry);
	SET_COLOR_DRAWER(arry, TEST_DRAWER, TEST_COLOR);
	
	for(i = LED_START_OFFSET(TEST_DRAWER); i < LED_END_OFFSET(TEST_DRAWER); i++) {
		if (arry[i] != TEST_COLOR) {
			fprintf(stderr,"Error: LED at pos %d doesn't match color %d\n", i, TEST_COLOR);
			return -1;
		}
	}
	
	CLEAR_DRAWER(arry, TEST_DRAWER);

	for(i = LED_END_OFFSET(TEST_DRAWER); i < LED_END_OFFSET(TEST_DRAWER); i++) {
		if (arry[i] != 0) {
			fprintf(stderr,"Error: LED at pos %d doesn't match expected value %d\n", i, 0);
			return -2;
		}
	}
	return 0;
}

void main() {
	MAKE_TEST(test1);
	MAKE_TEST(test2);
	MAKE_TEST(test3);
	MAKE_TEST(test4);
		
	fprintf(stderr,"ALL TEST PASS!\n");
}
