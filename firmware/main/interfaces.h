#pragma once


typedef int (*btx_iofunc_t) (int ch);


/*
 * iotype_t
 * in: call with 0 for non-blocking mode, 1 for blocking mode
 * 	returns -1 for error otherwise read character
 * out: parameter character to output
 * status: 1: init interface, 2: de-init interface
 * 	returns 0 for Terminal active
 * 		1 for Terminal inactive
 */

typedef struct {
	btx_iofunc_t in; //input function
	btx_iofunc_t out; //output function
	btx_iofunc_t status; //status function
} io_type_t;
