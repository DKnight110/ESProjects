#ifndef SERIAL_COMMS_H
#define SERIAL_COMMS_H

#include <stdint.h>
#include <stdarg.h>

#define START_CHAR	0xa5
#define ESCAPE_CHAR	0xde
#define END_CHAR	0x5a

#define CMD_LEN		255

#define __WEAK __attribute__((weak))

#ifndef ESP8266
	#if ERR_LEVEL == 3
		#define DEBUG(...) fprintf (stderr, __VA_ARGS__)
	#else
	#define DEBUG(...)
	#endif

	#define ERROR(...) fprintf (stderr, __VA_ARGS__)
#else
	#if ERR_LEVEL == 3
		#define DEBUG(...) send_log("DBG: ", ...)
	#else
		#define DEBUG(...)
	#endif
	#define ERROR(...) send_log("ERR: ", __VA_ARGS__)
#endif

#define LED_STATUS_1	0
#define LED_STATUS_2	1

enum cmd_type {
		MODEM_RESET		= 0,

		WIFI_CONNECTED 	= 0x10,
		WIFI_DISCONNECTED,
		MQTT_CONNECTED,
		MQTT_DISCONNECTED,

		SET_LED_COLOR 	= 0x20,
		SET_LED_COLOR_BULK,
		GET_LED_COLOR,
		GET_LED_COLOR_BULK,
		SET_LED_TIME,
		SET_LED_TIME_BULK,

		SET_FAN_POWER_STATE = 0x30,
		SET_FAN_PWM_PERC,
		GET_FAN_PWM_PERC,

		GET_TEMP = 0x40,
		SEND_TEMP,
		
		SEND_LOG = 0x50,

		SWITCH_PROGRAMS = 0x60,
};

enum parser_state {
	MSG_START,
	MSG_PARITY_RCV,
	MSG_RCV,
	MSG_ESCAPE,
	MSG_END
};

struct serial_cmd {
	uint8_t parity;
	uint8_t seq;
	uint8_t cmd_type;
	uint8_t cmd_len;
	char cmd[];
};

#ifdef __cplusplus
 extern "C" {
#endif
int uart_rx(unsigned char ch);

void process_message(char buf[]);

void send_log(const char *format,...);

void put_char(unsigned char ch);

#ifdef __cplusplus
}
#endif
#endif /* SERIAL_COMMS_H */
