/*------------------------------------------------------------------------/
/  Foolproof MMCv3/SDv1/SDv2 (in SPI mode) control module
/-------------------------------------------------------------------------/
/
/  Copyright (C) 2013, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/-------------------------------------------------------------------------/
/  Features and Limitations:
/
/ * Easy to Port Bit-banging SPI
/    It uses only four GPIO pins. No interrupt, no SPI is needed.
/
/  * Platform Independent
/    You need to modify only a few macros to control GPIO ports.
/
/  * Low Speed
/    The data transfer rate will be several times slower than hardware SPI.
/
/  * No Media Change Detection
/    Application program needs to perform f_mount() after media change.
/
/-------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <i86.h>
#include <dos.h>
#include <stdint.h>

#include "diskio.h"     // Common include file for FatFs and disk I/O layer
#include "cprint.h"


/*-------------------------------------------------------------------------/
/ Platform dependent macros and functions needed to be modified           /
/-------------------------------------------------------------------------*/
typedef struct {
    uint8_t out_in_reg_b;
    uint8_t out_in_reg_a;
    uint8_t data_dir_reg_b;         // data-direction, reg 'b'    
    uint8_t data_dir_reg_a;
    uint8_t timer1_ctr_lo;
    uint8_t timer1_ctr_hi;
    uint8_t timer1_latch_lo;
    uint8_t timer1_latch_hi;
    uint8_t timer2_ctr_lo;
    uint8_t timer2_ctr_hi;
    uint8_t shift_reg;
    uint8_t aux_ctrl_reg;           // auxiliary Ctrl reg        
    uint8_t periph_ctrl_reg;
    uint8_t int_flag_reg;           // interrupt flag register   
    uint8_t int_enable_reg;         // interrupt enable register 
    uint8_t out_in_reg_a_no_hs;     // out-in reg 'a' NO HANDSHAKE
} V9kParallelPort;

/*-------------------------------------------------------------------------/
/  Bit definitions for Centronics-style parallel interface, 'via1'.        /
/-------------------------------------------------------------------------*/ 
#define PHASE2_DEVICE_SEGMENT 0xE800
#define VIA1_REG_OFFSET    0x0020    // baseaddr for centronics    
#define DATA_0_OFFSET      0x01      // data strobe (PA0) / Pin 2 on DB-25
#define DATA_1_OFFSET      0x02      // this datum for vfu (PA1) / Pin 3 on DB-25 
#define DATA_2_OFFSET      0x04      // this datum for vfu (PA2) / Pin 4 on DB-25
#define DATA_STROBE_L      0x01      // printer busy (PB0)  / Pin 1 on DB-25      
#define END_OR_IDENTIFY_H  0x02      // printer busy (PB1)  / Pin 15 on DB-25
#define BUSY_H             0x20      // printer busy (PB5)  / Pin 11 on DB-25
#define ACKNOWLEDGE_L      0x40      // printer ack (PB6)   / Pin 10 on DB-25      
#define SELECT_H_OFFSET    0x80      // on-line and no error (PB7) / Pin 13 on DB-25


/*-------------------------------------------------------------------------/ 
/ Bit definitions for multi-use pio, 'via2'.                               /
/-------------------------------------------------------------------------*/
#define VIA2_REG_OFFSET   0x0040    // base address user port via2
#define TALK_ENABLE_H 0x01          // talk-enable line line VIA2 PB0


static volatile V9kParallelPort far *via1 = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                           VIA1_REG_OFFSET);
static volatile V9kParallelPort far *via2 = MK_FP(PHASE2_DEVICE_SEGMENT, 
                                                           VIA2_REG_OFFSET);

#define DO_INIT()
#define DI_INIT()
#define CK_INIT() 
#define CS_INIT()

static bool via_initialized;
extern bool initNeeded;
extern bool debug;
const int bit_delay_us = 2;


#define BITDLY() delay_us(bit_delay_us)

/*-------------------------------------------------------------------------*/
/* Platform dependent function to output and input bytes on port           */
/*-------------------------------------------------------------------------*/

/* something about our makefile isn't letting these be linked, defining here */
extern void Enable( void );
#pragma aux Enable = \
    "sti" \
    parm [] \
    modify [];
extern void Disable( void );
#pragma aux Disable = \
    "cli" \
    parm [] \
    modify [];

static void delay_us(unsigned int n);
#pragma aux delay_us = \
    "mov cx, ax", \
    "loopit:", \
    "loop loopit", \
    parm [ax] \
    modify [cx];


void par_port_init(void);

void outportbyte(volatile uint8_t far* port, uint8_t value) {
    //cdprintf("outportbyte: port %x, value %x\n", (void*)via1, value);
    if (! via_initialized) {
         //cdprintf("outportbyte: VIA NOT INITIALIZED, calling par_port_init()\n");
         par_port_init();
    }
    //Disable();  /* Disable interrupts */
    via1->out_in_reg_a = value;
    //Enable();   /* Enable interrupts */
    //*port = value;
    return;
}

uint8_t inportbyte(volatile uint8_t far* port) { 
   uint8_t data;
   //Disable();  /* Disable interrupts */
   data = via1->out_in_reg_b;
   //Enable();   /* Enable interrupts */
   //cdprintf("inportbyte: data: %x\n", data);
   return data;
}

//Victor 9K 0xE8020 for VIA 1 external parallel port
//or 0xE8040 for VIA 2 CS2 User port on motherboard
//TODO: re-enable this functionality for later
//static volatile V9kParallelPort far *portbases[5] = {&via1,&via2};

uint8_t sd_card_check = 0; 
uint8_t portbase = 1;


static volatile uint8_t far *OUTPORT;
static volatile uint8_t far *STATUSPORT;
static volatile uint8_t far *CONTROLPORT;

void par_port_init(void) {
   //Via-1 is main parallel port 
   // cdprintf("Address of via1: %x\n", (void*)via1);    
   // cdprintf("via1->out_in_reg_a: %x\n", (void*)via1);          
   via1->out_in_reg_a = 0;               /* out_in_reg_a is dataport, init with 0's =output bits */ 
   //cdprintf("data_dir_reg_a\n"); 
   via1->data_dir_reg_a = 0xFF;          /* register a is all outbound, 1111 = all bits outgoing */
   //cdprintf("out_in_reg_b\n"); 
   via1->out_in_reg_b = 0;               /* out_in_reg_b is input, clear register                */
   //cdprintf("data_dir_reg_b\n"); 
   via1->data_dir_reg_b = 0x00;             /* register b is all inbound, init with 0000's             */
   //cdprintf("periph_ctrl_reg\n"); 
   via1->periph_ctrl_reg = 0x00;            /* setting incoming usage of CA1/CA2 lines              */
   //via1->aux_ctrl_reg = 0x00;             /* turn off the timer / shift registers / etc.       */

   //Via-2 PB1 controls talk-enable line
   // cdprintf("Address of via2: %x\n", (void*)via2);
   // cdprintf("via2 talk enable output\n"); 
   via2->data_dir_reg_b |= TALK_ENABLE_H;                /* set talk-enable pin to output mode   */
   //cdprintf("via2 talk enable true\n"); 
   via2->out_in_reg_b |= TALK_ENABLE_H;                  /* set talk-enable pin value to true    */
   //cdprintf("about to return init()\n"); 
   OUTPORT=&via1->out_in_reg_a;
   STATUSPORT=&via1->out_in_reg_b;
   CONTROLPORT=&via1->out_in_reg_b;
   via_initialized = true;
   initNeeded = false;

   if (debug) { cdprintf("Finished via_initialized, cycling bits\n"); }
   //say hello
   BITDLY(); 
   outportbyte(OUTPORT,0xFF);
   BITDLY();
   outportbyte(OUTPORT,0);
   BITDLY();
   outportbyte(OUTPORT,0xFF);
   BITDLY();
   outportbyte(OUTPORT,0);
   BITDLY();
   outportbyte(OUTPORT,0xFF);
   BITDLY();
   outportbyte(OUTPORT,0);
   BITDLY();
   outportbyte(OUTPORT,0xFF);
   BITDLY();
   
   return;
}

/*-------------------------------------------------------------------------/ 
/ init by pointing struct at chip, init ports 
/ out_in_reg_a is dataport, init with O's  
/ set all out_in_reg_a bits as outgoing   
/ out_in_reg_b is ctrlport, init no ds/pi 
/ last 2 only are outgoing               
/-------------------------------------------------------------------------*/ 
#define VAR_INIT() par_port_init();


/* MISOPIN (SD card DAT0/DO pin 7) is PPORT SELECT (DB-25 pin 13) */
#define MISOPIN     (0x01 << 7)
/* Card Detect (N/A) is PPORT BUSY (VIA 1 PB5 / DB-25 pin 11) */
#define CDDETECTPIN (0x01 << 5)

/* Do not interface 5 to 3.3 volts directly! Use level converter... */

/* MOSI (SD card CMD/DI pin 2) is PPORT D0 (VIA 1 PA0 / DB-25 pin 2) */
#define MOSIPIN     (0x01 << 0)
/* CLOCK (SD card CLK/SCLK pin 5) is PPORT D1 (VIA 1 PA1 / DB-25 pin 3) */
#define CLOCKPIN    (0x01 << 1)
/* Card Select (SD card CAT3/CS pin 1) is PPORT D2 (VIA 1 PA2 / DB-25 pin 4) */
#define CSPIN       (0x01 << 2)
/* Connect ground to one of PPORT pins 18-25 */

#if 1
#define TOUTCHR(x)
#define TOUTHEX(x) 
#define TOUTuint16_t(x) 
#else
#define TOUTCHR(x) toutchr(x)
#define TOUTHEX(x) touthex(x)
#define TOUTuint16_t(x) toutword(x)

static uint8_t toutchr (unsigned char ch)
{
  _DI = _SI = 0;
  _AL = ch;  _AH = 0xE;  _BX = 0;
  asm  INT  0x10;
  return 0; 
}

static uint8_t touthex(unsigned char c)
{
  char d = c >> 4;
  toutchr(d > 9 ? d+('A'-10) : d+'0');
  d = c & 0x0F;
  toutchr(d > 9 ? d+('A'-10) : d+'0');
  return 0;
}

static uint8_t toutword(uint16_t x)
{
  touthex(x >> 8);
  touthex(x);
  return 0;
}

#endif

void setportbase(uint8_t val)
{
  // if ((val >= 1) && (val <= (sizeof(portbases)/sizeof(portbases[0])) ))
  //  //todo fix this assignment
  //   //OUTPORT = portbases[val-1];
  via_initialized = false;
  portbase=val;
  par_port_init();
}




#define DO(statusport) (inportbyte((statusport)) & MISOPIN)  
#define CDDETECT(statusport) (1)
#define CLOCKBITHIGHMOSIHIGH(outport) outportbyte((outport),MOSIPIN|CLOCKPIN) 
#define CLOCKBITHIGHMOSILOW(outport) outportbyte((outport),CLOCKPIN) 
#define CLOCKBITLOWMOSIHIGH(outport) outportbyte((outport),MOSIPIN) 
#define CLOCKBITLOWMOSILOW(outport) outportbyte((outport),0) 
#define CLOCKBITHIGHMOSIHIGHNOCS(outport) outportbyte((outport),MOSIPIN|CLOCKPIN|CSPIN) 
#define CLOCKBITLOWMOSIHIGHNOCS(outport) outportbyte((outport),MOSIPIN|CSPIN) 
#define CS_L(outport) outportbyte((outport),MOSIPIN)
#define CS_H(outport) outportbyte((outport),MOSIPIN|CSPIN)               
#define CK_L(outport)

#define ADJ_VAL 1

#define NOSHIFT
#ifdef NOSHIFT 

uint32_t dwordlshift(uint32_t d, int n)
{
   int i;
   uint16_t a = (uint16_t)(d & 0xFFFF);        // Extract lower uint16_t
   uint16_t b = (uint16_t)((d >> 16) & 0xFFFF); // Extract upper uint16_t
   uint32_t r;

   for (i=0; i<n; i++)
   {
      b <<= 1;
      b |= (a & 0x8000) ? 1 : 0;
      a <<= 1;
   }
   
   r = ((uint32_t)b << 16) | a; // Construct uint32_t from upper and lower uint16_t
   
   return r;
 }

#define uint32_tLSHIFT(d,n) dwordlshift(d,n)

uint32_t dwordrshift(uint32_t d, int n)
{
   int i;
   uint16_t a = ((uint16_t *)d)[0];
   uint16_t b = ((uint16_t *)d)[1];
   uint32_t r;

   for (i=0;i<n;i++)
   {
      a >>= 1;
      a |= (b & 0x1) ? 0x8000 : 0;
      b >>= 1;
   }
   ((uint16_t *)r)[0] = a;
   ((uint16_t *)r)[1] = b;
   return r;
}

#define uint32_tRSHIFT(d,n) dwordrshift(d,n)

#else

#define uint32_tLSHIFT(d,n) ((d) << (n))
#define uint32_tRSHIFT(d,n) ((d) >> (n))

#endif

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* MMC/SD command (SPI mode) */
#define CMD0   (0)         /* GO_IDLE_STATE */
#define CMD1   (1)         /* SEND_OP_COND */
#define ACMD41 (0x80+41)   /* SEND_OP_COND (SDC) */
#define CMD8   (8)         /* SEND_IF_COND */
#define CMD9   (9)         /* SEND_CSD */
#define CMD10  (10)     /* SEND_CID */
#define CMD12  (12)     /* STOP_TRANSMISSION */
#define CMD13  (13)     /* SEND_STATUS */
#define ACMD13 (0x80+13)   /* SD_STATUS (SDC) */
#define CMD16  (16)     /* SET_BLOCKLEN */
#define CMD17  (17)     /* READ_SINGLE_BLOCK */
#define CMD18  (18)     /* READ_MULTIPLE_BLOCK */
#define CMD23  (23)     /* SET_BLOCK_COUNT */
#define ACMD23 (0x80+23)   /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24  (24)     /* WRITE_BLOCK */
#define CMD25  (25)     /* WRITE_MULTIPLE_BLOCK */
#define CMD32  (32)     /* ERASE_ER_BLK_START */
#define CMD33  (33)     /* ERASE_ER_BLK_END */
#define CMD38  (38)     /* ERASE */
#define CMD55  (55)     /* APP_CMD */
#define CMD58  (58)     /* READ_OCR */


static
DSTATUS Stat = STA_NOINIT; /* Disk status */

static
uint8_t CardType;       /* b0:MMC, b1:SDv1, b2:SDv2, b3:Block addressing */



/*-----------------------------------------------------------------------*/
/* Transmit bytes to the card (bitbanging)                               */
/*-----------------------------------------------------------------------*/

static
void xmit_mmc (
   const uint8_t far * buff, /* Data to be sent */
   uint16_t bc                  /* Number of bytes to send */
)
{
   uint8_t d;
   volatile uint8_t far *outport = OUTPORT;

   do {
      d = *buff++;   /* Get a byte to be sent */
      if (d & 0x80) 
      {  CLOCKBITLOWMOSIHIGH(outport); BITDLY(); CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      } else
      {  CLOCKBITLOWMOSILOW(outport); BITDLY(); CLOCKBITHIGHMOSILOW(outport); BITDLY();
      } 
      if (d & 0x40) 
      {  CLOCKBITLOWMOSIHIGH(outport); BITDLY(); CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      } else
      {  CLOCKBITLOWMOSILOW(outport); BITDLY(); CLOCKBITHIGHMOSILOW(outport); BITDLY();
      } 
      if (d & 0x20) 
      {  CLOCKBITLOWMOSIHIGH(outport); BITDLY(); CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      } else
      {  CLOCKBITLOWMOSILOW(outport); BITDLY(); CLOCKBITHIGHMOSILOW(outport); BITDLY();
      } 
      if (d & 0x10) 
      {  CLOCKBITLOWMOSIHIGH(outport); BITDLY(); CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      } else
      {  CLOCKBITLOWMOSILOW(outport); BITDLY(); CLOCKBITHIGHMOSILOW(outport); BITDLY();
      } 
      if (d & 0x08) 
      {  CLOCKBITLOWMOSIHIGH(outport); BITDLY(); CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      } else
      {  CLOCKBITLOWMOSILOW(outport); BITDLY(); CLOCKBITHIGHMOSILOW(outport); BITDLY();
      } 
      if (d & 0x04) 
      {  CLOCKBITLOWMOSIHIGH(outport); BITDLY(); CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      } else
      {  CLOCKBITLOWMOSILOW(outport); BITDLY(); CLOCKBITHIGHMOSILOW(outport); BITDLY();
      } 
      if (d & 0x02) 
      {  CLOCKBITLOWMOSIHIGH(outport); BITDLY(); CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      } else
      {  CLOCKBITLOWMOSILOW(outport); BITDLY(); CLOCKBITHIGHMOSILOW(outport); BITDLY();
      } 
      if (d & 0x01) 
      {  CLOCKBITLOWMOSIHIGH(outport); BITDLY(); CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      } else
      {  CLOCKBITLOWMOSILOW(outport); BITDLY(); CLOCKBITHIGHMOSILOW(outport); BITDLY();
      } 
   } while (--bc);
   CLOCKBITLOWMOSIHIGH(outport);
}



/*-----------------------------------------------------------------------*/
/* Receive bytes from the card (bitbanging)                              */
/*-----------------------------------------------------------------------*/

static
void rcvr_mmc (
   uint8_t far *buff, /* Pointer to read buffer */
   uint16_t bc            /* Number of bytes to receive */
)
{
   uint8_t r;
   volatile uint8_t far *outport = OUTPORT;
   volatile uint8_t far *statusport = STATUSPORT;
   
   do {
      CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      r = 0;  if (DO(statusport)) r++;  /* bit7 */
      CLOCKBITLOWMOSIHIGH(outport); BITDLY();

      CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      r <<= 1; if (DO(statusport)) r++;   /* bit6 */
      CLOCKBITLOWMOSIHIGH(outport); BITDLY();

      CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      r <<= 1; if (DO(statusport)) r++;   /* bit5 */
      CLOCKBITLOWMOSIHIGH(outport); BITDLY();

      CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      r <<= 1; if (DO(statusport)) r++;   /* bit4 */
      CLOCKBITLOWMOSIHIGH(outport); BITDLY();

      CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      r <<= 1; if (DO(statusport)) r++;   /* bit3 */
      CLOCKBITLOWMOSIHIGH(outport); BITDLY();

      CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      r <<= 1; if (DO(statusport)) r++;   /* bit2 */
      CLOCKBITLOWMOSIHIGH(outport); BITDLY();

      CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      r <<= 1; if (DO(statusport)) r++;   /* bit1 */
      CLOCKBITLOWMOSIHIGH(outport); BITDLY();

      CLOCKBITHIGHMOSIHIGH(outport); BITDLY();
      r <<= 1; if (DO(statusport)) r++;   /* bit0 */
      CLOCKBITLOWMOSIHIGH(outport); BITDLY();

      *buff++ = r;         /* Store a received byte */
   } while (--bc);
}

/*-----------------------------------------------------------------------*/
/* Receive bytes from the card (bitbanging)                              */
/*-----------------------------------------------------------------------*/

static
void dummy_rcvr_mmc (void)
{
   int i;
   volatile uint8_t far *outport = OUTPORT;
   CLOCKBITLOWMOSIHIGHNOCS(outport); BITDLY();
   for (i=0;i<8;i++)
   {
      CLOCKBITHIGHMOSIHIGHNOCS(outport); BITDLY();
      CLOCKBITLOWMOSIHIGHNOCS(outport); BITDLY();
   }
}

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static
int wait_ready (void)   /* 1:OK, 0:Timeout */
{
   uint8_t d;
   uint16_t tmr;


   for (tmr = 5000; tmr; tmr--) {   /* Wait for ready in timeout of 500ms */
      rcvr_mmc(&d, 1);
      if (d == 0xFF) break;
      delay_us(100);
   }

   return tmr ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus                                 */
/*-----------------------------------------------------------------------*/

static
void deselect (void)
{
   uint8_t d;

   CS_H(OUTPORT);
   dummy_rcvr_mmc();  /* Dummy clock (force DO hi-z for multiple slave SPI) */
}



/*-----------------------------------------------------------------------*/
/* Select the card and wait for ready                                    */
/*-----------------------------------------------------------------------*/

static
int select (void) /* 1:OK, 0:Timeout */
{
   uint8_t d;

   CS_L(OUTPORT); 
   rcvr_mmc(&d, 1);  /* Dummy clock (force DO enabled) */

   if (wait_ready()) return 1;   /* OK */
   deselect();
   return 0;         /* Failed */
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from the card                                   */
/*-----------------------------------------------------------------------*/

static
int rcvr_datablock ( /* 1:OK, 0:Failed */
   uint8_t far *buff,       /* Data buffer to store received data */
   uint16_t btr                 /* Byte count */
)
{
   uint8_t d[2];
   uint16_t tmr;


   for (tmr = 1000; tmr; tmr--) {   /* Wait for data packet in timeout of 100ms */
      rcvr_mmc(d, 1);
      if (d[0] != 0xFF) break;
      delay_us(100);
   }
   if (d[0] != 0xFE) {
    return 0;      /* If not valid data token, return with error */
   }

   rcvr_mmc(buff, btr);       /* Receive the data block into buffer */
   rcvr_mmc(d, 2);               /* Discard CRC */

   return 1;                  /* Return with success */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to the card                                        */
/*-----------------------------------------------------------------------*/

static
int xmit_datablock ( /* 1:OK, 0:Failed */
   const uint8_t far *buff, /* 512 byte data block to be transmitted */
   uint8_t token               /* Data/Stop token */
)
{
   uint8_t d[2];


   if (!wait_ready()) return 0;

   d[0] = token;
   xmit_mmc(d, 1);            /* Xmit a token */
   if (token != 0xFD) {    /* Is it data token? */
      xmit_mmc(buff, 512); /* Xmit the 512 byte data block to MMC */
      rcvr_mmc(d, 2);         /* Xmit dummy CRC (0xFF,0xFF) */
      rcvr_mmc(d, 1);         /* Receive data response */
      if ((d[0] & 0x1F) != 0x05) /* If not accepted, return with error */
      {
         return 0;
      }
   }

   return 1;
}



/*-----------------------------------------------------------------------*/
/* Send a command packet to the card                                     */
/*-----------------------------------------------------------------------*/

static
uint8_t send_cmd (      /* Returns command response (bit7==1:Send failed)*/
   uint8_t cmd,      /* Command byte */
   uint32_t arg      /* Argument */
)
{
   uint8_t n, d, buf[6];

   
   if (cmd & 0x80) { /* ACMD<n> is the command sequense of CMD55-CMD<n> */
      cmd &= 0x7F;
      n = send_cmd(CMD55, 0);
      if (n > 1) return n;
   }

   /* Select the card and wait for ready except to stop multiple block read */
   if (cmd != CMD12) {
      deselect();
      if (!select()) return 0xFF;
   }

   /* Send a command packet */
   buf[0] = 0x40 | cmd;       /* Start + Command index */
 #ifdef NOSHIFT
   buf[1] = ((uint8_t *)&arg)[3];      /* Argument[31..24] */
   buf[2] = ((uint8_t *)&arg)[2];      /* Argument[23..16] */
   buf[3] = ((uint8_t *)&arg)[1];      /* Argument[15..8] */
   buf[4] = ((uint8_t *)&arg)[0];      /* Argument[7..0] */
 #else
   buf[1] = (uint8_t)(arg >> 24);      /* Argument[31..24] */
   buf[2] = (uint8_t)(arg >> 16);      /* Argument[23..16] */
   buf[3] = (uint8_t)(arg >> 8);    /* Argument[15..8] */
   buf[4] = (uint8_t)arg;           /* Argument[7..0] */
 #endif
   n = 0x01;                  /* Dummy CRC + Stop */
   if (cmd == CMD0) n = 0x95;    /* (valid CRC for CMD0(0)) */
   if (cmd == CMD8) n = 0x87;    /* (valid CRC for CMD8(0x1AA)) */
   buf[5] = n;
   TOUTCHR('L');
   TOUTHEX(buf[0]);
   TOUTHEX(buf[1]);
   TOUTHEX(buf[2]);
   TOUTHEX(buf[3]);
   TOUTHEX(buf[4]);
   TOUTHEX(buf[5]);
   xmit_mmc(buf, 6);

   /* Receive command response */
   if (cmd == CMD12) rcvr_mmc(&d, 1);  /* Skip a stuff byte when stop reading */
   n = 10;                       /* Wait for a valid response in timeout of 10 attempts */
   do
   {
      rcvr_mmc(&d, 1);
   }
   while ((d & 0x80) && (--n));
      TOUTCHR('P');
      TOUTHEX(d);
   return d;         /* Return with the response value */
}



/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
   uint8_t drv       /* Drive number (always 0) */
)
{
   if (drv) return STA_NOINIT;
   if ((sd_card_check) && (CDDETECT(STATUSPORT)))
   {
      Stat = STA_NOINIT;
      return STA_NOINIT;
   }
   return Stat;
}

DRESULT disk_result (
   uint8_t drv       /* Drive number (always 0) */
)
{
   if (drv) return RES_NOTRDY;
   if ((sd_card_check) && (CDDETECT(STATUSPORT)))
   {
      Stat = STA_NOINIT;
      return RES_NOTRDY;
   }
   return RES_OK;
}


/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (uint8_t drv)
{
   /* drv = Physical drive nmuber (0) */
   uint8_t n, card_type, cmd, buf[4];
   uint16_t tmr;
   DSTATUS s;

   if (debug) {
      cdprintf ("disk_initialize: before setportbase(), drv: %x\n", drv);
      cdprintf ("disk_initialize: before setportbase(), portbase: %x\n", portbase);
   }
   setportbase(portbase);

   if (debug) cdprintf ("disk_initialize: if (drv) return: %x\n", drv);
   if (drv) return RES_NOTRDY;
   
   if (debug) cdprintf ("disk_initialize: if sd_card_check: %x CDDETECT(STATUSPORT): %x \n", 
      sd_card_check, CDDETECT(STATUSPORT));
   if ((sd_card_check) && (CDDETECT(STATUSPORT))){
      return RES_NOTRDY;
   }
   
   card_type = 0;
   for (n = 5; n; n--) {
      if (debug) cdprintf ("disk_initialize: for: %x\n", n);
      CS_INIT(); 
      CS_H(OUTPORT);   /* Initialize port pin tied to CS */
      delay_us(10000);       /* 10ms. time for SD card to power up */
      CS_INIT(); 
      CS_H(OUTPORT);   /* Initialize port pin tied to CS */
      CK_INIT(); 
      CK_L(OUTPORT);   /* Initialize port pin tied to SCLK */
      DI_INIT();           /* Initialize port pin tied to DI */
      DO_INIT();           /* Initialize port pin tied to DO */
      for (tmr = 10; tmr; tmr--) dummy_rcvr_mmc(); /* Apply 80 dummy clocks and the card gets ready to receive command */
      if (send_cmd(CMD0, 0) == 1) {       /* Enter Idle state */
         if (send_cmd(CMD8, 0x1AA) == 1) {   /* SDv2? */
            rcvr_mmc(buf, 4);                   /* Get trailing return value of R7 resp */
            if (buf[2] == 0x01 && buf[3] == 0xAA) {      /* The card can work at vdd range of 2.7-3.6V */
               for (tmr = 1000; tmr; tmr--) {         /* Wait for leaving idle state (ACMD41 with HCS bit) */
                  if (send_cmd(ACMD41, 1UL << 30) == 0) break;
                  delay_us(1000);
               }
               if (tmr && send_cmd(CMD58, 0) == 0) {  /* Check CCS bit in the OCR */
                  rcvr_mmc(buf, 4);
                  card_type = (buf[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2; /* SDv2 */
               }
            }
         } else {                   /* SDv1 or MMCv3 */
            if (send_cmd(ACMD41, 0) <= 1)    {
               card_type = CT_SD1; cmd = ACMD41; /* SDv1 */
            } else {
               card_type = CT_MMC; cmd = CMD1;   /* MMCv3 */
            }
            for (tmr = 1000; tmr; tmr--) {         /* Wait for leaving idle state */
               if (send_cmd(cmd, 0) == 0) break;
               delay_us(1000);
            }
            if (!tmr || send_cmd(CMD16, 512) != 0) /* Set R/W block length to 512 */
               card_type = 0;
         }
         break;
      }
   }
   CardType = card_type;
   if (debug) cdprintf ("disk_initialize: CardType: %x\n", CardType);
   s = card_type ? 0 : STA_NOINIT;
   Stat = s;
   if (debug) cdprintf ("disk_initialize: Stat: %x\n", Stat);
   deselect();

   return s;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
   uint8_t drv,            /* Physical drive nmuber (0) */
   uint8_t far *buff,   /* Pointer to the data buffer to store read data */
   uint32_t sector,        /* Start sector number (LBA) */
   uint16_t count           /* Sector count (1..128) */
)
{
   DRESULT dr = disk_result(drv);
   if (dr != RES_OK) return dr;
   
   if (!(CardType & CT_BLOCK)) sector = uint32_tLSHIFT(sector,9);   /* Convert LBA to byte address if needed */

   if (count == 1) { /* Single block read */
      if ((send_cmd(CMD17, sector) == 0)  /* READ_SINGLE_BLOCK */
         && rcvr_datablock(buff, 512))
         count = 0;
   }
   else {            /* Multiple block read */
      if (send_cmd(CMD18, sector) == 0) { /* READ_MULTIPLE_BLOCK */
         do {
            if (!rcvr_datablock(buff, 512)) break;
            buff += 512;
         } while (--count);
         send_cmd(CMD12, 0);           /* STOP_TRANSMISSION */
      }
   }
   deselect();

   return count ? RES_ERROR : RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write (
   uint8_t drv,                /* Physical drive nmuber (0) */
   const uint8_t far *buff, /* Pointer to the data to be written */
   uint32_t sector,            /* Start sector number (LBA) */
   uint16_t count               /* Sector count (1..128) */
)
{
   DRESULT dr = disk_result(drv);
   if (dr != RES_OK) return dr;

   if (!(CardType & CT_BLOCK)) sector = uint32_tLSHIFT(sector,9);   /* Convert LBA to byte address if needed */
   
   if (count == 1) { /* Single block write */
      if ((send_cmd(CMD24, sector) == 0)  /* WRITE_BLOCK */
         && xmit_datablock(buff, 0xFE))
         count = 0;
   }
   else {            /* Multiple block write */
      if (CardType & CT_SDC) send_cmd(ACMD23, count); 
      if (send_cmd(CMD25, sector) == 0) { /* WRITE_MULTIPLE_BLOCK */
         do {
            if (!xmit_datablock(buff, 0xFC)) break;
            buff += 512;
         } while (--count);
         if (!xmit_datablock(0, 0xFD)) /* STOP_TRAN token */
            count = 1;
         if (!wait_ready()) count = 1;   /* Wait for card to write */
      }
   }
   deselect();

   return count ? RES_ERROR : RES_OK;
}


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
   uint8_t drv,             /* Physical drive nmuber (0) */
   uint8_t ctrl,            /* Control code */
   void far *buff     /* Buffer to send/receive control data */
)
{
   DRESULT res;
   uint8_t n, csd[16];
   uint32_t cs;
   DRESULT dr = disk_result(drv);
   if (dr != RES_OK) return dr;

   res = RES_ERROR;
   switch (ctrl) {
      case CTRL_SYNC :     /* Make sure that no pending write process */
         if (select()) res = RES_OK;
         break;

      case GET_SECTOR_COUNT : /* Get number of sectors on the disk (uint32_t) */
         if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
            if ((csd[0] >> 6) == 1) {  /* SDC ver 2.00 */
               cs = csd[9] + ((uint16_t)csd[8] << 8) + ((uint32_t)(csd[7] & 63) << 16) + 1;
               *(uint32_t far *)buff = uint32_tLSHIFT(cs,10);
            } else {             /* SDC ver 1.XX or MMC */
               n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
               cs = (csd[8] >> 6) + ((uint16_t)csd[7] << 2) + ((uint16_t)(csd[6] & 3) << 10) + 1;
               *(uint32_t far *)buff = uint32_tLSHIFT(cs,n-9);
            }
            res = RES_OK;
         }
         break;

      case GET_BLOCK_SIZE :   /* Get erase block size in unit of sector (uint32_t) */
         *(uint32_t far *)buff = 128;
         res = RES_OK;
         break;

      default:
         res = RES_PARERR;
   }

   deselect();

   return res;
}


