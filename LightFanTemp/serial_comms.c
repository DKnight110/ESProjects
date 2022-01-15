#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "serial_comms.h"

#ifndef ESP8266
#include "led_helpers.h"
/* Array of LEDs, used to flash status */
volatile struct led_programs led_programs[NUM_LED_PROGRAMS];

/* Sane defaults */
volatile struct led_programs *cur_prg = &led_programs[0];
volatile struct led_programs *shadow_prg = &led_programs[1];

uint8_t current_prg_idx = 0;

bool wifi_connected;
bool mqtt_connected;

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
	printf("0x%02x ",ch);
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
void send_modem_reset()
{
		struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
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

void send_temperature(int16_t *temperatures)
{
		struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
		uint8_t calc_parity;
		int i;

		rsp->cmd_type = SEND_TEMP;
		rsp->parity = calc_parity = 0;
		rsp->seq = tx_seq;

		/* 1 byte for each would be enough, -127..+128...
		 * BUT DS18B20 can do .1 degrees...
		 * so send this as int(Temp_f * 10) */
		rsp->cmd_len = NUM_TEMP_SENSORS * 2;
		rsp->seq = tx_seq;

		for (i = 0; i < NUM_TEMP_SENSORS * 2; i+=2)
		{
			rsp->cmd[i] = (temperatures[i/2] >> 8) & 0xFF;
			rsp->cmd[i + 1] = temperatures[i/2] & 0xFF;
		}

		for(i = 0; i < rsp->cmd_len + 4; i++) {
			calc_parity += rsp_buf[i];
		}

		rsp->parity = calc_parity;

		uart_tx(rsp_buf, rsp->cmd_len + 4);
		tx_seq++;
}

void send_tacho(uint16_t *fan_speed)
{
		struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
		uint8_t calc_parity;
		int i;

		rsp->cmd_type = SEND_FAN_PWM;
		rsp->parity = calc_parity = 0;

		/* speed can be > 255 => 2 bytes each */
		rsp->cmd_len = NUM_FANS * 2;
		rsp->seq = tx_seq;

		for (i = 0; i < rsp->cmd_len; i+=2)
		{
			rsp->cmd[i] = (fan_speed[i/2] >> 8) & 0xFF;
			rsp->cmd[i + 1] = fan_speed[i/2] & 0xFF;
		}

		for(i = 0; i < rsp->cmd_len + 4; i++) {
			calc_parity += rsp_buf[i];
		}

		rsp->parity = calc_parity;

		uart_tx(rsp_buf, rsp->cmd_len + 4);
		tx_seq++;
}

__WEAK void parse_log(uint8_t *cmd)
{
	printf("LOG: %s", cmd);
}
#endif

#ifdef ESP8266
void send_wifi_status(bool status)
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = status == true ? WIFI_CONNECTED : WIFI_DISCONNECTED;
	rsp->parity = calc_parity = 0;

	/* speed can be > 255 => 2 bytes each */
	rsp->cmd_len = 0;
	rsp->seq = tx_seq;

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;
}

void send_mqtt_status(bool status)
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = status == true ? MQTT_CONNECTED : MQTT_DISCONNECTED;
	rsp->parity = calc_parity = 0;

	rsp->cmd_len = 0;
	rsp->seq = tx_seq;

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;
}

void send_led_color(uint8_t *msg, uint8_t len)
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = SET_LED_COLOR;
	rsp->parity = calc_parity = 0;

	rsp->cmd_len = len;
	rsp->seq = tx_seq;

	for (i =  0; i < len; i++)
	{
		rsp->cmd[i] = msg[i];
	}

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;

}

void send_led_program_steps(uint8_t num_steps)
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = SET_LED_PROGRAM_STEPS;
	rsp->parity = calc_parity = 0;

	rsp->cmd_len = 1;
	rsp->seq = tx_seq;

	rsp->cmd[0] = num_steps;
	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;
}

void send_fan_state(bool state)
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = SET_FAN_POWER_STATE;
	rsp->parity = calc_parity = 0;

	rsp->cmd_len = 1;
	rsp->seq = tx_seq;

	rsp->cmd[0] = state ? 1 : 0;

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;
}

void send_fan_pwm(uint8_t fan, uint8_t pwm)
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = SET_FAN_PWM_PERC;
	rsp->parity = calc_parity = 0;

	rsp->cmd_len = 2;
	rsp->seq = tx_seq;

	rsp->cmd[0] = fan;
	rsp->cmd[1] = pwm;

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;
}

void send_led_program_switch()
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = SWITCH_PROGRAMS;
	rsp->parity = calc_parity = 0;

	rsp->cmd_len = 0;
	rsp->seq = tx_seq;

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;
}

void send_set_color(uint8_t r, uint8_t g, uint8_t b)
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = SET_COLOR_INTENSITY;
	rsp->parity = calc_parity = 0;

	rsp->cmd[0] = r;
	rsp->cmd[1] = g;
	rsp->cmd[2] = b;

	rsp->cmd_len = 3;
	rsp->seq = tx_seq;

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;
}

void send_resume_animation()
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = RESUME_ANIMATION;
	rsp->parity = calc_parity = 0;

	rsp->cmd_len = 0;
	rsp->seq = tx_seq;

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);
	tx_seq++;
}

void send_light_drawer(uint8_t drawer, uint8_t r, uint8_t g, uint8_t b)
{
	struct serial_cmd *rsp = (struct serial_cmd *)rsp_buf;
	uint8_t calc_parity;
	int i;

	rsp->cmd_type = SET_DRAWER_LIGHT;
	rsp->parity = calc_parity = 0;

	rsp->cmd[0] = drawer;
	rsp->cmd[1] = r;
	rsp->cmd[2] = g;
	rsp->cmd[3] = b;

	rsp->cmd_len = 4;
	rsp->seq = tx_seq;

	for(i = 0; i < rsp->cmd_len + 4; i++) {
		calc_parity += rsp_buf[i];
	}

	rsp->parity = calc_parity;

	uart_tx(rsp_buf, rsp->cmd_len + 4);

	tx_seq++
}

#endif
void process_message(char buf[])
{
	struct serial_cmd *cmd = (struct serial_cmd *)buf;
#ifndef ESP8266
	struct led_programs *tmp;
	uint8_t prg_step, led, drawer;
	uint32_t s_color;
	int i;
#endif

	switch (cmd->cmd_type) {
#ifdef ESP8266
		case MODEM_RESET:
			modem_reset();

			break;

		case SEND_FAN_PWM:
			publish_mqtt_fan_pwm(cmd->cmd_len, cmd->cmd);
			break;

		case SEND_TEMP:
			publish_mqtt_temp(cmd->cmd_len, cmd->cmd);
			break;

		default:
			send_log("Unknown command type 0x%02x, seq 0x%02x, cmd len %d, content %s\n", cmd->cmd_type, cmd->seq, cmd->cmd_len, cmd->cmd);
			break;
#else
		case WIFI_CONNECTED:
			ERROR("Wifi Connected!\n");
			wifi_connected = true;
			break;

		case WIFI_DISCONNECTED:
			ERROR("Wifi DisConnected!\n");
			wifi_connected = false;
			break;
			
		case MQTT_CONNECTED:
			ERROR("MQTT Connected!\n");
			mqtt_connected = true;
			break;

		case MQTT_DISCONNECTED:
			ERROR("MQTT DisConnected!\n");
			mqtt_connected = false;
			break;
		
		case SET_FAN_POWER_STATE:
			/* Kill power to all fans, 2 pins */
			ERROR("Setting FANs power state to %d\n", cmd->cmd[0]);
			set_fans_power_state(cmd->cmd[0]);
			break;

		case SET_FAN_PWM_PERC:
			ERROR("Setting FAN %d PWM to %d\n", cmd->cmd[0], cmd->cmd[1]);
			set_fan_pwm(cmd->cmd[0],cmd->cmd[1]);			
			break;

		case SET_LED_COLOR:
			prg_step = cmd->cmd[0];
			shadow_prg->led_program_entry[prg_step].time = (cmd->cmd[1] << 8) | cmd->cmd[2];
			ERROR("Setting LEDs in step %d (% d ms)\n", cmd->cmd[0], shadow_prg->led_program_entry[prg_step].time);
			for (i = 0; i < NUM_LEDS_IN_STRIP * 3; i+=3)
			{
				shadow_prg->led_program_entry[prg_step].leds[i/3] = (cmd->cmd[3 + i] << 16) | (cmd->cmd[4 + i] << 8) | cmd->cmd[5 + i];
			}

			break;

		case SET_LED_PROGRAM_STEPS:
			ERROR("Setting num steps to %d\n", cmd->cmd[0]);
			shadow_prg->num_steps = cmd->cmd[0];
			break;
						
		case SWITCH_PROGRAMS:
			ERROR("Switching programs\n");
			switch_programs();
			break;

		case SET_COLOR_INTENSITY:
			s_color = (cmd->cmd[0] << 16) | (cmd->cmd[1] << 8) | cmd->cmd[2];
			ERROR("Setting led strip color to 0x%08x\n", s_color);
			set_strip_intensity(s_color);
			break;

		case SET_DRAWER_LIGHT:
			drawer = cmd->cmd[0];
			s_color = (cmd->cmd[1] << 16) | (cmd->cmd[2] << 8) | cmd->cmd[3];
			ERROR("Setting drawer %d strip color to 0x%08x\n", drawer, s_color);
			light_drawer(drawer, s_color);
			break;


		case RESUME_ANIMATION:
			ERROR("Resuming animation...\n");
			resume_animation();
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
