# Novacube

This is the repo for my open-source dream game: an open-world survival sandbox game. Check the `docs` directory for more information.

## Building
1. `cd` into the project's root.
2. Run `python3 ./prepare-assets.py --compress-android` (or the equivalent Python command for your system). This will compress and/or copy textures, compile shaders, etc. You need to have `astcenc-avx2` in your `PATH` to be able to compress textures for Android, but the binary name is customizable in the script. You can also pass `--strip-exif` to use the Pillow package to remove EXIF data from the source assets before processing them.
3. Do a standard CMake build.
