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

#include <dos.h>
#include <stdint.h>

#include "devinit.h"
#include "template.h"
#include "cprint.h"

#pragma data_seg("_CODE")

//
// Place here any variables or constants that should go away after initialization
//
static char hellomsg[] = "\r\nDOS Device Driver Template in Open Watcom C\r\n$";
static char test_message[] = "Test message\n";


uint16_t DeviceInit( void )
{
    printMsg(hellomsg);

    fpRequest->r_endaddr = MK_FP(getCS(), &transient_data);

    cdprintf(test_message);
    return 0;
}
