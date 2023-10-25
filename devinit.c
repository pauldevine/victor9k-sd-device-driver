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

/* driver.c - MSDOS device driver functions           */
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

/* Paul Devine   Sept-2023
 * Combined code from: https://github.com/eduardocasino/dos-device-driver
 * that had a template to build an MS-DOS device driver with openwatcom
 * with code from: https://forum.vcfed.org/index.php?threads/sd-card-to-parallel-port-driver-for-msdos-ver-1-1.42008/
 * which had the code to access the SD card for an IBM-PC over the parallel port
 * I'm adopting that to work with the Victor 9000 / ACT Sirius 1 hardware
 */

#include <dos.h>
#include <stdint.h>
#include <i86.h> 
#include <string.h> 

#include "devinit.h"
#include "template.h"
#include "cprint.h"     /* Console printing direct to hardware */
#include "sd.h"         /* SD card glue */
#include "diskio.h"     /* SD card library header */

#pragma data_seg("_CODE")
bool debug = TRUE;
static uint8_t portbase;
static uint8_t partition_number;
//
// Place here any variables or constants that should go away after initialization
//
static char hellomsg[] = "\r\nDOS Device Driver Template in Open Watcom C\r\n$";

/*   WARNING!!  WARNING!!  WARNING!!  WARNING!!  WARNING!!  WARNING!!   */
/*                         */
/*   All code following this point in the file is discarded after the   */
/* driver initialization.  Make absolutely sure that no routine above   */
/* this line calls any routine below it!!          */
/*                         */
/*   WARNING!!  WARNING!!  WARNING!!  WARNING!!  WARNING!!  WARNING!!   */

/* Driver Initialization */
/*   DOS calls this function immediately after the driver is loaded and */
/* expects it to perform whatever initialization is required.  Since */
/* this function can never be called again, it's customary to discard   */
/* the memory allocated to this routine and any others that are used */
/* only at initialization.  This allows us to economize a little on the */
/* amount of memory used.                 */
/*                         */
/*   This routine's basic function is to initialize the serial port, go */
/* and make contact with the SD card, and then return a table of BPBs to   */
/* DOS.  If we can't communicate with the drive, then the entire driver */
/* is unloaded from memory.                  */
uint16_t device_init( void )
{
    struct ALL_REGS registers;
    uint16_t offset;
    uint16_t brkadr, reboot[2];  
    void (__interrupt far *reboot_address)();  /* Reboot vector */
    bpb far *bpb_ptr;
    char far *bpb_cast_ptr;
    uint8_t i;

    get_all_registers(&registers);

    fpRequest->r_endaddr = MK_FP(registers.cs, &transient_data);
    struct device_header far *dev_header = MK_FP(registers.cs, 0);

    cdprintf("\nSD pport device driver v0.1 (C) 2023 by Paul Devine\n");
    cdprintf("     based on (C) 2014 by Dan Marks and on TU58 by Robert Armstrong\n");
    cdprintf("     with help from an openwatcom driver by Eduardo Casino\n");


    //address to find passed by DOS in a combo of ES / BX registers
    
    cdprintf("about to parse bpb, ES: %x BX: %x\n", registers.es, registers.bx);
    cdprintf("SD: command: %d r_unit: %d\n", fpRequest->r_command, fpRequest->r_unit);
    cdprintf("SD: dh_num_drives: %d\n", dev_header->dh_num_drives);
    char name_buffer[8];  // 7 bytes for the name + 1 byte for the null terminator
    memcpy(name_buffer, (void const *) dev_header->dh_name, 7);
    name_buffer[7] = '\0';
    cdprintf("SD: dh_name: %s\n", name_buffer);
    cdprintf("SD: dh_next: %x\n", dev_header->dh_next);

    //DOS is overloading a data structure that in normal use stores the BPB, 
    //for init() it stores the string that sits in config.sys
    //hence I'm casting to a char
    bpb_cast_ptr = (char __far *)(fpRequest->r_bpbptr);  
    bpb_ptr = *(fpRequest->r_bpbptr);   //also need the BPB to return results to DOS

    cdprintf("gathered bpb_ptr: %x\n", bpb_ptr);
    /* Parse the options from the CONFIG.SYS file, if any... */
    if (!parse_options((char far *) bpb_cast_ptr)) {
        cdprintf("SD: bad options in CONFIG.SYS\n");
        //fpRequest->r_endaddr = MK_FP( getCS(), 0 );
        return (S_DONE | S_ERROR | E_UNKNOWN_MEDIA ); 
    }
    cdprintf("done parsing bpb_ptr: %x\n", bpb_ptr);

    /* Try to make contact with the drive... */
    if (debug) cdprintf("SD: initializing drive\n");
    if (!sd_initialize(fpRequest->r_unit, partition_number, bpb_ptr))    {
        cdprintf("SD: drive not connected or not powered\n");
        printMsg(hellomsg);
        //fpRequest->r_endaddr = MK_FP( getCS(), 0 );
        return (S_DONE | S_ERROR | E_NOT_READY ); 
    }
    fpRequest->r_nunits = 1;         //tell DOS how many drives we're instantiating.
    cdprintf("SD: dh_num_drives: %x r_unit: %x\n", dev_header->dh_num_drives, fpRequest->r_unit);

    cdprintf("SD: bpb_ptr = %4x:%4x\n", FP_SEG(bpb_ptr), FP_OFF(bpb_ptr));

    // /* All is well.  Tell DOS how many units and the BPBs... */
    cdprintf("SD initialized on DOS drive %c\n",(fpRequest->r_firstunit + 'A'));
    for (i=0; i < fpRequest->r_nunits; i++) {
        my_units[i] = fpRequest->r_firstunit + i;
    }

    if (debug)
    {   
      cdprintf("SD: BPB data:\n");
      cdprintf("Bytes per Sector: %d\n", bpb_ptr->bpb_nbyte);
      cdprintf("Sectors per Allocation Unit: %d\n", bpb_ptr->bpb_nsector);
      cdprintf("# Reserved Sectors: %d\n", bpb_ptr->bpb_nreserved);
      cdprintf("# FATs: %d\n", bpb_ptr->bpb_nfat);
      cdprintf("# Root Directory entries: %d  ", bpb_ptr->bpb_ndirent);
      cdprintf("Size in sectors: %d\n", bpb_ptr->bpb_nsize);
      cdprintf("MEDIA Descriptor Byte: %x  ", bpb_ptr->bpb_mdesc);
      cdprintf("FAT size in sectors: %d\n", bpb_ptr->bpb_nfsect);
      cdprintf("Sectors per track : %d  ", bpb_ptr->bpb_nsecs);
      cdprintf("Number of heads: %d\n", bpb_ptr->bpb_nheads);
      cdprintf("Hidden sectors: %d  ", bpb_ptr->bpb_hidden);
      cdprintf("Hidden sectors 32 hex: %L\n", bpb_ptr->bpb_hidden);
      cdprintf("Size in sectors if : %L\n", bpb_ptr->bpb_huge);
    }
    Enable(); 

    fpRequest->r_endaddr = MK_FP(getCS(), &transient_data);

  return S_DONE;    
}

/* iseol - return TRUE if ch is any end of line character */
bool iseol (char ch)
{  return ch=='\0' || ch=='\r' || ch=='\n';  }

/* spanwhite - skip any white space characters in the string */
char far *spanwhite (char far *p)
{  while (*p==' ' || *p=='\t') ++p;  return p;  }

/* option_value */
/*   This routine will parse the "=nnn" part of an option.  It should   */
/* be called with a text pointer to what we expect to be the '=' char-  */
/* acter.  If all is well, it will return the binary value of the arg-  */
/* ument and a pointer to the first non-numeric character.  If there is */
/* a syntax error, then it will return NULL.          */
char far *option_value (char far *p, uint16_t far *v)
{
  bool null = TRUE;
  if (*p++ != '=')  return FALSE;
  for (*v=0;  *p>='0' && *p<='9';  ++p)
    *v = (*v * 10) + (*p - '0'),  null = FALSE;
  return null ? FALSE : p;
}

/* parse_options */
/*   This routine will parse our line from CONFIG.SYS and extract the   */
/* driver options from it.  The routine returns TRUE if it parsed the   */
/* line successfully, and FALSE if there are any problems.  The pointer */
/* to CONFIG.SYS that DOS gives us actually points at the first char-   */
/* acter after "DEVICE=", so we have to first skip over our own file */
/* name by searching for a blank.  All the option values are stored in  */
/* global variables (e.g. DrivePort, DriveBaud, etc).          */
bool parse_options (char far *p)
{
  uint16_t temp;
  while (*p!=' ' && *p!='\t' && !iseol(*p))  ++p;
  p = spanwhite(p);
  while (!iseol(*p)) {
    p = spanwhite(p);
    if (*p++ != '/')  return FALSE;
    switch (*p++) {
    case 'd':
    case 'D':
        debug = TRUE;
        cdprintf("Parsing debug as true\n");
        break;
    case 'k':
    case 'K':
        //sd_card_check = 1;
        break;
    case 'p':
    case 'P':
        if ((p=option_value(p,&temp)) == FALSE)  return FALSE;
        if ((temp < 1) || (temp > 4))
            cdprintf("SD: Invalid partition number %x\n",temp);
        else
            partition_number = temp;
            cdprintf("SD: partition_number: %x\n", temp);
        break; 
    case 'b': 
    case 'B':
        if ((p=option_value(p,&temp)) == FALSE)  return FALSE;
        if ((temp < 1) || (temp > 5))
            cdprintf("SD: Invalid port base index %x\n",temp);
        else
            portbase = temp;
            if (portbase = 1)
            {
                cdprintf("SD: using parallel port %x\n",portbase);
            } else {
                cdprintf("SD: using serial port %x\n",portbase);
            }
        break; 
    default:
        return FALSE;
    }
    p = spanwhite(p);
}
return TRUE;
}


