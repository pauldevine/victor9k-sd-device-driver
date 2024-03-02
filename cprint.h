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

/* Paul Devine 2023
 * modified to work with the Victor 9000, writing directly to the graphics
 * hardware so I could print debugging info without invoking DOS 21h
 * Also adopted stdint.h naming conventions to match rest of codebase.
 */

#ifndef _CPRINT_H

#include "device.h"

#define _CPRINT_H
#define PHASE2_DEVICE_SEGMENT 0xE800
#define CONTRAST_BRIGHTNESS_REG_OFFSET 0x0040
#define CRTC_ADDR_REG_OFFSET 0x0  // CRT-chip address register target
#define CRTC_DATA_REG_OFFSET 0x1  // CRT-chip address register value
#define SCREEN_BUFFER_SEGMENT 0xF000
#define SCREEN_BUFFER_BOTTOM 0x0
#define SCREEN_BUFFER_TOP 0xFFF
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 24
#define SCREEN_BUFFER_SIZE 2000  // Total characters in the screen buffer
#define SCREEN_MASK 0x999   //Screen wrap around mask
#define FONT_GLYFF_SEGMENT 0xC00
#define FONT_GLYFF_OFFSET 0x0000
void set_crtc_reg(char reg, char value);
char get_crtc_reg(char reg);
void set_screen_start(uint16_t start_addr);
void set_cursor_position(uint16_t position);
unsigned short get_cursor_position(void);
void newline(void);
unsigned short calculate_font_cell_start(char ch);
void outchr (char ch);
void outstr (char *p);
void outdec (int val);
void outhex (unsigned val, int ndigits);
void outcrlf (void);
char* intToAscii(int32_t value, char *buffer, size_t bufferSize);
uint32_t calculateLinearAddress(uint16_t segment, uint16_t offset);
void writeToDriveLog(const char* format, ...);
void strreverse(char* begin, char* end);
void cdprintf (char *msg, ...);
#endif
