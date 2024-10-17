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
  9600,
  0,  // CR1 
  0,  // CR2 
  0   // CR3
};

uint8_t serstat = 1; // 0: Serial off, Timer On
uint32_t smallest_pulse = 0xFFFF;
uint32_t sp_temp;
//uint8_t count;
uint8_t last_src;
systime_t start;

icucnt_t last_width1, last_period1;

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
void send_data(uint8_t source, uint8_t c){
  if (last_src == source){
    if (source < 3)
      chprintf(dbg, "%02x ", c);
  }
  else{
    start = TIME_I2MS(chVTGetSystemTime());
    if (source < 3)
      chprintf(dbg, "\r\n%06d %d> %02x ",start, source, c);
    else
      chprintf(dbg, "\r\n%06d BREAK---------------- ",start);
    last_src = source;

  }

}

static THD_WORKING_AREA(waListener1, 128);
static THD_FUNCTION(Listener1, arg) {

  (void)arg;
  uint8_t c;
  chRegSetThreadName("listener1");
  while (true) {
    sdRead(&SD1, (uint8_t *)&c, 1);
    send_data(1,c);
    chThdSleepMilliseconds(50);
  }
}
static THD_WORKING_AREA(waListener2, 128);
static THD_FUNCTION(Listener2, arg) {

  (void)arg;
  uint8_t c;
  chRegSetThreadName("listener2");
  while (true) {
    sdRead(&SD2, (uint8_t *)&c, 1);
    send_data(2,c);
    chThdSleepMilliseconds(50);
  }
}
static THD_WORKING_AREA(waListener3, 128);
static THD_FUNCTION(Listener3, arg) {

  (void)arg;
  static uint8_t disable = 0;
  chRegSetThreadName("listener3");
  while (true) {
    if (palReadPad(GPIOA, 0)){
      if (disable == 0){
        send_data(3,0);
        disable = 1;
      }
    }
    else{
      disable = 0;
    }
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
