#ifndef SERIAL_COMMS_H
#define SERIAL_COMMS_H

#include <stdint.h>
#include <stdarg.h>

#define START_CHAR	0xa5
#define ESCAPE_CHAR	0xde
#define END_CHAR	0x5a

#ifndef CMD_LEN
#define CMD_LEN		255
#endif

#ifndef NUM_ENTRIES
#define NUM_ENTRIES	4
#endif

#define __WEAK __attribute__((weak))

#ifndef ESP8266
	#if ERR_LEVEL == 3
		#define DEBUG(...) printf(__VA_ARGS__)
	#else
	#define DEBUG(...)
	#endif

	#define ERROR(...) printf(__VA_ARGS__)
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
		SEND_FAN_PWM,

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

extern char double_rx_buf[CMD_LEN*NUM_ENTRIES];
extern char *rx_buf;
extern volatile uint8_t serial_buf_cidx, serial_buf_pidx;

#ifdef __cplusplus
 extern "C" {
#endif
int uart_rx(unsigned char ch);

void process_message(char buf[]);

void send_log(const char *format,...);

void put_char(unsigned char ch);

#ifndef ESP8266
void send_temperature(int8_t *temperatures);

void send_tacho(uint16_t *fan_speed);

void send_modem_reset(void);
#else
void publish_mqtt_fan_pwm(uint8_t len, uint8_t *cmd);

void publish_mqtt_temp(uint8_t len, uint8_t *cmd);

void modem_reset(void);
#endif

#ifdef __cplusplus
}
#endif
#endif /* SERIAL_COMMS_H */

