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
#include <stdbool.h>
#include <dos.h>        /* used only for MK_FP !      */
#include <stdarg.h>     /* needed for variable argument lists  */
#include <string.h>

#include "cprint.h"     /* Console printing */

char* intToAscii(int32_t value, char *buffer, size_t bufferSize) {
    if (bufferSize == 0) return NULL;   // Safety check
    char* p = buffer + bufferSize - 1;  // Start at the end of the buffer
    *p = '\0';  // Null-terminate the string

    // Handle zero explicitly, since the loop below won't handle it
    if (value == 0) {
        *--p = '0';
    }

    // Handle negative numbers
    bool isNegative = false;
    if (value < 0) {
        isNegative = true;
        value = -value;  // Make the value positive for processing
    }

    // Convert the number to ASCII
    while (value != 0) {
        *--p = '0' + (value % 10);
        value /= 10;
    }

    // Add minus sign for negative numbers
    if (isNegative) {
        *--p = '-';
    }

    // Return a pointer to the start of the ASCII representation
    return p;
}

void writeToDriveLog(MiniDrive *drive, const char* format, ...) {
    static size_t currentPos = 5 * SECTOR_SIZE;  // Start after the 5th sector
    char buffer[SECTOR_SIZE];  // Temporary buffer for formatted string
    char *bufferPtr = buffer;
    uint16_t remainingSize = SECTOR_SIZE;  // Remaining space in the buffer

    //insert some spacers just to help readability of log
    uint8_t delimeter = 3;
    for (uint8_t i =0; i<delimeter; i++) {
        *bufferPtr++ = ' ';
        remainingSize--;    
    }
    *bufferPtr++ = '|';
    
    va_list args;
    va_start(args, format);

    const char *p = format;
    while (*p && remainingSize > 0) {
        if (*p == '%') {
            switch (*++p) {
                case 'd': {
                    int16_t num = va_arg(args, int16_t);
                    char numStr[12];  // Enough for 32-bit int, sign, and null terminator
                    char *str = intToAscii(num, (char *)numStr, sizeof(numStr));
                    while (*str && remainingSize > 0) {
                        *bufferPtr++ = *str++;
                        remainingSize--;
                    }
                    break;
                }
                case 's': {
                    char *str = va_arg(args, char*);
                    while (*str && remainingSize > 0) {
                        *bufferPtr++ = *str++;
                        remainingSize--;
                    }
                    break;
                }
                case 'x': { // handle 16-bit hex number
                    uint16_t val = (uint16_t)va_arg(args, int); // Promoted to int, then cast to 16-bit
                    for (int i = (sizeof(uint16_t) * 2) - 1; i >= 0; i--) {
                        uint8_t digit = (val >> (4 * i)) & 0xF;
                        if (digit > 9) *bufferPtr++ = 'A' + digit - 10;
                        else *bufferPtr++ = '0' + digit;
                    }
                    break;
                }
                case 'X': { // handle 32-bit hex number
                    uint32_t val = va_arg(args, uint32_t);
                    for (int i = (sizeof(uint32_t) * 2) - 1; i >= 0; i--) {
                        uint8_t digit = (val >> (4 * i)) & 0xF;
                        if (digit > 9) *bufferPtr++ = 'A' + digit - 10;
                        else *bufferPtr++ = '0' + digit;
                    }
                    break;
                }

                case 'p': {
                    // Retrieve the pointer once and extract segment and offset
                    void far *ptr = va_arg(args, void*);
                    uint16_t seg = FP_SEG(ptr);
                    uint16_t off = FP_OFF(ptr);

                    // Calculate linear address (segment*16 + offset)
                    uint32_t linearAddr = ((uint32_t)seg << 4) + off;

                    // Convert segment to hexadecimal
                    for (int32_t i = (sizeof(uint16_t) * 2) - 1; i >= 0; i--) {
                        uint16_t digit = (seg >> (4 * i)) & 0xF;
                        *bufferPtr++ = digit > 9 ? 'A' + digit - 10 : '0' + digit;
                    }
                    *bufferPtr++ = ':';

                    // Convert offset to hexadecimal
                    for (int32_t i = (sizeof(uint16_t) * 2) - 1; i >= 0; i--) {
                        uint16_t digit = (off >> (4 * i)) & 0xF;
                        *bufferPtr++ = digit > 9 ? 'A' + digit - 10 : '0' + digit;
                    }

                    // Add space for readability
                    *bufferPtr++ = ' ';
                    *bufferPtr++ = '=';

                    // Convert linear address to hexadecimal
                    for (int32_t i = (sizeof(uint32_t) * 2) - 1; i >= 0; i--) {
                        uint32_t digit = (linearAddr >> (4 * i)) & 0xF;
                        *bufferPtr++ = digit > 9 ? 'A' + digit - 10 : '0' + digit;
                    }
                    break;
                }
                case 'L': {  // Custom specifier for int32_t
                    int32_t longNum = va_arg(args, int32_t);
                    char longNumStr[12];  // Enough for 32-bit int, sign, and null terminator
                    char *str = intToAscii(longNum, longNumStr, sizeof(longNumStr));
                    while (*str && remainingSize > 0) {
                        *bufferPtr++ = *str++;
                        remainingSize--;
                    }
                    break;
                }
                // Add other format specifiers as needed
                default:
                    if (remainingSize > 1) {
                        *bufferPtr++ = '%';
                        *bufferPtr++ = *p;
                        remainingSize -= 2;
                    }
                    break;
            }
        } else {
            *bufferPtr++ = *p;
            remainingSize--;
        }
        p++;
    }

    *bufferPtr = '\0'; // Null-terminate the buffer
    va_end(args);

    size_t messageLen = bufferPtr - buffer;

    // Calculate the total size of the drive in bytes
    size_t totalDriveSize = NUM_SECTORS * SECTOR_SIZE;

    // Check if there's enough space left in the drive
    if (currentPos + messageLen >= totalDriveSize) {
        // Handle the case where the drive is full
        return;
    }

    // Cast the drive to a char pointer
    char* drivePtr = (char*)drive->sectors;

    // Write the formatted message to the current position
    memcpy(drivePtr + currentPos, buffer, messageLen);

    cdprintf(buffer);

    // Update the current position
    currentPos += messageLen;
}


// A very simple version of a function similar to sprintf
// Writes a formatted string to a sector's data buffer.
// This example only handles %s (string) and %d (decimal integer) for simplicity.
void writeLog(Sector *sector, const char *format, ...) {
    va_list args;
    va_start(args, format);

    char *buffer = sector->data;
    const char *p = format;

    while (*p) {
        if (*p == '%') {
            switch (*++p) {
                case 'd': { // Handle decimal integer
                    int16_t num = va_arg(args, int16_t);
                    char *numStr[12];  // Enough for 32-bit int, sign, and null terminator
                    char *n = intToAscii(num, (char *)numStr, sizeof(numStr));
                    while (*n) {
                        *buffer++ = *n++; // Copy number to buffer
                    }
                    break;
                }
                case 'x': { // handle hex number
                    int16_t val = va_arg(args, int);
                    for (int16_t i = (sizeof(int16_t) * 2) - 1; i >= 0; i--) {
                        int16_t digit = (val >> (4 * i)) & 0xF;
                        if (digit > 9) *buffer++ = 'A' + digit - 10;
                        else *buffer++ = '0' + digit;
                    }
                    break;
                }
                case 's': { // Handle string
                    char *str = va_arg(args, char*);
                    while (*str) {
                        *buffer++ = *str++; // Copy string to buffer
                    }
                    break;
                }
                case 'X': { // handle 32-bit hex number
                    uint32_t val = va_arg(args, uint32_t);
                    for (int i = (sizeof(uint32_t) * 2) - 1; i >= 0; i--) {
                        uint8_t digit = (val >> (4 * i)) & 0xF;
                        if (digit > 9) *buffer++ = 'A' + digit - 10;
                        else *buffer++ = '0' + digit;
                    }
                    break;
                }
                case 'p': {
                    // Retrieve the pointer once and extract segment and offset
                    void far *ptr = va_arg(args, void*);
                    uint16_t seg = FP_SEG(ptr);
                    uint16_t off = FP_OFF(ptr);

                    // Calculate linear address (segment*16 + offset)
                    uint32_t linearAddr = ((uint32_t)seg << 4) + off;

                    // Convert segment to hexadecimal
                    for (int32_t i = (sizeof(uint16_t) * 2) - 1; i >= 0; i--) {
                        uint16_t digit = (seg >> (4 * i)) & 0xF;
                        *buffer++ = digit > 9 ? 'A' + digit - 10 : '0' + digit;
                    }
                    *buffer++ = ':';

                    // Convert offset to hexadecimal
                    for (int32_t i = (sizeof(uint16_t) * 2) - 1; i >= 0; i--) {
                        uint16_t digit = (off >> (4 * i)) & 0xF;
                        *buffer++ = digit > 9 ? 'A' + digit - 10 : '0' + digit;
                    }

                    // Add space for readability
                    *buffer++ = ' ';
                    *buffer++ = '=';

                    // Convert linear address to hexadecimal
                    for (int32_t i = (sizeof(uint32_t) * 2) - 1; i >= 0; i--) {
                        uint32_t digit = (linearAddr >> (4 * i)) & 0xF;
                        *buffer++ = digit > 9 ? 'A' + digit - 10 : '0' + digit;
                    }
                    break;
                }
                case 'L': {  // Custom specifier for int32_t
                    int32_t longNum = va_arg(args, int32_t);
                    char longNumStr[12];  // Enough for 32-bit int, sign, and null terminator
                    char *str = intToAscii(longNum, longNumStr, sizeof(longNumStr));
                    while (*str) {
                        *buffer++ = *str++;
                    }
                    break;
                }
                default: // Unsupported format
                    *buffer++ = '%'; // Copy the unsupported format specifier
                    *buffer++ = *p;
                    break;
            }
        } else {
            *buffer++ = *p; // Copy regular character
        }
        p++;
    }

    *buffer = '\0'; // Null-terminate the buffer
    va_end(args);
}

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
    uint16_t cursor_address = get_cursor_position();
    uint8_t screen_words = SCREEN_WIDTH * 2;   //each char is ASCII value + styling

    // Calculate remaining words to fill in current line
    uint16_t words_to_end = screen_words - (cursor_address % screen_words);
    for (int i = 0; i < words_to_end; i += 2) {
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
  uint16_t cursor_address = get_cursor_position();
  if (cursor_address >= 0xF00) {
      set_cursor_position(0x0);
      cursor_address = get_cursor_position();
  }
  uint16_t volatile far *absolute_cursor_address = MK_FP(SCREEN_BUFFER_SEGMENT, cursor_address);

  //glyph_pointer = display_attributes | ASCII + character_table_offset
  uint16_t glyph_pointer = calculate_font_cell_start(ch); //locate the glyph for the char in the font area of RAM
  uint16_t word = ((0x40 << 8) | glyph_pointer);  // convert the char to the format used in the screen buffer (attribute byte + char)
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
  newline();
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
void cdprintf (char *msg, ...)
{
  va_list ap;  char *str;  int size, ival;  unsigned uval; uint16_t luval;
  va_start (ap, msg);

  while (*msg != '\0') {
    //outhex((unsigned) msg, 4);  put_char_direct('=');  outhex(*msg, 2);  put_char_direct(' ');
    if (*msg == '%') {
      ++msg;  
      size = 0;
      if ((*msg >= '0')  &&  (*msg <= '9')) 
      {
        size = *msg - '0';  
        ++msg;
      }
      if (*msg == 'c') 
      {
        ival = va_arg(ap, int);  
        outchr(ival&0xff);  
        ++msg;
      } else if (*msg == 'd') 
      {
        ival = va_arg(ap, int);  
        outdec (ival);
        ++msg;
      } else if (*msg == 'x') 
      {
        uval = va_arg(ap, unsigned); 
        ++msg;
        outhex (uval,  (size > 0) ? size : 4);
      } else if (*msg == 'L') 
      {
        luval = va_arg(ap, uint16_t); 
        ++msg;
        outlhex (luval);
      } else if (*msg == 's') {
        str = va_arg(ap, char *);  
        outstr (str);  
        ++msg;
      }
    } else if (*msg == '\n') {
      newline();  
      ++msg;
    } else {
      outchr(*msg);  
      ++msg;
    }
  }

  va_end (ap);
}
