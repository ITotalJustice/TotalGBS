#include <gba.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "io_ezfo.h"
#include "gbs.h"
#include "m3u/m3u.h"

#define IWRAM_BSS __attribute__((section(".bss")))
#define NOINLINE __attribute__((noinline))

// enable to load gbs rom into psram rather than using fatfs + lru cache.
#define EZ_PRELOAD_ROM 0

struct Archive
{
    struct M3uSongInfo* infos;
    size_t m3u_count;
};

struct FileIo
{
    FIL fp;
};

struct MemIo
{
    const unsigned char* data;
    size_t size;
};

struct FileEntry
{
    char name[100];
    bool is_dir;
};

struct RomMeta {
    uint32_t magic; // 0x454D5530 "EMU0"
    uint32_t size; // size of rom
    uint32_t offset; // rom offset
    uint32_t m3u_size; // total size of all m3u strings
};

// set this offset so that it is the same size as TotalGBS.gba
enum { ROM_META_OFFSET = 1024*128 };
enum { ROM_M3U_OFFSET = ROM_META_OFFSET + sizeof(struct RomMeta) };

#define GBA_ROM8 ((const u8*)0x08000000)

#define FILE_ENTRY_MAX 100
static struct FileEntry EWRAM_BSS g_file_entries[FILE_ENTRY_MAX];
static unsigned EWRAM_BSS g_file_index;
static unsigned EWRAM_BSS g_file_count;

static Gbs* EWRAM_BSS gbs;
static struct GbsMeta EWRAM_BSS gbs_meta;
static const struct M3uSongInfo* EWRAM_BSS m3u_info;
static struct Archive EWRAM_BSS archive;
static unsigned char EWRAM_BSS cursor;
static unsigned char EWRAM_BSS lru_pool[GBS_BANK_SIZE * 8]; // 64k free.
bool IWRAM_BSS gbs_should_quit;

static void _Noreturn error_loop(const char* msg)
{
    iprintf(CON_CLS());
    iprintf("TotalGBS: " LIBGBS_VERSION_STR);
    iprintf("\n\n");
    iprintf("%s\n", msg);
    while(1) VBlankIntrWait();
}

static bool NOINLINE EWRAM_CODE ezflash_load_file_into_psram(FIL* file)
{
    if (!EZF0_is_ezflash())
    {
        return false;
    }

    for (u32 i = 0; !f_eof(file); i += sizeof(lru_pool))
    {
        UINT bytes_read;
        if (FR_OK != f_read(file, lru_pool, sizeof(lru_pool), &bytes_read))
        {
            return false;
        }

        // todo: check why f_read directly into psram doesn't work
        // todo: the above works in my emulator but not on hw, check why.
        EZF0_mount_os();
            dmaCopy(lru_pool, (u8*)(0x08800000 + ROM_META_OFFSET + i), bytes_read + 1);
        EZF0_mount_rom();
    }

    return true;
}

// hack to work around ezflash not clearing psram when loading a rom.
static void NOINLINE EWRAM_CODE ezflash_reset_psram(void)
{
    if (!EZF0_is_psram())
    {
        return;
    }

    if (EZF0_mount_os())
    {
        *(vu16*)(0x08800000 + ROM_META_OFFSET) = 0; // null magic.
        EZF0_mount_rom();
    }
}

static size_t EWRAM_CODE io_file_read(void* user, void* dst, size_t size, size_t addr)
{
    struct FileIo* io = user;
    if (addr != f_tell(&io->fp))
    {
        f_lseek(&io->fp, addr);
    }
    UINT bytes_read = 0;
    f_read(&io->fp, dst, size, &bytes_read);
    return bytes_read;
}

static size_t io_file_size(void* user)
{
    const struct FileIo* io = user;
    return f_size(&io->fp);
}

static size_t EWRAM_CODE io_mem_read(void* user, void* dst, size_t size, size_t addr)
{
    const struct MemIo* io = user;
    const size_t rs = addr + size > io->size ? io->size - addr : size;

    // if not aligned, use slow memcpy, otherwise dma.
    if (((u32)io->data + addr) & 0x1 || rs & 0x1)
    {
        memcpy(dst, io->data + addr, rs);
    }
    else
    {
        dmaCopy(io->data + addr, dst, rs);
    }

    return rs;
}

static size_t io_mem_size(void* user)
{
    const struct MemIo* io = user;
    return io->size;
}

static const struct GbsIo EWRAM_DATA FILEIO = {
    .user = NULL,
    .read = io_file_read,
    .size = io_file_size,
};

static const struct GbsIo EWRAM_DATA MEMIO = {
    .user = NULL,
    .read = io_mem_read,
    .size = io_mem_size,
};

static int cmpfunc(const void* a, const void* b)
{
    const struct M3uSongInfo* ea = a;
    const struct M3uSongInfo* eb = b;
    return (int)ea->songno - (int)eb->songno;
}

static int sortfunc(const void* a, const void* b)
{
    const struct FileEntry* ea = a;
    const struct FileEntry* eb = b;

    if (ea->is_dir && !eb->is_dir)
    {
        return -1;
    }
    else if (!ea->is_dir && eb->is_dir)
    {
        return 1;
    }
    else
    {
        return strcasecmp(ea->name, eb->name);
    }
}

static bool load_archive(Gbs* gbs, struct GbsIo* io, struct Archive* archive)
{
    memset(archive, 0, sizeof(*archive));

    struct GbsIo lio;
    if (!gbs_lru_init(io, &lio, lru_pool, sizeof(lru_pool)))
    {
        return false;
    }

    return gbs_load_io(gbs, &lio);
}

static const struct M3uSongInfo* EWRAM_CODE find_info_from_song(const struct Archive* archive, unsigned song)
{
    for (size_t i = 0; i < archive->m3u_count; i++)
    {
        if (archive->infos[i].songno == song)
        {
            return &archive->infos[i];
        }
    }

    return NULL;
}

static void EWRAM_CODE play_song(unsigned song)
{
    cursor = song % gbs_meta.max_song;
    gbs_set_song(gbs, song % gbs_meta.max_song);
    m3u_info = find_info_from_song(&archive, gbs_get_song(gbs));
}

static void EWRAM_CODE draw_menu(void)
{
    // ansi escape sequence to clear screen and home cursor
    iprintf(CON_CLS());

    // ansi escape sequence to move cursor up
    iprintf(CON_POS(0, 0) "TotalGBS: " LIBGBS_VERSION_STR);
    iprintf(CON_POS(0, 2) "%s", gbs_meta.title_string);
    iprintf(CON_POS(0, 6) "%s", gbs_meta.author_string);
    iprintf(CON_POS(0, 10) "%s", gbs_meta.copyright_string);
    iprintf(CON_POS(0, 19) "%03u / %03u", cursor + 1, gbs_meta.max_song);
    if (m3u_info)
    {
        iprintf(CON_POS(0, 10) "%s", m3u_info->song_tile);
    }
}

bool gbs_on_breakpoint(void)
{
    scanKeys();

    const u16 kdown = keysDown();
    if (!kdown)
    {
        return false;
    }
    else if ((kdown & KEY_DOWN) || (kdown & KEY_LEFT))
    {
        cursor = cursor ? cursor - 1 : gbs_meta.max_song - 1;
        play_song(cursor);
        draw_menu();
        return true;
    }
    else if ((kdown & KEY_UP) || kdown & KEY_RIGHT)
    {
        cursor = (cursor + 1) % gbs_meta.max_song;
        play_song(cursor);
        draw_menu();
        return true;
    }
    else if ((kdown & KEY_UP) || kdown & KEY_RIGHT)
    {
        cursor = (cursor + 1) % gbs_meta.max_song;
        play_song(cursor);
        draw_menu();
        return true;
    }
    else if ((kdown & KEY_B) && g_file_count)
    {
        gbs_should_quit = true;
        return true;
    }

    return false;
}

static void play(void) {
    gbs_should_quit = false;

    if (!gbs_get_meta(gbs, &gbs_meta))
    {
        error_loop("failed to get gbs meta\n");
    }

    play_song(0);
    draw_menu();
    gbs_run(gbs, 0);

    // restore default irq.
    irqDisable(0xFFFF);
    irqEnable(IRQ_VBLANK);
}

static bool load_from_mem(void)
{
    static struct EWRAM_BSS MemIo memio;

    // check if the gbs rom was cat at the end of the gba rom.
    if (gbs_validate_file_mem(GBA_ROM8 + ROM_META_OFFSET, 4))
    {
        memio.data = GBA_ROM8 + ROM_META_OFFSET;
        // todo: find out how to calculate size (use open bus?).
        memio.size = 1024 * 1024 * 1;
    }
    else
    {
        struct RomMeta rom_meta = {0};
        memcpy(&rom_meta, GBA_ROM8 + ROM_META_OFFSET, sizeof(rom_meta));

        // try and read meta data
        if (rom_meta.magic != 0x454D5530)
        {
            iprintf("\ngot magic: %lX wanted: %X\n", rom_meta.magic, 0x454D5530);
            return false;
        }

        memio.data = GBA_ROM8 + rom_meta.offset;
        memio.size = rom_meta.size;

        // try and load m3u data, if it exists.
        u32 m3u_offset = ROM_M3U_OFFSET;
        while (m3u_offset < ROM_M3U_OFFSET + rom_meta.m3u_size)
        {
            const char* str = (const char*)(GBA_ROM8 + m3u_offset);
            const size_t len = strlen(str);
            struct M3uSongInfo m3u_info;
            if (m3u_info_parse(str, len, &m3u_info))
            {
                archive.m3u_count++;
                archive.infos = realloc(archive.infos, archive.m3u_count * sizeof(*archive.infos));
                if (!archive.infos)
                {
                    error_loop("m3u realloc failed\n");
                }
                archive.infos[archive.m3u_count-1] = m3u_info;
            }
            m3u_offset += len + 1;
        }

        qsort(archive.infos, archive.m3u_count, sizeof(*archive.infos), cmpfunc);
    }

    struct GbsIo io = MEMIO;
    io.user = &memio;

    if (!load_archive(gbs, &io, &archive))
    {
        error_loop("bad gbs mem\n");
    }

    // reset gbs header in psram
    ezflash_reset_psram();

    play();
    return true;
}

static void load_from_file(const char* rpath)
{
    static FIL EWRAM_BSS file;
    if (FR_OK != f_open(&file, rpath, FA_READ)) {
        error_loop(rpath);
    }

    if (EZ_PRELOAD_ROM && ezflash_load_file_into_psram(&file))
    {
        if (!load_from_mem())
        {
            error_loop("failed to load rom from psram");
        }

        return;
    }

    f_rewind(&file);

    static struct FileIo EWRAM_BSS fileio;
    memcpy(&fileio.fp, &file, sizeof(fileio.fp));

    struct GbsIo io = FILEIO;
    io.user = &fileio;

    if (!load_archive(gbs, &io, &archive)) {
        error_loop("bad gbs file\n");
    }

    play();
}

static bool scan_dir(const char* rpath)
{
    if (FR_OK != f_chdir(rpath)) {
        error_loop("failed to chdir\n");
    }

    DIR dir;
    if (FR_OK != f_opendir(&dir, "./")) {
        error_loop("failed to open dir\n");
    }

    FILINFO info;
    unsigned count = 0;
    while (1) {
        if (FR_OK != f_readdir(&dir, &info) || info.fname[0] == '\0') {
            break;
        }

        struct FileEntry* e = &g_file_entries[count];
        if (info.fattrib & AM_DIR) {
            e->is_dir = true;
        } else {
            const char* ext = strrchr(info.fname, '.');
            if (!ext || strcasecmp(ext, ".gbs") || !info.fsize) {
                continue;
            }

            e->is_dir = false;
        }

        // strncpy warns, this doesn't.
        memcpy(e->name, info.fname, sizeof(e->name));
        e->name[sizeof(e->name) - 1] = '\0';

        count++;
        if (count >= FILE_ENTRY_MAX) {
            break;
        }
    }

    f_closedir(&dir);
    g_file_index = 0;
    g_file_count = count;

    qsort(g_file_entries, count, sizeof(*g_file_entries), sortfunc);
    return true;
}

static void dir_loop(void)
{
    bool redraw = true;
    scan_dir("/");

    while (1)
    {
        scanKeys();
        const u16 kdown = keysDown();

        if (kdown & KEY_DOWN) // scroll down.
        {
            if (g_file_index + 1 < g_file_count)
            {
                g_file_index++;
                redraw = true;
            }
        }
        else if (kdown & KEY_UP) // scroll up.
        {
            if (g_file_index)
            {
                g_file_index--;
                redraw = true;
            }
        }
        else if (kdown & KEY_A) // select.
        {
            if (g_file_count)
            {
                // open new folder.
                if (g_file_entries[g_file_index].is_dir)
                {
                    scan_dir(g_file_entries[g_file_index].name);
                    redraw = true;
                }
                // open gbs file.
                else
                {
                    load_from_file(g_file_entries[g_file_index].name);
                    redraw = true;
                }
            }
        }
        else if (kdown & KEY_B) // walk up.
        {
            scan_dir("..");
            redraw = true;
        }

        // if set, re-draw the screen.
        if (redraw)
        {
            redraw = false;

            iprintf(CON_CLS());
            iprintf("TotalGBS: " LIBGBS_VERSION_STR);
            iprintf("\n\n");

            // todo: add scrolling.
            for (unsigned i = 0; i < 10; i++)
            {
                if (i >= g_file_count)
                {
                    break;
                }

                // todo: text wrap around.
                const struct FileEntry* e = &g_file_entries[i];
                const char* selected = i == g_file_index ? "-> " : "";
                iprintf("%s%s%c\n", selected, e->name, e->is_dir ? '/' : ' ');
            }

            iprintf(CON_POS(0, 19) "%03u / %03u", g_file_index + 1, g_file_count);
        }

        VBlankIntrWait();
    }
}

int main(void)
{
    irqInit();
	irqEnable(IRQ_VBLANK);
    consoleDemoInit();
    // set the screen colors, color 0 is the background color
    // the foreground color is index 1 of the selected 16 color palette
    BG_COLORS[0] = RGB8(0,0,0); // RGB8(58,110,165);
    BG_COLORS[241] = RGB5(31,31,31);

    gbs = gbs_init(0);
    if (!gbs)
    {
        error_loop("failed to alloc gbs\n");
    }

    // try and load embed rom.
    if (!load_from_mem())
    {
        // fall back to file, this won't return if it fails.
        FATFS fs;
        if (FR_OK != f_mount(&fs, "", 1))
        {
            error_loop("failed to mount sd card\n");
        }

        dir_loop();

        // for debugging.
        // load_from_file("/gbs/DMG-ME-USA.gbs");
    }
}
