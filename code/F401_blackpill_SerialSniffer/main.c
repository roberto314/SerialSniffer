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
  {"format",cmd_format},
  {"flush",cmd_flush},
  {"stt",cmd_stt},
  {NULL, NULL}
};
static const ShellConfig shell_cfg1 = {
  (BaseSequentialStream *)&SHELLPORT,
  commands,
  history_buffer,
  sizeof(history_buffer),
  completion_buffer
};

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

#define BUFFSZ 1024 // must be a power of two!
#define BUFFMSK (BUFFSZ - 1)
lst_st ldat[BUFFSZ];
uint32_t ldat_cnt = 0, oldcnt = 0;
uint16_t flush_timeout = 1000;
uint8_t dump_format = 2, dump_in_progress = 0;
systime_t firstchar, last_received_char;

void flush_buffer(void){
  ldat_cnt = 0;
  oldcnt = 0;
  memset(ldat, 0, sizeof(ldat));
}
void safe_print(uint8_t c){  // print only printable characters
  if ((c > 0x1F) && (c < 128))
    chprintf(dbg, "%c", c);
  else
    chprintf(dbg, "%c",'.');    
}

int32_t get_count(void){
  uint32_t i;
  uint8_t src, next;
  if (ldat[oldcnt & BUFFMSK].src == 0) return -2;
  for (i=oldcnt;;i++){
    src = ldat[i & BUFFMSK].src;
    next = ldat[(i+1) & BUFFMSK].src;
    //chprintf(dbg, "i: %d, SRC: %d NXT: %d\r\n", i, src, next);
    if (src != next){ // we have a change in source
      if (next == 0) return -1; // block not finished
      else return (i - oldcnt + 1); // block is finished
    }
  }
  return -2;
}

uint32_t dump_data_0(uint32_t cnt, uint8_t src){
  uint32_t i, idx;
  uint8_t ch;
  systime_t tm, dt;
  if (src == 3){
    chprintf(dbg, "------------------ BREAK @ T: %06d\r\n", tm);
    return cnt+oldcnt;
  }  
  chprintf(dbg, "%d->%d cnt: %d \r\n", src, (src==1?2:1), cnt);
  for (i=0;i<cnt;i++){ // go through all the characters
    idx = (i+oldcnt) & BUFFMSK;
    ch = ldat[idx].lastchar;
    tm = ldat[idx].timestamp - firstchar; // tm starts at zero
    dt = ldat[idx].timestamp - ldat[(idx-1)].timestamp; // time difference 
    if (tm)
      chprintf(dbg, "%02X T: %06d dT: %04d\r\n", ch, tm, dt);
    else
      chprintf(dbg, "%02X T: %06d dT: ----\r\n", ch, tm);
  }
  //chprintf(dbg, "\r\n");
  return cnt+oldcnt;
}

uint32_t dump_data_1(uint32_t cnt, uint8_t src){
  uint32_t i, idx;
  uint8_t ch;
  systime_t tm;
  tm = ldat[oldcnt].timestamp - firstchar; // tm starts at zero
  if (src == 3){
    chprintf(dbg, "------------------ BREAK @ T: %06d\r\n", tm);
    return cnt+oldcnt;
  }
  chprintf(dbg, "%d->%d cnt: %d T: %06d\r\n", src, (src==1?2:1), cnt, tm);
  for (i=0;i<cnt;i++){ // go through all the characters
    idx = (i+oldcnt) & BUFFMSK;
    ch = ldat[idx].lastchar;
    if ((i%16 == 0) && (i>0)) chprintf(dbg, "\r\n");
    chprintf(dbg, "%02X ", ch);
  }
  chprintf(dbg, "\r\n");
  return cnt+oldcnt;
}

uint32_t dump_data_2(uint32_t cnt, uint8_t src){
  uint32_t i, idx, line;
  uint8_t ch;
  systime_t tm;
  tm = ldat[oldcnt].timestamp - firstchar; // tm starts at zero
  if (src == 3){
    chprintf(dbg, "------------------ BREAK @ T: %06d\r\n", tm);
    return cnt+oldcnt;
  }
  chprintf(dbg, "%d->%d cnt: %d T: %06d\r\n", src, (src==1?2:1), cnt, tm);
  for (line=0; line < cnt;line+=16){
    // print HEX here
    for (i=0;i<16;i++){
      idx = (line+i+oldcnt) & BUFFMSK;
      if ((i+line) >= cnt){
        chprintf(dbg, "   "); // fill the space
      }
      else{
        ch = ldat[idx].lastchar;
        chprintf(dbg, "%02X ", ch);
      }
    }
    chprintf(dbg, " | "); // make separator
    // print ASCII here
    for (i=0;i<16;i++){
      idx = (line+i+oldcnt) & BUFFMSK;
      if ((i+line) >= cnt){
        //chprintf(dbg, "."); // fill the space
      }
      else{
        safe_print(ldat[idx].lastchar);
      }
    }    
    chprintf(dbg, "\r\n");
  }
  //chprintf(dbg, "\r\n");
  return cnt+oldcnt;
}

void check_data(void){
  uint32_t cnt, cnt_temp, i;
  int32_t temp;
  uint8_t src;

  src = ldat[oldcnt & BUFFMSK].src;
  
  if (ldat_cnt == oldcnt) return;
  //dump_in_progress = 1;
  //chprintf(dbg, "getcnt ldat_cnt.: %d, src: %d\r\n", ldat_cnt, ldat[ldat_cnt].src);
  temp = get_count();
  if (temp == -2) return; // no data
  if (temp == -1){
    if (TIME_I2MS(chVTGetSystemTime()) - last_received_char > flush_timeout){
      cnt = ldat_cnt - oldcnt;
    }
    else return;
  }
  else cnt = (uint32_t)temp;
  //chprintf(dbg, "%d->%d c: %d OLD: %d LDAT: %d\r\n", src, dest, cnt, oldcnt, ldat_cnt);
  cnt_temp = oldcnt;
  switch(dump_format){
  case 0:
    oldcnt = dump_data_0(cnt, src);
    break;
  case 1:
    oldcnt = dump_data_1(cnt, src);
    break;
  case 2:
    oldcnt = dump_data_2(cnt, src);
    break;
  default:
    break;
  }
  for (i=cnt_temp; i<(oldcnt);i++ ){
    ldat[(i & BUFFMSK)].src = 0; // clear the source for the next time the pointer comes around
  }
  if (oldcnt > BUFFMSK) oldcnt = (oldcnt & BUFFMSK);
  dump_in_progress = 0;
  //chprintf(dbg, "CNT: %d, OLD: %d\r\n", ldat_cnt, oldcnt);
}

void got_char(uint8_t c, uint8_t src){
  //int32_t temp;
  //temp = ldat_cnt-1; // last entry
  last_received_char = TIME_I2MS(chVTGetSystemTime());
  ldat[ldat_cnt & BUFFMSK].lastchar = c;
  ldat[ldat_cnt & BUFFMSK].src = src;
  ldat[ldat_cnt & BUFFMSK].timestamp = last_received_char;
  if (ldat_cnt == 0){
    chprintf(dbg, "\r\n------------------------- START HERE ------------------------------\r\n");
    firstchar = last_received_char; // very first character
  } 
  ldat_cnt = ((ldat_cnt + 1)  & BUFFMSK) ;
}

static THD_WORKING_AREA(waListener1, 512);
static THD_FUNCTION(Listener1, arg) {

  (void)arg;
  uint8_t c;
  chRegSetThreadName("listener1");
  while (true) {
    sdRead(&SD1, (uint8_t *)&c, 1);
    //chprintf(dbg, "1: %c\r\n", c);
    got_char(c, 1);
  }
  chThdSleepMilliseconds(10);
}
static THD_WORKING_AREA(waListener2, 512);
static THD_FUNCTION(Listener2, arg) {

  (void)arg;
  uint8_t c;
  chRegSetThreadName("listener2");
  while (true) {
    sdRead(&SD2, (uint8_t *)&c, 1);
    //chprintf(dbg, "2: %c\r\n", c);
    got_char(c, 2);
  }
  chThdSleepMilliseconds(10);
}
static THD_WORKING_AREA(waListener3, 128);
static THD_FUNCTION(Listener3, arg) {

  (void)arg;
  static uint8_t disable = 0;
  chRegSetThreadName("listener3");
  while (true) {
    if (palReadPad(GPIOA, 0) == PAL_LOW){
      if (disable == 0){
        got_char(' ', 3);
        disable = 1;
      }
    }
    else{
      disable = 0;
    }
    if (dump_in_progress == 0)
      check_data();
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
    chSysLock();
    oqResetI(&SD1.oqueue);
    oqResetI(&SD2.oqueue);
    chSchRescheduleS();
    chSysUnlock();
    flush_buffer();
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
