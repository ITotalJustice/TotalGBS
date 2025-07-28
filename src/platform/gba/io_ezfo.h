/*

  io_ezfo.h

  Hardware Routines for reading the EZ Flash Omega filesystem

*/

#ifndef __IO_EZFO_H__
#define __IO_EZFO_H__

#include <gba_base.h>

// returns true if this is a ezo, implicitly called below.
bool EWRAM_CODE EZFO_init(void);
// direct access to r/w sector functions to be used with fatfs.
bool EWRAM_CODE EZFO_readSectors(u32 address, u32 count, void * buffer);
bool EWRAM_CODE EZFO_writeSectors(u32 address, u32 count, const void * buffer);

// returns true if this is an ezflash
bool EWRAM_CODE EZF0_is_ezflash(void);
// returns true if rom is loaded into psram.
bool EWRAM_CODE EZF0_is_psram(void);
// returns true if rom is loaded into nor flash.
bool EWRAM_CODE EZF0_is_norflash(void);

// allows for r/w access to psram and norflash, disables interrupts.
bool EWRAM_CODE EZF0_mount_os(void);
// remounts the rom, located in either psram or norflash, re-enables interrupts.
bool EWRAM_CODE EZF0_mount_rom(void);

#endif	// define __IO_EZFO_H__
