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
char rsp_buf[CMD_LEN];

char *rx_buf = double_rx_buf;

volatile uint8_t serial_buf_cidx, serial_buf_pidx;

int pos;
enum parser_state parser_state = MSG_START;
enum parser_state prev_parser_state = MSG_START;
uint8_t parity = 0;
uint8_t rx_seq = 0;
uint8_t tx_seq = 0;

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
		if ((src[i] == START_CHAR) || (src[i] == END_CHAR) || (src[i] == ESCAPE_CHAR))
		{
			put_char(ESCAPE_CHAR);
		}
		put_char(src[i]);
	}

	put_char(END_CHAR);
}

void send_log(const char *format, ...)
{
		struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
		uint8_t calc_parity;
		va_list arglist;
		int i, ret;

		rsp->cmd_type = SEND_LOG;
		rsp->parity = calc_parity = 0;
		rsp->seq = tx_seq;

		va_start(arglist, format);
		ret  = vsnprintf(rsp->cmd, sizeof(rsp_buf) - 4, format, arglist);
		va_end(arglist);

		if (ret < 0) {
			ERROR("Log too long!\n");
			return;
		}

		rsp->cmd_len  = (uint8_t)ret;

		/* Need to finish the string */
		rsp->cmd[rsp->cmd_len++] = 0;

		for(i = 0; i < rsp->cmd_len + 4; i++) {
			calc_parity += rsp_buf[i];
		}
		rsp->parity = calc_parity;

		uart_tx(rsp_buf, rsp->cmd_len + 4);

		tx_seq++;
}

int uart_rx(unsigned char ch)
{
	struct serial_cmd *cmd = (struct serial_cmd *)rx_buf;

	DEBUG("In %s\n", __func__);
	DEBUG("parser state = %d, ch = 0x%02x (%c)\n", parser_state, ch, ch);

	DEBUG("pos = %d, cmd_len = %d\n", pos, cmd->cmd_len);


	switch (parser_state)
	{
		case MSG_START:
			DEBUG("State = MSG_START\n");
			if (ch == START_CHAR) 
			{
				parser_state = MSG_PARITY_RCV;
				pos = 0;
				parity = 0;
			}
			else
			{
				ERROR("Invalid start char received in state 0x%02x\n", parser_state);
				parser_state = MSG_START;
			}
			break;

		case MSG_PARITY_RCV:
			DEBUG("State = MSG_PARITY_RCV\n");

			if (ch == ESCAPE_CHAR)
			{
				DEBUG("Got escape in MSG_PARITY_RCV\n");
				parser_state = MSG_ESCAPE;
				prev_parser_state = MSG_PARITY_RCV;
			}
			else
			{
				rx_buf[pos++] = ch;
				parser_state = MSG_RCV;
			}

			break;

		case MSG_RCV:
			DEBUG("State = MSG_RCV\n");
			switch (ch)
			{
				case  ESCAPE_CHAR:
					DEBUG("Got escape in MSG_RCV\n");
					parser_state = MSG_ESCAPE;
					prev_parser_state = MSG_RCV;
					break;

				case END_CHAR:
					parser_state = MSG_END;
					break;
	
				default:
					parity += ch;
					DEBUG("Parity = 0x%02x\n", parity);
					rx_buf[pos++] = ch;
			}
			break;

		case MSG_ESCAPE:
			DEBUG("State = MSG_ESCAPE\n");
			switch (prev_parser_state)
			{
				case MSG_RCV:
					parity += ch;
					DEBUG("ESCAPE Parity = 0x%02x\n", parity);
					rx_buf[pos++] = ch;
					break;
				
				case MSG_PARITY_RCV:
					rx_buf[pos++] = ch;
					break;

				default:
					ERROR("Unknown prev state %d\n", prev_parser_state);
					break;
			}
			parser_state = MSG_RCV;
//			parser_state = prev_parser_state;

			break;
	
	}

	if (parser_state == MSG_END) {
		if (cmd->seq != rx_seq) {
			ERROR("Warn: expected seq %d, got %d\n", rx_seq, cmd->seq);
			rx_seq = cmd->seq + 1;
		} else {
			rx_seq++;
		}

		if (cmd->parity != parity) {
			ERROR("Parity failed: expected 0x%02x, got 0x%02x\n", parity, cmd->parity);
		} else {
			DEBUG("Processing message\n");
			//process_message(rx_buf);
			if (((serial_buf_pidx + 1 ) % NUM_ENTRIES) == serial_buf_cidx)
			{
				ERROR("Serial overflow: pidx = %d, cidx = %d\n", serial_buf_pidx, serial_buf_cidx);
			}
			else
			{
				DEBUG("Serial pidx = %d, cidx = %d\n", serial_buf_pidx, serial_buf_cidx);
				serial_buf_pidx = (serial_buf_pidx + 1) % NUM_ENTRIES;
				rx_buf = &double_rx_buf[CMD_LEN * serial_buf_pidx];
			}
		}
		parser_state = MSG_START;

		return 0;
	}

	return 1;
}

#ifndef ESP8266
void send_modem_reset() {
		struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
		va_list arglist;
		int i;

		rsp->cmd_type = MODEM_RESET;
		rsp->parity = 0;

		rsp->cmd_len = 0;
		rsp->seq = tx_seq++;

		for(i = 0; i < rsp->cmd_len + 4; i++) {
			rsp->parity += rsp->cmd[i];
		}

		uart_tx(rsp_buf, rsp->cmd_len + 4);
}

__WEAK void parse_log(uint8_t *cmd)
{
	printf("LOG: %s", cmd);
}
#endif

#ifdef ESP8266

#endif
void process_message(char buf[])
{
	struct serial_cmd *cmd = (struct serial_cmd *)buf;
#ifndef ESP8266
	uint8_t prg_step, led;
	struct led_programs *tmp;
#endif

	switch (cmd->cmd_type) {
#ifdef ESP8266
		case MODEM_RESET:
			modem_reset();

			break;
		default:
			send_log("Unknown command type 0x%02x, seq 0x%02x, cmd len %d, content %s\n", cmd->cmd_type, cmd->seq, cmd->cmd_len, cmd->cmd);
			break;
#else
		case WIFI_CONNECTED:
			ERROR("Wifi Connected!\n");
			break;

		case WIFI_DISCONNECTED:
			ERROR("Wifi DisConnected!\n");
			break;
			
		case MQTT_CONNECTED:
			ERROR("MQTT Connected!\n");
			break;

		case MQTT_DISCONNECTED:
			ERROR("MQTT DisConnected!\n");
			break;
		
		case SET_LED_COLOR:
			prg_step = cmd->cmd[0];
			led = cmd->cmd[1];

			shadow_prg->led_program_entry[prg_step].leds[led] = (cmd->cmd[1] << 16) | (cmd->cmd[2] << 8) | cmd->cmd[3];

			break;

		case SET_LED_COLOR_BULK:
			prg_step = cmd->cmd[0];
			int i = 0;

			for(i = 0; i < NUM_LEDS_IN_STRIP; i++) {
				shadow_prg->led_program_entry[prg_step].leds[led] = (cmd->cmd[1 + i] << 16) | (cmd->cmd[2 + i] << 8) | cmd->cmd[3 + i];
			}
			break;
		
		case SET_LED_TIME:
			prg_step = cmd->cmd[0];
			shadow_prg->led_program_entry[prg_step].time = (cmd->cmd[1] << 8) | cmd->cmd[2];
			break;

		case SET_LED_TIME_BULK:
			prg_step = cmd->cmd[0];

			for(i = 0; i < NUM_LEDS_IN_STRIP * 2; i++) {
				shadow_prg->led_program_entry[prg_step].leds[led] = (cmd->cmd[1 + i] << 16) | (cmd->cmd[2 + i] << 8) | cmd->cmd[3 + i];
			}

			break;
			
		case SWITCH_PROGRAMS:
			tmp = cur_prg;
			cur_prg = shadow_prg;
			shadow_prg = cur_prg;
			break;

		case SEND_LOG:
			parse_log(cmd->cmd);
			break;

		default:
			ERROR("Unknown command type 0x%02x, seq 0x%02x, cmd len %d, content %s\n", cmd->cmd_type, cmd->seq, cmd->cmd_len, cmd->cmd);
			break;
#endif /* ESP8266 */
	}	
}
