//------------------------------------------------------------------------------
/**
 * @file server.c
 * @author charles-park (charles.park@hardkernel.com)
 * @brief server main control.
 * @version 0.1
 * @date 2022-05-11
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
#include <sys/time.h>
#include <getopt.h>

#define	SYSTEM_LOOP_DELAY_uS	100
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

#include "server.h"
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

//------------------------------------------------------------------------------
bool run_interval_check (struct timeval *t, double interval_ms)
{
	struct timeval base_time;
	double difftime;

	gettimeofday(&base_time, NULL);

	if (interval_ms) {
		/* 현재 시간이 interval시간보다 크면 양수가 나옴 */
		difftime = (base_time.tv_sec - t->tv_sec) +
					((base_time.tv_usec - (t->tv_usec + interval_ms * 1000)) / 1000000);

		if (difftime > 0) {
			t->tv_sec  = base_time.tv_sec;
			t->tv_usec = base_time.tv_usec;
			return true;
		}
		return false;
	}
	/* 현재 시간 저장 */
	t->tv_sec  = base_time.tv_sec;
	t->tv_usec = base_time.tv_usec;
	return true;
}

//------------------------------------------------------------------------------
void time_display (jig_server_t *pserver)
{
	static struct timeval i_time;
	static int i = 0;

	if (run_interval_check(&i_time, 500)) {
		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		ui_set_printf (pserver->pfb, pserver->pui, 0, "%s", pserver->model);
		ui_set_printf (pserver->pfb, pserver->pui, 1, "%s", pserver->bdate);
		ui_set_printf (pserver->pfb, pserver->pui, 2, "%02d:%02d:%02d",
			tm.tm_hour, tm.tm_min, tm.tm_sec);

		if (i++ % 2) {
			ui_set_ritem (pserver->pfb, pserver->pui, 0, COLOR_GREEN, -1);
			ui_set_sitem (pserver->pfb, pserver->pui, 3, COLOR_RED, -1, "OFF");
		}
		else {
			ui_set_ritem (pserver->pfb, pserver->pui, 0, COLOR_RED, -1);
			ui_set_sitem (pserver->pfb, pserver->pui, 3, COLOR_GREEN, -1, "ON");
		}
		info("%s\n", ctime(&t));
	}
}

//------------------------------------------------------------------------------
void receive_check(ptc_grp_t *ptc_grp)
{
	__u8 idata, p_cnt;

	if (queue_get (&ptc_grp->rx_q, &idata))
		ptc_event (ptc_grp, idata);

	for (p_cnt = 0; p_cnt < ptc_grp->pcnt; p_cnt++) {
		if (ptc_grp->p[p_cnt].var.pass) {
			char *msg = (char *)ptc_grp->p[p_cnt].var.arg;
			ptc_grp->p[p_cnt].var.pass = false;
			ptc_grp->p[p_cnt].var.open = true;
			info ("pass message = %s\n", msg);
		}
	}
};

//------------------------------------------------------------------------------
int protocol_check(ptc_var_t *var)
{
	/* head & tail check with protocol size */
	if(var->buf[(var->p_sp + var->size -1) % var->size] != '#')	return 0;
	if(var->buf[(var->p_sp               ) % var->size] != '@')	return 0;
	return 1;
}

//------------------------------------------------------------------------------
int protocol_catch(ptc_var_t *var)
{
	int i;
	char *rdata = (char *)var->arg, resp = var->buf[(var->p_sp) % var->size];

	memset (rdata, 0, sizeof(PROTOCOL_DATA_SIZE));
	switch (resp) {
		case 'O':
			for (i = 2; i < var->size -2; i++)
				rdata[i] = var->buf[(var->p_sp +i) % var->size];
		break;
		case 'A':	case 'R':	case 'B':
		default :
			info ("%s : resp = %c\n", __func__, resp);
		break;
	}
	return 1;
}

//------------------------------------------------------------------------------
void send_msg (jig_server_t *pserver, char cmd, __u8 cmd_id, char *pmsg)
{
	protocol_t s;
	int m_size;
	__u8 *p = (__u8 *)&s;

	memset (&s, 0, sizeof(protocol_t));
	s.head = '@';	s.tail = '#';
	s.cmd  = cmd;
	sprintf(s.id, ",%03d,", cmd_id);

	if (pmsg != NULL) {
		m_size = strlen(pmsg);
		m_size = (m_size > PROTOCOL_DATA_SIZE) ? PROTOCOL_DATA_SIZE : m_size;
		strncpy (s.data, pmsg, m_size);
	}
	for (m_size = 0; m_size < sizeof(protocol_t); m_size++) {
		queue_put(&pserver->puart[0]->tx_q, p + m_size);
	}
}

//------------------------------------------------------------------------------
void send_msg_check (jig_server_t *pserver)
{
	static struct timeval i_time;

	if (pserver->cmd_run) {
		if (run_interval_check(&i_time, 2000)) {
			info ("Retry Send.... \n");
			goto retry;
		}
		return;
	}

retry:
	info ("%s : send id %d, msg = %s, protocol_size = %ld\n", __func__,
			pserver->cmd_id, pserver->cmds[pserver->cmd_id], sizeof(protocol_t));
	if (pserver->cmd_id < CMD_COUNT_MAX) {
		send_msg (pserver, 'S', pserver->cmd_id, pserver->cmds[pserver->cmd_id]);
		pserver->cmd_run = true;
		run_interval_check (&i_time, 0);
	}
}

//------------------------------------------------------------------------------
int server_main (jig_server_t *pserver)
{
	__s8 MsgData[PROTOCOL_DATA_SIZE];

	if (ptc_grp_init (pserver->puart[0], 1)) {
		if (!ptc_func_init (pserver->puart[0], 0, sizeof(protocol_t), 
    							protocol_check, protocol_catch, MsgData))
			return 0;
	}

	while (1) {
		time_display(pserver);

		/* uart data processing */
		receive_check(pserver->puart[0]);
		if (pserver->dual_ch)
			receive_check(pserver->puart[1]);

		send_msg_check (pserver);

		usleep(SYSTEM_LOOP_DELAY_uS);
	}
	return 0;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
