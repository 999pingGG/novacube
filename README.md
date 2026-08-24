# Novacube

This is the repo for my open-source dream game: an open-world survival sandbox game. Check the `docs` directory for more
information.

## Building

Install these prerequisites first:

- CMake and a C/C++ toolchain supported by CMake.
- SDL3 installed with its CMake package configuration.
- The Vulkan SDK, including `glslc` on `PATH`.

Texture compressors are optional and platform-specific; you only need the compressor for the asset platform you want
to bake:

- Desktop: make `texconv` available on `PATH`, or pass its executable path with `--texconv`.
- Mobile: make `astcenc-avx2` available on `PATH`, or pass any astcenc executable path with `--astcenc`.

Then do a standard CMake build, for example:

```sh
cmake -S . -B build
cmake --build build
```

Through some CMake magic, the `novacube_dev` target detects changed, added, and removed assets, then re-bakes only the
affected assets before launching the game. The game is its own asset baker, so CMake first builds the game, uses it to
bake the assets, and finally launches it. Isn't this cool?

## Source assets

Assets use this directory layout:

```text
src-assets/<namespace>/shader/<name>.vert
src-assets/<namespace>/shader/<name>.frag
src-assets/<namespace>/shader/<name>.inc.<stage>
src-assets/<namespace>/texture/block/<name>.png
src-assets/<namespace>/texture/gui/<name>.png
```

Namespaces and asset names are C-style identifiers. Textures must be PNG files. Shader entry points support vertex and
fragment stages; files containing `.inc.` are includes and are not compiled directly.

## Asset compiler command line

Try `novacube --help`:

```text
novacube
novacube --help
novacube --version
novacube --build-assets <source-directory> [changed-asset ...]
         [-o <database>]
         [--platform desktop|mobile]
         [--texconv <executable>]
         [--astcenc <executable>]
         [--debug]
         [--strip-png-metadata]
```

With no arguments, the executable runs the game. Omitting changed asset paths performs a full rebuild; otherwise each
path is relative to the source directory. `-o` defaults to `assets.db`, and `--platform` defaults to `desktop`.
Desktop baking uses texconv and mobile baking uses astcenc; the compressor for the other platform is not needed. By
default, the baker resolves `texconv` or `astcenc-avx2` through `PATH`. Use `--texconv <executable>` or
`--astcenc <executable>` to provide its path directly instead.

`--debug` compiles shaders with debug information and disables optimization. `--strip-png-metadata` puts source PNGs on
a diet by removing unnecessary metadata, including EXIF data, to preserve your privacy and debloat them. Automatic
CMake asset builds enable this option.
