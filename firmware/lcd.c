/****************************************************************************
 Title	:   HD44780U LCD library / stripped down version for lcd@usb
 Author:    Peter Fleury <pfleury@gmx.ch>  http://jump.to/fleury
            lcd@usb modifications by Till Harbaum <till@harbaum.org>
 File:	    $Id: lcd.c,v 1.2 2007/01/14 12:12:27 harbaum Exp $
 Software:  AVR-GCC 3.3 
 Target:    any AVR device, memory mapped mode only for AT90S4414/8515/Mega

 DESCRIPTION
       Basic routines for interfacing a HD44780U-based text lcd display

       Originally based on Volker Oth's lcd library,
       changed lcd_init(), added additional constants for lcd_command(),
       added 4-bit I/O mode, improved and optimized code.

       Memory mapped mode compatible with Kanda STK200, but supports also
       generation of R/W signal through A8 address line.

       Major changes for LCD2USB: Removed many functions not needed in 
       LCD2USB. Added support for a second controller.

 USAGE
       See the C include lcd.h file for a description of each function
       
*****************************************************************************/
#include <inttypes.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include "lcd.h"

/* 
** constants/macros 
*/
#define DDR(x) (*(&x - 1))  /* address of data direction register of port x */
#define PIN(x) (*(&x - 2))  /* address of input register of port x          */

#define lcd_e_delay()   __asm__ __volatile__( "rjmp 1f\n 1:" );
#define lcd_e0_high()   LCD_E_PORT  |=  _BV(LCD_E0_PIN);
#define lcd_e0_low()    LCD_E_PORT  &= ~_BV(LCD_E0_PIN);
#define lcd_e1_high()   LCD_E_PORT  |=  _BV(LCD_E1_PIN);
#define lcd_e1_low()    LCD_E_PORT  &= ~_BV(LCD_E1_PIN);
#define lcd_rw_high()   LCD_RW_PORT |=  _BV(LCD_RW_PIN)
#define lcd_rw_low()    LCD_RW_PORT &= ~_BV(LCD_RW_PIN)
#define lcd_rs_high()   LCD_RS_PORT |=  _BV(LCD_RS_PIN)
#define lcd_rs_low()    LCD_RS_PORT &= ~_BV(LCD_RS_PIN)

/* we don't really know anything about the display attached, */
/* so assume that it's a display with two lines              */
#define LCD_FUNCTION_DEFAULT    LCD_FUNCTION_4BIT_2LINES 

/* 
** function prototypes 
*/
static void lcd_e_toggle(uint8_t ctrl);

/*
** local functions
*/


/*************************************************************************
 delay loop for small accurate delays: 16-bit counter, 4 cycles/loop
*************************************************************************/
static inline void _delayFourCycles(unsigned int __count)
{
    if ( __count == 0 )    
        __asm__ __volatile__( "rjmp 1f\n 1:" );    // 2 cycles
    else
        __asm__ __volatile__ (
    	    "1: sbiw %0,1" "\n\t"                  
    	    "brne 1b"                              // 4 cycles/loop
    	    : "=w" (__count)
    	    : "0" (__count)
    	   );
}


/************************************************************************* 
delay for a minimum of <us> microseconds
the number of loops is calculated at compile-time from MCU clock frequency
*************************************************************************/
#define delay(us)  _delayFourCycles( ( ( 1*(XTAL/4000) )*us)/1000 )


/* toggle Enable Pin to initiate write */
static void lcd_e_toggle(uint8_t ctrl)
{
    if(ctrl & LCD_CTRL_0) lcd_e0_high();
    if(ctrl & LCD_CTRL_1) lcd_e1_high();

    lcd_e_delay();

    if(ctrl & LCD_CTRL_0) lcd_e0_low();
    if(ctrl & LCD_CTRL_1) lcd_e1_low();
}


/*************************************************************************
Low-level function to write byte to LCD controller
Input:    data   byte to write to LCD
          rs     1: write data    
                 0: write instruction
Returns:  none
*************************************************************************/
static void lcd_write(uint8_t ctrl, uint8_t data, uint8_t rs) 
{
    unsigned char dataBits ;


    if (rs) lcd_rs_high();   /* write data        (RS=1, RW=0) */
    else    lcd_rs_low();    /* write instruction (RS=0, RW=0) */
    lcd_rw_low();

    /* configure data pins as output */
    DDR(LCD_DATA_PORT) |= 0xF0;

    /* output high nibble first */
    dataBits = LCD_DATA_PORT & 0x0F;
    LCD_DATA_PORT = dataBits |(data&0xF0);
    lcd_e_toggle(ctrl);

    /* output low nibble */
    LCD_DATA_PORT = dataBits | (data<<4);
    lcd_e_toggle(ctrl);

    /* all data pins high (inactive) */
    LCD_DATA_PORT = dataBits | 0xF0;
}

/*************************************************************************
Low-level function to read byte from LCD controller
Input:    rs     1: read data    
                 0: read busy flag / address counter
Returns:  byte read from LCD controller
*************************************************************************/
static uint8_t lcd_read(uint8_t ctrl, uint8_t rs) 
{
    uint8_t data;
    
    if (rs) lcd_rs_high();                   /* RS=1: read data      */
    else    lcd_rs_low();                    /* RS=0: read busy flag */
    lcd_rw_high();                           /* RW=1  read mode      */
    
    DDR(LCD_DATA_PORT) &= 0x0F;         /* configure data pins as input */
    LCD_DATA_PORT |= 0xF0;              /* enable pullups to get a busy */
                                        /* on unconnected display       */

    if(ctrl & LCD_CTRL_0) lcd_e0_high();
    if(ctrl & LCD_CTRL_1) lcd_e1_high();
    lcd_e_delay();        
    data = PIN(LCD_DATA_PORT) & 0xF0;     /* read high nibble first */
    if(ctrl & LCD_CTRL_0) lcd_e0_low();
    if(ctrl & LCD_CTRL_1) lcd_e1_low();

    lcd_e_delay();                        /* Enable 500ns low       */
    
    if(ctrl & LCD_CTRL_0) lcd_e0_high();
    if(ctrl & LCD_CTRL_1) lcd_e1_high();
    lcd_e_delay();
    data |= PIN(LCD_DATA_PORT) >> 4;      /* read low nibble        */
    if(ctrl & LCD_CTRL_0) lcd_e0_low();
    if(ctrl & LCD_CTRL_1) lcd_e1_low();

    return data;
}


/*************************************************************************
loops while lcd is busy, returns address counter
*************************************************************************/
static void lcd_waitbusy(uint8_t ctrl)
{
#if 1
    uint8_t busy;

    do {
        busy = 0;

        /* check all controllers separately */
        if(ctrl & LCD_CTRL_0)
            if((lcd_read(LCD_CTRL_0, 0)) & (1<<LCD_BUSY))
	        busy = 1;

        if(ctrl & LCD_CTRL_1)
            if((lcd_read(LCD_CTRL_1, 0)) & (1<<LCD_BUSY))
	        busy = 1;

        /* wait until busy flag is cleared */
    } while (busy);
#else
    /* check all controllers at once (ugly!!!) */
   while ( (lcd_read(ctrl, 0)) & (1<<LCD_BUSY));
#endif
}

/*
** PUBLIC FUNCTIONS 
*/

/*************************************************************************
Send LCD controller instruction command
Input:   instruction to send to LCD controller, see HD44780 data sheet
Returns: none
*************************************************************************/
void lcd_command(uint8_t ctrl, uint8_t cmd)
{
    lcd_waitbusy(ctrl);
    lcd_write(ctrl, cmd, 0);
}


/*************************************************************************
Send data byte to LCD controller 
Input:   data to send to LCD controller, see HD44780 data sheet
Returns: none
*************************************************************************/
void lcd_data(uint8_t ctrl, uint8_t data)
{
    lcd_waitbusy(ctrl);
    lcd_write(ctrl, data, 1);
}

/*************************************************************************
Clear display and set cursor to home position
*************************************************************************/
void lcd_clrscr(uint8_t ctrl)
{
    lcd_command(ctrl, 1<<LCD_CLR);
}

/*************************************************************************
Display string without auto linefeed 
Input:    string to be displayed
Returns:  none
*************************************************************************/
void lcd_puts(uint8_t ctrl, const char *s)
/* print string on lcd (no auto linefeed) */
{
    while ( (*s) ) 
         lcd_data(ctrl, *s++);

}/* lcd_puts */


/*************************************************************************
Initialize display
Returns:  0 on failure, 1 else
*************************************************************************/
uint8_t lcd_init(uint8_t ctrl)
{
    /*
     *  Initialize LCD to 4 bit I/O mode
     */

    /* configure all port bits as output (all LCD lines on same port) */
    DDR(LCD_DATA_PORT) |= 0xF0;

    /* configure all port bits as output (all LCD data lines on same */
    /* port, but control lines on different ports) */
    DDR(LCD_RS_PORT)    |= _BV(LCD_RS_PIN);
    DDR(LCD_RW_PORT)    |= _BV(LCD_RW_PIN);
    DDR(LCD_E_PORT)     |= _BV(LCD_E0_PIN);  /* first controller   */
    DDR(LCD_E_PORT)     |= _BV(LCD_E1_PIN);  /* seconds controller */
  
    delay(16000);        /* wait 16ms or more after power-on       */

    /* initial write to lcd is 8bit */
    LCD_DATA_PORT = (LCD_DATA_PORT & 0x0F) | _BV(LCD_FUNCTION) | 
      _BV(LCD_FUNCTION_8BIT);
    lcd_e_toggle(ctrl);
    delay(4992);         /* delay, busy flag can't be checked here */
   
    /* repeat last command */ 
    lcd_e_toggle(ctrl);      
    delay(64);           /* delay, busy flag can't be checked here */
    
    /* repeat last command a third time */
    lcd_e_toggle(ctrl);      
    delay(64);           /* delay, busy flag can't be checked here */

    /* now configure for 4bit mode */
    LCD_DATA_PORT &= ~(_BV(LCD_FUNCTION) >> 4);
    lcd_e_toggle(ctrl);
    delay(64);           /* some displays need this additional delay */

    /* from now the LCD only accepts 4 bit I/O, we can use lcd_command() */    

    /* try to find out of there's a controller and return 0 if not */

    /* display must not be busy anymore */
    if((lcd_read(ctrl, 0)) & (1<<LCD_BUSY))
      return 0;

    /* function set: display lines  */
    lcd_command(ctrl, LCD_FUNCTION_DEFAULT);

    /* wait some time */
    delay(64);

    /* display must not be busy anymore */
    if((lcd_read(ctrl, 0)) & (1<<LCD_BUSY))
      return 0;

    lcd_command(ctrl, LCD_DISP_OFF);         /* display off                  */
    lcd_clrscr(ctrl);                        /* display clear                */
    lcd_command(ctrl, LCD_MODE_DEFAULT);     /* set entry mode               */
    lcd_command(ctrl, LCD_DISP_ON);          /* display/cursor control       */
  
    return 1;
}/* lcd_init */
