# Novacube

This is the repo for my open-source dream game: an open-world survival sandbox game. Check the `docs` directory for more
information.

## Building

1. Install SDL3 globally with CMake.
   1. Open an administrative command prompt (Windows only) or a terminal (Linux).
   2. `git clone https://github.com/libsdl-org/SDL`
   3. `cd SDL`
   4. `cmake build -B build -DCMAKE_BUILD_TYPE=Debug` (you can change the build type to `Release` or any other CMake
      build type).
   5. `cmake --build build`
   6. `cmake --install build` (add `sudo` in Linux).

   You might get CMake errors from SDL3 telling you extra libraries need to be installed. You can instead add the given
   command line argument to exclude compiling against said libraries, this game doesn't need them so far.

2. `cd` into the project's root.
3. Run `python3 ./prepare-assets.py --compress-android` (the script is also directly executable on Linux). This will
   compress and/or copy textures, compile shaders, etc. You need to have `astcenc-avx2` in your `PATH` to be able to
   compress textures for Android, but the binary name is customizable in the script. You can also pass `--strip-exif` to
   strip EXIF data from the source assets before processing them. Useful to not commit images with metadata in them.
4. Do a standard CMake build.
