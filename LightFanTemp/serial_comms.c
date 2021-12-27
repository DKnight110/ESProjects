#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "serial_comms.h"

#ifndef ESP8266
#include "led_helpers.h"
/* Array of LEDs, used to flash status */
struct led_programs led_programs[NUM_LED_PROGRAMS];

/* Sane defaults */
struct led_programs *cur_prg = &led_programs[0];
struct led_programs *shadow_prg = &led_programs[1];

uint8_t current_prg_idx = 0;

#endif
/* Globals */
uint8_t rsp_buf[CMD_LEN];

char rx_buf[255];
int pos;
enum parser_state parser_state = MSG_START;
uint8_t parity = 0;
uint8_t rx_seq = 0;

/* Funcs */
__WEAK void put_char(unsigned char ch)
{
	fprintf(stderr, "0x%02x ",ch);
}

void uart_tx(char *src, int len)
{
	int i;

	/* First we put out the start char */
	put_char(START_CHAR);
	
	for (i = 0; i < len; i++) { 
		put_char(src[i]);
	}

	put_char(END_CHAR);
}

void send_log(const char *format,...) {
		struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
		va_list arglist;
		int i;

		rsp->cmd_type = SEND_LOG;
		rsp->parity = 0;

		va_start(arglist, format);
		vsprintf(rsp->cmd, format, arglist);
		va_end(arglist);

		/* Yeah, if it's longer... tough luck */
		rsp->cmd_len = strlen(rsp->cmd);

		for(i = 0; i < rsp->cmd_len + 4; i++) {
			rsp->parity += rsp->cmd[i];
		}

		uart_tx(rsp_buf, rsp->cmd_len + 4);
}

void uart_rx(unsigned char ch)
{
	DEBUG("In %s\n", __func__);
	DEBUG("ch = 0x%02x\n", ch);
	switch(parser_state) {
		case MSG_START:
			if (ch == START_CHAR) {
				DEBUG("State = MSG_RCV\n");
				parser_state = MSG_PARITY_RCV;
				pos = 0;
				parity = 0;
			}
			break;

		case MSG_PARITY_RCV:
			/* Skip Parity, assume 0 for calculation */
			rx_buf[pos++] = ch;
			parser_state = MSG_RCV;
			break;
	
		case MSG_RCV:
			if (ch == END_CHAR) {
				DEBUG("State = MSG_END\n");
				parser_state = MSG_END;
			} else {
				parity += ch;
				DEBUG("Parity = 0x%02x\n", parity);
				rx_buf[pos++] = ch;
			}
			break;

		default:
			DEBUG("Unknown state 0x0%2x\n", parser_state);
			break;
	}

	if (parser_state == MSG_END) {
		struct serial_cmd *cmd = (struct serial_cmd *)rx_buf;

		if (cmd->seq != rx_seq) {
			DEBUG("Warn: expected seq %d, got %d\n", rx_seq, cmd->seq);
			rx_seq = cmd->seq;
		} else {
			rx_seq++;
		}

		if (cmd->parity != parity) {
			DEBUG("Parity failed: expected 0x%02x, got 0x%02x\n", parity, cmd->parity);
		} else {
			DEBUG("Processing message\n");
			process_message(rx_buf);
		}
		parser_state = MSG_START;
	}
}

#ifndef ESP8266
void send_modem_reset() {
		struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
		va_list arglist;
		int i;

		rsp->cmd_type = MODEM_RESET;
		rsp->parity = 0;

		rsp->cmd_len = 0;

		for(i = 0; i < rsp->cmd_len + 4; i++) {
			rsp->parity += rsp->cmd[i];
		}

		uart_tx(rsp_buf, rsp->cmd_len + 4);
}
#endif

#ifdef ESP8266

#endif
void process_message(char buf[])
{
	struct serial_cmd *cmd = (struct serial_cmd *)buf;
#ifndef ESP8266
	uint32_t prev_color;
#endif

	switch (cmd->cmd_type) {
#ifdef ESP8266
		case MODEM_RESET:
			client.disconnect();
			delay(5000);
			ESP.restart();
			
		default:
			send_log("Unknown command type 0x%02x, seq 0x%02x, cmd len %d, content %s\n", cmd->cmd_type, cmd->seq, cmd->cmd_len, cmd->cmd);
			break;
#endif
#ifndef ESP8266:
		case WIFI_CONNECTED:
			prev_color = led_data[LED_STATUS_0];
			led_data[LED_STATUS_0] = COLOR_GREEN;
			show_pixels();
			delay(500);
			led_data[LED_STATUS_0] = prev_color;
			show_pixels();
			break;

		case WIFI_DISCONNECTED:
			prev_color = led_data[LED_STATUS_1];
			led_data[LED_STATUS_1] = COLOR_RED;
			show_pixels();
			delay(500);
			led_data[LED_STATUS_1] = prev_color;
			show_pixels();
			break;
			
		case MQTT_CONNECTED:
			prev_color = led_data[LED_STATUS_1];
			led_data[LED_STATUS_1] = COLOR_GREEN;
			show_pixels();
			delay(500);
			led_data[LED_STATUS_1] = prev_color;
			show_pixels();
			break;

		case MQTT_DISCONNECTED:
			prev_color = led_data[LED_STATUS_1];
			led_data[LED_STATUS_1] = COLOR_RED;
			show_pixels();
			delay(500);
			led_data[LED_STATUS_1] = prev_color;
			show_pixels();
			break;
#else /* ifndef ESP8266 */		
		case SET_LED_COLOR:
			uint8_t prg_step = cmd->cmd[0];
			uint8_t led = cmd->cmd[1];

			shadow_prg->led_program_entry[prg_step].leds[led] = (cmd->cmd[1] << 16) | (cmd->cmd[2] << 8) | cmd->cmd[3]);

			break;

		case SET_LED_COLOR_BULK:
			uint8_t prg_step = cmd->cmd[0];
			int i = 0;

			for(i = 0; i < NUM_LEDS_IN_STRIP; i++) {
				shadow_prg->led_program_entry[prg_step].leds[led] = (cmd->cmd[1 + i] << 16) | (cmd->cmd[2 + i] << 8) | cmd->cmd[3 + i]);
			}
			break;
		
		case SET_LED_TIME:
			uint8_t prg_step = cmd->cmd[0];
			shadow_prg->led_program_entry[prg_step].time = (cmd->cmd[1] << 8) | cmd->cmd[2];
			break;

		case SET_LED_TIME_BULK:
			uint8_t prg_step = cmd->cmd[0];

			for(i = 0; i < NUM_LEDS_IN_STRIP * 2; i++) {
				shadow_prg->led_program_entry[prg_step].leds[led] = (cmd->cmd[1 + i] << 16) | (cmd->cmd[2 + i] << 8) | cmd->cmd[3 + i]);
			}

			break;
			
		case SWITCH_PROGRAMS:
			struct led_programs *tmp = cur_prg;
			cur_prg = shadow_prg;
			shadow_prg = cur_prg;
			break;

		case SEND_LOG:
			parse_log(cmd->
			break;

		default:
			SERIAL_PRINTLN("Unknown command type 0x%02x, seq 0x%02x, cmd len %d, content %s\n", cmd->cmd_type, cmd->seq, cmd->cmd_len, cmd->cmd);
			break;
#endif /* ESP8266 */
	}	
}
