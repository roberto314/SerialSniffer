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

/*===========================================================================*/
/* Function Prototypes                                                       */
/*===========================================================================*/
void got_char(uint8_t c, uint8_t src);
uint32_t increase_rwcnt(uint32_t cnt);

/*===========================================================================*/
/* Global Variables                                                          */
/*===========================================================================*/
BaseSequentialStream *const shell = (BaseSequentialStream *)&SHELLPORT;
BaseSequentialStream *const dbg = (BaseSequentialStream *)&DEBUGPORT;

#define CLOCKFREQ 10000000UL
#define BUFFSZ 6000 

lst_st ldat[BUFFSZ];                // This is the Buffer
uint32_t write_cnt = 0, read_cnt = 0; // Indizes for Write and Read from Buffer
uint32_t rx_cnt = 0, tx_cnt = 0;
uint16_t flush_timeout = 1000;
uint8_t dump_format = 2, dump_in_progress = 0, first_char = 1;
systime_t fc_timestamp, last_received_char;

uint8_t serstat = 1; // 0: Serial off, Timer On
uint32_t smallest_pulse = 0xFFFF;
icucnt_t last_width1, last_period1;
uint32_t sp_temp;

/*===========================================================================*/
/* Button related code.                                                      */
/*===========================================================================*/

/* Function prototypes needed as the two callbacks call each other.
   there is no way to order the callback without triggering an error. */
static void button_cb(void *arg);
static void vt_cb(virtual_timer_t *vtp, void *p);
static uint8_t btn_second_edge = 0, btn_cnt = 0;

/* Virtual timer. */
static virtual_timer_t vt, vt2;

/* Callback of the virtual timer. */
static void vt_cb(virtual_timer_t *vtp, void *p) { // Timer cb after 50ms (single click)
  (void)vtp;
  (void)p;
  chSysLockFromISR();
  /* Enabling the event and associating the callback. */
  if (btn_second_edge) // The first edge is a negative one, the second a rising one
    palEnableLineEventI(EXTBTN, PAL_EVENT_MODE_RISING_EDGE); // Prepare for release of button
  else
    palEnableLineEventI(EXTBTN, PAL_EVENT_MODE_FALLING_EDGE);
  palSetLineCallbackI(EXTBTN, button_cb, NULL);
  chSysUnlockFromISR();
}

static void vt2_cb(virtual_timer_t *vtp, void *p) { // Timer cb after 200ms (double click)
  (void)vtp;
  (void)p;
  chSysLockFromISR();
  switch (btn_cnt){
  case 1:
    if (btn_second_edge){ // only for the first edge
      //chprintf(dbg, "click long. %d \r\n");
    }
    break;
  case 2:
    //chprintf(dbg, "click once. %d \r\n");
    got_char(' ', 3);
    break;
  default:
    //chprintf(dbg, "click double. %d \r\n");
    break;
  }

  btn_cnt = 0;
  chSysUnlockFromISR();
}
/* Callback associated to the falling or rising edge of the button line. */
static void button_cb(void *arg) {
  (void)arg;
  //palToggleLine(LED);
  if (btn_second_edge){    // next edge
    btn_second_edge = 0;
  }
  else{                  // very first negative going edge or Button released
    btn_second_edge = 1;
    //chprintf(dbg, "click immediately. %d \r\n");
  }
  btn_cnt++;           // count edges
  chSysLockFromISR();
  /* Disabling the event on the line and setting a timer to
     re-enable it. */
  palDisableLineEventI(EXTBTN);
  /* Arming the VT timer to re-enable the event in 50ms. */
  chVTResetI(&vt);
  chVTDoSetI(&vt, TIME_MS2I(50), vt_cb, NULL);
  chVTResetI(&vt2);
  chVTDoSetI(&vt2, TIME_MS2I(200), vt2_cb, NULL);
  chSysUnlockFromISR();
}

/*===========================================================================*/
/* Character Write Function                                                  */
/*===========================================================================*/

void got_char(uint8_t c, uint8_t src){ // This function fills the Ringbuffer
  //int32_t temp;
  //temp = write_cnt-1; // last entry
  last_received_char = TIME_I2MS(chVTGetSystemTime()); // get Timestamp
  if (src == 3){
    chprintf(dbg, "------------------ BREAK @ T[ms]: %06d\r\n", last_received_char);
  }
  else{
    ldat[write_cnt].lastchar = c;
    ldat[write_cnt].src = src;
    ldat[write_cnt].timestamp = last_received_char;
    if ((rx_cnt+tx_cnt) <= 1){
      //first_char = 0;
      chprintf(dbg, "\r\n------------------------- START HERE ------------------------------\r\n");
      fc_timestamp = last_received_char; // very first character
    } 
    write_cnt = increase_rwcnt(write_cnt);
  }
}

/*
 * This callback is invoked when a character is received but the application
 * was not ready to receive it, the character is passed as parameter.
 */
static void rxchar1(UARTDriver *uartp, uint16_t c) {
  (void)uartp;
  (void)c;
  rx_cnt++;
  got_char((uint8_t)c, 1);
}

UARTConfig uart_cfg1 = {
  NULL,
  NULL,
  NULL,
  rxchar1,
  NULL,
  NULL,
  38400,
  0,  // CR1 
  0,  // CR2 
  0   // CR3
};

static void rxchar2(UARTDriver *uartp, uint16_t c) {
  (void)uartp;
  (void)c;
  tx_cnt++;
  got_char((uint8_t)c, 2);
}

UARTConfig uart_cfg2 = {
  NULL,
  NULL,
  NULL,
  rxchar2,
  NULL,
  NULL,
  38400,
  0,  // CR1 
  0,  // CR2 
  0   // CR3
};

/*===========================================================================*/
/* Baudrate Measurement related                                              */
/*===========================================================================*/

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

void flush_buffer(void){
  write_cnt = 0;
  read_cnt = 0;
  memset(ldat, 0, sizeof(ldat));
  chprintf(dbg, "RX Count: %d or 0x%04X\r\n", rx_cnt, rx_cnt);
  chprintf(dbg, "TX Count: %d or 0x%04X\r\n", tx_cnt, tx_cnt);
  chprintf(dbg, "Total Count: %d or 0x%04X\r\n",(tx_cnt+rx_cnt), (tx_cnt+rx_cnt));
  rx_cnt = 0;
  tx_cnt = 0;
}

/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/

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

void safe_print(uint8_t c){  // print only printable characters
  if ((c > 0x1F) && (c < 128))
    chprintf(dbg, "%c", c);
  else
    chprintf(dbg, "%c",'.');    
}

uint32_t increase_rwcnt(uint32_t cnt){
  cnt++;
  if (cnt >= BUFFSZ) cnt = 0; // check for oerflow
  return cnt;
}

int32_t get_count(void){
/* Data in Array (c: Character, 1,2: Source, t: Timestamp)
* c2tc2tc2tc2tc1tc1tc2t
* Dump this  |
*        make CR
*            |Dump|
*              make CR
*              wait until a change in source or a timeout happens
*/
  uint32_t temp_cnt = read_cnt;
  uint8_t src, next;
  if (ldat[temp_cnt].src == 0) return -2; // no more data in array
  while (1) {
    src = ldat[temp_cnt].src;
    temp_cnt = increase_rwcnt(temp_cnt);
    next = ldat[temp_cnt].src;
    if (src != next){ // we have a change in source
      if (next == 0) return -1; // block not finished, wait for timeout
      else return (temp_cnt - read_cnt); // block is finished
    } 
  }
  //chprintf(dbg, "i: %d, SRC: %d NXT: %d\r\n", i, src, next);
  return -2;
}

void dump_data_0(uint32_t cnt){
  uint32_t i;
  uint8_t ch, src;
  systime_t tm, dt;

  src = ldat[read_cnt].src;
  chprintf(dbg, "%d->%d cnt: %d \r\n", src, (src==1?2:1), cnt);
  for (i=0;i<cnt;i++){ // go through all the characters
    ch = ldat[read_cnt].lastchar;
    src = ldat[read_cnt].src;
    ldat[read_cnt].src = 0; // delete for next round
    tm = ldat[read_cnt].timestamp - fc_timestamp; // tm starts at zero
    dt = ldat[read_cnt].timestamp - ldat[(read_cnt-1)].timestamp; // time difference 
    if (tm)
      chprintf(dbg, "%02X T: %06d dT: %04d\r\n", ch, tm, dt);
    else
      chprintf(dbg, "%02X T: %06d dT: ----\r\n", ch, tm);
    read_cnt = increase_rwcnt(read_cnt);
  }
  //chprintf(dbg, "\r\n");
  return;
}

void dump_data_1(uint32_t cnt){
  uint32_t i;
  uint8_t ch, src;
  systime_t tm;

  src = ldat[read_cnt].src;
  tm = ldat[read_cnt].timestamp - fc_timestamp; // tm starts at zero
  chprintf(dbg, "%d->%d cnt: %d T: %06d\r\n", src, (src==1?2:1), cnt, tm);
  for (i=0;i<cnt;i++){ // go through all the characters
    ch = ldat[read_cnt].lastchar;
    ldat[read_cnt].src = 0; // delete for next round
    read_cnt = increase_rwcnt(read_cnt);

    if ((i%16 == 0) && (i>0)) chprintf(dbg, "\r\n");
    chprintf(dbg, "%02X ", ch);
  }
  chprintf(dbg, "\r\n");
  return;
}

void dump_data_2(uint32_t cnt){
  uint32_t i, j, line, width = 0;
  uint8_t ch, src;
  uint8_t ascii[16];
  systime_t tm;

  src = ldat[read_cnt].src;
  tm = ldat[read_cnt].timestamp - fc_timestamp; // tm starts at zero
  chprintf(dbg, "%d->%d cnt: %d T: %06d\r\n", src, (src==1?2:1), cnt, tm);
  for (line=0; line < cnt;line+=16){
    width = 0;
    // print HEX here
    for (i=0;i<16;i++){ // print max 16 char per line
      if ((i+line) >= cnt){
        chprintf(dbg, "   "); // fill the space
      }
      else{
        ch = ldat[read_cnt].lastchar;
        ascii[width] = ch;
        ldat[read_cnt].src = 0; // delete for next round
        read_cnt = increase_rwcnt(read_cnt);
        chprintf(dbg, "%02X ", ch);
        width++;
      }
    }
    chprintf(dbg, " | "); // make separator
    // print ASCII here
    for (j=0;j<width;j++){ // print ASCII
      safe_print(ascii[j]);
    }
    chprintf(dbg, "\r\n");
  } // end Line   
  return;
}

void check_data(void){       // Thread, every 50ms
  uint32_t cnt;
  int32_t temp;

  if ((rx_cnt+tx_cnt) == 0) return; // nothing to print
  
  temp = get_count();
  if (temp == -2) return; // no data
  if (temp == -1){
    if (TIME_I2MS(chVTGetSystemTime()) - last_received_char > flush_timeout){
      cnt = write_cnt - read_cnt; // dump everything
    }
    else return;
  }
  else cnt = (uint32_t)temp;
  //chprintf(dbg, "%d->%d c: %d OLD: %d LDAT: %d\r\n", src, dest, cnt, read_cnt, write_cnt);
  switch(dump_format){
  case 0:
    dump_data_0(cnt);
    break;
  case 1:
    dump_data_1(cnt);
    break;
  case 2:
    dump_data_2(cnt);
    break;
  default:
    break;
  }
}

static THD_WORKING_AREA(waListener3, 128);
static THD_FUNCTION(Listener3, arg) { // This Thread listens to the onboard Button to make a "Mark" in the Logging,
                                      // It also checks if data is to output

  (void)arg;
  static uint8_t disable = 0;
  chRegSetThreadName("listener3");
  while (true) {
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
  palSetLineMode(EXTBTN, PAL_MODE_INPUT_PULLUP); // Button
  /* Enabling the event and associating the callback. */
  palEnableLineEvent(EXTBTN, PAL_EVENT_MODE_FALLING_EDGE);
  palSetLineCallback(EXTBTN, button_cb, NULL);

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
    uartStart(&UARTD1, &uart_cfg1);
    uartStart(&UARTD2, &uart_cfg2);
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
