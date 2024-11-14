/*
 * comm.c
 *
 *  Created on: 04.02.2018
 *      Author: Anwender
 */



#include "ch.h"
#include "hal.h"
#include "comm.h"

#include "chprintf.h"
//#include "chscanf.h"
#include "stdlib.h"
#include "string.h" /* for memset */
#include "shell.h"
#include "portab.h"
#include "main.h"

extern BaseSequentialStream *const dbg; //DEBUGPORT
extern SerialConfig serial_config;
extern ICUConfig icucfg1, icucfg2;
extern uint8_t serstat, dump_format;
extern uint16_t flush_timeout;
/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/

void cmd_test(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)* argv;
  (void)argc;
  char text[10];
  uint16_t val;

  chprintf(chp, "Enter Number (<256) \r\n");
//  ret = chscanf(chp, "%7s", &text);

  val = (uint16_t)strtol(text, NULL, 0);

  chprintf(chp, "You entered text: %s Val: %04x got: %02x\r\n",
                      text, val);
  chprintf(dbg, "OK\r\n");

}
void cmd_sbr(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)* argv;
  (void)argc;
  uint32_t baud;
  baud = serial_config.speed;
  if(argc <1){
    chprintf(chp, "Sets Baudrate of listeners.\r\n");
    chprintf(chp, "Baudrate now: %d \r\n", baud);
    chprintf(chp, "Usage: sbr [BAUDRATE]\r\n");
    return;
  }
  baud = (uint32_t)strtol(argv[0], NULL, 0);
  if ((baud < 300) || (baud > 115200)){
    chprintf(chp, "Baudrate out of Range: (%d) \r\n", baud);
    return;
  }
  serial_config.speed = baud;
  sdStart(&SD1, &serial_config);
  sdStart(&SD2, &serial_config);
  chprintf(chp, "UARTs updated.\r\n");
}

void cmd_stt(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)* argv;
  (void)argc;
  uint32_t temp;
  if(argc <1){
    chprintf(chp, "Sets Timeout for buffer flush.\r\n");
    chprintf(chp, "Timeout in ms now: %d \r\n", flush_timeout);
    chprintf(chp, "Usage: stt [TIMEOUT in ms]\r\n");
    return;
  }
  temp = (uint32_t)strtol(argv[0], NULL, 0);
  if ((temp < 10) || (temp > 65535)){
    chprintf(chp, "Timeout out of Range: (%d) \r\n", temp);
    return;
  }
  flush_timeout = temp;
  chprintf(chp, "Timeout updated.\r\n");
}

void cmd_flush(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)* argv;
  (void)* chp;
  (void)argc;
  flush_buffer();
}

void cmd_format(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)* argv;
  (void)argc;
  int32_t stat;
  if(argc <1){
    chprintf(chp, "Changes Dump Format.\r\n");
    chprintf(chp, "Format is now: %d \r\n", dump_format);
    chprintf(chp, "Usage: format [0..3]\r\n");
    return;
  }
  stat = strtol(argv[0], NULL, 0);
  if ((stat < 0) || (stat > 3)){
    chprintf(chp, "Only 0..3 allowed. (%d) \r\n", stat);
    return;
  }
  dump_format = (uint8_t)stat;
}

void cmd_son(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)* argv;
  (void)argc;
  uint8_t stat;
  stat = serstat;
  if(argc <1){
    chprintf(chp, "Turns Listeners on or off.\r\n");
    chprintf(chp, "If Listeners are off, timer is on and vice versa.\r\n");
    if (stat)
      chprintf(chp, "Listeners are now on. %d \r\n", stat);
    else
      chprintf(chp, "Listeners are now off. %d \r\n", stat);
    chprintf(chp, "Usage: son [0|1]\r\n");
    return;
  }
  stat = (uint8_t)strtol(argv[0], NULL, 0);
  if (stat > 1){
    chprintf(chp, "Only 0 or 1 allowed. (%d) \r\n", stat);
    return;
  }
  if (stat){
    icuStop(&ICUD2);
    icuStopCapture(&ICUD2);
    palSetPadMode(GPIOA, 2, PAL_MODE_ALTERNATE(7));  // TX2
    palSetPadMode(GPIOA, 3, PAL_MODE_ALTERNATE(7));  // RX2
    palSetPadMode(GPIOA, 9, PAL_MODE_ALTERNATE(7));  // TX1
    palSetPadMode(GPIOA, 10, PAL_MODE_ALTERNATE(7)); // RX1
    palSetPadMode(GPIOA, 0, PAL_MODE_INPUT_PULLUP);  // Button

    sdStart(&SD1, &serial_config);
    sdStart(&SD2, &serial_config);    
    chprintf(chp, "UARTs started, Timer stopped.\r\n");
  }
  else {
    sdStop(&SD1);
    sdStop(&SD2);
    palSetPadMode(GPIOA, 0, PAL_MODE_ALTERNATE(1));  // TIM2/1
    icuStart(&ICUD2, &icucfg1);
    icuStartCapture(&ICUD2);
    icuEnableNotifications(&ICUD2);
    chprintf(chp, "UARTs stopped, Timer started.\r\n");
  }
}

