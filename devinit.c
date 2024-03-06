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
bool debug = FALSE;
static uint8_t portbase;
static uint8_t partition_number = 0;
//
// Place here any variables or constants that should go away after initialization
//
static char hellomsg[] = "\r\nDOS Device Driver Template in Open Watcom C\r\n$";
extern bpb my_bpb;
extern bpb near *my_bpb_ptr;
extern bpbtbl_t far *my_bpbtbl_ptr;
extern bool initNeeded;

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
uint16_t deviceInit( void )
{
    struct ALL_REGS registers;
    uint16_t offset;
    uint16_t brkadr, reboot[2];  
    void (__interrupt far *reboot_address)();  /* Reboot vector */
    bpb my_bpb;
    bpb far *bpb_ptr;
    char far *bpb_cast_ptr;
    

    get_all_registers(&registers);

    fpRequest->r_endaddr = MK_FP(registers.cs, &transient_data);
    struct device_header far *dev_header = MK_FP(registers.cs, 0);

    cdprintf("\nSD pport device driver v0.1 (C) 2023 by Paul Devine\n");
    cdprintf("     based on (C) 2014 by Dan Marks and on TU58 by Robert Armstrong\n");
    cdprintf("     with help from an openwatcom driver by Eduardo Casino\n");


    //address to find passed by DOS in a combo of ES / BX get_all_registers
    if (debug) {
        cdprintf("about to parse bpb, ES: %x BX: %x\n", registers.es, registers.bx);
        cdprintf("SD: command: %d r_unit: %d\n", fpRequest->r_command, fpRequest->r_unit);
        cdprintf("SD: dh_num_drives: %d\n", dev_header->dh_num_drives);
    }
    char name_buffer[8];  // 7 bytes for the name + 1 byte for the null terminator
    memcpy(name_buffer, (void const *) dev_header->dh_name, 7);
    name_buffer[7] = '\0';

    if (debug) {
        cdprintf("SD: dh_name: %s\n", name_buffer);
        cdprintf("SD: dh_next: %x\n", dev_header->dh_next);
    }

    //setting unit count to 1 to make DOS happy
    dev_header->dh_num_drives = 1;
    fpRequest->r_nunits = 1;
    if (debug) cdprintf("SD: dh_num_drives: %x r_unit: %x\n", dev_header->dh_num_drives, fpRequest->r_unit);

    //DOS is overloading a data structure that in normal use stores the BPB, 
    //for init() it stores the string that sits in config.sys
    //hence I'm casting to a char
    bpb_cast_ptr = (char __far *)(fpRequest->r_bpbptr);  

    if (debug) cdprintf("gathered bpb_ptr: %x\n", bpb_cast_ptr);
    /* Parse the options from the CONFIG.SYS file, if any... */
    if (!parse_options((char far *) bpb_cast_ptr)) {
        if (debug) cdprintf("SD: bad options in CONFIG.SYS\n");
        //fpRequest->r_endaddr = MK_FP( getCS(), 0 );
        return (S_DONE | S_ERROR | E_UNKNOWN_MEDIA ); 
    }
    if (debug) cdprintf("done parsing bpb_ptr: %x\n", bpb_ptr);

    /* Try to make contact with the drive... */
    if (debug) cdprintf("SD: initializing drive r_unit: %d, partition_number: %d, my_bpb_ptr: %X\n", 
        fpRequest->r_unit, partition_number, my_bpb_ptr);
    if (!sd_initialize(fpRequest->r_unit, partition_number, my_bpb_ptr))    {
        cdprintf("SD: drive not connected or not powered\n");
        printMsg(hellomsg);
        //fpRequest->r_endaddr = MK_FP( getCS(), 0 );
        return (S_DONE | S_ERROR | E_NOT_READY ); 
    }
    //setting unit count to 1 to make DOS happy
    dev_header->dh_num_drives = 1;
    fpRequest->r_nunits = 1;         //tell DOS how many drives we're instantiating.
    *(my_bpbtbl_ptr)[fpRequest->r_unit] = my_bpb_ptr;
    fpRequest->r_bpbptr = my_bpbtbl_ptr;
    bpb_ptr = my_bpb_ptr;

    if (debug) {
        cdprintf("SD: done parsing bpb_ptr: = %4x:%4x\n", FP_SEG(bpb_ptr), FP_OFF(bpb_ptr));
        cdprintf("SD: done parsing my_bpb_ptr = %4x:%4x\n", FP_SEG(my_bpb_ptr), FP_OFF(my_bpb_ptr));
        cdprintf("SD: done parsing my_bpbtbl_ptr = %4x:%4x\n", FP_SEG(my_bpbtbl_ptr), FP_OFF(my_bpbtbl_ptr));
        cdprintf("SD: done parsing registers.cs = %4x:%4x\n", FP_SEG(registers.cs), FP_OFF(0));
        cdprintf("SD: done parsing getCS() = %4x:%4x\n", FP_SEG(getCS()), FP_OFF(&transient_data));
        cdprintf("SD: dh_num_drives: %x r_unit: %x\n", dev_header->dh_num_drives, fpRequest->r_unit);
    }

    uint32_t bpb_start = calculateLinearAddress(FP_SEG(my_bpb_ptr) , FP_OFF(my_bpb_ptr));

    if (debug) {
        cdprintf("SD: my_bpb_ptr = %4x:%4x  %5X\n", FP_SEG(my_bpb_ptr), FP_OFF(my_bpb_ptr), bpb_start);
        writeToDriveLog("SD: my_bpb_ptr = %4x:%4x  %5X\n", FP_SEG(my_bpb_ptr), FP_OFF(my_bpb_ptr), bpb_start);
        cdprintf("SD: initialized on DOS drive %c r_firstunit: %d r_nunits: %d\n",(
            fpRequest->r_firstunit + 'A'), fpRequest->r_firstunit, fpRequest->r_nunits);
    
    }

    // /* All is well.  Tell DOS how many units and the BPBs... */
    uint8_t i;
    for (i=0; i < fpRequest->r_nunits; i++) {
        if (debug) {cdprintf("SD:  my_units[%d]: %d drive %c\n",i, i, (fpRequest->r_firstunit + 'A'));}
        my_units[i] = i;
    }
    initNeeded = false;


    if (debug)
    {   
      cdprintf("SD: BPB data:\n");
      cdprintf("Bytes per Sector: %d\n", my_bpb_ptr->bpb_nbyte);
      cdprintf("Sectors per Allocation Unit: %d\n", my_bpb_ptr->bpb_nsector);
      cdprintf("# Reserved Sectors: %d\n", my_bpb_ptr->bpb_nreserved);
      cdprintf("# FATs: %d\n", my_bpb_ptr->bpb_nfat);
      cdprintf("# Root Directory entries: %d  ", my_bpb_ptr->bpb_ndirent);
      cdprintf("Size in sectors: %d\n", my_bpb_ptr->bpb_nsize);
      cdprintf("MEDIA Descriptor Byte: %x  ", my_bpb_ptr->bpb_mdesc);
      cdprintf("FAT size in sectors: %d\n", my_bpb_ptr->bpb_nfsect);
      // cdprintf("Sectors per track : %d  ", my_bpb_ptr->bpb_nsecs);
      // cdprintf("Number of heads: %d\n", my_bpb_ptr->bpb_nheads);
      // cdprintf("Hidden sectors: %d  ", my_bpb_ptr->bpb_hidden);
      // cdprintf("Hidden sectors 32 hex: %L\n", my_bpb_ptr->bpb_hidden);
      // cdprintf("Size in sectors if : %L\n", my_bpb_ptr->bpb_huge);
      cdprintf("SD: fpRequest->r_endaddr = %4x:%4x\n", FP_SEG(fpRequest->r_endaddr), FP_OFF(&transient_data));
      cdprintf("SD: fpRequest->r_endaddr = %4x:%4x\n", FP_SEG(fpRequest->r_endaddr), FP_OFF(fpRequest->r_endaddr));

    }
   
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
            cdprintf("SD: Invalid partition number %d\n",temp);
        else
            partition_number = temp;
            cdprintf("SD: partition number: %d\n", partition_number);
        break; 
    case 'b': 
    case 'B':
        if ((p=option_value(p,&temp)) == FALSE)  return FALSE;
        if ((temp < 1) || (temp > 5))
            if (debug) cdprintf("SD: Invalid port base index %x\n",temp);
        else
            portbase = temp;
            if (portbase = 1)
            {
                if (debug) cdprintf("SD: using parallel port %x\n",portbase);
            } else {
                if (debug) cdprintf("SD: using serial port %x\n",portbase);
            }
        break; 
    default:
        return FALSE;
    }
    p = spanwhite(p);
}
return TRUE;
}


