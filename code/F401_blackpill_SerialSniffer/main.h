/*
 * main.h
 *
 *  Created on: Sep 23, 2020
 *      Author: rob
 */

#ifndef MAIN_H_
#define MAIN_H_

#include <stdio.h>
#include <string.h>
#include "ch.h"
#include "hal.h"

#include "shell.h"
#include "chprintf.h"
#include "comm.h"

void flush_buffer(void);
typedef struct{
  uint8_t lastchar;
  uint8_t src;
  systime_t timestamp;
} lst_st;

#endif /* MAIN_H_ */
