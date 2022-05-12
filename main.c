//------------------------------------------------------------------------------
/**
 * @file main.c
 * @author charles-park (charles.park@hardkernel.com)
 * @brief ODROID-N2 Lite JIG Server application.
 * @version 0.1
 * @date 2022-05-10
 * 
 * @copyright Copyright (c) 2022
 * 
 */
//------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <getopt.h>

//------------------------------------------------------------------------------
// for my lib
//------------------------------------------------------------------------------
/* 많이 사용되는 define 정의 모음 */
#include "typedefs.h"

/* framebuffer를 control하는 함수 */
#include "lib_fb.h"

/* file parser control 함수 */
#include "lib_ui.h"

/* uart control 함수 */
#include "lib_uart.h"

#if 0

/* jig용으로 만들어진 adc board control 함수 */
#include "lib_adc.h"

/* i2c control 함수 */
#include "lib_i2c.h"

/* network label printer control 함수 */
#include "lib_nlp.h"

/* mac server control 함수 */
#include "lib_mac.h"

#endif

#include "server.h"
//------------------------------------------------------------------------------
// Default global value
//------------------------------------------------------------------------------
const char	*OPT_UI_CFG_FILE		= "default_ui.cfg";
const char	*OPT_SERVER_CFG_FILE 	= "default_server.cfg";

//------------------------------------------------------------------------------
// function prototype define
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
static void tolowerstr (char *p)
{
	while (*p++)   *p = tolower(*p);
}

//------------------------------------------------------------------------------
static void toupperstr (char *p)
{
	while (*p++)   *p = toupper(*p);
}

//------------------------------------------------------------------------------
static void print_usage(const char *prog)
{
	printf("Usage: %s [-fu]\n", prog);
	puts("  -f --server_cfg_file    default default_server.cfg.\n"
		 "  -u --ui_cfg_file        default file name is default_ui.cfg\n"
	);
	exit(1);
}

//------------------------------------------------------------------------------
static void parse_opts (int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "server_config_file"	, 1, 0, 'f' },
			{ "ui_config_file"		, 1, 0, 'u' },
			{ NULL, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "f:u:", lopts, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'f':
			OPT_SERVER_CFG_FILE = optarg;
			break;
		case 'u':
			OPT_UI_CFG_FILE = optarg;
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
}

//------------------------------------------------------------------------------
char *_str_remove_space (char *ptr)
{
	/* 문자열이 없거나 앞부분의 공백이 있는 경우 제거 */
	int slen = strlen(ptr);

	while ((*ptr == 0x20) && slen--)
		ptr++;

	return ptr;
}

//------------------------------------------------------------------------------
void _strtok_strcpy (char *dst)
{
	char *ptr;

	if ((ptr = strtok (NULL, ",")) != NULL) {
		ptr = _str_remove_space(ptr);
		strncpy(dst, ptr, strlen(ptr));
	}
}

//------------------------------------------------------------------------------
void _parse_model_name (jig_server_t *pserver)
{
	char *ptr;

	_strtok_strcpy(pserver->model);

	pserver->dual_ch = false;
	if ((ptr = strtok (NULL, ",")) != NULL)
		pserver->dual_ch = (atoi(ptr) > 1) ? true : false;
}

//------------------------------------------------------------------------------
void _parse_fb_config (jig_server_t *pserver)
{
	_strtok_strcpy(pserver->fb_dev);
}

//------------------------------------------------------------------------------
void _parse_uart_config (jig_server_t *pserver)
{
	_strtok_strcpy(pserver->uart_dev[0]);
	_strtok_strcpy(pserver->uart_dev[1]);
}

//------------------------------------------------------------------------------
void _parse_adc_config (jig_server_t *pserver)
{

}

//------------------------------------------------------------------------------
void _parse_nlp_config (jig_server_t *pserver)
{

}

//------------------------------------------------------------------------------
void _parse_cmd_config (jig_server_t *pserver)
{

}

//------------------------------------------------------------------------------
bool parse_cfg_file (char *cfg_filename, jig_server_t *pserver)
{
	FILE *pfd;
	char buf[256], *ptr, is_cfg_file = 0;

	if ((pfd = fopen(cfg_filename, "r")) == NULL) {
		err ("%s file open fail!\n", cfg_filename);
		return false;
	}

	/* config file에서 1 라인 읽어올 buffer 초기화 */
	memset (buf, 0, sizeof(buf));
	while(fgets(buf, sizeof(buf), pfd) != NULL) {
		/* config file signature 확인 */
		if (!is_cfg_file) {
			is_cfg_file = strncmp ("ODROID-JIG-CONFIG", buf, strlen(buf)-1) == 0 ? 1 : 0;
			memset (buf, 0x00, sizeof(buf));
			continue;
		}

		ptr = strtok (buf, ",");
		if (!strncmp(ptr, "MODEL", strlen("MODEL")))	_parse_model_name (pserver);
		if (!strncmp(ptr,    "FB", strlen("FB")))		_parse_fb_config  (pserver);
		if (!strncmp(ptr,  "UART", strlen("UART")))		_parse_uart_config(pserver);
		if (!strncmp(ptr,   "ADC", strlen("ADC")))		_parse_adc_config (pserver);
		if (!strncmp(ptr,   "NLP", strlen("NLP")))		_parse_nlp_config (pserver);
		if (!strncmp(ptr,   "CMD", strlen("CMD")))		_parse_cmd_config (pserver);
		memset (buf, 0x00, sizeof(buf));
	}

	if (pfd)
		fclose (pfd);

	if (!is_cfg_file) {
		err("This file is not JIG Config File! (filename = %s)\n", cfg_filename);
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
int main(int argc, char **argv)
{
	jig_server_t	*pserver;

    parse_opts(argc, argv);

	if ((pserver = (jig_server_t *)malloc(sizeof(jig_server_t))) == NULL) {
		err ("create server fail!\n");
		goto err_out;
	}
	memset  (pserver, 0, sizeof(jig_server_t));

	info("JIG Server config file : %s\n", OPT_SERVER_CFG_FILE);
	if (!parse_cfg_file ((char *)OPT_SERVER_CFG_FILE, pserver)) {
		err ("server init fail!\n");
		goto err_out;
	}
	strncpy (pserver->bdate, __DATE__, strlen(__DATE__));
	strncpy (pserver->btime, __TIME__, strlen(__TIME__));
	info ("Application Build : %s / %s\n", pserver->bdate, pserver->btime);

	info("Framebuffer Device : %s\n", pserver->fb_dev);
	if ((pserver->pfb = fb_init (pserver->fb_dev)) == NULL) {
		err ("create framebuffer fail!\n");
		goto err_out;
	}

	info("UART Device : %s, baud = 115200bps(%d)\n", pserver->uart_dev[0], B115200);
	if ((pserver->puart[0] = uart_init (pserver->uart_dev[0], B115200)) == NULL) {
		err ("create ui fail!\n");
		goto err_out;
	}

	info("UI Config file : %s\n", OPT_UI_CFG_FILE);
	if ((pserver->pui = ui_init (pserver->pfb, OPT_UI_CFG_FILE)) == NULL) {
		err ("create ui fail!\n");
		goto err_out;
	}

	// main control function (server.c)
	server_main (pserver);

err_out:
	uart_close (pserver->puart[0]);
	ui_close (pserver->pui);
	fb_clear (pserver->pfb);
	fb_close (pserver->pfb);

	return 0;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
