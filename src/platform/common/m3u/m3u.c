#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m3u.h"

/*
NEZplug extend M3U playlist format(v0.9) for Winamp

    filename::GBS,[0 based songno|$songno],[title],[time(h:m:s)],[loop(h:m:s)][-],[fade(h:m:s)],[loopcount]

    filename  song file relative path(*.zip;*.nsf;*.kss;...)

    songno    0-based songno(::NSF playlist only has 1-based songno for historical reason.)
    $songno   0-based hexadecimal songno

    title     song title

    time      song play time   h * 3600 + m * 60 + s (sec)
                Default time(5min) will be used, if time not specified.

    loop(h:m:s)
              loop length      h * 3600 + m * 60 + s (sec)
    loop(h:m:s-)
              loop start time  h * 3600 + m * 60 + s (sec)
    loop(-)
              loop length is equal to play time.
                Song will not loop, if loop not specified,

    fade      fadeout length   h * 3600 + m * 60 + s (sec)
                Default fadeout length(5sec) will be used, if time not specified.

    loopcount
              loop count
                Default LoopCount will be used, if time not specified.

    You may fail to play extend M3U because of Winamp 2.6x's bug.
    Try M3U2PLS converter.
*/

static const char GBS_MAGIC[] = { 'G', 'B', 'S' };

static size_t m3u_get_time(const char* str, size_t size, size_t offset, unsigned short* t)
{
    if (str[offset] != ',')
    {
        for (size_t i = 0; i < 3; i++)
        {
            const size_t start_offset = offset;
            while (isalnum(str[offset]))
            {
                offset++; if (offset >= size) { goto early_end; }
            }

            *t *= 60;
            *t += atoi(str + start_offset);

            if (str[offset] == ',')
            {
                goto early_end;
            }

            // skip over colon
            offset += 1; if (offset >= size) { goto early_end; }
        }
    }

early_end:
    return offset;
}

// this by all means isn't good code, however, it works well enough for now.
bool m3u_info_parse(const char* str, size_t size, struct M3uSongInfo* info)
{
    memset(info, 0, sizeof(*info));

    // starting offset
    size_t start_offset = 0;
    size_t offset = 0;

    // get the filename.
    while (!(str[offset] == ':' && str[offset + 1] == ':'))
    {
        offset++; if (offset >= size) { goto fail; }
    }

    // confirm that this is a gbs file.
    if (memcmp(str + offset + 2, GBS_MAGIC, sizeof(GBS_MAGIC)))
    {
        printf("bad magic, got: %.*s and: %s\n", 3, str + offset + 2, str + offset);
        goto fail;
    }

    info->filename = calloc(offset + 1, sizeof(char));
    memcpy(info->filename, str, offset);

    // skip over the dots, magic and comma
    offset += 2 + sizeof(GBS_MAGIC) + 1; if (offset >= size) { goto fail; }

    // get the song num.
    start_offset = offset;
    while (str[offset] != ',')
    {
        // some m3u prepend with $
        if (str[start_offset] == '$') start_offset++;
        offset++; if (offset >= size) { goto fail; }
    }
    info->songno = atoi(str + start_offset);

    // skip over comma
    offset += 1; if (offset >= size) { goto fail; }

    // get the song name.
    start_offset = offset;
    while (!(str[offset] == ',' && str[offset - 1] != '\\'))
    {
        offset++; if (offset >= size) { goto fail; }
    }

    // copy over name, skipping any backslashes
    info->song_tile = calloc((offset - start_offset) + 1, sizeof(char));
    for (size_t i = start_offset, j = 0; i < offset; i++)
    {
        if (isascii(str[i]) && str[i] != '\\')
        {
            info->song_tile[j++] = str[i];
        }
    }

    // skip over comma
    offset += 1; if (offset >= size) { goto early_end; }

    // get time if available
    offset = m3u_get_time(str, size, offset, &info->time);

    // skip over comma
    offset += 1; if (offset >= size) { goto early_end; }

    // get loop if available
    offset = m3u_get_time(str, size, offset, &info->loop);
    // const char* aaa = "Zen-Nippon Shounen Soccer Taikai";

    // skip over comma
    offset += 1; if (offset >= size) { goto early_end; }

    // get fade if available
    // offset = m3u_get_time(str, size, offset, &info->fade);

    // skip over comma
    // offset += 1; if (offset >= size) { goto early_end; }

    // get loopcount
    info->loopcount = atoi(str + offset);

early_end:
    #if 0
    printf("\tfilename: %s\n", info->filename);
    printf("\tsong_tile: %s\n", info->song_tile);
    printf("\ttime:\n");
        printf("\t\th: %u\n", info->time.h);
        printf("\t\tm: %u\n", info->time.m);
        printf("\t\ts: %u\n", info->time.s);
    printf("\tloop:\n");
        printf("\t\th: %u\n", info->loop.h);
        printf("\t\tm: %u\n", info->loop.m);
        printf("\t\ts: %u\n", info->loop.s);
    printf("\tfade:\n");
        printf("\t\th: %u\n", info->fade.h);
        printf("\t\tm: %u\n", info->fade.m);
        printf("\t\ts: %u\n", info->fade.s);
    printf("\tsongno: %u\n", info->songno);
    printf("\tloopcount: %u\n", info->loopcount);
    #endif
    // printf("got: [%.*s] and: [%s]\n", offset - start_offset, str + start_offset, str + offset);
    // m3u_info_free(info);
    return true;

fail:
    m3u_info_free(info);
    return false;
}

void m3u_info_free(struct M3uSongInfo* info)
{
    if (info->filename)
    {
        free(info->filename);
    }
    if (info->song_tile)
    {
        free(info->song_tile);
    }
    memset(info, 0, sizeof(*info));
}
