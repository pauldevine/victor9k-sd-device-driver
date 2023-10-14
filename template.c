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

#include <stdint.h>
#include <stddef.h>

#include "device.h"
#include "devinit.h"
#include "template.h"
#include "cprint.h"     /* Console printing direct to hardware */
#include "sd.h"

#ifdef USE_INTERNAL_STACK

static uint8_t our_stack[STACK_SIZE];
uint8_t *stack_bottom = our_stack + STACK_SIZE;
uint32_t dos_stack;

#endif // USE_INTERNAL_STACK

request __far *fpRequest = (request __far *)0;

static uint16_t open( void )
{
    return S_DONE;
}

static uint16_t close( void )
{
    return S_DONE;
} 

/* media_check */
/*    DOS calls this function to determine if the tape in the drive has   */
/* been changed.  The SD hardware can't determine this (like many      */
/* older 360K floppy drives), and so we always return the "Don't Know" */
/* response.  This works well enough...                                */
static uint16_t media_check (void)
{
  if (debug) cdprintf("SD: media_check: unit=%d\n", fpRequest->r_mcvid);
  fpRequest->r_mcretcode = sd_media_check(fpRequest->r_unit) ? M_CHANGED : M_NOT_CHANGED;
  return M_NOT_CHANGED;
}

/* build_bpb */
/*   DOS uses this function to build the BIOS parameter block for the   */
/* specified drive.  For diskettes, which support different densities   */
/* and formats, the driver actually has to read the BPB from the boot   */
/* sector on the disk.  */
static uint16_t build_bpb (void)
{
  if (debug)
      cdprintf("SD: build_bpb: unit=%d\n", fpRequest->r_bpmdesc);
  //we build the BPB during the device_init() method.
  return S_DONE;
}

static uint16_t IOCTL_input(void)
{
    CHCPCMD __far *command = (CHCPCMD __far *)fpRequest->r_trans;

    DPUTS("IOCTLOutput()");

    if (fpRequest->r_count >= sizeof(CHCPCMD))
    {
        switch (command->num)
        {
        case GET_DISK_DRIVE_PHYSICAL_INFO:
            v9k_disk_info *sd_card_info;
            sd_card_info = malloc(sizeof(v9k_disk_info));
            v9k_disk_info->ioctl_type = GET_DISK_DRIVE_PHYSICAL_INFO;
            bool success = 0;
            bool hard_disk = true;
            bool left = false;
            v9k_disk_info->ioctl_status = success;
            v9k_disk_info->disk_type = hard_disk;
            v9k_disk_info->disk_location = left;
            return v9k_disk_info;
            break;

        default:
            return (S_DONE | S_ERROR | E_CMD);
            break;
        }
    }

    return (S_DONE | S_ERROR | E_LENGTH);
}

/* Get Parameters */
/*   This routine implements the Get Parameters subfunction of the DOS  */
/* Generic IOCTL call. It gets a pointer to the device paramters block, */
/* which it then fills in.  We do NOT create the track/sector map that  */
/* is defined by recent DOS manuals to be at the end of the block - it  */
/* never seems to be used...                 */
static void get_parameters (device_params_t far *dp)
{
  dp->form_factor = FORM_FACTOR;  
  dp->attributes = 0;  
  dp->media_type = 0;
  dp->cylinders = bpb.total_sectors / (bpb.track_size * bpb.head_count);
  _fmemcpy(&(dp->bpb), &bpb, sizeof(bpb_t));
}

/* dos_error */
/*   This routine will translate a SD error code into an appropriate  */
/* DOS error code.  This driver never retries on any error condition.   */
/* For actual tape read/write errors it's pointless because the drive   */
/* will have already tried several times before reporting the failure.  */
/* All the other errors (e.g. write lock, communications failures, etc) */
/* are not likely to succeed without user intervention, so we go thru   */
/* the usual DOS "Abort, Retry or Ignore" dialog. Communications errors */
/* are a special situation.  In these cases we also set global flag to  */
/* force a controller initialization before the next operation.      */
int dos_error (int status)
{
  switch (status) {
    case RES_OK:     return 0;
    case RES_WRPRT:  return WRITE_PROTECT;
    case RES_NOTRDY: InitNeeded= TRUE; return NOT_READY;
    case RES_ERROR:  InitNeeded= TRUE; return BAD_SECTOR;
    case RES_PARERR: return CRC_ERROR;

    default:
    cdprintf("SD: unknown drive error - status = 0x%2x\n", status);
        return GENERAL_FAILURE;
  }
}

unsigned get_stackpointer();
#pragma aux get_stackpointer = \
    "mov ax, sp" \
    value [ax];

/* Read Data */
void read_block (rh_io_t far *rh)
{
  uint32_t lbn;
  uint16_t count;  int status;  uint8_t far *dta;
  uint16_t sendct;
  if (Debug)
    cdprintf("SD: read block: unit=%d, start=%d, count=%d, dta=%4x:%4x\n",
      rh->rh.unit, rh->start, rh->count, FP_SEG(rh->dta), FP_OFF(rh->dta));
  if (!drive_init ((rh_t far *) rh))  return;
  count = rh->count,  lbn = rh->start,  dta = rh->dta;
  lbn = (rh->start == 0xFFFF) ? rh->longstart : rh->start;
  while (count > 0) {
      sendct = (count > 16) ? 16 : count;
      status = SDRead(rh->rh.unit, lbn, dta, sendct);
      if (status != RES_OK)  {
   if (Debug) cdprintf("SD: read error - status=%d\n", status);
   _fmemset(dta, 0, BLOCKSIZE);
   rh->rh.status = DONE | ERROR | dos_error(status);
   return;
      }
    lbn += sendct;
    count -= sendct;
    dta += (sendct*BLOCKSIZE);
  }
  rh->rh.status = DONE;
}


/* Write Data */
/* Write Data with Verification */
void write_block (rh_io_t far *rh, bool verify)
{
  uint32_t lbn;
  uint16_t count;  int status;  uint8_t far *dta;
  uint16_t sendct;
  if (Debug)
    cdprintf("SD: %d write block: unit=%d, start=%d, count=%d, dta=%4x:%4x\n",
      verify, rh->rh.unit, rh->start, rh->count, FP_SEG(rh->dta), FP_OFF(rh->dta));
  if (!drive_init ((rh_t far *) rh))  return;
  count = rh->count,  dta = rh->dta;
  lbn = (rh->start == 0xFFFF) ? rh->longstart : rh->start;
  while (count > 0) {
    sendct = (count > 16) ? 16 : count;
    status = SDWrite(rh->rh.unit, lbn, dta, sendct);
    if (status != RES_OK)  {
      if (Debug) cdprintf("SD: write error - status=%d\n", status);
      rh->rh.status = DONE | ERROR | dos_error(status);
      return;
    }
    lbn += sendct;
    count -= sendct;
    dta += (sendct*BLOCKSIZE);
  }
  rh->rh.status = DONE;
}


static driverFunction_t dispatchTable[] =
{
    device_init,         // 0x00 Initialize
    media_check,         // 0x01 MEDIA Check
    build_bpb,           // 0x02 Build BPB
    IOCTL_input,         // 0x03 Ioctl In
    read_block,          // 0x04 Input (Read)
    NULL,                // 0x05 Non-destructive Read
    NULL,                // 0x06 Input Status
    NULL,                // 0x07 Input Flush
    write_block,         // 0x08 Output (Write)
    NULL,                // 0x09 Output with verify
    NULL,                // 0x0A Output Status
    NULL,                // 0x0B Output Flush
    NULL,                // 0x0C Ioctl Out
    open,                // 0x0D Device Open
    close,               // 0x0E Device Close
    NULL,                // 0x0F Removable MEDIA
    NULL,                // 0x10 Output till busy
    NULL,                // 0x11 Unused
    NULL,                // 0x12 Unused
    NULL,       // 0x13 Generic Ioctl
    NULL,                // 0x14 Unused
    NULL,                // 0x15 Unused
    NULL,                // 0x16 Unused
    NULL,                // 0x17 Get Logical Device
    NULL,                // 0x18 Set Logical Device
    NULL                 // 0x19 Ioctl Query
};

static driverFunction_t currentFunction;

void __far DeviceInterrupt( void )
#pragma aux DeviceInterrupt __parm []
{
#ifdef USE_INTERNAL_STACK
    switch_stack();
#endif

    push_regs();

    if ( fpRequest->r_command > C_MAXCMD || NULL == (currentFunction = dispatchTable[fpRequest->r_command]) )
    {
        fpRequest->r_status = S_DONE | S_ERROR | E_CMD;
    }
    else
    {
        fpRequest->r_status = currentFunction();
    }

    pop_regs();

#ifdef USE_INTERNAL_STACK
    restore_stack();
#endif
}

void __far DeviceStrategy( request __far *req )
#pragma aux DeviceStrategy __parm [__es __bx]
{
    fpRequest = req;
}

