#include "gb_apu.h"
#include <gba.h>

struct GbApu
{
    unsigned char dummy;
};

// APU (square1)
#define REG_NR10 *(vu8*)(0x04000060 + 0)
#define REG_NR11 *(vu8*)(0x04000062 + 0)
#define REG_NR12 *(vu8*)(0x04000062 + 1)
#define REG_NR13 *(vu8*)(0x04000064 + 0)
#define REG_NR14 *(vu8*)(0x04000064 + 1)
// APU (square2)
#define REG_NR21 *(vu8*)(0x04000068 + 0)
#define REG_NR22 *(vu8*)(0x04000068 + 1)
#define REG_NR23 *(vu8*)(0x0400006C + 0)
#define REG_NR24 *(vu8*)(0x0400006C + 1)
// APU (wave)
#define REG_NR30 *(vu8*)(0x04000070 + 0)
#define REG_NR31 *(vu8*)(0x04000072 + 0)
#define REG_NR32 *(vu8*)(0x04000072 + 1)
#define REG_NR33 *(vu8*)(0x04000074 + 0)
#define REG_NR34 *(vu8*)(0x04000074 + 1)
#define REG_WAVE_TABLE ((vu8*)(0x04000090))
// APU (noise)
#define REG_NR41 *(vu8*)(0x04000078 + 0)
#define REG_NR42 *(vu8*)(0x04000078 + 1)
#define REG_NR43 *(vu8*)(0x0400007C + 0)
#define REG_NR44 *(vu8*)(0x0400007C + 1)
// APU (control)
#define REG_NR50 *(vu8*)(0x04000080 + 0)
#define REG_NR51 *(vu8*)(0x04000080 + 1)
#define REG_NR52 *(vu8*)(0x04000084 + 0)

/* ensure you call this at start-up */
GbApu* apu_init(double clock_rate, double sample_rate)
{
    static GbApu dummy = {0};
    return &dummy;
}

/* call to free allocated memory by blip buf. */
void apu_quit(GbApu* apu)
{
}

/* clock_rate should be the cpu speed of the system. */
void apu_reset(GbApu* apu, enum GbApuType type)
{
    // reset apu
    REG_NR52 = 0;

    // psg volume 100%, disable fifo sound
    REG_SOUNDCNT_H = 0x2;

    // bias 256, resample mode 6bit / 262.144kHz
    REG_SOUNDBIAS = 0x200 | (0x3 << 14);
}

/* reads from io register, unused bits are set to 1. */
unsigned apu_read_io(GbApu* apu, unsigned addr, unsigned time)
{
    addr &= 0x7F;
    switch (addr)
    {
        case 0x10: return REG_NR10;
        case 0x11: return REG_NR11;
        case 0x12: return REG_NR12;
        case 0x13: return REG_NR13;
        case 0x14: return REG_NR14;

        case 0x16: return REG_NR21;
        case 0x17: return REG_NR22;
        case 0x18: return REG_NR23;
        case 0x19: return REG_NR24;

        case 0x1A: return REG_NR30 | 0x7F;
        case 0x1B: return REG_NR31;
        case 0x1C: return REG_NR32 | 0x80;
        case 0x1D: return REG_NR33;
        case 0x1E: return REG_NR34;

        case 0x20: return REG_NR41;
        case 0x21: return REG_NR42;
        case 0x22: return REG_NR43;
        case 0x23: return REG_NR44;

        case 0x24: return REG_NR50;
        case 0x25: return REG_NR51;
        case 0x26: return REG_NR52;

        case 0x30: case 0x31: case 0x32: case 0x33: // WAVE RAM
        case 0x34: case 0x35: case 0x36: case 0x37: // WAVE RAM
        case 0x38: case 0x39: case 0x3A: case 0x3B: // WAVE RAM
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: // WAVE RAM
            return REG_WAVE_TABLE[addr & 0xF];
    }

    return 0xFF;
}

/* writes to an io register. */
void apu_write_io(GbApu* apu, unsigned addr, unsigned value, unsigned time)
{
    addr &= 0x7F;
    switch (addr)
    {
        case 0x10: REG_NR10 = value; break;
        case 0x11: REG_NR11 = value; break;
        case 0x12: REG_NR12 = value; break;
        case 0x13: REG_NR13 = value; break;
        case 0x14: REG_NR14 = value; break;

        case 0x16: REG_NR21 = value; break;
        case 0x17: REG_NR22 = value; break;
        case 0x18: REG_NR23 = value; break;
        case 0x19: REG_NR24 = value; break;

        // force bank mode 0, swap banks on enable.
        case 0x1A: REG_NR30 = (value & 0x80) ? 0xC0 : 0x0; break;
        case 0x1B: REG_NR31 = value; break;
        // disable force volume
        case 0x1C: REG_NR32 = value & 0x7F; break;
        case 0x1D: REG_NR33 = value; break;
        case 0x1E: REG_NR34 = value; break;

        case 0x20: REG_NR41 = value; break;
        case 0x21: REG_NR42 = value; break;
        case 0x22: REG_NR43 = value; break;
        case 0x23: REG_NR44 = value; break;

        case 0x24: REG_NR50 = value; break;
        case 0x25: REG_NR51 = value; break;
        case 0x26: REG_NR52 = value; break;

        case 0x30: case 0x31: case 0x32: case 0x33: // WAVE RAM
        case 0x34: case 0x35: case 0x36: case 0x37: // WAVE RAM
        case 0x38: case 0x39: case 0x3A: case 0x3B: // WAVE RAM
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: // WAVE RAM
            REG_WAVE_TABLE[addr & 0xF] = value;
            break;
    }
}

/* call this on the falling edge of bit 4/5 of DIV. */
void apu_frame_sequencer_clock(GbApu* apu, unsigned time)
{
}

unsigned apu_cgb_read_pcm12(GbApu*, unsigned time)
{
    return 0;
}

unsigned apu_cgb_read_pcm34(GbApu*, unsigned time)
{
    return 0;
}

/* sets the filter that's applied to apu_read_samples(). */
void apu_set_highpass_filter(GbApu* apu, enum GbApuFilter filter, double clock_rate, double sample_rate)
{
}

/* charge factor should be in the range 0.0 - 1.0. */
void apu_set_highpass_filter_custom(GbApu* apu, double charge_factor, double clock_rate, double sample_rate)
{
}

/* channel volume, max range: 0.0 - 1.0. */
void apu_set_channel_volume(GbApu* apu, unsigned channel_num, float volume)
{
}

/* master volume, max range: 0.0 - 1.0. */
void apu_set_master_volume(GbApu* apu, float volume)
{
}

/* only available with Blip_Buffer. */
void apu_set_bass(GbApu* apu, int frequency)
{
}

/* only available with Blip_Buffer. */
void apu_set_treble(GbApu* apu, double treble_db)
{
}

/* returns how many cycles are needed until sample_count == apu_samples_avaliable() */
int apu_clear_clocks_needed(GbApu* apu, int sample_count)
{
    return 0;
}

/* call this when you want to read out samples. */
void apu_end_frame(GbApu* apu, unsigned time)
{
}

/* returns how many samples are available, call apu_end_frame() first. */
int apu_samples_avaliable(const GbApu* apu)
{
    return 0;
}

/* read stereo samples, returns the amount read. */
int apu_read_samples(GbApu* apu, short out[], int count)
{
    return 0;
}

/* removes all samples. */
void apu_clear_samples(GbApu* apu)
{
}
