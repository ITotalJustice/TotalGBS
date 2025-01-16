
#include <gba.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gbs.h"
#include "m3u/m3u.h"

struct Archive
{
    struct M3uSongInfo* infos;
    size_t m3u_count;
};

struct MemIo
{
    const unsigned char* data;
    size_t size;
};

static Gbs* EWRAM_BSS gbs;
static struct GbsMeta EWRAM_BSS gbs_meta;
static const struct M3uSongInfo* EWRAM_BSS m3u_info;
static struct Archive EWRAM_BSS archive;
static unsigned char EWRAM_BSS cursor;
static unsigned char EWRAM_BSS lru_pool[1024 * 16 * 8]; // 64k free.

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

static const struct GbsIo EWRAM_DATA MEMIO = {
    .user = NULL,
    .read = io_mem_read,
    .size = io_mem_size,
};

static int cmpfunc(const void* a, const void* b)
{
    const struct M3uSongInfo* info_a = a;
    const struct M3uSongInfo* info_b = b;
    return (int)info_a->songno - (int)info_b->songno;
}

static void archive_close(struct Archive* archive)
{
    for (size_t i = 0; i < archive->m3u_count; i++)
    {
        m3u_info_free(&archive->infos[i]);
    }
    memset(archive, 0, sizeof(*archive));
}

static bool load_archive_data(Gbs* gbs, const void* data, size_t size, struct Archive* archive)
{
    memset(archive, 0, sizeof(*archive));

    static struct MemIo memio;
    memio.data = data;
    memio.size = size;

    struct GbsIo io = MEMIO;
    io.user = &memio;

    struct GbsIo lio;
    if (!gbs_lru_init(&io, &lio, lru_pool, sizeof(lru_pool)))
    {
        return false;
    }

    return gbs_load_io(gbs, &lio);
}

static void error_loop(const char* msg)
{
    iprintf("%s\n", msg);
    while(1) VBlankIntrWait();
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
    gbs_set_song(gbs, song % gbs_meta.max_song);
    m3u_info = find_info_from_song(&archive, gbs_get_song(gbs));
}

static void EWRAM_CODE draw_menu(void)
{
    // ansi escape sequence to clear screen and home cursor
    iprintf(CON_CLS());

    // ansi escape sequence to move cursor up
    iprintf(CON_POS(0, 0) "TotalGBS: 1.0.0");
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

    return false;
}

struct RomMeta {
    uint32_t magic; // 0x454D5530 "EMU0"
    uint32_t size; // size of rom
    uint32_t offset; // rom offset
    uint32_t m3u_size; // total size of all m3u strings
};

// set this offset so that it is LARGER than TotalGBS.gba
// otherwise it will overwrite code!
// current size is 97k 02/09/2024.
enum { ROM_META_OFFSET = 1024*128 };
enum { ROM_M3U_OFFSET = ROM_META_OFFSET + sizeof(struct RomMeta) };

#define GBA_ROM8 (const u8*)(0x08000000)

int main(void)
{
    irqInit();
	irqEnable(IRQ_VBLANK);
    consoleDemoInit();
    // set the screen colors, color 0 is the background color
    // the foreground color is index 1 of the selected 16 color palette
    BG_COLORS[0] = RGB8(0,0,0); // RGB8(58,110,165);
    BG_COLORS[241] = RGB5(31,31,31);

    struct RomMeta rom_meta = {0};
    // NOTE: don't use dma here as some emulators timing sucks (including mine).
    memcpy(&rom_meta, GBA_ROM8 + ROM_META_OFFSET, sizeof(rom_meta));
    // dmaCopy(GBA_ROM8 + ROM_META_OFFSET, &rom_meta, sizeof(rom_meta));

    // try and read meta data
    if (rom_meta.magic != 0x454D5530)
    {
        iprintf("\ngot magic: %lX wanted: %X\n", rom_meta.magic, 0x454D5530);
        error_loop("failed to mount fat device :(\n");
    }

    gbs = gbs_init(0);

    if (!load_archive_data(gbs, GBA_ROM8 + rom_meta.offset, rom_meta.size, &archive))
    {
        archive_close(&archive);
        error_loop("bad gbs\n");
    }

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

    gbs_get_meta(gbs, &gbs_meta);
    play_song(0);
    draw_menu();
    gbs_run(gbs, 0);

    archive_close(&archive);
    gbs_quit(gbs);

    return 0;
}
