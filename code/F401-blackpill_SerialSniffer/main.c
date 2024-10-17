/*
    ChibiOS - Copyright (C) 2006..2018 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/
#include "main.h"
#include "ch.h"
#include "hal.h"
#include <string.h>
#include <stdlib.h>
#include "portab.h"
#include "shell.h"
#include "chprintf.h"

#include "usbcfg.h"
#include "comm.h"
#define CLOCKFREQ 10000000UL
SerialConfig serial_config = {
  38400,
  0,  // CR1 
  0,  // CR2 
  0   // CR3
};

uint8_t serstat = 1; // 0: Serial off, Timer On
uint32_t smallest_pulse = 0xFFFF;
icucnt_t last_width1, last_period1;
uint32_t sp_temp;
systime_t oldstart, blockstart = 0, last_received_char;
uint8_t dump_in_progress, dump_ascii = 1;

uint8_t last_src = 0xFF, other_src;
struct listener_st {
  uint8_t  lastchar[8192];
  uint32_t charcnt;
  systime_t starttime;
};

struct listener_st ls[2];

static void icuwidthcb1(ICUDriver *icup) { // This gets called every falling edge.

  last_width1 = icuGetWidthX(icup);
  palTogglePad(GPIOC, 14);
  //if (last_width1 < smallest_pulse)
  //  smallest_pulse = last_width1;
}

static void icuperiodcb1(ICUDriver *icup) { // This gets called every rising edge.

  last_period1 = icuGetPeriodX(icup);
  palTogglePad(GPIOC, 15);
  sp_temp = last_period1 - last_width1;
  if (sp_temp < smallest_pulse)
    smallest_pulse = sp_temp;
  //if ((last_period1 - last_width1) < smallest_pulse)
  //  smallest_pulse = (last_period1 - last_width1);
}

ICUConfig icucfg1 = {
  ICU_INPUT_ACTIVE_HIGH,
  CLOCKFREQ,                                    /* 10MHz ICU clock frequency.   */
  icuwidthcb1,
  icuperiodcb1,
  NULL,
  ICU_CHANNEL_1,
  0U,
  0xFFFFFFFFU
};

/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/
BaseSequentialStream *const shell = (BaseSequentialStream *)&SHELLPORT;
BaseSequentialStream *const dbg = (BaseSequentialStream *)&DEBUGPORT;

#define SHELL_WA_SIZE   THD_WORKING_AREA_SIZE(2048)

char history_buffer[8*64];
char *completion_buffer[SHELL_MAX_COMPLETIONS];

static const ShellCommand commands[] = {
  {"test",cmd_test},
  {"sbr",cmd_sbr},
  {"son",cmd_son},
  {"aon",cmd_aon},
  {NULL, NULL}
};
static const ShellConfig shell_cfg1 = {
  (BaseSequentialStream *)&SHELLPORT,
  commands,
  history_buffer,
  sizeof(history_buffer),
  completion_buffer
};

//static const ShellCommand commands[] = {
//  {"test", cmd_test},
//  {NULL, NULL}
//};
//
//static const ShellConfig shell_cfg1 = {
//  (BaseSequentialStream *)&SHELLPORT,
//  commands
//};

uint32_t calc_baud(uint32_t time){
  double baudf = 0;
  uint32_t baud, baudu, baudl;
  uint32_t sbr[] = {150,300,600,1200,2400,4800,9600,19200,38400,57600,115200,230400};
  uint8_t idx = 0;
  if (time == 0)
    return 0;
  baudf = (CLOCKFREQ/time);
  baudu = (uint32_t)(baudf * 1.09); // Window is +- 9%
  baudl = (uint32_t)(baudf / 1.09); // Window is +- 9%
  baud = (uint32_t)baudf;
  //chprintf(dbg, "baud: %d, time: %d\r\n", baud, time);
  if (baudu < sbr[0])
    return 0;
  if (baudl > sbr[11])
    return 0xFFFFFFFF;
  for (idx = 0;idx < 12; idx++){
    //chprintf(dbg, "sbrl: %d sbru: %d\r\n", sbrl[idx], sbru[idx]);
    if ((baudl < sbr[idx]) && (baudu > sbr[idx]))
      return sbr[idx];
  }
  return baud;
}

/*
 * Green LED blinker thread, times are in milliseconds.
 */
static THD_WORKING_AREA(waThread1, 128);
static THD_FUNCTION(Thread1, arg) {

  (void)arg;
  chRegSetThreadName("blinker");
  while (true) {
    systime_t time;
    time = serusbcfg1.usbp->state == USB_ACTIVE ? 250 : 500;
    palClearPad(GPIOC, 13);
    chThdSleepMilliseconds(time);
    palSetPad(GPIOC, 13);
    chThdSleepMilliseconds(time);
    if (smallest_pulse < 65535){
      //chprintf(dbg, "Time: %d \r\n", smallest_pulse);
      chprintf(dbg, "Baud: %d \r\n", calc_baud(smallest_pulse));
      smallest_pulse = 0xFFFF;
    }
    //chprintf(dbg, "LastWidth: %d \r\n", last_width1);
    //chprintf(dbg, "LastPeriod: %d \r\n", last_period1);
//    count++;
//    if (count == 10){
//      count = 0;
//      smallest_pulse = 0xFFFF;
//    }
  }
}

void safe_print(uint8_t c){
  if (c > 0x1F) // print only printable characters
    chprintf(dbg, "%c", c);
  else
    chprintf(dbg, "%c",'.');    
}

void dump_data(uint8_t source){
  uint8_t i,j,c;
  dump_in_progress = 1;
  systime_t diff = ls[source].starttime - oldstart;
  if (ls[source].charcnt > 0){ // nice formatting if more than one character
    if (blockstart == 0){ // start of new block has no diff
      blockstart = ls[source].starttime;
      chprintf(dbg, "\r\n%06d | ---- | %d> Charcount: %d",ls[source].starttime, source, ls[source].charcnt);
    }
    else{
      chprintf(dbg, "\r\n%06d | %04d | %d> Charcount: %d",ls[source].starttime, diff, source, ls[source].charcnt);
    }
    chprintf(dbg, "\r\n");
    for (i=0;i<ls[source].charcnt;i++){ // go through all the characters
      c = ls[source].lastchar[i];
      if ((i%16 == 0) && (i>0)){ // every 16 chars dump character representation and newline
        if (dump_ascii != 0){
          chprintf(dbg, " | "); // make separator
          //chprintf(dbg, "i: %d  ", i);
          for (j=i-16;j<i;j++){ // print the characters again
            safe_print(ls[source].lastchar[j]);
          }
        }
        chprintf(dbg, "\r\n");
      }
      chprintf(dbg, "%02x ", c);
    }
    // now all characters have been printed. 
    // But there could be a block of < 16 chars rest
    if (dump_ascii != 0){
      uint8_t rest = (((ls[source].charcnt / 16) + 1) * 16) - ls[source].charcnt;
      for (j=0;j<rest;j++){ 
        chprintf(dbg, "   "); // fill the space
      }
      chprintf(dbg, " | "); // make separator
      //chprintf(dbg, "Rest: %d i: %d\r\n", rest, i);
      for (j=i-(16-rest);j<i;j++){
        safe_print(ls[source].lastchar[j]);
      }
    }
    //chprintf(dbg, "\r\n");
    chprintf(dbg, "\r\n");
  }
  else{ // only one character
    if (blockstart == 0){ // start of new block has no diff
      blockstart = ls[source].starttime;
      chprintf(dbg, "\r\n%06d | ---- | %d> ",ls[source].starttime, source);
    }
    else{
      chprintf(dbg, "\r\n%06d | %04d | %d> ",ls[source].starttime, diff, source);
    }
    uint8_t c = ls[source].lastchar[i];
    if (dump_ascii != 0){
      if (c > 0x1F) // print only printable characters
        chprintf(dbg, "%02x | %c", c, c);
      else
        chprintf(dbg, "%02x |", c);          
    }
    else{
      chprintf(dbg, "%02x", c);
    }
  }
  //chprintf(dbg, "\r\n");
  ls[source].charcnt = 0;
  dump_in_progress = 0;
}

void receive_data(uint8_t source, uint8_t c){
  if (source == 2){
    chprintf(dbg, "\r\n%06d BREAK---------------- ",TIME_I2MS(chVTGetSystemTime()));
  }
  else{
    if (last_src != source){ // first char of other listener
      //if ((ls[other_src].charcnt == 0) && (ls[source].charcnt == 0)){
      //  blockstart = TIME_I2MS(chVTGetSystemTime());
      //}
      oldstart = ls[source].starttime;
      ls[source].starttime = TIME_I2MS(chVTGetSystemTime());
      other_src = (source+1) & 1; // get other listener
      if (ls[other_src].charcnt){ // it is NOT the first transmission
        dump_data(other_src);
      }
    }
    ls[source].lastchar[ls[source].charcnt] = c;
    ls[source].charcnt ++;
  }
  last_src = source;
}

static THD_WORKING_AREA(waListener1, 128);
static THD_FUNCTION(Listener1, arg) {

  (void)arg;
  uint8_t c;
  chRegSetThreadName("listener1");
  while (true) {
    sdRead(&SD1, (uint8_t *)&c, 1);
    last_received_char = TIME_I2MS(chVTGetSystemTime());
    receive_data(0,c);
  }
  chThdSleepMilliseconds(50);
}
static THD_WORKING_AREA(waListener2, 128);
static THD_FUNCTION(Listener2, arg) {

  (void)arg;
  uint8_t c;
  chRegSetThreadName("listener2");
  while (true) {
    sdRead(&SD2, (uint8_t *)&c, 1);
    last_received_char = TIME_I2MS(chVTGetSystemTime());
    receive_data(1,c);
  }
  chThdSleepMilliseconds(50);
}
static THD_WORKING_AREA(waListener3, 128);
static THD_FUNCTION(Listener3, arg) {

  (void)arg;
  static uint8_t disable = 0, source = 0;
  chRegSetThreadName("listener3");
  while (true) {
    if (palReadPad(GPIOA, 0)){
      if (disable == 0){
        receive_data(2,0);
        disable = 1;
      }
    }
    else{
      disable = 0;
    }
    if ((TIME_I2MS(chVTGetSystemTime()) - last_received_char > 100) && 
        (ls[source].charcnt) &&
        (ls[(source+1)&1].charcnt == 0) &&
        (dump_in_progress == 0)){
      //chprintf(dbg, "\r\n---------------------------\r\n");
      //chprintf(dbg, "Now: %d lastchar: %d\r\n", TIME_I2MS(chVTGetSystemTime()), last_received_char);
      oldstart = ls[(source+1)&1].starttime;
      dump_data(source);
      blockstart = 0;
    }
    source = (source+1)&1;
    chThdSleepMilliseconds(50);
  }
}

/*
 * Application entry point.
 */
int main(void) {
  thread_t *shelltp = NULL;
  event_listener_t shell_el;

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  palSetPadMode(GPIOC, 13, PAL_MODE_OUTPUT_PUSHPULL ); // LED
  palSetPadMode(GPIOC, 14, PAL_MODE_OUTPUT_PUSHPULL ); // Debug
  palSetPadMode(GPIOC, 15, PAL_MODE_OUTPUT_PUSHPULL ); // Debug


  sduObjectInit(&SHELLPORT);
  sduStart(&SHELLPORT, &serusbcfg1);

  usbDisconnectBus(serusbcfg1.usbp);
  chThdSleepMilliseconds(1500);
  usbStart(serusbcfg1.usbp, &usbcfg);
  usbConnectBus(serusbcfg1.usbp);

  //chprintf(dbg, "\r\nSerial Sniffer Programmer: %i.%i \r\nSystem started. (Shell)\r\n", VMAJOR, VMINOR);
  if (serstat){
    //palSetPadMode(GPIOA, 2, PAL_MODE_ALTERNATE(7));  // TX2
    palSetPadMode(GPIOA, 3, PAL_MODE_ALTERNATE(7));  // RX2
    //palSetPadMode(GPIOA, 9, PAL_MODE_ALTERNATE(7));  // TX1
    palSetPadMode(GPIOA, 10, PAL_MODE_ALTERNATE(7)); // RX1
    palSetPadMode(GPIOA, 0, PAL_MODE_INPUT_PULLUP);  // Button
    sdStart(&SD2, &serial_config);
    sdStart(&SD1, &serial_config);
  }
  else{
    palSetPadMode(GPIOA, 0, PAL_MODE_ALTERNATE(1));  // TIM2/1
    //palSetPadMode(GPIOA, 3, PAL_MODE_ALTERNATE(3));  // TIM9/2
    //palSetPadMode(GPIOA, 10, PAL_MODE_ALTERNATE(1)); // TIM1/3
    icuStart(&ICUD2, &icucfg1);
    icuStartCapture(&ICUD2);
    icuEnableNotifications(&ICUD2);
  }
  /*
   * Shell manager initialization.
   * Event zero is shell exit.
   */
  shellInit();
  chEvtRegister(&shell_terminated, &shell_el, 0);
  chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);
  chThdCreateStatic(waListener1, sizeof(waListener1), NORMALPRIO, Listener1, NULL);
  chThdCreateStatic(waListener2, sizeof(waListener2), NORMALPRIO, Listener2, NULL);
  chThdCreateStatic(waListener3, sizeof(waListener3), NORMALPRIO, Listener3, NULL);
  
  /*
   * Normal main() thread activity, in this demo it does nothing except
   * sleeping in a loop and check the button state.
   */
  while (true) {
#if USB_SHELL == 1
    if (SHELLPORT.config->usbp->state == USB_ACTIVE) {
      /* Starting shells.*/
      if (shelltp == NULL) {
        shelltp = chThdCreateFromHeap(NULL, SHELL_WA_SIZE,
                                       "shell1", NORMALPRIO + 1,
                                       shellThread, (void *)&shell_cfg1);
      }
    //chThdWait(shelltp);               /* Waiting termination.             */
    chEvtWaitAny(EVENT_MASK(0));
    if (chThdTerminatedX(shelltp)) {
      chThdRelease(shelltp);
      shelltp = NULL;
    }
#else
    if (!shelltp)
      shelltp = chThdCreateFromHeap(NULL, SHELL_WA_SIZE,
                                    "shell", NORMALPRIO + 1,
                                    shellThread, (void *)&shell_cfg1);
    else if (chThdTerminatedX(shelltp)) {
      chThdRelease(shelltp);    /* Recovers memory of the previous shell.   */
      shelltp = NULL;           /* Triggers spawning of a new shell.        */
    }
#endif
    /* Waiting for an exit event then freeing terminated shells.*/
    }
    else{
      chThdSleepMilliseconds(1000);
    }
  }
}
