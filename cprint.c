/* cprint.c                      */
/*                         */
/*   This file contains simple ASCII output routines.  These are used   */
/* by the device driver for debugging, and to issue informational */
/* messages (e.g. while loading).  In general the C run time library    */
/* functions probably aren't safe for use withing the context of a   */
/* device driver and should be avoided.               */
/*                         */
/*   All these routines do their output thru the routine outchr, which  */
/* is defined in DRIVER.ASM.  It calls the BIOS INT 10 "dumb TTY" */
/* output function directly and does not use MSDOS at all.     */
/*                         */
/* Copyright (C) 1994 by Robert Armstrong          */
/*                         */
/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation; either version 2 of the License, or */
/* (at your option) any later version.             */
/*                         */
/* This program is distributed in the hope that it will be useful, but  */
/* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANT- */
/* ABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General */
/* Public License for more details.             */
/*                         */
/* You should have received a copy of the GNU General Public License */
/* along with this program; if not, visit the website of the Free */
/* Software Foundation, Inc., www.gnu.org.            */

#include <stdio.h>      /* NULL, etc...            */
#include <stdint.h>
#include <dos.h>        /* used only for MK_FP !      */
#include <stdarg.h>     /* needed for variable argument lists  */

#include "cprint.h"     /* Console printing */


void set_crtc_reg(char reg, char value) {
    static volatile char far *crtc_addr_reg = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                   CRTC_ADDR_REG_OFFSET);

    static volatile char far *crtc_data_reg = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                   CRTC_DATA_REG_OFFSET); 
    *crtc_addr_reg = reg;   //which register to set
    *crtc_data_reg = value; //what value to set it to
}

char get_crtc_reg(char reg) {
    static volatile char far *crtc_addr_reg = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                   CRTC_ADDR_REG_OFFSET);

    static volatile char far *crtc_data_reg = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                   CRTC_DATA_REG_OFFSET); 
    char value = 0;
    *crtc_addr_reg = reg;   //which register to set
    value = *crtc_data_reg; //what value to set it to
    return value;
}

void set_screen_start(uint16_t start_addr) {
    set_crtc_reg(0x0C, (start_addr >> 8) & 0xFF); // R12
    set_crtc_reg(0x0D, start_addr & 0xFF);        // R13
}

void set_cursor_position(uint16_t position) {
    set_crtc_reg(0x0E, (position >> 8) & 0xFF); // R14
    set_crtc_reg(0x0F, position & 0xFF);        // R15
}

unsigned short get_cursor_position() {
    unsigned short position = 0;
    char high_byte, low_byte;

    high_byte = get_crtc_reg(0x0E); // R14
    low_byte = get_crtc_reg(0x0F); // R15
    position = (high_byte << 8) | low_byte;
    return position;
}

void newline() {
    unsigned short cursor_address = get_cursor_position();
    unsigned short screen_words = SCREEN_WIDTH * 2;   //each char is ASCII value + styling

    // Calculate remaining words to fill in current line
    unsigned short words_to_end = screen_words - (cursor_address % screen_words);
    int i;
    for (i = 0; i < words_to_end; i += 2) {
        outchr(' ');
    }
}

//function to calculate the start location of an ASCII character's font cell
unsigned short calculate_font_cell_start(char ch) 
{
  //each glyph is 16 consecutive words and each word is 2 bytes = 32 bytes
  unsigned short glyph_size = 32;
  unsigned short char_table_offset = 0xC80;
  unsigned short glyph_offset= ch * glyph_size;
  unsigned short glyph_pointer = glyph_offset + char_table_offset;
  //shift right 5 bits as the CRT controller sets those bits for each row of the glpyh
  glyph_pointer = glyph_pointer >> 5;
  return glyph_pointer;
}


/* outchr - print a single ASCII character */
void outchr (char ch)
{
  unsigned short cursor_address = get_cursor_position();
  unsigned short volatile far *absolute_cursor_address = MK_FP(SCREEN_BUFFER_SEGMENT, cursor_address);

  //glyph_pointer = display_attributes | ASCII + character_table_offset
  unsigned short glyph_pointer = calculate_font_cell_start(ch); //locate the glyph for the char in the font area of RAM
  unsigned short word = ((0x08 << 8) | glyph_pointer);  // convert the char to the format used in the screen buffer (char + attribute byte)
  *absolute_cursor_address = word;
  
  //increment cursor location
  cursor_address +=2;
  set_cursor_position(cursor_address);
  return;
}


/* outstr - print an ASCIZ string */
void outstr (char *p)
{
  while (*p != '\0') 
  {
    // handle newline character
    if (*p == '\n') 
    {
        newline();
        continue;
    }
    outchr (*p++);
  }
}

/* outdec - print a signed decimal integer */
void outdec (int val)
{
  if (val < 0)
    {outchr('-');  val = -val;}
  if (val > 9)
    {outdec( val/10 );  val %= 10;}
  outchr('0' + val);
}

/* outhex - print a n digit hex number with leading zeros */
void outhex (unsigned val, int ndigits)
{
  if (ndigits > 1)
    outhex (val >> 4, ndigits-1);
  val &= 0xf;
  if (val > 9)
    outchr('A'+val-10);
  else
    outchr('0'+val);
}

/* outhex - print a n digit hex number with leading zeros */
void outlhex (uint16_t lval)
{
  int i;
  for (i=3;i>=0;i--)
     outhex(((unsigned char *)&lval)[i],2);
}


/* outcrlf - print a carriage return, line feed pair */
void outcrlf (void)
{
  unsigned short cursor_address = get_cursor_position();
  char screen_words = SCREEN_WIDTH * 2;   //each char is ASCII value + styling

  // Calculate remaining words to fill in current line
  unsigned short words_to_end = screen_words - (cursor_address % screen_words);
  int i;
  for (i = 0; i < words_to_end; i += 2) 
  {
      outchr(' ');
  }
}

void cdprint(const char* str) {
    uint16_t cursor_pos = get_cursor_position();
    const char* ch;
    for (ch = str; *ch; ++ch, cursor_pos += 2) {
        // handle newline character
        if (*ch == '\n') {
            newline();
            continue;
        }
        outchr(*ch);
    }
    return;
}

void cdprintln(const char* str) {
    cdprint(str);
    newline();
    return;
}


/* cprintf */
/*   This routine provides a simple emulation for the printf() function */
/* using the "safe" console output routines.  Only a few escape seq- */
/* uences are allowed: %d, %x and %s.  A width modifier (e.g. %2x) is   */
/* recognized only for %x, and then may only be a single decimal digit. */
void cdprintf (char near *msg, ...)
{
  va_list ap;  char *str;  int size, ival;  unsigned uval; uint16_t luval;
  va_start (ap, msg);

  while (*msg != '\0') {
/*outhex((unsigned) msg, 4);  outchr('=');  outhex(*msg, 2);  outchr(' ');*/
    if (*msg == '%') {
      ++msg;  size = 0;
      if ((*msg >= '0')  &&  (*msg <= '9'))
   {size = *msg - '0';  ++msg;}
      if (*msg == 'c') {
        ival = va_arg(ap, int);  outchr(ival&0xff);  ++msg;
      } else if (*msg == 'd') {
   ival = va_arg(ap, int);  outdec (ival);   ++msg;
      } else if (*msg == 'x') {
   uval = va_arg(ap, unsigned); ++msg;
        outhex (uval,  (size > 0) ? size : 4);
      } else if (*msg == 'L') {
   luval = va_arg(ap, uint16_t); ++msg;
        outlhex (luval);
      } else if (*msg == 's') {
        str = va_arg(ap, char *);  outstr (str);  ++msg;
      }
    } else if (*msg == '\n') {
      newline();  ++msg;
    } else {
      outchr(*msg);  ++msg;
    }
  }

  va_end (ap);
}
