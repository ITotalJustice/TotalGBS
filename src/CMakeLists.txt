if (ANDROID_NDK_SDK)
    add_subdirectory(platform/android)
endif()

if (PC)
    add_subdirectory(platform/pc)
endif()

if (GBA)
    add_subdirectory(platform/gba)
endif()

if (NDS)
    add_subdirectory(platform/nds)
endif()

if (3DS)
    add_subdirectory(platform/3ds)
endif()

if (N64)
    add_subdirectory(platform/n64)
endif()

if (GAMECUBE OR WII)
    add_subdirectory(platform/gamecube)
endif()

if (WIIU)
    add_subdirectory(platform/wiiu)
endif()

if (SWITCH)
    add_subdirectory(platform/switch)
endif()

if (PS1)
    add_subdirectory(platform/ps1)
endif()

if (PS2)
    add_subdirectory(platform/ps2)
endif()

if (PS4)
    add_subdirectory(platform/ps4)
endif()

if (PSP)
    add_subdirectory(platform/psp)
endif()

if (VITA)
    add_subdirectory(platform/vita)
endif()

if (NXDK)
    add_subdirectory(platform/xbox)
endif()

if (XENON)
    add_subdirectory(platform/xbox360)
endif()

if (DREAMCAST)
    kos_enable_sh4_math()
    add_subdirectory(platform/dreamcast)
endif()

if (EMSCRIPTEN)
    add_subdirectory(platform/emscripten)
endif()

add_subdirectory(libgbs)
