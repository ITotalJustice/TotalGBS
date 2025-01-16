# TotalGBS

TotalGBS is a GBS player written in C.

- Very high compatability (i haven't found a gbs file that doesn't work).
- gbs2gb export function.
- gbs2gba export function.
- convert individual songs / entire gbs files to wav.

## Platforms

Currently there are 2 main ports, PC and GBA.

### PC

To build the PC port:

```cmake
cmake --preset pc
cmake --build --preset pc
```

To run the PC port:

```sh
./build/pc/src/platform/TotalGBS --rom path_to_rom.gbs
```

### GBA

To build the GBA port, place all gbs roms (.gbs or .zip) in a folder named `roms` in the root of the source folder:

```cmake
cmake --preset gba
cmake --build --preset gba
```

This will output a .gba rom for each .gbs file into `./build/gba/out/`
