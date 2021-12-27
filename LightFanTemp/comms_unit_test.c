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
int test1()
{
	char msg[] = {START_CHAR, 0x6c, 0x00, 0x50, 0x07, 'H', 'e', 'l', 'l', 'o', '!', 0, END_CHAR};
	int i = 0;

	do {
		uart_rx(msg[i]);
		if (msg[i] == END_CHAR) {
			break;
		}
		i++;
	} while(1);

	return 0;
}

void main() {
	MAKE_TEST(test1);
		
	fprintf(stderr,"ALL TEST PASS!\n");
}
