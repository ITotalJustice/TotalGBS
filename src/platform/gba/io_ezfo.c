/*

  io_ezfo.c

  Hardware Routines for reading the EZ Flash Omega filesystem

*/

#include "io_ezfo.h"
#include <gba_dma.h>
#include <gba_interrupt.h>
#include <string.h>

static u8 ALIGN(4) EWRAM_BSS g_buf[512 * 4];

// SOURCE: https://github.com/ez-flash/omega-de-kernel/blob/main/source/Ezcard_OP.c
static void EWRAM_CODE delay(u32 R0)
{
    int volatile i;

    for ( i = R0; i; --i );
    return;
}
// --------------------------------------------------------------------
static void EWRAM_CODE SetSDControl(u16  control)
{
    *(vu16 *)0x9fe0000 = 0xd200;
    *(vu16 *)0x8000000 = 0x1500;
    *(vu16 *)0x8020000 = 0xd200;
    *(vu16 *)0x8040000 = 0x1500;
    *(vu16 *)0x9400000 = control;
    *(vu16 *)0x9fc0000 = 0x1500;
}
// --------------------------------------------------------------------
static void EWRAM_CODE SD_Enable(void)
{
    SetSDControl(1);
}
// --------------------------------------------------------------------
static void EWRAM_CODE SD_Read_state(void)
{
    SetSDControl(3);
}
// --------------------------------------------------------------------
static void EWRAM_CODE SD_Disable(void)
{
    SetSDControl(0);
}
// --------------------------------------------------------------------
static u16 EWRAM_CODE SD_Response(void)
{
    return *(vu16 *)0x9E00000;
}
// --------------------------------------------------------------------
static u32 EWRAM_CODE Wait_SD_Response()
{
    vu16 res;
    u32 count=0;
    while(1)
    {
        res = SD_Response();
        if(res != 0xEEE1)
        {
            return 0;
        }

        count++;
        if(count>0x100000)
        {
            //DEBUG_printf("time out %x",res);
            //wait_btn();
            return 1;
        }
    }
}
// --------------------------------------------------------------------
static u32 EWRAM_CODE Read_SD_sectors(u32 address,u16 count,u8* SDbuffer)
{
    SD_Enable();

    u8* wbuf = SDbuffer;
    if ((u32)SDbuffer & 0x1) {
        wbuf = g_buf;
    }

    u16 i;
    u16 blocks;
    u32 res;
    u32 times=2;
    for(i=0;i<count;i+=4)
    {
        blocks = (count-i>4)?4:(count-i);
        const u32 size = blocks * 512;

    read_again:
        *(vu16 *)0x9fe0000 = 0xd200;
        *(vu16 *)0x8000000 = 0x1500;
        *(vu16 *)0x8020000 = 0xd200;
        *(vu16 *)0x8040000 = 0x1500;
        *(vu16 *)0x9600000 = ((address+i)&0x0000FFFF) ;
        *(vu16 *)0x9620000 = ((address+i)&0xFFFF0000) >>16;
        *(vu16 *)0x9640000 = blocks;
        *(vu16 *)0x9fc0000 = 0x1500;
        SD_Read_state();
        res = Wait_SD_Response();
        SD_Enable();
        if(res==1)
        {
            times--;
            if(times)
            {
                delay(5000);
                goto read_again;
            }
        }

        dmaCopy((void*)0x9E00000, wbuf, size);

        if ((u32)SDbuffer & 0x1) {
            // don't call memcpy because we have no rom to jump to!
            for (u32 j = 0; j < size; j++) {
                SDbuffer[j+i*512] = g_buf[j];
            }
        } else {
            wbuf += size;
        }
    }
    SD_Disable();
    return 0;
}
// --------------------------------------------------------------------
static u32 EWRAM_CODE Write_SD_sectors(u32 address,u16 count, const u8* SDbuffer)
{
    SD_Enable();
    SD_Read_state();
    u16 i;
    u16 blocks;
    u32 res;
    for(i=0;i<count;i+=4)
    {
        blocks = (count-i>4)?4:(count-i);
        const u32 size = blocks * 512;

        const u8* rbuf = SDbuffer + i*512;
        if ((u32)SDbuffer & 0x1) {
            rbuf = g_buf;
            // don't call memcpy because we have no rom to jump to!
            for (u32 j = 0; j < size; j++) {
                g_buf[j] = SDbuffer[j+i*512];
            }
        }

        dmaCopy(rbuf,(void*)0x9E00000, size);
        *(vu16 *)0x9fe0000 = 0xd200;
        *(vu16 *)0x8000000 = 0x1500;
        *(vu16 *)0x8020000 = 0xd200;
        *(vu16 *)0x8040000 = 0x1500;
        *(vu16 *)0x9600000 = ((address+i)&0x0000FFFF);
        *(vu16 *)0x9620000 = ((address+i)&0xFFFF0000) >>16;
        *(vu16 *)0x9640000 = 0x8000+blocks;
        *(vu16 *)0x9fc0000 = 0x1500;

        res = Wait_SD_Response();
        if(res==1)
            return 1;
    }
    delay(3000);
    SD_Disable();
    return 0;
}
// --------------------------------------------------------------------
static void EWRAM_CODE SetRompage(u16 page)
{
    *(vu16 *)0x9fe0000 = 0xd200;
    *(vu16 *)0x8000000 = 0x1500;
    *(vu16 *)0x8020000 = 0xd200;
    *(vu16 *)0x8040000 = 0x1500;
    *(vu16 *)0x9880000 = page;//C4
    *(vu16 *)0x9fc0000 = 0x1500;
}
// --------------------------------------------------------------------

#define ROMPAGE_BOOTLOADER 0x8000
#define ROMPAGE_PSRAM 0x200
#define S98WS512PE0_FLASH_PAGE_MAX 0x200
#define ROM_HEADER_CHECKSUM *(vu16*)(0x8000000 + 188)

static u16 EWRAM_BSS ROMPAGE_ROM;

// returns true if the data is a match
static bool EWRAM_CODE _EZFO_TestRompage(u16 wanted, u16 page)
{
    SetRompage(page);
    if (wanted == ROM_HEADER_CHECKSUM)
    {
        ROMPAGE_ROM = page;
        return true;
    }
    return false;
}

static bool EWRAM_CODE _EZFO_startUp(void)
{
    const u16 complement = ROM_HEADER_CHECKSUM;

    // unmap rom, if the data matches, then this is not an ezflash
    if (_EZFO_TestRompage(complement, ROMPAGE_BOOTLOADER))
    {
        return false;
    }

    // find where the rom is mapped, try psram first
    if (_EZFO_TestRompage(complement, ROMPAGE_PSRAM))
    {
        return true;
    }

    // try and find it within norflash, test each 1MiB page (512 pages)
    for (int i = 0; i < S98WS512PE0_FLASH_PAGE_MAX; i++)
    {
        if (_EZFO_TestRompage(complement, i))
        {
            return true;
        }
    }

    // this literally shouldn't happen, contact me if you hit this!
    return false;
}

bool EWRAM_CODE _EZFO_readSectors(u32 address, u32 count, void * buffer)
{
    const u16 ime = REG_IME;
    REG_IME = 0;
    SetRompage(ROMPAGE_BOOTLOADER);
    const u32 result = Read_SD_sectors(address, count, buffer);
    SetRompage(ROMPAGE_ROM);
    REG_IME = ime;
    return result == 0;
}

bool EWRAM_CODE _EZFO_writeSectors(u32 address, u32 count, const void * buffer)
{
    const u16 ime = REG_IME;
    REG_IME = 0;
    SetRompage(ROMPAGE_BOOTLOADER);
    const u32 result = Write_SD_sectors(address, count, buffer);
    SetRompage(ROMPAGE_ROM);
    REG_IME = ime;
    return result == 0;
}

static bool _EZFO_nop(void)
{
    return true;
}

const DISC_INTERFACE _io_ezfo = {
    DEVICE_TYPE_EZFO,
    FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_SLOT_GBA,
    ( FN_MEDIUM_STARTUP )&_EZFO_startUp,
    ( FN_MEDIUM_ISINSERTED )&_EZFO_nop,
    ( FN_MEDIUM_READSECTORS )&_EZFO_readSectors,
    ( FN_MEDIUM_WRITESECTORS )&_EZFO_writeSectors,
    ( FN_MEDIUM_CLEARSTATUS )&_EZFO_nop,
    ( FN_MEDIUM_SHUTDOWN )&_EZFO_nop
};
