//-----------------------------------------------------------------------------
/**
 * @file server.h
 * @author charles-park (charles-park@hardkernel.com)
 * @brief server control header file.
 * @version 0.1
 * @date 2022-05-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#ifndef __SERVER_H__
#define __SERVER_H__

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
typedef struct jig_server__t {
	/* build info */
	char		bdate[32], btime[32];
	/* JIG model name */
	char		model[32];
	/* JIG is dual channel? */
	bool		dual_ch;
	/* UART dev node */
	char		uart_dev[2][32];
	/* I2C dev node */
	char		adc_dev[2][32];
	/* FB dev node */
	char		fb_dev[32];

	fb_info_t	*pfb;
	ui_grp_t	*pui;
	ptc_grp_t	*puart[2];
}	jig_server_t;

//------------------------------------------------------------------------------
#define	PROTOCOL_DATA_SIZE	32

#pragma packet(1)
typedef struct protocol__t {
	/* @ : start protocol signal */
	__s8	head;

	/*
		command description:
			server to client : 'C'ommand, 'R'eady(boot)
			client to server : 'O'kay, 'A'ck, 'R'eady(boot), 'E'rror, 'B'usy
	*/
	__s8	cmd;

	/* msg no, msg group, msg data1, msg data2, ... */
	__s8	data[PROTOCOL_DATA_SIZE];

	/* # : end protocol signal */
	__s8	tail;
}	protocol_t;

//------------------------------------------------------------------------------
extern  int server_main (jig_server_t *pserver);

//------------------------------------------------------------------------------
#endif  // #define __SERVER_H__
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
