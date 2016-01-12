/* Name: main.c
 * Project: LCD2USB; lcd display interface based on AVR USB driver
 * Author: Till Harbaum
 * Creation Date: 2006-01-20
 * Tabsize: 4
 * Copyright: (c) 2005 by Till Harbaum <till@harbaum.org>
 * License: GPL
 * This Revision: $Id: main.c,v 1.2 2007/01/14 12:12:27 harbaum Exp $
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>

#include <util/delay.h>

#include "lcd.h"

// use avrusb library
#include "usbdrv.h"
#include "oddebug.h"

#define VERSION_MAJOR 1
#define VERSION_MINOR 9
#define VERSION_STR "1.09"
// change USB_CFG_DEVICE_VERSION in usbconfig.h as well

// EEMEM wird bei aktuellen Versionen der avr-lib in eeprom.h definiert
// hier: definiere falls noch nicht bekannt ("alte" avr-libc)
#ifndef EEMEM
// alle Textstellen EEMEM im Quellcode durch __attribute__ ... ersetzen
#define EEMEM  __attribute__ ((section (".eeprom")))
#endif
 
/* bitmask of detected lcd controllers */
uchar controller = 0;

/* ------------------------------------------------------------------------- */
/* PWM units are used for contrast and backlight brightness */

/* contrast and brightness are stored in eeprom */
uchar eeprom_valid EEMEM;
uchar eeprom_contrast EEMEM;
uchar eeprom_brightness EEMEM;

void pwm_init(void) {

  /* check if eeprom is valid and set default values if not */
  /* initial values: full contrast and full brightness */
  if(eeprom_read_byte(&eeprom_valid) != 0x42) {

    /* write magic "eeprom is valid" marker 0x42 and default values */
    eeprom_write_byte(&eeprom_valid, 0x42);
    eeprom_write_byte(&eeprom_contrast, 0xff);
    eeprom_write_byte(&eeprom_brightness, 0xff);
  }

  /* PortB: set DDB1 and DDB2 => PORTB1 and PORTB2 are output */
  DDRB |= _BV(1) | _BV(2);
 
  /* Set Timer1:
    - Fast PWM,8bit => Mode 5 (WGM13=0,WGM12=1,WGM11=0,WGM10=1)
    - Output-Mode: lower voltage is higher contrast
      => Set OC1A on Compare Match, clear OC1A at BOTTOM, (inverting mode)
	 COM1A1=1,COM1A0=1
	 higher voltage is higher brightness
      => Clear OC1B on Compare Match, set OC1B at BOTTOM, (non-inverting mode)
	 COM1B1=1,COM1B0=0
    - Timer runs at inernal clock with no prescaling => CS12=0,CS11=0,CS10=1
  */
  TCCR1A = _BV(COM1A1) | _BV(COM1A0) | _BV(COM1B1) | _BV(WGM10);
  TCCR1B = _BV(WGM12) | _BV(CS10);

  TIMSK &=( (~_BV(2)) & (~_BV(3)) & (~_BV(4)) & (~_BV(5)));

  OCR1A = eeprom_read_byte(&eeprom_contrast);
  OCR1B = eeprom_read_byte(&eeprom_brightness);
}

void set_contrast(uchar value) {
  /* store value in eeprom if it actually changed */
  if(value != eeprom_read_byte(&eeprom_contrast))
    eeprom_write_byte(&eeprom_contrast, value);

  OCR1A = value;  // lower voltage is higher contrast
}

void set_brightness(uchar value) {
  /* store value in eeprom if it actually changed */
  if(value != eeprom_read_byte(&eeprom_brightness))
    eeprom_write_byte(&eeprom_brightness, value);

  OCR1B = value;  // higher voltage is higher brightness
}

/* ------------------------------------------------------------------------- */

uchar	usbFunctionSetup(uchar data[8]) {
  static uchar replyBuf[4];
  usbMsgPtr = replyBuf;
  uchar len = (data[1] & 3)+1;       // 1 .. 4 bytes 
  uchar target = (data[1] >> 3) & 3; // target 0 .. 3
  uchar i;

  // request byte:

  // 7 6 5 4 3 2 1 0
  // C C C T T R L L

  // TT = target bit map 
  // R = reserved for future use, set to 0
  // LL = number of bytes in transfer - 1 

  switch(data[1] >> 5) {

  case 0: // echo (for transfer reliability testing)
    replyBuf[0] = data[2];
    replyBuf[1] = data[3];
    return 2;
    break;
    
  case 1: // command
    target &= controller;  // mask installed controllers

    if(target) // at least one controller should be used ...
      for(i=0;i<len;i++)
	lcd_command(target, data[2+i]);
    break;

  case 2: // data
    target &= controller;  // mask installed controllers

    if(target) // at least one controller should be used ...
      for(i=0;i<len;i++)
	lcd_data(target, data[2+i]);
    break;

  case 3: // set
    switch(target) {

    case 0:  // contrast
      set_contrast(data[2]);
      break;

    case 1:  // brightness
      set_brightness(data[2]);
      break;

    default:
      // must not happen ...
      break;      
    }
    break;

  case 4: // get
    switch(target) {
    case 0: // version
      replyBuf[0] = VERSION_MAJOR;
      replyBuf[1] = VERSION_MINOR;
      return 2;
      break;

    case 1: // keys
      replyBuf[0] = ((PINC & _BV(5))?0:1) | 
	            ((PINB & _BV(0))?0:2);
      replyBuf[1] = 0;
      return 2;
      break;

    case 2: // controller map
      replyBuf[0] = controller;
      replyBuf[1] = 0;
      return 2;
      break;      

    default:
      // must not happen ...
      break;      
    }
    break;

  default:
    // must not happen ...
    break;
  }

  return 0;  // reply len
}

/* ------------------------------------------------------------------------- */

int	main(void) {
  wdt_enable(WDTO_1S);

  /* let debug routines init the uart if they want to */
  odDebugInit();

  /* all outputs except INT0 and RxD/TxD */
  DDRD = ~(_BV(2) | _BV(1) | _BV(0));  
  PORTD = 0;
  PORTC = 0;		   /* no pullups on USB pins */
  DDRC = ~0;		   /* output SE0 for USB reset */

  /* USB Reset by device only required on Watchdog Reset */
  _delay_loop_2(40000);   // 10ms

  DDRC = ~USBMASK;	   /* all outputs except USB data */
  usbInit();

  pwm_init();

  DDRC &= ~_BV(5);         /* input S1 */
  PORTC |= _BV(5);         /* with pullup */
  DDRB &= ~_BV(0);         /* input S2 */
  PORTB |= _BV(0);         /* with pullup */

  /* try to init two controllers */
  if(lcd_init(LCD_CTRL_0)) controller |= LCD_CTRL_0;
  if(lcd_init(LCD_CTRL_1)) controller |= LCD_CTRL_1;

  /* put string to display (line 1) with linefeed */
  if(controller & LCD_CTRL_0)
    lcd_puts(LCD_CTRL_0, "LCD2USB V" VERSION_STR);

  if(controller & LCD_CTRL_1)
    lcd_puts(LCD_CTRL_1, "2nd ctrl");

  if((controller & LCD_CTRL_0) && (controller & LCD_CTRL_1))
    lcd_puts(LCD_CTRL_0 | LCD_CTRL_1, " both!");

  sei();
  for(;;) {	/* main event loop */
    wdt_reset();
    usbPoll();
  }
  return 0;
}

/* ------------------------------------------------------------------------- */
