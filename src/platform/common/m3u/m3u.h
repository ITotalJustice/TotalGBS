#ifndef _M3U_H_
#define _M3U_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

struct M3uSongInfo
{
    char* filename;
    char* song_tile;
    unsigned short time;
    unsigned short loop;
    unsigned short fade;
    unsigned char songno;
    unsigned char loopcount;
};

bool m3u_info_parse(const char* str, size_t size, struct M3uSongInfo* info);

void m3u_info_free(struct M3uSongInfo* info);

#ifdef __cplusplus
}
#endif

#endif
