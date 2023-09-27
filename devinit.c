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

#pragma data_seg("_CODE")
bool Debug = TRUE;
uint8_t portbase;
uint8_t partition_number;
//
// Place here any variables or constants that should go away after initialization
//
static char hellomsg[] = "\r\nDOS Device Driver Template in Open Watcom C\r\n$";
static char test_message[] = "Test message\n";

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
uint16_t DeviceInit( void )
{
    struct ALL_REGS registers;
    uint16_t offset;
    uint16_t brkadr, reboot[2];  
    void (__interrupt far *reboot_address)();  /* Reboot vector */
    bpb far *bpb_pointer;

    get_all_registers(&registers);

    fpRequest->r_endaddr = MK_FP(registers.cs, &transient_data);

    cdprintf("\nSD pport device driver V0.1 (C) 2023 by Paul Devine\n");
    cdprintf("     based on (C) 2014 by Dan Marks\n");
    cdprintf("     based on TU58 by Robert Armstrong\n");

    //address to find passed by DOS in a combo of ES / BX registers
    
    cdprintf("about to parse bpb, ES: %x BX: %x\n", registers.es, registers.bx);
    bpb_pointer = *(fpRequest->r_bpbptr);
    cdprintf("gathered bpb_pointer: %x\n", bpb_pointer);
    /* Parse the options from the CONFIG.SYS file, if any... */
    if (!parse_options((char far *) bpb_pointer)) {
        cdprintf("SD: bad options in CONFIG.SYS\n");
        goto unload2;   
    }
    cdprintf("done parsing bpb_pointer: %x\n", bpb_pointer);

    /* Try to make contact with the drive... */
    if (Debug) cdprintf("SD: initializing drive\n");
    if (!sd_initialize(fpRequest->r_unit, partition_number, bpb_pointer)) {
        cdprintf("SD: drive not connected or not powered\n");
        goto unload1;
    }
    cdprintf("SD: rh = %4x:%4x\n", FP_SEG(bpb_pointer), FP_OFF(bpb_pointer));

    // /* All is well.  Tell DOS how many units and the BPBs... */
    // cdprintf("SD initialized on DOS drive %c\n",
    //     rh->drive+'A');
    // rh->nunits = 1;  
    // rh->bpbtbl = &bpbtbl;
    // rh->rh.status = DONE;

  //   if (Debug)
  //   {   
  //     cdprintf("SD: BPB data:\n");
  //     cdprintf("Sector Size: %d   ", bpb.sector_size);
  //     cdprintf("Allocation unit: %d\n", bpb.allocation_unit);
  //     cdprintf("Reserved sectors: %d  ", bpb.reserved_sectors);
  //     cdprintf("Fat Count: %d\n", bpb.fat_count);
  //     cdprintf("Directory size: %d  ", bpb.directory_size);
  //     cdprintf("Total sectors: %d\n", bpb.total_sectors);
  //     cdprintf("Media descriptor: %x  ", bpb.media_descriptor);
  //     cdprintf("Fat sectors: %d\n", bpb.fat_sectors);
  //     cdprintf("Track size: %d  ", bpb.track_size);
  //     cdprintf("Head count: %d\n", bpb.head_count);
  //     cdprintf("Hidden sectors: %d  ", bpb.hidden_sectors);
  //     cdprintf("Sector Ct 32 hex: %L\n", bpb.sector_count);
  //     cdprintf("Partition offset: %L\n", partition_offset);
  // }
  return 0;

  /*   We get here if there are any errors in initialization.  In that  */
  /* case we can unload this driver completely from memory by setting   */
  /* (1) the break address to the starting address, (2) the number of   */
  /* units to 0, and (3) the error flag.           */
  unload1:
  { };
  unload2:
    fpRequest->r_endaddr = MK_FP( getCS(), 0 );
    return 0;
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
        Debug = TRUE;
        cdprintf("Parsing Debug as true\n");
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
            cdprintf("SD: port base index %x\n",portbase);
        break; 
    default:
        return FALSE;
    }
    p = spanwhite(p);
}
return TRUE;
}
