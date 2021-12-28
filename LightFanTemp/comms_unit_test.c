#include "macro_helpers.h"
#include "serial_comms.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAKE_TEST(func)	\
	if (func()) { 				\
		fprintf(stderr, "Test " # func " failed\n"); \
		exit(-1); \
	}

uint8_t rx_buf[255], rx_pos;
void put_char(unsigned char ch)
{
	rx_buf[rx_pos++] = ch;
}

extern uint8_t rx_seq;

void parse_log(uint8_t *cmd)
{
	strcpy(rx_buf, cmd);
	DEBUG("Got %s\n", rx_buf);
}

int test1()
{
	char msg[] = {START_CHAR, 0x6c, 0x00, 0x50, 0x07, 'H', 'e', 'l', 'l', 'o', '!', 0, END_CHAR};
	int i = 0, ret;
	rx_seq = 0;

	do {
		ret = uart_rx(msg[i++]);
	} while(ret);

	return 0;
}

int test2()
{
	int i = 0, ret;
	char test_str[128];
	rx_seq = 0;
	rx_pos = 0;

	sprintf(test_str, "This is a formated string %d!\nWith multiple lines and chars (%c)", 5, 'Z');
	send_log(test_str);

	do {
		ret = uart_rx(rx_buf[i++]);
	} while(ret);

	i = strcmp(test_str, rx_buf);
	if (!i) {
		fprintf(stderr,"Strings OK: sent \"%s\", got \"%s\"\n", test_str, rx_buf);
		return 0;
	} else {
		fprintf(stderr, "Failed str compare on pos %d: sent \"%s\", got \"%s\"\n", i, test_str, rx_buf);
		return 1;
	}
}

int test3()
{
	char msg[] = {START_CHAR, 0x5a, 0x2D, 0x50, 0x07, 'H', '8', 'l', 'l', ']', '!', 0, END_CHAR};
	int i = 0, ret;

	rx_seq = 45;
	do {
		ret = uart_rx(msg[i++]);
	} while(ret);

	return 0;
}

void main() {
	MAKE_TEST(test1);
	MAKE_TEST(test2);
	MAKE_TEST(test3);

	fprintf(stderr,"ALL TEST PASS!\n");
}
