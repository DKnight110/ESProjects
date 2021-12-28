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

uint8_t test_rx_buf[255], rx_pos;
char double_rx_buf[CMD_LEN*NUM_ENTRIES];

void put_char(unsigned char ch)
{
	test_rx_buf[rx_pos++] = ch;
}

extern uint8_t rx_seq;

void parse_log(uint8_t *cmd)
{
	strcpy(test_rx_buf, cmd);
	DEBUG("Got %s\n", test_rx_buf);
}

int test1()
{
	char msg[] = {START_CHAR, 0x6c, 0x00, 0x50, 0x07, 'H', 'e', 'l', 'l', 'o', '!', 0, END_CHAR};
	char exp_str[] = {'H', 'e', 'l', 'l', 'o', '!', 0};
	int i = 0, ret;
	rx_seq = 0;

	for (i = 0; i < sizeof(msg); i++) {
		uart_rx(msg[i]);
	}

	if (serial_buf_cidx == serial_buf_pidx)
	{
		fprintf(stderr, "Failed pidx %d cidx %d", serial_buf_cidx, serial_buf_pidx);
		return 1;
	}

	process_message(&double_rx_buf[CMD_LEN * serial_buf_cidx]);
	serial_buf_cidx = (serial_buf_cidx + 1) % NUM_ENTRIES;

	i = strcmp(exp_str, test_rx_buf);
	if (!i) {
		fprintf(stderr,"Strings OK!\n");
		return 0;
	} else {
		fprintf(stderr, "Failed str compare on pos %d\n", i);
		return 1;
	}

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
		ret = uart_rx(test_rx_buf[i++]);
	} while(ret);

	if (serial_buf_cidx == serial_buf_pidx)
	{
		fprintf(stderr, "Failed pidx %d cidx %d", serial_buf_cidx, serial_buf_pidx);
		return 1;
	}

	process_message(&double_rx_buf[CMD_LEN * serial_buf_cidx]);
	serial_buf_cidx = (serial_buf_cidx + 1) % NUM_ENTRIES;

	i = strcmp(test_str, test_rx_buf);
	if (!i) {
		fprintf(stderr,"Strings OK: sent \"%s\", got \"%s\"\n", test_str, test_rx_buf);
		return 0;
	} else {
		fprintf(stderr, "Failed str compare on pos %d: sent \"%s\", got \"%s\"\n", i, test_str, test_rx_buf);
		return 1;
	}
}

int test3()
{
	char msg[] = {START_CHAR, 0xde, 0x5a, 0x00, 0x50, 0x07, 'H', 'e', 'l', 'l', ']', '!', 0, END_CHAR};
	char test_str[] = {'H', 'e', 'l', 'l', ']', '!', 0};
	int i = 0, ret;

	rx_seq = 0;
	do {
		ret = uart_rx(msg[i++]);
	} while(ret);

	if (serial_buf_cidx == serial_buf_pidx)
	{
		fprintf(stderr, "Failed pidx %d cidx %d", serial_buf_cidx, serial_buf_pidx);
		return 1;
	}

	process_message(&double_rx_buf[CMD_LEN * serial_buf_cidx]);
	serial_buf_cidx = (serial_buf_cidx + 1) % NUM_ENTRIES;

	i = strcmp(test_str, test_rx_buf);
	if (!i) {
		fprintf(stderr,"Strings OK: sent \"%s\", got \"%s\"\n", test_str, test_rx_buf);
		return 0;
	} else {
		fprintf(stderr, "Failed str compare on pos %d: sent \"%s\", got \"%s\"\n", i, test_str, test_rx_buf);
		return 1;
	}
	return 0;
}

int test4()
{
	char msg[] = {START_CHAR, 0xc6, 0xde, 0x5a, 0x50, 0x07, 'H', 'e', 'l', 'l', 'o', '!', 0, END_CHAR};
	char test_str[] = {'H', 'e', 'l', 'l', 'o', '!', 0};
	int i = 0, ret;

	rx_seq = 90;
	do {
		ret = uart_rx(msg[i++]);
	} while(ret);

	if (serial_buf_cidx == serial_buf_pidx)
	{
		fprintf(stderr, "Failed pidx %d cidx %d", serial_buf_cidx, serial_buf_pidx);
		return 1;
	}

	process_message(&double_rx_buf[CMD_LEN * serial_buf_cidx]);
	serial_buf_cidx = (serial_buf_cidx + 1) % NUM_ENTRIES;

	i = strcmp(test_str, test_rx_buf);
	if (!i) {
		fprintf(stderr,"Strings OK: sent \"%s\", got \"%s\"\n", test_str, test_rx_buf);
		return 0;
	} else {
		fprintf(stderr, "Failed str compare on pos %d: sent \"%s\", got \"%s\"\n", i, test_str, test_rx_buf);
		return 1;
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
