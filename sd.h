/* sd.h - SD card driver glue */

#ifndef _SD_H
#define _SD_H

#include <stdbool.h>

#include "template.h"

#define BLOCKSIZE 512

extern uint32_t partition_offset;

/* sd_initialize - establish two way communications with the drive */
bool sd_initialize (uint8_t unit, uint8_t partno, bpb far *bpb);

/* sd_read - read one 512 uint8_t logical block from the tape */
int sd_read (uint16_t, uint32_t, uint8_t far *, uint16_t count);

/* sd_write - write one 512 uint8_t logical block to the tape */
int sd_write (uint16_t, uint32_t, uint8_t far *, uint16_t count);

/* sd_media_check - check if media changed */
bool sd_media_check (uint8_t unit);

#endif
