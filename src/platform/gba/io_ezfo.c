/*

  io_ezfo.c

  Hardware Routines for reading the EZ Flash Omega filesystem

*/

#include "io_ezfo.h"
#include <gba_dma.h>
#include <gba_interrupt.h>

#if 0
extern bool EWRAM_BSS _force_align;
#else
#define _force_align 1
#endif

// max size per transfer, aligned for dma transfer.
u8 ALIGN(2) EWRAM_BSS _ezio_align_buf[512 * 4];

// version of memcpy that resides in call location rather than rom.
#define memcpy_inline(_dst, _src, _len) do { \
    const u32 len = _len;                    \
    const u8* restrict src = _src;           \
    u8* restrict dst = _dst;                 \
    for (u32 i = 0; i < len; i++)            \
    {                                        \
        dst[i] = src[i];                     \
    }                                        \
} while (0)

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
static bool EWRAM_CODE Wait_SD_Response()
{
    for (u32 i = 0; i < 0x100000; i++)
    {
        const u16 res = SD_Response();
        if (res != 0xEEE1)
        {
            return true;
        }
    }

    return false;
}
// --------------------------------------------------------------------
static bool EWRAM_CODE Read_SD_sectors(u32 address, u16 count, u8* SDbuffer)
{
    SD_Enable();

    u32 times = 2;
    for (u16 i = 0; i < count; i += 4)
    {
        const u16 blocks = (count - i > 4) ? 4 : (count - i);
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
        const bool res = Wait_SD_Response();
        SD_Enable();

        if (!res)
        {
            times--;
            if(times)
            {
                delay(5000);
                goto read_again;
            }
        }

        if (_force_align && (u32)SDbuffer & 0x1)
        {
            dmaCopy((void*)0x9E00000, _ezio_align_buf, size);
            memcpy_inline(SDbuffer + i * 512, _ezio_align_buf, size);
        }
        else
        {
            dmaCopy((void*)0x9E00000, SDbuffer + i * 512, size);
        }
    }

    SD_Disable();
    return true;
}
// --------------------------------------------------------------------
static bool EWRAM_CODE Write_SD_sectors(u32 address, u16 count, const u8* SDbuffer)
{
    SD_Enable();
    SD_Read_state();

    for (u16 i = 0; i < count; i += 4)
    {
        const u16 blocks = (count - i > 4) ? 4 : (count - i);
        const u32 size = blocks * 512;

        if (_force_align && (u32)SDbuffer & 0x1)
        {
            memcpy_inline(_ezio_align_buf, SDbuffer + i * 512, size);
            dmaCopy(_ezio_align_buf, (void*)0x9E00000, size);
        }
        else
        {
            dmaCopy(SDbuffer + i * 512, (void*)0x9E00000, size);
        }

        *(vu16 *)0x9fe0000 = 0xd200;
        *(vu16 *)0x8000000 = 0x1500;
        *(vu16 *)0x8020000 = 0xd200;
        *(vu16 *)0x8040000 = 0x1500;
        *(vu16 *)0x9600000 = ((address+i)&0x0000FFFF);
        *(vu16 *)0x9620000 = ((address+i)&0xFFFF0000) >>16;
        *(vu16 *)0x9640000 = 0x8000+blocks;
        *(vu16 *)0x9fc0000 = 0x1500;

        const bool res = Wait_SD_Response();
        if (!res)
        {
            return false;
        }
    }

    delay(3000);
    SD_Disable();
    return true;
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
static bool EWRAM_BSS CHECKED_ROMPAGE;
static bool EWRAM_BSS ROMPAGE_RESULT;

// disables ime and re-enables it at the end of the scope.
#define SCOPED_IME(...)      \
    const u16 ime = REG_IME; \
    REG_IME = 0;             \
    __VA_ARGS__              \
    REG_IME = ime;

// in adition to the above, also sets the rompage and restores as the end of the scope.
#define SCOPED_ROMPAGE(func)            \
    SCOPED_IME(                         \
        SetRompage(ROMPAGE_BOOTLOADER); \
        func                            \
        SetRompage(ROMPAGE_ROM);        \
    )

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
    // check if we already
    if (CHECKED_ROMPAGE)
    {
        return ROMPAGE_RESULT;
    }

    // set so that we cache the result.
    CHECKED_ROMPAGE = true;

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
    for (u16 i = 0; i < S98WS512PE0_FLASH_PAGE_MAX; i++)
    {
        if (_EZFO_TestRompage(complement, i))
        {
            return true;
        }
    }

    // this literally shouldn't happen, contact me if you hit this!
    return false;
}

bool EWRAM_CODE EZFO_init(void)
{
    if (CHECKED_ROMPAGE)
    {
        return ROMPAGE_RESULT;
    }

    SCOPED_IME(
        const bool result = _EZFO_startUp();
    );

    return ROMPAGE_RESULT = result;
}

bool EWRAM_CODE EZFO_readSectors(u32 address, u32 count, void * buffer)
{
    if (!EZFO_init())
    {
        return false;
    }

    SCOPED_ROMPAGE(
        const bool result = Read_SD_sectors(address, count, buffer);
    );

    return result;
}

bool EWRAM_CODE EZFO_writeSectors(u32 address, u32 count, const void * buffer)
{
    if (!EZFO_init())
    {
        return false;
    }

    SCOPED_ROMPAGE(
        const bool result = Write_SD_sectors(address, count, buffer);
    );

    return result;
}

bool EWRAM_CODE EZF0_is_ezflash(void)
{
    return EZFO_init();
}

bool EWRAM_CODE EZF0_is_psram(void)
{
    return EZF0_is_ezflash() && ROMPAGE_ROM == ROMPAGE_PSRAM;
}

bool EWRAM_CODE EZF0_is_norflash(void)
{
    return EZF0_is_ezflash() && ROMPAGE_ROM < S98WS512PE0_FLASH_PAGE_MAX;
}

// set if we are in os mode.
static bool EWRAM_BSS g_mounted_os;
// the cached value of ime before disabling it to mount os mode.
static bool EWRAM_BSS g_cached_ime;

bool EWRAM_CODE EZF0_mount_os(void)
{
    if (!EZF0_is_ezflash())
    {
        return false;
    }

    // check if we are already in os mode.
    if (g_mounted_os)
    {
        return true;
    }

    g_mounted_os = true;
    g_cached_ime = REG_IME;
    REG_IME = 0;
    SetRompage(ROMPAGE_BOOTLOADER);

    return true;
}

bool EWRAM_CODE EZF0_mount_rom(void)
{
    if (!EZF0_is_ezflash())
    {
        return false;
    }

    // check if we are already in rom mode.
    if (!g_mounted_os)
    {
        return true;
    }

    g_mounted_os = false;
    SetRompage(ROMPAGE_ROM);
    REG_IME = g_cached_ime;

    return true;
}
