#
# Template for writing DOS device drivers in Open Watcom C
#
# Copyright (C) 2022, Eduardo Casino (mail@eduardocasino.es)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA  02110-1301, USA.
#

!define USE_INTERNAL_STACK

CC = wcc
AS = wasm
LD = wlink
RM = rm -f
CFLAGS  = -0 -bt=dos -ms -q -s -osh -za99
ASFLAGS = -bt=DOS -zq -mt -0
LDFLAGS =	SYSTEM dos &
			ORDER clname HEADER clname DATA clname CODE clname BSS clname INIT &
			DISABLE 1014 OPTION QUIET, STATICS, MAP=parap-sd.map &
			LIBPATH /Users/pauldevine/projects/rel/lib286/dos LIBRARY clibs.lib

!ifdef USE_INTERNAL_STACK
CFLAGS += -DUSE_INTERNAL_STACK -DSTACK_SIZE=4096
!else
CFLAGS += -zu
!endif

TARGET = parapsd.sys

OBJ =	cstrtsys.obj template.obj cprint.obj sd.obj sdmm.obj devinit.obj

all : $(TARGET)

clean : .SYMBOLIC
	$(RM) $(OBJ) $(TARGET) *.map *.err

$(TARGET) : $(OBJ)
	$(LD) $(LDFLAGS) NAME $(TARGET) FILE {$(OBJ)}

devinit.obj : devinit.c .AUTODEPEND
	$(CC) $(CFLAGS) -nt=_INIT -nc=INIT -fo=$@ $<

.asm.obj : .AUTODEPEND
	$(AS) $(ASFLAGS) -fo=$@ $<

.c.obj : .AUTODEPEND
	$(CC) $(CFLAGS) -fo=$@ $<
