#ifndef SERIAL_COMMS_H
#define SERIAL_COMMS_H

#include <stdint.h>
#include <stdarg.h>

#define START_CHAR	0xa5
#define ESCAPE_CHAR	0xde
#define END_CHAR	0x5a

#define CMD_LEN		256

#define __WEAK __attribute__((weak))

#define DEBUG(...) fprintf (stderr, __VA_ARGS__)

#define LED_STATUS_1	0
#define LED_STATUS_2	1

enum cmd_type {
		MODEM_RESET		= 0,

		WIFI_CONNECTED 	= 0x10,
		WIFI_DISCONNECTED,
		WIFI_MQTT_CONNECTED,
		WIFI_MQTT_DISCONNECTED

		SET_LED_COLOR 	= 0x20,
		SET_LED_COLOR_BULK,
		GET_LED_COLOR,
		GET_LED_COLOR_BULK,

		SET_FAN_POWER_STATE = 0x30,
		SET_FAN_PWM_PERC,
		GET_FAN_PWM_PERC,

		GET_TEMP = 0x40,
		SEND_TEMP,
		
		SEND_LOG = 0x50,
};

enum parser_state {
	MSG_START,
	MSG_PARITY_RCV,
	MSG_RCV,
	MSG_END
};

struct serial_cmd {
	uint8_t parity;
	uint8_t seq;
	uint8_t cmd_type;
	uint8_t cmd_len;
	uint8_t cmd[];
};

void uart_rx(unsigned char ch);

void process_message(char buf[]);

#endif /* SERIAL_COMMS_H */
