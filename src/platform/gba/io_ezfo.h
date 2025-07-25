/*

  io_ezfo.h

  Hardware Routines for reading the EZ Flash Omega filesystem

*/

#ifndef __IO_EZFO_H__
#define __IO_EZFO_H__

// 'EZFO'
#define DEVICE_TYPE_EZFO 0x4F465A45

#include <disc_io.h>
#include <gba_base.h>

// direct access to r/w sector functions to be used with fatfs.
bool EWRAM_CODE _EZFO_readSectors(u32 address, u32 count, void * buffer);
bool EWRAM_CODE _EZFO_writeSectors(u32 address, u32 count, const void * buffer);

// Export interface
extern const DISC_INTERFACE _io_ezfo;

#endif	// define __IO_EZFO_H__
