#ifndef WAV_WRITER_H
#define WAV_WRITER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool wav_open(const char* path, uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
void wav_close(void);
size_t wav_write(const void* data, size_t size_in_bytes);

#ifdef __cplusplus
}
#endif

#endif // WAV_WRITER_H