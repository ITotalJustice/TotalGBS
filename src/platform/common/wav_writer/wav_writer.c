#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static FILE* file;
static size_t number_of_samples;
static uint32_t sample_rate;
static uint8_t channels;
static uint8_t bits_per_sample;
static uint8_t wav_header[44];

static void w16(uint8_t* data, uint16_t value) {
    data[0] = value >> 0;
    data[1] = value >> 8;
}

static void w32(uint8_t* data, uint32_t value) {
    data[0] = value >> 0;
    data[1] = value >> 8;
    data[2] = value >> 16;
    data[3] = value >> 24;
}

bool wav_open(const char* path, uint32_t _sample_rate, uint8_t _channels, uint8_t _bits_per_sample) {
    file = fopen(path, "wb");
    if (!file) {
        return false;
    }

    // write empty 44 bytes, this is filled in wave_close().
    fwrite(wav_header, 1, sizeof(wav_header), file);

    number_of_samples = 0;
    sample_rate = _sample_rate;
    channels = _channels;
    bits_per_sample = _bits_per_sample;
    return true;
}

void wav_close(void) {
    if (!file) {
        return;
    }

    // header data
    const char* ChunkID = "RIFF";
    const char* Format = "WAVE";
    const char* Subchunk1ID = "fmt ";
    const char* Subchunk2ID = "data";
    const uint32_t Subchunk1Size = 16; // 16 for PCM
    const uint16_t AudioFormat = 1; // PCM (uncompressed)
    const uint16_t NumChannels = channels;
    const uint32_t SampleRate = sample_rate;
    const uint16_t BitsPerSample = bits_per_sample;
    const uint32_t ByteRate = SampleRate * NumChannels * BitsPerSample / 8;
    const uint32_t BlockAlign = NumChannels * BitsPerSample / 8;
    const uint32_t Subchunk2Size = number_of_samples * BitsPerSample / 8;
    const uint32_t ChunkSize = 4 + (8 + Subchunk1Size) + (8 + Subchunk2Size);

    // big endian
    memcpy(wav_header + 0, ChunkID, 4);
    memcpy(wav_header + 8, Format, 4);
    memcpy(wav_header + 12, Subchunk1ID, 4);
    memcpy(wav_header + 36, Subchunk2ID, 4);
    // little endian
    w32(wav_header + 4, ChunkSize);
    w32(wav_header + 16, Subchunk1Size);
    w16(wav_header + 20, AudioFormat);
    w16(wav_header + 22, NumChannels);
    w32(wav_header + 24, SampleRate);
    w32(wav_header + 28, ByteRate);
    w16(wav_header + 32, BlockAlign);
    w16(wav_header + 34, BitsPerSample);
    w32(wav_header + 40, Subchunk2Size);

    rewind(file); // go to beginning
    fwrite(wav_header, 1, sizeof(wav_header), file); // write header
    fclose(file); // finished!
    file = NULL;
}

size_t wav_write(const void* data, size_t size_in_bytes) {
    if (!file || !data || !size_in_bytes) {
        return false;
    }

    const size_t nwritten = fwrite(data, 1, size_in_bytes, file);
    number_of_samples += nwritten;
    return nwritten;
}
