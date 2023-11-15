/*
 * Template for writing DOS device drivers in Open Watcom C
 *
 * Copyright (C) 2022, Eduardo Casino (mail@eduardocasino.es)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 *
 */
#ifndef _TEMPLATE_H_
#define _TEMPLATE_H_

#include <stdint.h>

#include "device.h"
#pragma pack(1)

#if ( _M_IX86 >= 0x200 )
#define push_all "pusha"
#define pop_all "popa"
#else
#define push_all "push ax" "push cx" "push dx" "push bx" "push sp" "push bp" "push si" "push di"
#define pop_all "pop di" "pop si" "pop bp" "pop bx" "pop bx" "pop dx" "pop cx" "pop ax"
#endif

#if ( _M_IX86 >= 0x300 )
#define push_segregs "push ds" "push es" "push fs" "push gs"
#define pop_segregs "pop gs" "pop fs" "pop es" "pop ds"
#else
#define push_segregs "push ds" "push es"
#define pop_segregs "pop es" "pop ds"
#endif

#ifndef ALL_REGS
struct ALL_REGS {
    uint16_t cs, ds, es, ss;  // Segment registers
    uint16_t ax, bx, cx, dx;  // General-purpose registers
};
#endif

#ifdef USE_INTERNAL_STACK

#ifndef STACK_SIZE
#define STACK_SIZE 4096
#endif


extern uint8_t *stack_bottom;
extern uint32_t dos_stack;
extern bool debug, initNeeded;
extern int8_t my_units[9];
extern request __far *fpRequest;
extern struct device_header far *dev_header;


extern void switch_stack( void );
#pragma aux switch_stack = \
    "cli" \
    "mov word ptr [cs:dos_stack], sp" \
    "mov word ptr [cs:dos_stack+2], ss" \
    "push cs" \
    "pop ss" \
    "mov sp, word ptr [cs:stack_bottom]" \
    "sti";

extern void restore_stack( void );
#pragma aux restore_stack = \
    "cli" \
    "mov sp, word ptr [cs:dos_stack]" \
    "mov ss, word ptr [cs:dos_stack+2]" \
    "sti";

#endif /* USE_INTERNAL_STACK */

extern void push_regs( void );
#pragma aux push_regs = \
    "pushf" \
    push_all \
    push_segregs \
    "push cs" "pop ds";

extern void pop_regs( void );
#pragma aux pop_regs = \
    pop_segregs \
    pop_all \
    "popf";

extern __segment getCS( void );
#pragma aux getCS = \
    "mov ax, cs";

extern __segment getCS( void );
#pragma aux getCS = \
    "mov ax, cs";

extern void get_segments(struct SREGS far *sregs);
#pragma aux get_segments = \
    "mov ax, cs" \
    "mov [si], ax" \
    "mov ax, ds" \
    "mov [si+2], ax" \
    "mov ax, es" \
    "mov [si+4], ax" \
    "mov ax, ss" \
    "mov [si+6], ax" \
    parm [si] \
    modify [ax];

extern void get_all_registers(struct ALL_REGS far *all_regs);
#pragma aux get_all_registers = \
    "mov ax, cs" \
    "mov [si], ax" \
    "mov ax, ds" \
    "mov [si+2], ax" \
    "mov ax, es" \
    "mov [si+4], ax" \
    "mov ax, ss" \
    "mov [si+6], ax" \
    "mov ax, ax" \
    "mov [si+8], ax" \
    "mov ax, bx" \
    "mov [si+10], ax" \
    "mov ax, cx" \
    "mov [si+12], ax" \
    "mov ax, dx" \
    "mov [si+14], ax" \
    parm [si] \
    modify [ax];

extern void printMsg( const char * );
#pragma aux printMsg =        \
    "mov    ah, 0x9"          \
    "int    0x21"             \
    __parm [__dx]             \
    __modify [__ax __di __es];

extern void Enable( void );
#pragma aux Enable = \
    "sti" \
    parm [] \
    modify [];


typedef uint16_t (*driverFunction_t)(void);

#pragma pack( pop )

#endif /* _TEMPLATE_H_ */
