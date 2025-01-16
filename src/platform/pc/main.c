#include "gbs.h"

#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>
#include <minizip/unzip.h>
#include "wav_writer/wav_writer.h"
#include "m3u/m3u.h"
#include "args/args.h"

typedef enum AppResult {
    AppResult_SUCCESS,
    AppResult_FALIURE,
    AppResult_CONTINUE,
} AppResult;

typedef struct Archive {
    void* gbs_data;
    size_t gbs_size;

    struct M3uSongInfo* infos;
    size_t m3u_count;
} Archive;

typedef struct App {
    Gbs* gbs;
    struct GbsMeta gbs_meta;
    int song_number;

    Archive archive;

    SDL_AudioSpec obtained_spec;
    SDL_AudioDeviceID audio_device_id;
    bool quit;

    int end_time;
    volatile int song_internal_time_elapsed;
    // number of seconds
    volatile int song_time_elapsed;

    char output_name[512];
} App;

enum ArgsId {
    ArgsId_help,
    ArgsId_version,
    ArgsId_rom,
    ArgsId_info,
    ArgsId_song,
    ArgsId_freq,
    ArgsId_gbs2gb,
    ArgsId_wav,
};

#define ARGS_ENTRY(_key, _type, _single) \
    { .key = #_key, .id = ArgsId_##_key, .type = _type, .single = _single },

static const struct ArgsMeta ARGS_META[] = {
    ARGS_ENTRY(help, ArgsValueType_NONE, 'h')
    ARGS_ENTRY(version, ArgsValueType_NONE, 'v')
    ARGS_ENTRY(rom, ArgsValueType_STR, 'r')
    ARGS_ENTRY(info, ArgsValueType_NONE, 'i')
    ARGS_ENTRY(song, ArgsValueType_INT, 's')
    ARGS_ENTRY(freq, ArgsValueType_INT, 'f')
    ARGS_ENTRY(wav, ArgsValueType_STR, 'w')
    ARGS_ENTRY(gbs2gb, ArgsValueType_STR, 'g')
};

static void sdl2_callback(void* user, unsigned char* data, int count)
{
    const int number_of_samples = count / sizeof(short);
    short* samples = (short*)data;

    App* app = user;
    gbs_run(app->gbs, gbs_clocks_needed(app->gbs, number_of_samples));
    gbs_read_samples(app->gbs, samples, number_of_samples);

    app->song_internal_time_elapsed += number_of_samples / 2; // for stereo.
    if (app->song_internal_time_elapsed >= (int)app->obtained_spec.freq + number_of_samples / 2) {
        app->song_internal_time_elapsed -= (int)app->obtained_spec.freq + number_of_samples / 2;
        app->song_time_elapsed++;
    }
}

static const struct M3uSongInfo* find_info_from_song(const Archive* archive, unsigned song)
{
    for (size_t i = 0; i < archive->m3u_count; i++) {
        if (archive->infos[i].songno == song) {
            return &archive->infos[i];
        }
    }

    return NULL;
}

// returns the end time
static void play_song(App* app, unsigned song)
{
    song %= app->gbs_meta.max_song;
    song = song < app->gbs_meta.first_song ? app->gbs_meta.first_song : song;

    SDL_LockAudioDevice(app->audio_device_id);
        app->song_internal_time_elapsed = 0;
        app->song_time_elapsed = 0;
        app->song_number = song;
        gbs_set_song(app->gbs, app->song_number);

        const struct M3uSongInfo* info = find_info_from_song(&app->archive, song);
        if (info) {
            printf("now playing [%u] %s %u\n", song, info->song_tile, info->time);
        }
        else {
            printf("now playing [%u]\n", song);
        }

        // config stuff to save to json
        const int song_timout_seconds = 3 * 60;
        const bool apply_loopcount = false;

        if (info && info->time) {
            if (apply_loopcount && info->loopcount) {
                app->end_time = info->time * info->loopcount;
            }
            else {
                app->end_time = info->time;
            }
        }
        else {
            app->end_time = song_timout_seconds;
        }
    SDL_UnlockAudioDevice(app->audio_device_id);
}

// returns true if it found a valid gbs
static bool parse_zip(zlib_filefunc_def* ff, const char* path, Archive* archive)
{
    // this is safe to be NULL, it'll call stdio internally
    unzFile* zf = unzOpen2(path, ff);

    if (zf)
    {
        unz_global_info global_info;
        if (UNZ_OK == unzGetGlobalInfo(zf, &global_info))
        {
            bool already_jumped = false;
        start_again:
            for (unsigned i = 0; i < global_info.number_entry; i++)
            {
                if (UNZ_OK == unzOpenCurrentFile(zf))
                {
                    unz_file_info file_info = {0};
                    char name[256] = {0};

                    if (UNZ_OK == unzGetCurrentFileInfo(zf, &file_info, name, sizeof(name), NULL, 0, NULL, 0))
                    {
                        const char* ext = SDL_strrchr(name, '.');
                        if (!archive->gbs_data && !SDL_strcasecmp(ext, ".gbs") && file_info.uncompressed_size >= 0x70)
                        {
                            // read a chunk of data, see if it's valid
                            char header[0x70];
                            int result = unzReadCurrentFile(zf, header, sizeof(header));
                            if (result > 0 && result == sizeof(header))
                            {
                                if (gbs_validate_file_mem(header, sizeof(header)))
                                {
                                    archive->gbs_data = SDL_malloc(file_info.uncompressed_size);
                                    if (archive->gbs_data)
                                    {
                                        SDL_memcpy(archive->gbs_data, header, sizeof(header));
                                        const int read_size = file_info.uncompressed_size - sizeof(header);
                                        result = unzReadCurrentFile(zf, archive->gbs_data + sizeof(header), read_size);
                                        if (result > 0 && result == read_size)
                                        {
                                            archive->gbs_size = file_info.uncompressed_size;
                                        }
                                        else
                                        {
                                            SDL_free(archive->gbs_data);
                                            archive->gbs_data = NULL;
                                        }
                                    }
                                }
                            }
                        }
                        else if (archive->gbs_data && !SDL_strcasecmp(ext, ".m3u") && file_info.uncompressed_size < 512)
                        {
                            char buffer[file_info.uncompressed_size + 1];
                            buffer[file_info.uncompressed_size] = '\0';

                            int result = unzReadCurrentFile(zf, buffer, sizeof(buffer));
                            if (result > 0 && (unsigned)result == file_info.uncompressed_size)
                            {
                                struct M3uSongInfo m3u_info;
                                if (m3u_info_parse(buffer, sizeof(buffer), &m3u_info))
                                {
                                    archive->m3u_count++;
                                    archive->infos = SDL_realloc(archive->infos, archive->m3u_count * sizeof(*archive->infos));
                                    archive->infos[archive->m3u_count-1] = m3u_info;
                                }
                            }
                        }
                    }
                    unzCloseCurrentFile(zf);
                }

                if (archive->gbs_data && !already_jumped)
                {
                    already_jumped = true;
                    unzGoToFirstFile(zf);
                    goto start_again;
                }
                // advance to the next file (if there is one)
                else if (i + 1 < global_info.number_entry)
                {
                    unzGoToNextFile(zf); // todo: error handling
                }
            }
        }
        unzClose(zf);
    }

    return archive->gbs_data != NULL;
}

static size_t gbs_io_write(void* user, const void* src, size_t size, size_t addr)
{
    SDL_RWseek(user, addr, RW_SEEK_SET);
    return SDL_RWwrite(user, src, 1, size);
}

static bool parse_file(const char* path, Archive* archive)
{
    archive->gbs_data = SDL_LoadFile(path, &archive->gbs_size);
    if (!archive->gbs_data) {
        goto fail;
    }

    return true;
fail:
    if (archive->gbs_data) {
        SDL_free(archive->gbs_data);
        archive->gbs_data = NULL;
    }

    return false;
}

static bool load_archive(zlib_filefunc_def* ff, const char* path, Archive* archive)
{
    SDL_memset(archive, 0, sizeof(*archive));

    const char* ext = SDL_strrchr(path, '.');

    if (!SDL_strcasecmp(ext, ".gbs")) {
        return parse_file(path, archive);
    }
    else if (!SDL_strcasecmp(ext, ".zip")) {
        return parse_zip(ff, path, archive);
    }
    // todo: gbs files are commonly distributed in 7zip format
    else if (!SDL_strcasecmp(ext, ".7z")) {
    }

    return false;
}

static int cmpfunc(const void* a, const void* b)
{
    const struct M3uSongInfo* info_a = a;
    const struct M3uSongInfo* info_b = b;
    return (int)info_a->songno - (int)info_b->songno;
}

static void archive_close(Archive* archive)
{
    if (archive->gbs_data) {
        SDL_free(archive->gbs_data);
    }

    for (size_t i = 0; i < archive->m3u_count; i++) {
        m3u_info_free(&archive->infos[i]);
    }
    SDL_memset(archive, 0, sizeof(*archive));
}

static void convert_str_to_printable_chars(char* str)
{
    const size_t len = SDL_strlen(str);
    for (size_t i = 0; i < len; i++) {
        if (str[i] < 32) {
            str[i] = '-';
        }
    }
}

static bool do_gbs2gb(App* app, const char* dir)
{
    char path[512];
    SDL_snprintf(path, sizeof(path), "%s/%s.%s", dir, app->output_name, gbs2gb_get_system_type(app->gbs) == Gbs2GbSystem_DMG ? "gb" : "gbc");

    struct GbsWriteIo write_io;
    write_io.user = SDL_RWFromFile(path, "wb");
    if (!write_io.user){
        return false;
    }

    write_io.write = gbs_io_write;
    const bool result = gbs2gb_io(app->gbs, &write_io);
    SDL_RWclose(write_io.user);

    if (!result) {
        SDL_SetError("\nfailed to write gbc file!\n");
    }
    else {
        printf("output: \"%s\"\n", path);
    }
    return result;
}

static bool do_wav_song(App* app, const char* dir, int freq, unsigned char song)
{
    if (!gbs_set_song(app->gbs, song)) {
        SDL_SetError("failed to set song: %u", song);
        return false;
    }

    char path[512];
    const struct M3uSongInfo* info = find_info_from_song(&app->archive, song);
    if (info) {
        SDL_snprintf(path, sizeof(path), "%s/%s - %u - %s.wav", dir, app->output_name, song, info->song_tile);
    }
    else {
        SDL_snprintf(path, sizeof(path), "%s/%s - %u.wav", dir, app->output_name, song);
    }

    if (!wav_open(path, freq, 2, 16)) {
        SDL_SetError("failed to open wav: %s", path);
        return false;
    }

    unsigned time = 60*3;
    if (info) {
        time = info->time;
    }

    static short samples[48000*2/10];
    const unsigned number_of_samples = sizeof(samples) / sizeof(short);

    for (unsigned i = 0; i < time; i++) {
        for (unsigned j = 0; j < 10; j++) {
            gbs_run(app->gbs, gbs_clocks_needed(app->gbs, number_of_samples));
            gbs_read_samples(app->gbs, samples, number_of_samples);
            wav_write(samples, sizeof(samples));
        }
    }

    wav_close();
    return true;
}

static bool do_wav(App* app, const char* dir, int freq)
{
    for (unsigned i = 0; i < app->gbs_meta.max_song; i++) {
        if (!do_wav_song(app, dir, freq, app->gbs_meta.first_song + i)) {
            return false;
        }
    }

    return true;
}

static int print_usage(int code) {
    printf("\
[TotalGBS " LIBGBS_VERSION_STR " By TotalJustice] \n\n\
Usage\n\n\
    -h, --help      = Display help.\n\
    -v, --version   = Display version.\n\
    -r, --rom       = Set GBS rom.\n\
    -i, --info      = Log info.\n\
    -s, --song      = Set song.\n\
    -f, --freq      = Set output frequency.\n\
    -w, --wav       = Output folder to convert song(s) to wav.\n\
    -g, --gbs2gb    = Output folder to convert GBS rom to gb rom.\n\
    \n");

    return code;
}

static AppResult app_init(void** appstate, int argc, char** argv)
{
    App* app = SDL_calloc(1, sizeof(*app));
    if (!app) {
        return AppResult_FALIURE;
    }
    *appstate = app;

    if (argc < 2) {
        print_usage(0);
        SDL_SetError("missing args...\n");
        return AppResult_FALIURE;
    }

    const char* rom_file = NULL;
    const char* gbs2gb = NULL;
    const char* wav = NULL;
    int freq = 48000;
    int song = -1;
    bool info = false;

    int arg_index = 1;
    struct ArgsData arg_data;
    enum ArgsResult arg_result;
    while (!(arg_result = args_parse(&arg_index, argc, argv, ARGS_META, SDL_arraysize(ARGS_META), &arg_data))) {
        switch (ARGS_META[arg_data.meta_index].id) {
            case ArgsId_help:
            case ArgsId_version:
                print_usage(0);
                return AppResult_SUCCESS;
            case ArgsId_rom:
                rom_file = arg_data.value.s;
                break;
            case ArgsId_song:
                song = arg_data.value.i;
                break;
            case ArgsId_freq:
                freq = arg_data.value.i;
                break;
            case ArgsId_info:
                info = true;
                break;
            case ArgsId_wav:
                wav = arg_data.value.s;
                break;
            case ArgsId_gbs2gb:
                gbs2gb = arg_data.value.s;
                break;
        }
    }

    // handle error.
    if (arg_result < 0) {
        if (arg_result == ArgsResult_UNKNOWN_KEY) {
            SDL_SetError("unknown arg [%s]", argv[arg_index]);
        }
        else if (arg_result == ArgsResult_BAD_VALUE) {
            SDL_SetError("arg [--%s] had bad value type [%s]", ARGS_META[arg_data.meta_index].key, arg_data.value.s);
        }
        else if (arg_result == ArgsResult_MISSING_VALUE) {
            SDL_SetError("arg [--%s] requires a value", ARGS_META[arg_data.meta_index].key);
        }
        else {
            SDL_SetError("bad args: %d", arg_result);
        }
        return AppResult_FALIURE;
    }
    // handle warning.
    else if (arg_result == ArgsResult_EXTRA_ARGS) {
        if (!rom_file) {
            rom_file = argv[arg_index];
        }
    }
    else if (arg_index < argc) {
        rom_file = argv[arg_index];
    }

    if (!rom_file) {
        SDL_SetError("no rom file loaded\n");
        return AppResult_FALIURE;
    }

    if (!load_archive(NULL, rom_file, &app->archive)) {
        SDL_SetError("bad m3u\n");
        return AppResult_FALIURE;
    }

    SDL_qsort(app->archive.infos, app->archive.m3u_count, sizeof(*app->archive.infos), cmpfunc);

    app->gbs = gbs_init(freq);
    if (!app->gbs) {
        SDL_SetError("failed to init gbs...\n");
        return AppResult_FALIURE;
    }

    if (!gbs_load_mem(app->gbs, app->archive.gbs_data, app->archive.gbs_size)) {
        SDL_SetError("invalid movie file at: %s\n", rom_file);
        return AppResult_FALIURE;
    }

    gbs_set_master_volume(app->gbs, 1.0);
    if (!gbs_get_meta(app->gbs, &app->gbs_meta)) {
        SDL_SetError("failed to get gbs meta\n");
        return AppResult_FALIURE;
    }

    convert_str_to_printable_chars(app->gbs_meta.title_string);
    convert_str_to_printable_chars(app->gbs_meta.author_string);
    convert_str_to_printable_chars(app->gbs_meta.copyright_string);

    printf("SUCCESS GBS file: %s loaded!\n", rom_file);
    printf("\tfirst_song: %u\n", app->gbs_meta.first_song);
    printf("\tmax_song: %u\n", app->gbs_meta.max_song);
    printf("\ttitle_string: %s\n", app->gbs_meta.title_string);
    printf("\tauthor_string: %s\n", app->gbs_meta.author_string);
    printf("\tcopyright_string: %s\n", app->gbs_meta.copyright_string);

    SDL_snprintf(app->output_name, sizeof(app->output_name), "TotalGBS - %s By %s", app->gbs_meta.title_string, app->gbs_meta.author_string);

    if (info) {
        for (unsigned i = 0; i < app->archive.m3u_count; i++) {
            printf("M3u info [%u]\n", i);
            printf("\tfilename: %s\n", app->archive.infos[i].filename);
            printf("\tsong_tile: %s\n", app->archive.infos[i].song_tile);
            printf("\ttime: %u\n", app->archive.infos[i].time);
            printf("\tloop: %u\n", app->archive.infos[i].loop);
            printf("\tfade: %u\n", app->archive.infos[i].fade);
            printf("\tsongno: %u\n", app->archive.infos[i].songno);
            printf("\tloopcount: %u\n", app->archive.infos[i].loopcount);
        }
        return AppResult_SUCCESS;
    }
    else if (wav) {
        if (song >= 0) {
            return do_wav_song(app, wav, freq, song) ? AppResult_SUCCESS : AppResult_FALIURE;
        }
        return do_wav(app, wav, freq) ? AppResult_SUCCESS : AppResult_FALIURE;
    }
    else if (gbs2gb) {
        return do_gbs2gb(app, gbs2gb) ? AppResult_SUCCESS : AppResult_FALIURE;
    }

    if (SDL_Init(SDL_INIT_AUDIO)) {
        return AppResult_FALIURE;
    }

    const SDL_AudioSpec wanted_spec = {
        .freq = freq,
        .format = AUDIO_S16SYS,
        .channels = 2,
        .samples = 2048,
        .callback = sdl2_callback,
        .userdata = app,
    };

    app->audio_device_id = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &app->obtained_spec, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (!app->audio_device_id) {
        return AppResult_FALIURE;
    }

    app->song_number = app->gbs_meta.first_song;
    if (song >= 0) {
        app->song_number = song;
    }

    // and play :)
    play_song(app, app->song_number);
    SDL_PauseAudioDevice(app->audio_device_id, 0);

    printf("\nControls:\n");
    printf("\tn = Play next song.\n");
    printf("\tp = Play previous song.\n");
    printf("\tr = Play Random song.\n");
    printf("\tq = Quit.\n\n");

    return AppResult_CONTINUE;
}

static AppResult app_loop(void *appstate)
{
    App* app = appstate;

    const int c = getchar();
    switch (c) {
        case 'n': play_song(app, app->song_number + 1); break;
        case 'p': play_song(app, app->song_number > app->gbs_meta.first_song ? app->song_number - 1 : app->gbs_meta.max_song - 1); break;
        case 'r': play_song(app, rand()); break;
        case 'q': app->quit = true; break;
    }
    return app->quit ? AppResult_SUCCESS : AppResult_CONTINUE;
}

static void app_exit(void *appstate, AppResult result)
{
    App* app = appstate;
    if (result == AppResult_FALIURE) {
        printf("Failed: %s\n", SDL_GetError());
    }

    if (app) {
        SDL_PauseAudioDevice(app->audio_device_id, 1);
        SDL_CloseAudioDevice(app->audio_device_id);
        SDL_Quit();

        archive_close(&app->archive);
        gbs_quit(app->gbs);
        SDL_free(app);
    }
}

int main(int argc, char** argv)
{
    void* appstate = NULL;
    AppResult result = app_init(&appstate, argc, argv);
    while (result == AppResult_CONTINUE) {
        result = app_loop(appstate);
    }
    app_exit(appstate, result);
    return result == AppResult_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
