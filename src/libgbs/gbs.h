#ifndef GBS_H
#define GBS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { GBS_VERSION_MAJOR = 1 };
enum { GBS_VERSION_MINOR = 0 };
enum { GBS_VERSION_MICRO = 0 };

enum { GBS_MAGIC_SIZE = 0x03 };
enum { GBS_HEADER_SIZE = 0x70 };
enum { GBS_BANK_SIZE = 1024 * 16 };

#ifndef GBS_ENABLE_LRU
    #define GBS_ENABLE_LRU 1
#endif

#ifndef GBS_ENABLE_GBS2GB
    #define GBS_ENABLE_GBS2GB 0
#endif

typedef struct Gbs Gbs;

struct GbsIo
{
    /* user data passed into the below functions. */
    void* user;
    /* reads from the file, used in init(). */
    size_t(*read)(void* user, void* dst, size_t size, size_t addr);
    /* returns the size of the file. */
    size_t(*size)(void* user);
    /* returns a pointer to GBS_BANK_SIZE of memory, used in bank swap. */
    /*
        NOTE: bank0, bank1 and the last bank are read immediatley in gbs_load()
        so get_pointer will never be called for those banks.
        internally, this can be worked around, and would save 32k being
        allocated.
        Contact me if such a implementation is desired.
    */
    const uint8_t*(*pointer)(void* user, size_t addr, uint8_t bank);
};

struct GbsMeta
{
    uint8_t first_song; // zero based first song.
    uint8_t max_song; // number of songs.
    char title_string[33]; // NULL terminated.
    char author_string[33]; // NULL terminated.
    char copyright_string[33]; // NULL terminated.
};

struct GbsPcm
{
    uint8_t channel[4]; // 4-bit pcm values for all 4 channels
};

Gbs* gbs_init(double sample_rate);
void gbs_quit(Gbs*);

/* resets the gbs game, restarts with specified song. */
void gbs_reset(Gbs*, uint8_t song);

/*
* performs very quick validation, only needs 3 bytes to check the magic.
* ideal if you're scanning a directory for gbs files.
*/
bool gbs_validate_file_io(const struct GbsIo* io);
bool gbs_validate_file_mem(const void* data, size_t size);

/*
* load a gbs file
* this is useful on platforms with low memory.
*/
bool gbs_load_io(Gbs*, const struct GbsIo* io);
bool gbs_load_mem(Gbs*, const void* data, size_t size);

/* fills out meta, needs at least 0x70 bytes of data for the header. */
bool gbs_get_meta(const Gbs*, struct GbsMeta* meta);
bool gbs_get_meta_io(struct GbsIo* io, struct GbsMeta* meta);
bool gbs_get_meta_data(const void* data, size_t size, struct GbsMeta* meta);

/* returns current song. */
uint8_t gbs_get_song(const Gbs*);
/* set current song. */
bool gbs_set_song(Gbs*, uint8_t song);

/* run for x amount fo cycles. */
void gbs_run(Gbs*, unsigned cycles);

/* channel volume, max range: 0.0 - 1.0. */
void gbs_set_channel_volume(Gbs*, unsigned channel_num, float volume);
/* master volume, max range: 0.0 - 1.0. */
void gbs_set_master_volume(Gbs*, float volume);
/* only available with Blip_Buffer. */
void gbs_set_bass(Gbs*, int frequency);
/* only available with Blip_Buffer. */
void gbs_set_treble(Gbs*, double treble_db);
/* returns how many cycles are needed until sample_count == gbs_samples_avaliable() */
int gbs_clocks_needed(Gbs*, int sample_count);
/* returns how many samples are available, call gbs_end_frame() first. */
int gbs_samples_avaliable(const Gbs*);
/* read stereo samples, returns the amount read. */
int gbs_read_samples(Gbs*, short out[], int count);
/* removes all samples. */
void gbs_clear_samples(Gbs*);

/* reads raw 4-bit pcm channel output using PCM12/PCM34 regs. */
void gbs_read_pcm(Gbs*, struct GbsPcm* pcm);

/*
* creates a lru (least recently used cache) using a pool of memory
* which is passed into the function.
* this uses the io_in for read() and size().
* pointer() is overloaded and returns a pointer in the memory pool.

* this is ideal for platforms which are very memory limited and
* have slow / complicated io.

* example: the GBA has 256k of ewram, this isn't enough for most roms.
* we only need ~64k for globals, gbs ram and functions.
* so we have 192k free, or, 12 banks free.

* GBA rom access times are very slow, so using a lru cache is a must.
* the end result is smooth lag free playback.
*/
#if GBS_ENABLE_LRU
bool gbs_lru_init(const struct GbsIo* io_in, struct GbsIo* io_out, uint8_t* pool, size_t size);
void gbs_lru_quit(struct GbsIo* io);
#endif

/*
* converts a gbs file to a gbc file.
* very basic impl, change songs using the A buttons.
*/
#if GBS_ENABLE_GBS2GB
enum Gbs2GbSystem
{
    Gbs2GbSystem_DMG = 0x80, // DMG-CGB
    Gbs2GbSystem_CGB = 0xC0, // CGB-ONLY
};

struct GbsWriteIo
{
    /* user data passed into the below functions. */
    void* user;
    /* writes from the file, used in init(). */
    size_t(*write)(void* user, const void* src, size_t size, size_t addr);
};

size_t gbs2gb_calc_size(const Gbs*);
enum Gbs2GbSystem gbs2gb_get_system_type(const Gbs*);
bool gbs2gb_io(const Gbs*, struct GbsWriteIo* write_io);
bool gbs2gb_mem(const Gbs*, void* data, size_t size);
#endif

#ifdef __cplusplus
}
#endif

#endif /* GBS_H */
