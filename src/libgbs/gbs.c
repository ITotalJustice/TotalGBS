#include "gbs.h"
#include <gb_apu.h>
#include <scheduler.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef __NDS__
	#include <nds.h>
	#define FAST_TABLE DTCM_DATA
	#define FAST_CODE
	#define FORCE_INLINE inline __attribute__((always_inline))
#elif defined(__GBA__)
	#include <gba.h>
    #define LR35902_NO_CYCLES
	#define FAST_TABLE
	#define FAST_CODE IWRAM_CODE
	#define FORCE_INLINE inline __attribute__((always_inline))
#else
    #define FAST_TABLE
	#define FAST_CODE
    #define FORCE_INLINE
#endif

#define LR35902_FAST_TABLE FAST_TABLE
#define LR35902_FAST_CODE FAST_CODE
#define LR35902_FORCE_INLINE FORCE_INLINE
#define LR35902_STATIC
#define LR35902_ON_HALT
#define LR35902_ON_STOP
#define LR35902_BUILTIN_INTERRUTS
#define LR35902_IMPLEMENTATION
#include <LR35902.h>

#if defined(__has_builtin)
    #define HAS_BUILTIN(x) __has_builtin(x)
#else
    #if defined(__GNUC__)
        #define HAS_BUILTIN(x) (1)
    #else
        #define HAS_BUILTIN(x) (0)
    #endif
#endif // __has_builtin

#if HAS_BUILTIN(__builtin_expect)
    #define LIKELY(c) (__builtin_expect(c,1))
    #define UNLIKELY(c) (__builtin_expect(c,0))
#else
    #define LIKELY(c) ((c))
    #define UNLIKELY(c) ((c))
#endif // __has_builtin(__builtin_expect)

#ifndef GBS_LOGS
    #define GBS_LOGS 0
#endif

#if GBS_LOGS
    #include <stdio.h>
    #ifdef __GBA__
        #define LOGI(...) iprintf(__VA_ARGS__)
        #define LOGE(...) iprintf(__VA_ARGS__)
    #else
        #define LOGI(...) printf(__VA_ARGS__)
        #define LOGE(...) fprintf(stderr, __VA_ARGS__)
    #endif
#else
    #define LOGI(...)
    #define LOGE(...)
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// the values are representative of the IE/IF flag bits.
enum GbsTimingType
{
    GbsTimingType_Vblank = 0x1,
    GbsTimingType_Timer = 0x4,
    GbsTimingType_Ugetab = GbsTimingType_Vblank | GbsTimingType_Timer,
};

struct MemIo
{
    const uint8_t* data;
    size_t size;
};

struct GbsHeader
{
    uint8_t magic[3];
    uint8_t version;
    uint8_t number_of_songs;
    uint8_t first_song;
    uint16_t load_address;
    uint16_t init_address;
    uint16_t play_address;
    uint16_t stack_pointer;
    uint8_t timer_modulo;
    uint8_t timer_control;
    char title_string[32];
    char author_string[32];
    char copyright_string[32];
};

struct Mem
{
    const uint8_t* rmap[0x10];
    uint8_t* wmap[0x10];

    // io regs
    uint8_t tima;
    uint8_t tma;
    uint8_t tac;
    uint8_t key1;

    uint8_t rom_bank;
    uint8_t max_rom_bank;

    uint8_t* bank0; // first bank
    uint8_t* bank1; // second bank
    uint8_t* bankx; // last bank
    uint8_t* sram;
    uint8_t* wram;
    uint8_t* hram;
};

struct Gbs
{
    struct LR35902 cpu;
    struct Mem mem;
    struct Scheduler scheduler;
    GbApu* apu;
    struct GbsIo io;
    struct GbsHeader header;

    // local copy avoids alloc
    struct MemIo memio;

    uint8_t song;
    bool waiting_vsync;
    bool end_frame;
};

enum { FRAME_SEQUENCER_CLOCK = 8192 };
enum { VSYNC_CLOCK = 70224 };

enum Event
{
    Event_FRAME_SEQUENCER,
    Event_VSYNC,
    Event_TIMER,
    Event_END_FRAME,
    Event_MAX,
};

static const uint16_t TAC_FREQ[4] = { 1024, 16, 64, 256 };
static const uint8_t GBS_MAGIC[3] = {'G', 'B', 'S' };
#if GBS_LOGS
static const uint32_t timer_rate[4] = { 4096, 262144, 65536, 16384 };
static const char* timer_type[] = { "vblank", "timer" };
static const char* timer_clock[] = { "normal", "double speed" };
#endif

static enum GbsTimingType get_timing_type(const Gbs* gbs)
{
    if (gbs->header.timer_control & 0x4)
    {
        if (gbs->header.timer_control & 0x40)
        {
            return GbsTimingType_Ugetab;
        }
        return GbsTimingType_Timer;
    }
    else
    {
        return GbsTimingType_Vblank;
    }
}

#ifndef __GBA__
static void schedule_vsync_event(Gbs* gbs, unsigned late);
static void schedule_timer_event(Gbs* gbs, unsigned late);

static void on_timeout_event(void* user, unsigned id, unsigned late)
{
    Gbs* gbs = user;
    apu_update_timestamp(gbs->apu, -SCHEDULER_TIMEOUT_CYCLES);
    scheduler_reset_event(&gbs->scheduler);
    scheduler_add_absolute(&gbs->scheduler, id, SCHEDULER_TIMEOUT_CYCLES, on_timeout_event, user);
}

static void on_fs_event(void* user, unsigned id, unsigned late)
{
    Gbs* gbs = user;
    apu_frame_sequencer_clock(gbs->apu, scheduler_get_ticks(&gbs->scheduler) - late);
    scheduler_add(&gbs->scheduler, id, FRAME_SEQUENCER_CLOCK - late, on_fs_event, user);
}

static void on_vsync_event(void* user, unsigned id, unsigned late)
{
    Gbs* gbs = user;
    gbs->waiting_vsync = false;
    gbs->cpu.IF |= 0x1;
    schedule_vsync_event(gbs, late);
}

static void on_timer_event(void* user, unsigned id, unsigned late)
{
    Gbs* gbs = user;
    if (gbs->mem.tac & 0x4) // was commented out for some reasons
    {
        gbs->mem.tima++;
        if (!gbs->mem.tima)
        {
            gbs->mem.tima = gbs->mem.tma;
            gbs->cpu.IF |= 0x4;
            gbs->waiting_vsync = false;
        }
    }
    schedule_timer_event(gbs, late);
}

static void on_endframe_event(void* user, unsigned id, unsigned late)
{
    Gbs* gbs = user;
    gbs->end_frame = true;
}

static void schedule_vsync_event(Gbs* gbs, unsigned late)
{
    scheduler_add(&gbs->scheduler, Event_VSYNC, VSYNC_CLOCK - late, on_vsync_event, gbs);
}

static void schedule_timer_event(Gbs* gbs, unsigned late)
{
    const bool double_speed = gbs->mem.key1 & 0x80;
    const unsigned freq = TAC_FREQ[gbs->mem.tac & 0x03] >> double_speed;
    while (late >= freq)
    {
        // todo:
        late -= freq;
    }
    assert(!scheduler_has_event(&gbs->scheduler, Event_TIMER));
    scheduler_add(&gbs->scheduler, Event_TIMER, freq - late, on_timer_event, gbs);
}

static void LR35902_on_halt(void* user)
{
    Gbs* gbs = user;
    assert(gbs->cpu.SP == gbs->header.stack_pointer);
    gbs->waiting_vsync = true;
}
#else
static irqMASK EWRAM_BSS irq_timeout = false;
static bool EWRAM_BSS timer_int_happened = false;
static bool EWRAM_BSS vblank_int_happened = false;

static void ARM_CODE on_timer_interrupt(void)
{
    timer_int_happened = true;
}

static void ARM_CODE on_vblank_interrupt(void)
{
    vblank_int_happened = true;
}

static void on_fs_event(void* user, unsigned id, unsigned late)
{
}

static void schedule_vsync_event(Gbs* gbs, unsigned late)
{
    irq_timeout |= IRQ_VBLANK;
}

static void schedule_timer_event(Gbs* gbs, unsigned late)
{
    irq_timeout |= IRQ_TIMER1;
}

static void LR35902_on_halt(void* user)
{
    Gbs* gbs = user;
    assert(gbs->cpu.SP == gbs->header.stack_pointer);

    extern bool gbs_on_breakpoint(void);

    if UNLIKELY(gbs_on_breakpoint())
    {
        return;
    }

    // wait until timeout happens
    // this is basically hle-ing the interrupts
    if (!vblank_int_happened && !timer_int_happened)
    {
        IntrWait(0, irq_timeout);
    }

    if (vblank_int_happened)
    {
        vblank_int_happened = false;
        gbs->cpu.IF |= 0x1;
    }
    if (timer_int_happened)
    {
        timer_int_happened = false;
        gbs->cpu.IF |= 0x4;
    }
}
#endif

static void LR35902_on_stop(void* user)
{
    Gbs* gbs = user;
    // only set if speed-switch is requested
    if (gbs->mem.key1 & 0x1)
    {
        // switch speed state.
        const uint8_t old_state = !(gbs->mem.key1 & 0x80);
        // this clears bit-0 and sets bit-7 to whether we are in double or normal speed mode.
        gbs->mem.key1 = (old_state << 7);
    }
}

static size_t io_mem_read(void* user, void* dst, size_t size, size_t addr)
{
    const struct MemIo* io = user;
    const size_t rs = addr + size > io->size ? io->size - addr : size;
    memcpy(dst, io->data + addr, rs);
    return rs;
}

static size_t io_mem_size(void* user)
{
    const struct MemIo* io = user;
    return io->size;
}

static const uint8_t* io_mem_pointer(void* user, size_t addr, uint8_t bank)
{
    const struct MemIo* io = user;
    assert(io->size > addr && "oob pointer access");
    assert(io->size >= addr + GBS_BANK_SIZE && "pointer access exeeds array size");
    return io->data + addr;
}

static const struct GbsIo MEMIO = {
    .user = NULL,
    .read = io_mem_read,
    .size = io_mem_size,
    .pointer = io_mem_pointer,
};

static void get_bank_addr_and_size(const Gbs* gbs, uint8_t bank, size_t* addr_out, size_t* size_out, size_t* off_out)
{
    size_t addr, size, off;
    const uint8_t load_bank = gbs->header.load_address / GBS_BANK_SIZE;
    const uint16_t mod = gbs->header.load_address % GBS_BANK_SIZE;

    // is empty.
    if (bank < load_bank)
    {
        addr = 0;
        size = 0;
        off = 0;
    }
    else if (bank == load_bank)
    {
        addr = sizeof(gbs->header);
        size = GBS_BANK_SIZE - mod;
        off = mod;
    }
    else
    {
        if (load_bank)
        {
            bank--;
        }
        addr = sizeof(gbs->header) + GBS_BANK_SIZE - mod + GBS_BANK_SIZE * (bank - 1);
        size = GBS_BANK_SIZE;
        off = 0;
    }

    if (off_out)
    {
        *off_out = off;
    }

    if (addr_out)
    {
        *addr_out = addr;
    }

    if (size_out)
    {
        *size_out = size;
    }
}

static const uint8_t* get_pointer_internal(const Gbs* gbs, uint8_t bank)
{
    if (!bank)
    {
        return gbs->mem.bank0;
    }
    else if (bank == 1)
    {
        return gbs->mem.bank1;
    }
    else if (bank == gbs->mem.max_rom_bank - 1)
    {
        return gbs->mem.bankx;
    }
    else
    {
        size_t addr;
        get_bank_addr_and_size(gbs, bank, &addr, NULL, NULL);
        return gbs->io.pointer(gbs->io.user, addr, bank);
    }
}

static void set_rom_bank(Gbs* gbs, uint8_t bank)
{
    assert(bank);
    gbs->mem.rom_bank = bank % gbs->mem.max_rom_bank;
    // gbs->mem.rom_bank = gbs->mem.rom_bank ? gbs->mem.rom_bank : 1;

    const uint8_t* ptr = get_pointer_internal(gbs, gbs->mem.rom_bank);
    gbs->mem.rmap[0x4] = ptr + 0x1000 * 0;
    gbs->mem.rmap[0x5] = ptr + 0x1000 * 1;
    gbs->mem.rmap[0x6] = ptr + 0x1000 * 2;
    gbs->mem.rmap[0x7] = ptr + 0x1000 * 3;
}

static void setup_rwmap(Gbs* gbs)
{
    gbs->mem.rmap[0x0] = gbs->mem.bank0 + 0x1000 * 0;
    gbs->mem.rmap[0x1] = gbs->mem.bank0 + 0x1000 * 1;
    gbs->mem.rmap[0x2] = gbs->mem.bank0 + 0x1000 * 2;
    gbs->mem.rmap[0x3] = gbs->mem.bank0 + 0x1000 * 3;

    set_rom_bank(gbs, 1);

    // vram reads and writes are ignored, so we return whatever is in bank0
    gbs->mem.rmap[0x8] = gbs->mem.bank0 + 0x1000 * 0;
    gbs->mem.rmap[0x9] = gbs->mem.bank0 + 0x1000 * 1;

    // sram
    gbs->mem.rmap[0xA] = gbs->mem.wmap[0xA] = gbs->mem.sram + 0x1000 * 0;
    gbs->mem.rmap[0xB] = gbs->mem.wmap[0xB] = gbs->mem.sram + 0x1000 * 1;

    // wram and mirrors
    gbs->mem.rmap[0xE] = gbs->mem.rmap[0xC] = gbs->mem.wmap[0xE] = gbs->mem.wmap[0xC] = gbs->mem.wram + 0x1000 * 0;
    gbs->mem.rmap[0xF] = gbs->mem.rmap[0xD] = gbs->mem.wmap[0xF] = gbs->mem.wmap[0xD] = gbs->mem.wram + 0x1000 * 1;
}

static uint8_t FAST_CODE io_read(Gbs* gbs, uint16_t addr)
{
    addr &= 0x7F;
    switch (addr)
    {
        #ifndef __GBA__
        case 0x05: return gbs->mem.tima;
        #else
        case 0x05: return REG_TM1CNT_L;
        #endif
        case 0x06: return gbs->mem.tma;
        case 0x07: return gbs->mem.tac;

        case 0x0F: return gbs->cpu.IF; // IF (interrupt)
        case 0x4D: return gbs->mem.key1; // key1 (speed switch)

        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x14: case 0x16: case 0x17: case 0x18:
        case 0x19: case 0x1A: case 0x1B: case 0x1C:
        case 0x1D: case 0x1E: case 0x20: case 0x21:
        case 0x22: case 0x23: case 0x24: case 0x25:
        case 0x26:
        case 0x30: case 0x31: case 0x32: case 0x33: // WAVE RAM
        case 0x34: case 0x35: case 0x36: case 0x37: // WAVE RAM
        case 0x38: case 0x39: case 0x3A: case 0x3B: // WAVE RAM
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: // WAVE RAM
            return apu_read_io(gbs->apu, addr, scheduler_get_ticks(&gbs->scheduler));
    }

    LOGE("addr: 0x%X 0x%X\n", addr);
    assert(!"unhandled io read");

    return 0xFF;
}

static void FAST_CODE io_write(Gbs* gbs, uint16_t addr, uint8_t value)
{
    addr &= 0x7F;
    switch (addr)
    {
        #ifndef __GBA__
        case 0x05:
            gbs->mem.tima = value;
            break;
        case 0x06:
            gbs->mem.tma = value;
            break;
        #else
        case 0x05:
            // disable timer
            REG_TM1CNT_H = 0;
            // immediately apply reload value
            REG_TM1CNT_L = 0xFF00 | value;
            // re-enable timer
            REG_TM1CNT_H = TIMER_START | TIMER_COUNT | TIMER_IRQ;
            // set correct reload value.
            REG_TM1CNT_L = gbs->mem.tma;
            break;
        case 0x06:
            // set reload value
            REG_TM1CNT_L = value;
            gbs->mem.tma = value;
            break;
        #endif
        case 0x07: {
            const uint8_t old_value = gbs->mem.tac;
            gbs->mem.tac = value;
            LOGI("updating tac: %X\n", value);
            if (value & 0x4 && !(old_value & 0x4))
            {
                schedule_timer_event(gbs, 0);
            }
            else if (!(value & 0x4) && old_value & 0x4)
            {
                scheduler_remove(&gbs->scheduler, Event_TIMER);
            }
        }   break;
        case 0x4D:// key1 (speed switch)
            gbs->mem.key1 |= value & 0x1;
            break;

        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x14: case 0x16: case 0x17: case 0x18:
        case 0x19: case 0x1A: case 0x1B: case 0x1C:
        case 0x1D: case 0x1E: case 0x20: case 0x21:
        case 0x22: case 0x23: case 0x24: case 0x25:
        case 0x26:
        case 0x30: case 0x31: case 0x32: case 0x33: // WAVE RAM
        case 0x34: case 0x35: case 0x36: case 0x37: // WAVE RAM
        case 0x38: case 0x39: case 0x3A: case 0x3B: // WAVE RAM
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: // WAVE RAM
            apu_write_io(gbs->apu, addr, value, scheduler_get_ticks(&gbs->scheduler));
            break;
    }
}

static uint8_t FAST_CODE slow_read(Gbs* gbs, uint16_t addr)
{
    if (addr >= 0xFF00 && addr <= 0xFF7F)
    {
        return io_read(gbs, addr);
    }
    // hram
    else if (addr >= 0xFF80 && addr <= 0xFFFE)
    {
        return gbs->mem.hram[addr & 0x7F];
    }
    else if (addr == 0xFFFF)
    {
        return gbs->cpu.IE;
    }
    else
    {
        LOGE("addr: 0x%X 0x%X\n", addr);
        assert(!"unhandled read");
    }

    return 0xFF;
}

uint8_t LR35902_read(void* user, uint16_t addr)
{
    Gbs* gbs = user;

    if LIKELY(addr < 0xFE00)
    {
        return gbs->mem.rmap[addr >> 12][addr & 0xFFF];
    }
    else
    {
        return slow_read(gbs, addr);
    }
}

static void FAST_CODE slow_write(Gbs* gbs, uint16_t addr, uint8_t value)
{
    // rom bank
    if (addr >= 0x2000 && addr <= 0x3FFF)
    {
        value &= 0x1F;
        gbs->mem.rom_bank &= ~0x1F;
        gbs->mem.rom_bank |= value ? value : 1;
        set_rom_bank(gbs, gbs->mem.rom_bank);
    }
    else if (addr >= 0x4000 && addr <= 0x5FFF)
    {
        value &= 0x3;
        gbs->mem.rom_bank &= ~(0x3 << 5);
        gbs->mem.rom_bank |= value << 5;
        set_rom_bank(gbs, gbs->mem.rom_bank);
        assert(value == 0x0 && "setting upper bits of rom bank!");
    }
    // todo banking mode select
    // needed for mapping banks $20 $40 $60
    // https://gbdev.io/pandocs/MBC1.html#60007fff--banking-mode-select-write-only

    // hram
    else if (addr >= 0xFF80 && addr <= 0xFFFE)
    {
        gbs->mem.hram[addr & 0x7F] = value;
    }
    else if (addr == 0xFFFF)
    {
        gbs->cpu.IE = value;
    }
    else
    {
        LOGE("addr: 0x%X 0x%X\n", addr, value);
        assert(!"unhandled write");
    }
}

void LR35902_write(void* user, uint16_t addr, uint8_t value)
{
    Gbs* gbs = user;

    if LIKELY(addr >= 0xA000 && addr <= 0xFDFF)
    {
        gbs->mem.wmap[addr >> 12][addr & 0xFFF] = value;
    }
    // io
    else if LIKELY(addr >= 0xFF00 && addr <= 0xFF7F)
    {
        io_write(gbs, addr, value);
    }
    else
    {
        slow_write(gbs, addr, value);
    }
}

static bool gbs_get_meta_from_header(const struct GbsHeader* h, struct GbsMeta* meta)
{
    // copy over data, along with null terminated strings
    meta->first_song = h->first_song;
    meta->max_song = h->number_of_songs;
    strncpy(meta->title_string, h->title_string, sizeof(meta->title_string) - 1);
    strncpy(meta->author_string, h->author_string, sizeof(meta->author_string) - 1);
    strncpy(meta->copyright_string, h->copyright_string, sizeof(meta->copyright_string) - 1);
    return true;
}

static bool gbs_verify_and_fix_header(struct GbsHeader* h)
{
    // verify magic.
    if (memcmp(h->magic, GBS_MAGIC, sizeof(GBS_MAGIC)))
    {
        LOGE("bad magic\n");
        return false;
    }

    // verify it's version 1.
    if (h->version != 1)
    {
        LOGE("bad version: %u\n", h->version);
        return false;
    }

    // ensure that these start from 1.
    if (!h->first_song || !h->number_of_songs)
    {
        LOGE("bad song index\n");
        return false;
    }

    // max range is 0x7FFF (bank0 - bank1).
    if (h->load_address > 0x7FFF || h->init_address > 0x7FFF || h->play_address > 0x7FFF)
    {
        LOGE("bad upper bounds\n");
        return false;
    }

    // make zero based.
    h->first_song--;

    // byteswap short values to work on any endian.
    const uint8_t* rp = (const uint8_t*)h;
    h->load_address = (rp[6]) | (rp[7] << 8);
    h->init_address = (rp[8]) | (rp[9] << 8);
    h->play_address = (rp[10]) | (rp[11] << 8);
    h->stack_pointer = (rp[12]) | (rp[13] << 8);

    return true;
}

static void gbs_free_mem(Gbs* gbs)
{
    if (gbs->mem.bank0)
    {
        free(gbs->mem.bank0);
        gbs->mem.bank0 = NULL;
    }
    if (gbs->mem.bank1)
    {
        free(gbs->mem.bank1);
        gbs->mem.bank1 = NULL;
    }
    if (gbs->mem.bankx)
    {
        free(gbs->mem.bankx);
        gbs->mem.bankx = NULL;
    }
    if (gbs->mem.sram)
    {
        free(gbs->mem.sram);
        gbs->mem.sram = NULL;
    }
    if (gbs->mem.wram)
    {
        free(gbs->mem.wram);
        gbs->mem.wram = NULL;
    }
    if (gbs->mem.hram)
    {
        free(gbs->mem.hram);
        gbs->mem.hram = NULL;
    }

    memset(&gbs->io, 0, sizeof(gbs->io));
}

Gbs* gbs_init(double sample_rate)
{
#if defined(__GBA__)
    static Gbs __attribute__((section(".bss"))) _gbs; // iwram_bss
    Gbs* gbs = &_gbs;
#else
    Gbs* gbs = calloc(1, sizeof(*gbs));
#endif
    if (!gbs)
    {
        goto fail;
    }

    gbs->cpu.userdata = gbs;
    if (scheduler_init(&gbs->scheduler, Event_MAX)) {
        goto fail;
    }

    if (!(gbs->apu = apu_init(GbApuClockRate_DMG, sample_rate))) {
        goto fail;
    }

    apu_set_highpass_filter(gbs->apu, GbApuFilter_DMG, 4194304, sample_rate);

    return gbs;

fail:
    gbs_quit(gbs);
    return NULL;
}

void gbs_quit(Gbs* gbs)
{
    if (gbs)
    {
        scheduler_quit(&gbs->scheduler);
        apu_quit(gbs->apu);
        gbs_free_mem(gbs);
    #if !defined(__GBA__)
        free(gbs);
    #endif
    }
}

void gbs_reset(Gbs* gbs, uint8_t song)
{
    gbs->song = song;

    LR35902_reset_cgb(&gbs->cpu);
    apu_reset(gbs->apu, GbApuType_CGB);
    gbs->waiting_vsync = false;
    #ifndef __GBA__
    scheduler_reset(&gbs->scheduler, 0, on_timeout_event, gbs);
    #else
    irq_timeout = 0;
    #endif

    gbs->mem.tima = 0;
    gbs->mem.tma = gbs->header.timer_modulo;
    gbs->mem.tac = gbs->header.timer_control & 0x7F;
    gbs->mem.key1 = gbs->header.timer_control & 0x80;

    LOGI("reset called, playing song: %u\n", song);

    // setup rom banks
    memset(gbs->mem.sram, 0, 0x2000);
    memset(gbs->mem.wram, 0, 0x2000);
    memset(gbs->mem.hram, 0, 0x80);
    setup_rwmap(gbs);

    // the area at 0x100-0x150 is reserved for the rom header
    // due to this, we have this entire area free for use.
    uint16_t addr = 0x100;
    gbs->mem.bank0[addr++] = 0xF3; // DI
    gbs->mem.bank0[addr++] = 0x31; // LD_SP_u16
    gbs->mem.bank0[addr++] = gbs->header.stack_pointer & 0xFF;
    gbs->mem.bank0[addr++] = gbs->header.stack_pointer >> 8;
    gbs->mem.bank0[addr++] = 0x3E; // LD_r_u8 (REG_A)
    gbs->mem.bank0[addr++] = get_timing_type(gbs); // REG_A = 0x05 (Vblank+Timer)
    gbs->mem.bank0[addr++] = 0xE0; // LD_FFu8_A (REG_A)
    gbs->mem.bank0[addr++] = 0xFF; // IE
    gbs->mem.bank0[addr++] = 0x3E; // LD_r_u8 (REG_A)
    gbs->mem.bank0[addr++] = song; // REG_A = song
    gbs->mem.bank0[addr++] = 0xCD; // call
    gbs->mem.bank0[addr++] = gbs->header.init_address & 0xFF;
    gbs->mem.bank0[addr++] = gbs->header.init_address >> 8;
    gbs->mem.bank0[addr++] = 0xFB; // EI
    gbs->mem.bank0[addr++] = 0x00; // nop (EI is delayed)
    // halt loop
    gbs->mem.bank0[addr++] = 0x76; // halt
    gbs->mem.bank0[addr++] = 0x18; // JR
    gbs->mem.bank0[addr++] = -3; // loop

    apu_write_io(gbs->apu, 0xFF26, 0x00, 0);
    apu_write_io(gbs->apu, 0xFF26, 0xF1, 0);
    apu_write_io(gbs->apu, 0xFF10, 0x80, 0);
    apu_write_io(gbs->apu, 0xFF11, 0xBF, 0);
    apu_write_io(gbs->apu, 0xFF12, 0xF3, 0);
    apu_write_io(gbs->apu, 0xFF13, 0xFF, 0);
    apu_write_io(gbs->apu, 0xFF14, 0xBF, 0);
    apu_write_io(gbs->apu, 0xFF16, 0x3F, 0);
    apu_write_io(gbs->apu, 0xFF17, 0x00, 0);
    apu_write_io(gbs->apu, 0xFF18, 0xFF, 0);
    apu_write_io(gbs->apu, 0xFF19, 0xBF, 0);
    apu_write_io(gbs->apu, 0xFF1A, 0x7F, 0);
    apu_write_io(gbs->apu, 0xFF1B, 0xFF, 0);
    apu_write_io(gbs->apu, 0xFF1C, 0x9F, 0);
    apu_write_io(gbs->apu, 0xFF1D, 0xFF, 0);
    apu_write_io(gbs->apu, 0xFF1E, 0xBF, 0);
    apu_write_io(gbs->apu, 0xFF20, 0xFF, 0);
    apu_write_io(gbs->apu, 0xFF21, 0x00, 0);
    apu_write_io(gbs->apu, 0xFF22, 0x00, 0);
    apu_write_io(gbs->apu, 0xFF23, 0xBF, 0);
    apu_write_io(gbs->apu, 0xFF24, 0x77, 0);
    apu_write_io(gbs->apu, 0xFF25, 0xF3, 0);

    scheduler_add(&gbs->scheduler, Event_FRAME_SEQUENCER, FRAME_SEQUENCER_CLOCK, on_fs_event, gbs);

    switch (get_timing_type(gbs))
    {
        case GbsTimingType_Vblank:
            schedule_vsync_event(gbs, 0);
            break;
        case GbsTimingType_Timer:
            schedule_timer_event(gbs, 0);
            break;
        case GbsTimingType_Ugetab:
            schedule_vsync_event(gbs, 0);
            schedule_timer_event(gbs, 0);
            break;
    }

#ifdef __GBA__
    // reset timer
    REG_TM0CNT_H = 0;
    REG_TM1CNT_H = 0;

    if (get_timing_type(gbs) & GbsTimingType_Timer)
    {
        const bool double_speed = gbs->header.timer_control & 0x80;
        // set modulo (convert gb time to gba)
        REG_TM0CNT_L = -((16777216 / 4096) / (double_speed ? 2 : 1));
        REG_TM1CNT_L = 0xFF00 | gbs->header.timer_modulo;
        // enable timer
        REG_TM0CNT_H = TIMER_START;
        REG_TM1CNT_H = TIMER_START | TIMER_COUNT | TIMER_IRQ;
    }

    irqDisable(IRQ_VBLANK | IRQ_TIMER1);
    irqEnable(irq_timeout);
    irqSet(IRQ_VBLANK, on_vblank_interrupt);
    irqSet(IRQ_TIMER1, on_timer_interrupt);
    IntrWait(1, irq_timeout);
    timer_int_happened = false;
    vblank_int_happened = false;
#endif
}

bool gbs_validate_file_io(const struct GbsIo* io)
{
    if (!io)
    {
        return false;
    }

    char header[3];
    if (io->read(io->user, header, sizeof(header), 0))
    {
        return !memcmp(header, GBS_MAGIC, sizeof(GBS_MAGIC));
    }

    return false;
}

bool gbs_validate_file_mem(const void* data, size_t size)
{
    struct MemIo memio = { .data = data, .size = size };
    struct GbsIo io = MEMIO;
    io.user = &memio;

    return gbs_validate_file_io(&io);
}

bool gbs_load_io(Gbs* gbs, const struct GbsIo* io)
{
    if (!gbs)
    {
        goto fail;
    }

    gbs_free_mem(gbs);

    if (!io || !io->read || !io->size || !io->pointer)
    {
        goto fail;
    }

    const size_t gbs_size = io->size(io->user);
    if (!gbs_size)
    {
        LOGE("bad size\n");
        goto fail;
    }

    if (!io->read(io->user, &gbs->header, sizeof(gbs->header), 0))
    {
        LOGE("bad read1\n");
        goto fail;
    }

    if (!gbs_verify_and_fix_header(&gbs->header))
    {
        LOGE("wat\n");
        goto fail;
    }

    gbs->mem.bank0 = calloc(1, GBS_BANK_SIZE);
    gbs->mem.bank1 = calloc(1, GBS_BANK_SIZE);
    gbs->mem.bankx = calloc(1, GBS_BANK_SIZE);
    gbs->mem.sram = calloc(1, 0x2000);
    gbs->mem.wram = calloc(1, 0x2000);
    gbs->mem.hram = calloc(1, 0x80);
    if (!gbs->mem.bank0 || !gbs->mem.bank1 || !gbs->mem.bankx || !gbs->mem.sram || !gbs->mem.wram || !gbs->mem.hram)
    {
        LOGE("failed to allocate memory: %s:%d\n", __FUNCTION__, __LINE__);
        goto fail;
    }

    LOGI("timer_modulo: %u\n", gbs->header.timer_modulo);
    LOGI("timer_rate: %u Hz\n", timer_rate[gbs->header.timer_control & 0x3]);
    LOGI("timer_type: %s\n", (gbs->header.timer_control & 0x44) ? "VBlank/Timer (ugetab)" : timer_type[(gbs->header.timer_control >> 2) & 0x1]);
    LOGI("timer_clock: %s\n", timer_clock[gbs->header.timer_control >> 7]);

    LOGI("init_address: 0x%04X\n", gbs->header.init_address);
    LOGI("load_address: 0x%04X\n", gbs->header.load_address);
    LOGI("play_address: 0x%04X\n", gbs->header.play_address);

    const uint8_t load_bank = gbs->header.load_address / GBS_BANK_SIZE;
    size_t bankl_addr, bankl_size, bankl_off;
    get_bank_addr_and_size(gbs, load_bank, &bankl_addr, &bankl_size, &bankl_off);

    // this is the actual size of the gbs file.
    // const size_t corrected_gbs_size = gbs_size - bankl_off - sizeof(gbs->header) + GBS_BANK_SIZE;
    const size_t corrected_gbs_size = gbs_size - bankl_addr - bankl_size + GBS_BANK_SIZE;

    // set max rom bank
    gbs->mem.max_rom_bank = corrected_gbs_size / GBS_BANK_SIZE;
    if (corrected_gbs_size % GBS_BANK_SIZE)
    {
        gbs->mem.max_rom_bank++;
    }
    if (load_bank)
    {
        gbs->mem.max_rom_bank++;
    }

    // shouldn't exceed 127 banks.
    if (gbs->mem.max_rom_bank > 127)
    {
        LOGE("too many many banks: %u\n", gbs->mem.max_rom_bank);
        goto fail;
    }

    if (!load_bank)
    {
        // load bank0
        if (!io->read(io->user, gbs->mem.bank0 + bankl_off, bankl_size, bankl_addr))
        {
            LOGE("bad read bank0-l\n");
            goto fail;
        }

        // load bank1
        if (gbs_size - bankl_size)
        {
            size_t bank1_addr, bank1_size, bank1_off;
            get_bank_addr_and_size(gbs, 1, &bank1_addr, &bank1_size, &bank1_off);

            if (!io->read(io->user, gbs->mem.bank1 + bank1_off, bank1_size, bank1_addr))
            {
                LOGE("bad read bank1\n");
                goto fail;
            }
        }
    }
    else
    {
        // load bank1
        if (!io->read(io->user, gbs->mem.bank1 + bankl_off, bankl_size, bankl_addr))
        {
            LOGE("bad read bank1-l\n");
            goto fail;
        }
    }

    // read last bank into buffer. The reason for this is the last bank
    // doesn't need to be 16k, it can be as small as it needs to be.
    // the spec states that the bank should be padded with zeros in that case.
    size_t bankx_addr, bankx_size, bankx_off;
    get_bank_addr_and_size(gbs, gbs->mem.max_rom_bank - 1, &bankx_addr, &bankx_size, &bankx_off);
    if (!io->read(io->user, gbs->mem.bankx + bankx_off, bankx_size, bankx_addr))
    {
        LOGE("bad bankx read addr: %zu size: %zu\n", bankx_addr, bankx_size);
        goto fail;
    }

    // setup reset vectors
    for (unsigned i = 0; i <= 0x38; i += 8)
    {
        // rst is relative to the load address.
        const uint16_t addr = gbs->header.load_address + i;
        gbs->mem.bank0[i + 0] = 0xC3; // jp
        gbs->mem.bank0[i + 1] = addr & 0xFF;
        gbs->mem.bank0[i + 2] = addr >> 8;
    }

    // set default interrupt reset vectors
    for (unsigned i = 0x40; i <= 0x60; i += 8)
    {
        gbs->mem.bank0[i] = 0xD9; // reti
    }

    switch (get_timing_type(gbs))
    {
        case GbsTimingType_Vblank:
            gbs->mem.bank0[0x40 + 0] = 0xCD; // call
            gbs->mem.bank0[0x40 + 1] = gbs->header.play_address & 0xFF;
            gbs->mem.bank0[0x40 + 2] = gbs->header.play_address >> 8;
            gbs->mem.bank0[0x40 + 3] = 0xD9; // reti
            break;
        case GbsTimingType_Timer:
            gbs->mem.bank0[0x50 + 0] = 0xCD; // call
            gbs->mem.bank0[0x50 + 1] = gbs->header.play_address & 0xFF;
            gbs->mem.bank0[0x50 + 2] = gbs->header.play_address >> 8;
            gbs->mem.bank0[0x50 + 3] = 0xD9; // reti
            break;
        case GbsTimingType_Ugetab:
            // load at 0x40
            gbs->mem.bank0[0x40 + 0] = 0xCD; // call
            gbs->mem.bank0[0x40 + 1] = (gbs->header.load_address + 0x40) & 0xFF;
            gbs->mem.bank0[0x40 + 2] = (gbs->header.load_address + 0x40) >> 8;
            gbs->mem.bank0[0x40 + 3] = 0xD9; // reti
            // load at 0x48
            // YGO DDS proves that play_addr must be loading in timer intr
            // NOT in vblank intr, otherwise it will periodically crash!
            gbs->mem.bank0[0x50 + 0] = 0xCD; // call
            gbs->mem.bank0[0x50 + 1] = (gbs->header.load_address + 0x48) & 0xFF;
            gbs->mem.bank0[0x50 + 2] = (gbs->header.load_address + 0x48) >> 8;
            gbs->mem.bank0[0x50 + 3] = 0xCD; // call
            gbs->mem.bank0[0x50 + 4] = gbs->header.play_address & 0xFF;
            gbs->mem.bank0[0x50 + 5] = gbs->header.play_address >> 8;
            gbs->mem.bank0[0x50 + 6] = 0xD9; // reti
            break;
    }

    gbs->io = *io;
    gbs_reset(gbs, 0);

    return true;

fail:
    gbs_free_mem(gbs);
    return false;
}

bool gbs_load_mem(Gbs* gbs, const void* data, size_t size)
{
    memset(&gbs->memio, 0, sizeof(gbs->memio));

    gbs->memio.data = data;
    gbs->memio.size = size;

    struct GbsIo io = MEMIO;
    io.user = &gbs->memio;

    return gbs_load_io(gbs, &io);
}

bool gbs_get_meta(const Gbs* gbs, struct GbsMeta* meta)
{
    return gbs_get_meta_from_header(&gbs->header, meta);
}

bool gbs_get_meta_io(struct GbsIo* io, struct GbsMeta* meta)
{
    struct GbsHeader h = {0};
    if (!io->read(io->user, &h, sizeof(h), 0))
    {
        return false;
    }

    if (!gbs_verify_and_fix_header(&h))
    {
        return false;
    }

    return gbs_get_meta_from_header(&h, meta);
}

bool gbs_get_meta_data(const void* data, size_t size, struct GbsMeta* meta)
{
    struct MemIo memio = { .data = data, .size = size };
    struct GbsIo io = MEMIO;
    io.user = &memio;

    return gbs_get_meta_io(&io, meta);
}

uint8_t gbs_get_song(const Gbs* gbs)
{
    return gbs->song;
}

bool gbs_set_song(Gbs* gbs, uint8_t song)
{
    if (song < gbs->header.first_song || song >= gbs->header.number_of_songs)
    {
        return false;
    }

    gbs_reset(gbs, song);
    return true;
}

#ifndef __GBA__
void gbs_run(Gbs* gbs, unsigned cycles)
{
    gbs->end_frame = false;
    scheduler_add(&gbs->scheduler, Event_END_FRAME, cycles, on_endframe_event, gbs);

    while LIKELY(!gbs->end_frame)
    {
        // keep ticking cpu until it needs syncing.
        if LIKELY(!gbs->waiting_vsync)
        {
            LR35902_run(&gbs->cpu);
            scheduler_tick(&gbs->scheduler, gbs->cpu.cycles);
        }
        else
        {
            // nothing to emulate so jump to the next event.
            scheduler_advance_to_next_event(&gbs->scheduler);
        }

        // fire scheduler if needed
        if UNLIKELY(scheduler_should_fire(&gbs->scheduler))
        {
            scheduler_fire(&gbs->scheduler);
        }
    }

    // make samples available
    apu_end_frame(gbs->apu, scheduler_get_ticks(&gbs->scheduler));
}
#else
void IWRAM_CODE gbs_run(Gbs* gbs, unsigned cycles)
{
    extern bool gbs_should_quit;

    while LIKELY(!gbs_should_quit)
    {
        LR35902_run(&gbs->cpu);
    }

    // disable audio and timers on exit.
    REG_SOUNDCNT_X = 0;
    REG_TM1CNT_H = 0;
}
#endif

void gbs_set_channel_volume(Gbs* gbs, unsigned channel_num, float volume)
{
   apu_set_channel_volume(gbs->apu, channel_num, volume);
}

void gbs_set_master_volume(Gbs* gbs, float volume)
{
   apu_set_master_volume(gbs->apu, volume);
}

void gbs_set_bass(Gbs* gbs, int frequency)
{
   apu_set_bass(gbs->apu, frequency);
}

void gbs_set_treble(Gbs* gbs, double treble_db)
{
   apu_set_treble(gbs->apu, treble_db);
}

int gbs_clocks_needed(Gbs* gbs, int sample_count)
{
   return apu_clocks_needed(gbs->apu, sample_count);
}

int gbs_samples_avaliable(const Gbs* gbs)
{
   return apu_samples_avaliable(gbs->apu);
}

int gbs_read_samples(Gbs* gbs, short out[], int count)
{
   return apu_read_samples(gbs->apu, out, count);
}

void gbs_clear_samples(Gbs* gbs)
{
   apu_clear_samples(gbs->apu);
}

void gbs_read_pcm(Gbs* gbs, struct GbsPcm* pcm)
{
    const uint8_t pcm12 = apu_cgb_read_pcm12(gbs->apu, scheduler_get_ticks(&gbs->scheduler));
    const uint8_t pcm34 = apu_cgb_read_pcm34(gbs->apu, scheduler_get_ticks(&gbs->scheduler));

    pcm->channel[0] = (pcm12 & 0x0F) >> 0;
    pcm->channel[1] = (pcm12 & 0xF0) >> 4;
    pcm->channel[2] = (pcm34 & 0x0F) >> 0;
    pcm->channel[3] = (pcm34 & 0xF0) >> 4;
}

#if GBS_ENABLE_LRU
// 33 banks is the most any game uses (Hanasaka Tenshi Tenten-kun no Beat Breaker)
enum { ZROM_MAX_BANKS = 33 };

struct Zrom
{
    uint8_t* pool;
    size_t pool_count;

    uint8_t last_used_count;
    uint8_t pad[3];
    uint8_t last_used[ZROM_MAX_BANKS];
    uint8_t pool_idx[ZROM_MAX_BANKS];
    bool slots[ZROM_MAX_BANKS];
};

struct ZromIo
{
    struct GbsIo io;
    struct Zrom z;
};

static bool zrom_uncompress_bank_to_pool(struct Zrom* z, struct GbsIo* io, size_t addr, uint8_t bank)
{
    // bank zero is always uncompressed
    if (bank == 0)
    {
        return true;
    }

    LOGI("in zrom ready to load bank: %u\n", bank);
    bank--;

    // if already compressed
    if (z->slots[bank])
    {
        for (size_t i = 0; i < z->last_used_count; i++)
        {
            if (z->last_used[i] == bank)
            {
                // break early if bank is in the first slot
                if (i == 0)
                {
                    return true;
                }

                memmove(z->last_used + 1, z->last_used, i);
                z->last_used[0] = bank;
                return true;
            }
        }

        return true;
    }

    // if we uncompressed max banks already, then we need to
    // uncompress this new bank over an old bank.
    if (z->last_used_count == z->pool_count)
    {
        LOGI("hit max\n");
        // check which is the last used bank.
        const uint8_t old_bank = z->last_used[z->last_used_count - 1];
        // update the next free bank slot
        z->pool_idx[bank] = z->pool_idx[old_bank];
        // update slot array to say that it is no longer used!
        z->slots[old_bank] = false;
    }
    else
    {
        z->pool_idx[bank] = z->last_used_count;
        z->last_used_count = MIN((size_t)z->last_used_count + 1, z->pool_count);
    }

    uint8_t* next_free_bank = z->pool + (z->pool_idx[bank] * GBS_BANK_SIZE);

    // new bank, not compressed
    memmove(z->last_used + 1, z->last_used, z->last_used_count);
    z->last_used[0] = bank;

    io->read(io->user, next_free_bank, GBS_BANK_SIZE, addr);
    z->slots[bank] = true;

    LOGI("loading bank: %u idx: %u\n", bank+1, z->pool_idx[bank]);

    return true;
}

static uint8_t* zrom_mbc_common_get_rom_bank(struct Zrom* z, struct GbsIo* io, size_t addr, uint8_t bank)
{
    assert(bank > 0 && "invalid bank");
    zrom_uncompress_bank_to_pool(z, io, addr, bank);
    return z->pool + (z->pool_idx[bank - 1] * GBS_BANK_SIZE);
}

static size_t zio_mem_read(void* user, void* dst, size_t size, size_t addr)
{
    struct ZromIo* zio = user;
    return zio->io.read(zio->io.user, dst, size, addr);
}

static size_t zio_mem_size(void* user)
{
    const struct ZromIo* zio = user;
    return zio->io.size(zio->io.user);
}

static const uint8_t* zio_mem_pointer(void* user, size_t addr, uint8_t bank)
{
    struct ZromIo* zio = user;
    return zrom_mbc_common_get_rom_bank(&zio->z, &zio->io, addr, bank);
}

static const struct GbsIo ZMEMIO = {
    .user = NULL,
    .read = zio_mem_read,
    .size = zio_mem_size,
    .pointer = zio_mem_pointer,
};

bool gbs_lru_init(const struct GbsIo* io_in, struct GbsIo* io_out, uint8_t* pool, size_t size)
{
    if (!io_in || !io_out || !pool || !size || size % GBS_BANK_SIZE)
    {
        return false;
    }

    struct ZromIo* zio = calloc(1, sizeof(*zio));
    if (!zio)
    {
        return false;
    }

    zio->io = *io_in;
    zio->z.pool = pool;
    zio->z.pool_count = size / GBS_BANK_SIZE;

    *io_out = ZMEMIO;
    io_out->user = zio;

    return true;
}

void gbs_lru_quit(struct GbsIo* io)
{
    if (io->user)
    {
        free(io->user);
        memset(io, 0, sizeof(*io));
    }
}
#endif

#if GBS_ENABLE_GBS2GB
enum { GBS_SONG_IO_ADDR_DMG = 0x4A }; // 0xFF4A (WY)
enum { GBS_JOYP_IO_ADDR_DMG = 0x4B }; // 0xFF4B (WX)
enum { GBS_SONG_IO_ADDR_CGB = 0x72 }; // 0xFF72
enum { GBS_JOYP_IO_ADDR_CGB = 0x73 }; // 0xFF73

struct CartHeader
{
    uint8_t entry_point[0x4];
    uint8_t logo[0x30];
    char title[0x10];
    uint8_t new_licensee_code[2];
    uint8_t sgb_flag;
    uint8_t cart_type;
    uint8_t rom_size;
    uint8_t ram_size;
    uint8_t destination_code;
    uint8_t old_licensee_code;
    uint8_t rom_version;
    uint8_t header_checksum;
    uint8_t global_checksum[2];
};

struct MemIoWrite
{
    uint8_t* data;
    size_t size;
};

static const uint8_t nintendoLogo[0x30] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

static size_t io_mem_write(void* user, const void* src, size_t size, size_t addr)
{
    struct MemIoWrite* io = user;
    const size_t rs = addr + size > io->size ? io->size - addr : size;
    memcpy(io->data + addr, src, rs);
    return rs;
}

static const struct GbsWriteIo MEMIO_WRITE = {
    .user = NULL,
    .write = io_mem_write,
};

static uint8_t gbs2gb_calc_cart_bank_size(const Gbs* gbs)
{
    if (!gbs || !gbs->mem.bank0)
    {
        return 0;
    }

    const size_t size = gbs->mem.max_rom_bank * GBS_BANK_SIZE;

    for (unsigned i = 0; i < 8; i++) {
        if (size <= (0x8000U << i)) {
            return i;
        }
    }

    return 0;
}

static void update_global_checksum(const uint8_t* data, size_t off, size_t len, uint16_t* sum)
{
    for (size_t i = off; i < len; i++)
    {
        *sum += data[i];
    }
}

size_t gbs2gb_calc_size(const Gbs* gbs)
{
    return 0x8000U << gbs2gb_calc_cart_bank_size(gbs);
}

enum Gbs2GbSystem gbs2gb_get_system_type(const Gbs* gbs)
{
    // simply check if the game requires double speed mode.
    if (gbs->header.timer_control & 0x80)
    {
        return Gbs2GbSystem_CGB;
    }
    else
    {
        return Gbs2GbSystem_DMG;
    }
}

bool gbs2gb_io(const Gbs* gbs, struct GbsWriteIo* io)
{
    size_t addr = 0;
    size_t result = 0;
    uint16_t global_checksum = 0;
    uint8_t* bank0 = NULL;

    if (!gbs || !io->write)
    {
        goto fail;
    }

    // get system type.
    const enum Gbs2GbSystem system_type = gbs2gb_get_system_type(gbs);
    const uint8_t song_io_addr = system_type == Gbs2GbSystem_DMG ? GBS_SONG_IO_ADDR_DMG : GBS_SONG_IO_ADDR_CGB;
    const uint8_t joyp_io_addr = system_type == Gbs2GbSystem_DMG ? GBS_JOYP_IO_ADDR_DMG : GBS_JOYP_IO_ADDR_CGB;

    // check if we have enough space to write below code (0x400)
    const uint8_t load_bank = gbs->header.load_address / GBS_BANK_SIZE;
    const uint16_t bank_size = gbs->header.load_address % GBS_BANK_SIZE;
    if (!load_bank && bank_size < 0x400)
    {
        LOGE("not enough space for gbs2gb code\n");
        goto fail;
    }

    const size_t out_size = gbs2gb_calc_size(gbs);
    if (!out_size)
    {
        goto fail;
    }

    LOGI("out_size: %u banks: %u\n", out_size, out_size / GBS_BANK_SIZE);

    bank0 = calloc(1, GBS_BANK_SIZE);
    if (!bank0)
    {
        goto fail;
    }

    memcpy(bank0, gbs->mem.bank0, GBS_BANK_SIZE);

    // create cart header.
    unsigned entry_after_header = 0;
    const unsigned entrypoint = 0x100;
    {
        struct CartHeader cart_header = {0};
        entry_after_header = entrypoint + sizeof(cart_header);

        // set entrypoint.
        cart_header.entry_point[0 + 0] = 0xF3; // DI
        cart_header.entry_point[0 + 1] = 0xC3; // jp
        cart_header.entry_point[0 + 2] = entry_after_header & 0xFF;
        cart_header.entry_point[0 + 3] = entry_after_header >> 8;

        cart_header.cart_type = 0x02; // MBC1+RAM
        cart_header.rom_size = gbs2gb_calc_cart_bank_size(gbs);
        memcpy(cart_header.logo, nintendoLogo, sizeof(nintendoLogo));
        memcpy(cart_header.title, "TotalGBS 0.1.0", sizeof(cart_header.title) - 1);
        cart_header.title[sizeof(cart_header.title) - 1] = system_type;
        cart_header.destination_code = 0x1; // Non-Japanese
        cart_header.ram_size = 2; // 8k

        // fix header checksum.
        for (unsigned i = 0x34; i < 0x4D; i++)
        {
            cart_header.header_checksum -= ((const uint8_t*)&cart_header)[i] + 1;
        }

        // store cart header.
        for (unsigned i = 0; i < sizeof(cart_header); i++)
        {
            bank0[entrypoint + i] = ((const uint8_t*)&cart_header)[i];
        }
    }

    #define set_io_reg(reg, value) do { \
        if (!(value)) { \
            bank0[entry_after_header++] = 0xAF; /* XOR_r (REG_A) */ \
        } else { \
            bank0[entry_after_header++] = 0x3E; /* LD_r_u8 */ \
            bank0[entry_after_header++] = (value); \
        } \
        bank0[entry_after_header++] = 0xE0; /* LD_FFu8_A */ \
        bank0[entry_after_header++] = (reg) & 0xFF; \
    } while (0)

    // this is the fastest / smallest memset i could come up with.
    #define zero_mem_region(addr, len) do { \
        bank0[entry_after_header++] = 0xAF; /* XOR_r (REG_A) */ \
        bank0[entry_after_header++] = 0x01; /* LD_BC_u16 (len) */ \
        bank0[entry_after_header++] = (len) & 0xFF; \
        bank0[entry_after_header++] = (len) >> 8; \
        bank0[entry_after_header++] = 0x21; /* LD_HL_u16 (dst) */ \
        bank0[entry_after_header++] = (addr) & 0xFF; \
        bank0[entry_after_header++] = (addr) >> 8; \
        /* loop hi */ \
        bank0[entry_after_header++] = 0x22; /* LD_HLi_A */ \
        bank0[entry_after_header++] = 0x0B; /* DEC_BC */ \
        bank0[entry_after_header++] = 0xB8; /* CP_r (REG_B) */ \
        bank0[entry_after_header++] = 0x20; /* JR_NZ */ \
        bank0[entry_after_header++] = -5; \
        /* loop lo */ \
        bank0[entry_after_header++] = 0x22; /* LD_HLi_A */ \
        bank0[entry_after_header++] = 0x0B; /* DEC_BC */ \
        bank0[entry_after_header++] = 0xB9; /* CP_r (REG_C) */ \
        bank0[entry_after_header++] = 0x20; /* JR_NZ */ \
        bank0[entry_after_header++] = -5; \
    } while (0)

    // enter double speed mode.
    if (system_type == Gbs2GbSystem_CGB)
    {
        // check if already in double speed mode!
        bank0[entry_after_header++] = 0xF0; // LD_A_FFu8 (REG_A) = KEY1
        bank0[entry_after_header++] = 0x4D; // KEY1
        bank0[entry_after_header++] = 0xCB; // CB
        bank0[entry_after_header++] = 0x7F; // BIT_r (REG_A & 0x80)
        bank0[entry_after_header++] = 0x20; // JR_NZ
        bank0[entry_after_header++] = 0x06; // skips below if already set

        set_io_reg(0xFF4D, 0x01);
        bank0[entry_after_header++] = 0x10; // STOP
        bank0[entry_after_header++] = 0x00; // nop
    }

    // disable background, window and sprites (optional).
    set_io_reg(0xFF40, 0x80);

    // reset the apu.
    set_io_reg(0xFF26, 0x00); // OFF
    set_io_reg(0xFF26, 0xF1); // ON

    set_io_reg(0xFF10, 0x80);
    set_io_reg(0xFF11, 0xBF);
    set_io_reg(0xFF12, 0xF3);
    set_io_reg(0xFF13, 0xFF);
    set_io_reg(0xFF14, 0xBF);

    set_io_reg(0xFF16, 0x3F);
    set_io_reg(0xFF17, 0x00);
    set_io_reg(0xFF18, 0xFF);
    set_io_reg(0xFF19, 0xBF);

    set_io_reg(0xFF1A, 0x7F);
    set_io_reg(0xFF1B, 0xFF);
    set_io_reg(0xFF1C, 0x9F);
    set_io_reg(0xFF1D, 0xFF);
    set_io_reg(0xFF1E, 0xBF);

    set_io_reg(0xFF20, 0xFF);
    set_io_reg(0xFF21, 0x00);
    set_io_reg(0xFF22, 0x00);
    set_io_reg(0xFF23, 0xBF);

    set_io_reg(0xFF24, 0x77);
    set_io_reg(0xFF25, 0xF3);

    zero_mem_region(0xC000, 1024*4); // wram[0]
    zero_mem_region(0xD000, 1024*4); // wram[1]
    zero_mem_region(0xFF80, 0x80); // hram
    zero_mem_region(0xFE00, 0xA0); // oam

    // set stack pointer.
    bank0[entry_after_header++] = 0x31; // LD_SP_u16
    bank0[entry_after_header++] = gbs->header.stack_pointer & 0xFF;
    bank0[entry_after_header++] = gbs->header.stack_pointer >> 8;

    // set interrupt flags
    set_io_reg(0xFFFF, 0x01 | 0x04); // IE

    // set initial joyp value.
    bank0[entry_after_header++] = 0xF0; // LD_A_FFu8 (REG_A) = JOYP
    bank0[entry_after_header++] = 0x00; // JOYP
    bank0[entry_after_header++] = 0xE0; // LD_FFu8_A (REG_A)
    bank0[entry_after_header++] = joyp_io_addr; // set inital joyp value

    // set timers.
    set_io_reg(0xFF04, 0x00); // DIV
    set_io_reg(0xFF07, 0x00); // TAC
    set_io_reg(0xFF05, 0x00); // TIMA
    set_io_reg(0xFF06, gbs->header.timer_modulo); // TMA
    set_io_reg(0xFF07, gbs->header.timer_control & 0x7); // TAC

    // load song into REG_A.
    bank0[entry_after_header++] = 0xF0; // LD_A_FFu8 (REG_A)
    bank0[entry_after_header++] = song_io_addr; // load song number into reg_a
    // check if song is below the first song.
    bank0[entry_after_header++] = 0x0E; // LD_r_u8 (REG_C)
    bank0[entry_after_header++] = gbs->header.first_song;
    bank0[entry_after_header++] = 0xB9; // CP_r (REG_A - REG_C)
    bank0[entry_after_header++] = 0x30; // JR_NC
    bank0[entry_after_header++] = 1; // skip below
    // load first song.
    bank0[entry_after_header++] = 0x79; // ld_r_r (REG_A = REG_C)
    // write new song value back.
    bank0[entry_after_header++] = 0xE0;
    bank0[entry_after_header++] = song_io_addr;

    // load init code.
    bank0[entry_after_header++] = 0xCD; // call
    bank0[entry_after_header++] = gbs->header.init_address & 0xFF;
    bank0[entry_after_header++] = gbs->header.init_address >> 8;
    bank0[entry_after_header++] = 0xFB; // EI
    bank0[entry_after_header++] = 0x00; // nop (EI is delayed)

    // add input poll code.
    const uint16_t poll_loop_addr = entry_after_header;
    {
        bank0[entry_after_header++] = 0xF3; // DI
        bank0[entry_after_header++] = 0xC5; // PUSH_BC
        bank0[entry_after_header++] = 0xF5; // PUSH_AF

        // load max song.
        bank0[entry_after_header++] = 0x0E; // LD_r_u8 (REG_C)
        bank0[entry_after_header++] = gbs->header.number_of_songs;
        // load old value into REG_B.
        bank0[entry_after_header++] = 0xF0; // LD_A_FFu8
        bank0[entry_after_header++] = joyp_io_addr;
        bank0[entry_after_header++] = 0x47; // ld_r_r (REG_B = REG_A)
        // read on both lines (A+RIGHT) / (B+LEFT).
        bank0[entry_after_header++] = 0x3E;
        bank0[entry_after_header++] = 0x00;
        bank0[entry_after_header++] = 0xE0;
        bank0[entry_after_header++] = 0x00; // JOYP
        // check A value.
        bank0[entry_after_header++] = 0xF0; // LD_A_FFu8 (REG_A)
        bank0[entry_after_header++] = 0x00; // JOYP
        // set new value.
        bank0[entry_after_header++] = 0xE0; // LD_FFu8_A
        bank0[entry_after_header++] = joyp_io_addr;

        // jump if bit was already down.
        bank0[entry_after_header++] = 0xCB; // CB
        bank0[entry_after_header++] = 0x40; // BIT_r (REG_B & 0x00)
        bank0[entry_after_header++] = 0x20; // JR_NZ
        bank0[entry_after_header++] = 13; // skip below.
        // check if A button is now down.
        bank0[entry_after_header++] = 0xCB; // CB
        bank0[entry_after_header++] = 0x47; // BIT_r (REG_A & 0x00)
        bank0[entry_after_header++] = 0x28; // JR_Z
        bank0[entry_after_header++] = 9; // skip below.
        // load current song
        bank0[entry_after_header++] = 0xF0; // LD_A_FFu8 (REG_A)
        bank0[entry_after_header++] = song_io_addr; // load song number into reg_a

        // increment.
        bank0[entry_after_header++] = 0x3C; // INC_r (REG_A)
        // check if we exceeded maximum song.
        bank0[entry_after_header++] = 0xB9; // CP_r (REG_A - REG_C)
        bank0[entry_after_header++] = 0x38; // JR_C
        bank0[entry_after_header++] = 1;
        // set song to 0
        bank0[entry_after_header++] = 0xAF; // XOR_r (REG_A)
        // jump.
        bank0[entry_after_header++] = 0x18; // JR
        bank0[entry_after_header++] = 16;

        bank0[entry_after_header++] = 0xCB; // CB
        bank0[entry_after_header++] = 0x48; // BIT_r (REG_B & 0x01)
        bank0[entry_after_header++] = 0x20; // JR_NZ
        bank0[entry_after_header++] = 17; // skip below.
        // check if B button is now down.
        bank0[entry_after_header++] = 0xCB; // CB
        bank0[entry_after_header++] = 0x4F; // BIT_r (REG_A & 0x01)
        bank0[entry_after_header++] = 0x28; // JR_Z
        bank0[entry_after_header++] = 13; // skip below.
        // load current song
        bank0[entry_after_header++] = 0xF0; // LD_A_FFu8 (REG_A)
        bank0[entry_after_header++] = song_io_addr; // load song number into reg_a

        // check if we are already at the first song.
        // bank0[entry_after_header++] = 0xB7; // OR_r (REG_A)
        bank0[entry_after_header++] = 0xFE; // CP_u8
        bank0[entry_after_header++] = gbs->header.first_song;
        bank0[entry_after_header++] = 0x20; // JR_NZ
        bank0[entry_after_header++] = 1; // skip below
        // load max song if we are already zero (wrap around).
        bank0[entry_after_header++] = 0x79; // ld_r_r (REG_A = REG_C)
        // decrement.
        bank0[entry_after_header++] = 0x3D; // DEC_r (REG_A)

        // write new song value back.
        bank0[entry_after_header++] = 0xE0;
        bank0[entry_after_header++] = song_io_addr;
        // jump.
        bank0[entry_after_header++] = 0xC3; // JP
        bank0[entry_after_header++] = 0x100 & 0xFF;
        bank0[entry_after_header++] = 0x100 >> 8;

        bank0[entry_after_header++] = 0xF1; // POP_AF
        bank0[entry_after_header++] = 0xC1; // POP_BC
        bank0[entry_after_header++] = 0xFB; // EI
        bank0[entry_after_header++] = 0x00; // nop (EI is delayed)
    }

    bank0[entry_after_header++] = 0x76; // halt
    #if 1
    bank0[entry_after_header++] = 0x18; // JR
    bank0[entry_after_header] = poll_loop_addr - entry_after_header;
    #else
    bank0[entry_after_header++] = 0xC3; // jp
    bank0[entry_after_header++] = poll_loop_addr & 0xFF;
    bank0[entry_after_header++] = poll_loop_addr >> 8;
    #endif

    // global checksum does not include checksumed parts.
    update_global_checksum(bank0, 0, 0x14E, &global_checksum);
    update_global_checksum(bank0, 0x150, GBS_BANK_SIZE, &global_checksum);

    // write bank0.
    addr += result = io->write(io->user, bank0, GBS_BANK_SIZE, addr);
    if (!result)
    {
        goto fail;
    }

    // write the rest of the banks
    for (unsigned bank = 1; bank < gbs->mem.max_rom_bank; bank++)
    {
        const uint8_t* ptr = get_pointer_internal(gbs, bank);
        update_global_checksum(ptr, 0, GBS_BANK_SIZE, &global_checksum);

        addr += result = io->write(io->user, ptr, GBS_BANK_SIZE, addr);
        if (!result)
        {
            goto fail;
        }
    }

    // pad the remaining bytes
    while (addr < out_size)
    {
        uint8_t pad[256] = {0};
        const size_t rs = addr + sizeof(pad) > out_size ? out_size - addr : sizeof(pad);
        addr += result = io->write(io->user, pad, rs, addr);
        if (!result)
        {
            goto fail;
        }
    }

    const size_t global_checksum_addr = 0x100 + sizeof(struct CartHeader) - 2;
    const uint8_t global_checksum_lo = global_checksum & 0xFF;
    const uint8_t global_checksum_hi = global_checksum >> 8;

    io->write(io->user, &global_checksum_lo, sizeof(global_checksum_lo), global_checksum_addr + 0);
    io->write(io->user, &global_checksum_hi, sizeof(global_checksum_hi), global_checksum_addr + 1);

    free(bank0);

    return true;

fail:
    if (bank0)
    {
        free(bank0);
    }
    return false;
}

bool gbs2gb_mem(const Gbs* gbs, void* data, size_t size)
{
    if (!gbs || !data || !size)
    {
        return false;
    }

    const size_t out_size = gbs2gb_calc_size(gbs);
    if (!out_size || out_size > size)
    {
        return false;
    }

    struct MemIoWrite memio = { .data = data, .size = size };
    struct GbsWriteIo io = MEMIO_WRITE;
    io.user = &memio;

    return gbs2gb_io(gbs, &io);
}
#endif
