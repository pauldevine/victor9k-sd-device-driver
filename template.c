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
#include <dos.h>
#include <string.h>

#include "device.h"
#include "diskio.h"
#include "devinit.h"
#include "template.h"
#include "cprint.h"     /* Console printing direct to hardware */
#include "sd.h"

#ifdef USE_INTERNAL_STACK

static uint8_t our_stack[STACK_SIZE];
uint8_t *stack_bottom = our_stack + STACK_SIZE;
uint32_t dos_stack;
bool initNeeded = TRUE;
int8_t my_units[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
bpb my_bpb;
bpb near *my_bpb_ptr = &my_bpb;
bpbtbl_t my_bpbtbl[9] = {NULL};
bpbtbl_t far *my_bpbtbl_ptr = (bpbtbl_t far *)my_bpbtbl;
extern bool debug;

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

/* mediaCheck */
/*    DOS calls this function to determine if the tape in the drive has   */
/* been changed.  The SD hardware can't determine this (like many      */
/* older 360K floppy drives), and so we always return the "Don't Know" */
/* response.  This works well enough...                                */
static uint16_t mediaCheck (void)
{
  // struct ALL_REGS registers;
  // get_all_registers(&registers);
  // mediaCheck_data far *media_ptr;
  //cdprintf("SD: mediaCheck()\n");
  //cdprintf("about to parse mediaCheck, ES: %x BX: %x\n", registers.es, registers.bx);
  //fpRequest->r_mediaCheck = MK_FP(registers.es, registers.bx);
  //cdprintf("SD: mediaCheck: unit=%x\n", fpRequest->r_mc_vol_id);
  
  writeToDriveLog("SD: mediaCheck(): r_unit 0x%2xh media_descriptor = 0x%2xh r_mc_red_code: %d fpRequest: %x:%x\n", 
    fpRequest->r_unit, fpRequest->r_mc_media_desc, M_NOT_CHANGED,
    FP_SEG(fpRequest), FP_OFF(fpRequest));
 
  fpRequest->r_mc_ret_code = M_NOT_CHANGED;
  //fpRequest->r_mc_ret_code = sd_mediaCheck(*fpRequest->r_mc_vol_id) ? M_CHANGED : M_NOT_CHANGED;
  return S_DONE;
}

/* buildBpb */
/*   DOS uses this function to build the BIOS parameter block for the   */
/* specified drive.  For diskettes, which support different densities   */
/* and formats, the driver actually has to read the BPB from the boot   */
/* sector on the disk.  */
static uint16_t buildBpb (void)
{
  // cdprintf("SD: buildBpb()\n");
  // if (debug)
  //     cdprintf("SD: buildBpb: unit=%x\n", fpRequest->r_bpmdesc);
  uint32_t bpb_start = calculateLinearAddress(FP_SEG(fpRequest->r_bpptr) , FP_OFF(fpRequest->r_bpptr));
    
  writeToDriveLog("SD: buildBpb(): media_descriptor=0x%2xh r_bpfat: %x:%x r_bpptr: %x:%x %5X", 
      fpRequest->r_bpmdesc, FP_SEG(fpRequest->r_bpfat), FP_OFF(fpRequest->r_bpfat),
      FP_SEG(fpRequest->r_bpptr), FP_OFF(fpRequest->r_bpptr), bpb_start);
  //we build the BPB during the deviceInit() method. just return pointer to built table
  fpRequest->r_bpptr = my_bpb_ptr;


  return S_DONE;
}

static uint16_t IOCTLInput(void)
{
    struct ALL_REGS regs;
    
    get_all_registers(&regs);

    //for the Victor disk IOCTL the datastructure is passed on thd DS:DX registers
    V9kDiskInfo far *v9k_disk_info_ptr = MK_FP(regs.ds, regs.dx);

    //cdprintf("SD: IOCTLInput()");
    writeToDriveLog("SD: IOCTLInput(): di_ioctl_type = 0x%xh\n", v9k_disk_info_ptr->di_ioctl_type);
    {
        switch (v9k_disk_info_ptr->di_ioctl_type)
        {
        case GET_DISK_DRIVE_PHYSICAL_INFO:
            // cdprintf("SD: IOCTLInput() - GET_DISK_DRIVE_PHYSICAL_INFO");
            // cdprintf("SD: IOCTLInput() -AH — IOCTL function number (44h) %x", registers.ax);
            // cdprintf("SD: IOCTLInput() -AL — IOCTL device driver read request value (4) %x", registers.ax);
            // cdprintf("SD: IOCTLInput() -BL — drive (0 = A, 1 = B, etc.) %x", registers.bx);
            // cdprintf("SD: IOCTLInput() -CX — length in bytes of this request structure (6) %x", registers.cx);
            // cdprintf("SD: IOCTLInput() -DS:DX — pointer to data structure %x:%x", registers.ds, registers.dx);

            v9k_disk_info_ptr->di_ioctl_type = GET_DISK_DRIVE_PHYSICAL_INFO;
            bool failed, hard_disk, left;
            failed = false;         /* 0 = success, 1 = failure, so when failed=false that means success=true */
            hard_disk = true;
            left = false;
            v9k_disk_info_ptr->di_ioctl_status = failed;
            v9k_disk_info_ptr->di_disk_type = hard_disk;
            v9k_disk_info_ptr->di_disk_location = left;

            //cdprintf("SD: IOCTLInput() - GET_DISK_DRIVE_PHYSICAL_INFO - success: %d", success);

            return S_DONE;
            break;

        default:
            failed = true;
            v9k_disk_info_ptr->di_ioctl_status = failed;
            return (S_DONE | S_ERROR | E_UNKNOWN_COMMAND);
            break;
        }
    }

    return (S_DONE | S_ERROR | E_HEADER_LENGTH);
}

/* dosError */
/*   This routine will translate a SD error code into an appropriate  */
/* DOS error code.  This driver never retries on any error condition.   */
/* For actual tape read/write errors it's pointless because the drive   */
/* will have already tried several times before reporting the failure.  */
/* All the other errors (e.g. write lock, communications failures, etc) */
/* are not likely to succeed without user intervention, so we go thru   */
/* the usual DOS "Abort, Retry or Ignore" dialog. Communications errors */
/* are a special situation.  In these cases we also set global flag to  */
/* force a controller initialization before the next operation.      */
int dosError (int status)
{
  switch (status) {
    case RES_OK:     return 0;
    case RES_WRPRT:  return E_WRITE_PROTECT;
    case RES_NOTRDY: initNeeded = false; return E_NOT_READY;               
    case RES_ERROR:  initNeeded = false; return E_SECTOR_NOT_FND;
    case RES_PARERR: return E_CRC_ERROR;

    default:
    writeToDriveLog("SD: unknown drive error - status = 0x%2x\n", status);
        return E_GENERAL_FAILURE;
  }
}

unsigned get_stackpointer();
#pragma aux get_stackpointer = \
    "mov ax, sp" \
    value [ax];

/* Read Data */

static uint16_t readBlock (void)
{
  // cdprintf("SD: readBlock()\n");
  if (debug) {
    writeToDriveLog("SD: read block: media_descriptor=0x%2xh, start=%d, count=%d, r_trans=%x:%x\n",
     fpRequest->r_meddesc, fpRequest->r_start, fpRequest->r_count, 
             FP_SEG(fpRequest->r_trans), FP_OFF(fpRequest->r_trans));
  }
  if (initNeeded)  return (S_DONE | S_ERROR | E_NOT_READY); //not initialized yet

  uint16_t count; 
  uint16_t numberOfSectorsToCopy = fpRequest->r_count;  

  if (initNeeded)  return (S_DONE | S_ERROR | E_NOT_READY); //not initialized yet
  //TODO: double check all this math below. differs greatly across media
  count = fpRequest->r_count,  
          fpRequest->r_start;  
  uint8_t far * dta = (uint8_t far *)fpRequest->r_trans;
  uint32_t lbn = fpRequest->r_start;
  while (count > 0) {
      uint16_t sendct = (count > 16) ? 16 : count;
      //int sd_read (uint16_t unit, uint32_t lbn, uint8_t far *buffer, uint16_t count)
      int16_t status = sd_read(fpRequest->r_unit, lbn, dta, sendct);

      if (status != RES_OK)  {
        if (debug) cdprintf("SD: read error - status=%d\n", status);
        //_fmemset(dta, 0, BLOCKSIZE);
        return (S_DONE | S_ERROR | dosError(status));
      }

    lbn += sendct;
    count -= sendct;
    dta += (sendct*BLOCKSIZE);
  }
  return (S_DONE);
}


/* Write Data */
/* Write Data with Verification */
static uint16_t write_block (bool verify)
{
  //TODO Double check this math, differs by media
  uint32_t lbn;
  uint16_t count;  
  int status; 
  uint8_t far *dta;
  uint16_t sendct;
  bool success, hard_disk, left;

  if (debug) {
        writeToDriveLog("SD: write block: media_desc=%d, start=%d, count=%d, r_trans=%x:%x verify: %d fpRequest: %x:%x\n",
                 fpRequest->r_meddesc, fpRequest->r_start, fpRequest->r_count, 
                 FP_SEG(fpRequest->r_trans), FP_OFF(fpRequest->r_trans), verify, 
                 FP_SEG(fpRequest), FP_OFF(fpRequest));
  }

  if (initNeeded)  return (S_DONE | S_ERROR | E_NOT_READY); //not initialized yet
  count = fpRequest->r_count;
  dta = (uint8_t far *)fpRequest->r_trans;
  lbn = fpRequest->r_start;
  while (count > 0) {
    sendct = (count > 16) ? 16 : count;
    status = sd_write(fpRequest->r_unit, lbn, dta, sendct);

    if (status != RES_OK)  {
      if (debug) cdprintf("SD: write error - status=%d\n", status);
      return (S_DONE | S_ERROR | dosError(status));
    }

    lbn += sendct;
    count -= sendct;
    dta += (sendct*BLOCKSIZE);
  }
  return (S_DONE);
}

static uint16_t writeNoVerify () {
    return write_block(FALSE);
}

static uint16_t writeVerify () {
    return write_block(TRUE);
}

static bool isMyUnit(int8_t unitCode) {
  for (int i = 0; i < sizeof(my_units); ++i) {
    if (my_units[i] == -1) {
      break;  // End of valid unit codes in the array
    }
    if (my_units[i] == unitCode) {
      return true;  // This unit code is for my driver
    }
  }
  return false;  // This unit code is not for my driver
}

static driverFunction_t dispatchTable[] =
{
    deviceInit,          // 0x00 Initialize
    mediaCheck,          // 0x01 MEDIA Check
    buildBpb,            // 0x02 Build BPB
    IOCTLInput,          // 0x03 Ioctl In
    readBlock,           // 0x04 Input (Read)
    NULL,                // 0x05 Non-destructive Read
    NULL,                // 0x06 Input Status
    NULL,                // 0x07 Input Flush
    writeNoVerify,       // 0x08 Output (Write)
    writeVerify,         // 0x09 Output with verify
    NULL,                // 0x0A Output Status
    NULL,                // 0x0B Output Flush
    NULL,                // 0x0C Ioctl Out
    open,                // 0x0D Device Open
    close,               // 0x0E Device Close
    NULL,                // 0x0F Removable MEDIA
    NULL,                // 0x10 Output till busy
    NULL,                // 0x11 Unused
    NULL,                // 0x12 Unused
    NULL,                // 0x13 Generic Ioctl
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
        fpRequest->r_status = S_DONE | S_ERROR | E_UNKNOWN_COMMAND;
    }
    else
    {
        // writeToDriveLog("SD: DeviceInterrupt command: %d r_unit: 0x%2xh isMyUnit(): %d r_status: %d r_length: %d initNeeded: %d\n",
        //     fpRequest->r_command, fpRequest->r_unit, isMyUnit(fpRequest->r_unit), fpRequest->r_status, fpRequest->r_length, initNeeded);   
        if ((initNeeded && fpRequest->r_command == C_INIT) || isMyUnit(fpRequest->r_unit)) {
            fpRequest->r_status = currentFunction();
        } else {
            // This is  not for me to handle
            struct device_header __far *deviceHeader = MK_FP(getCS(), 0);
            struct device_header __far *nextDeviceHeader = deviceHeader->dh_next;
            nextDeviceHeader->dh_interrupt();
        }
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
     // writeToDriveLog("SD: DeviceStrategy command: %d r_unit: %d r_status: %d r_length: %x fpRequest: %x:%x\n",
     //       req->r_command, req->r_unit, req->r_status, req->r_length, FP_SEG(fpRequest), FP_OFF(fpRequest));

}

