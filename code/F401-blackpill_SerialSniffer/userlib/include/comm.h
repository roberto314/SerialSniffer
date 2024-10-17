/*
 * comm.h
 *
 *  Created on: 04.02.2018
 *      Author: Anwender
 */

#ifndef USERLIB_INCLUDE_COMM_H_
#define USERLIB_INCLUDE_COMM_H_

#include "main.h"

#define cli_println(a); chprintf((BaseSequentialStream *)&DEBUGPORT, a"\r\n");
#define OK(); do{cli_println(" ... OK"); chThdSleepMilliseconds(20);}while(0)

void cmd_test(BaseSequentialStream *chp, int argc, char *argv[]);
void cmd_sbr(BaseSequentialStream *chp, int argc, char *argv[]);
void cmd_son(BaseSequentialStream *chp, int argc, char *argv[]);
void cmd_aon(BaseSequentialStream *chp, int argc, char *argv[]);

#endif /* USERLIB_INCLUDE_COMM_H_ */
