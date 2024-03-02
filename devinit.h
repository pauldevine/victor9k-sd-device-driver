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
#ifndef _DEVINIT_H_
#define _DEVINIT_H_
#include <stdbool.h>

extern void *transient_data;
extern bool debug;
extern int8_t my_units[9];
extern uint16_t deviceInit( void );
extern struct device_header far *dev_header;

//used for RAMdrive version
extern int alloc_memory(uint16_t paragraphs, uint16_t *segment);
#pragma aux alloc_memory =    \
    "mov dx, 1"               \
    "mov ah, 0x48"            \
    "int 0x21"                \
    "jc allocation_failed"    \
    "jmp allocation_ok"       \
    "allocation_failed:"      \
    "xor dx, dx"              \
    "allocation_ok:"          \
    "mov [si], ax"            \
    parm [bx] [si]            \
    value [dx]                \
    modify [ax dx];

#endif /* _DEVINIT_H_ */
