#!/usr/bin/env python3
import argparse
import shutil
import subprocess
from pathlib import Path
import sys

# ---------------- CONFIG ----------------

SOURCE_DIR = Path('src-assets')

ANDROID_ASSETS = Path('android-project/app/src/main/assets')
PC_ASSETS = Path('assets')

TEXTURE_DIRS = [
    SOURCE_DIR / 'textures',
]

# These images carry numeric data rather than human-viewable sRGB color. Keep this list in sync with the texture type
# passed to the renderer API. Alpha-only masks belong here because alpha is always linear.
DATA_TEXTURES = {
    Path('textures/gui/crosshair.png'),
}

IMAGE_EXTS = ['.png', '.jpg', '.jpeg']

SHADER_DIRS = [
    SOURCE_DIR / 'shaders',
]

SHADER_STAGES = {'vert', 'frag', 'comp'}

ASTCENC_COMMAND = 'astcenc-avx2'

# ---------------------------------------


def run(cmd):
    result = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    if result.returncode != 0:
        print("Command failed:")
        print(" ".join(cmd))
        if result.stdout:
            print("---- stdout ----")
            print(result.stdout)
        if result.stderr:
            print("---- stderr ----")
            print(result.stderr)

    return result.returncode == 0


def strip_exif_if_requested(path: Path, enabled: bool):
    if not enabled:
        return

    try:
        from PIL import Image
    except ImportError:
        print('EXIF stripping requested but Pillow is not installed.', file=sys.stderr)
        sys.exit(1)

    img = Image.open(path)
    data = list(img.getdata())
    clean = Image.new(img.mode, img.size)
    clean.putdata(data)
    clean.save(path)


def process_textures(compress_android, strip_exif):
    for base_dir in TEXTURE_DIRS:
        for img in base_dir.rglob('*'):
            if img.suffix.lower() not in IMAGE_EXTS:
                continue

            strip_exif_if_requested(img, strip_exif)

            rel = img.relative_to(base_dir.parent)
            block = '4x4'

            # -------- ANDROID --------
            if compress_android:
                android_out_dir = ANDROID_ASSETS / rel.parent
                android_out_dir.mkdir(parents=True, exist_ok=True)
                out = android_out_dir / (img.stem + '.astc')
                print(f'[ANDROID] Compressing {img} -> {out}')
                profile = '-cl' if rel in DATA_TEXTURES else '-cs'
                run([
                    ASTCENC_COMMAND,
                    profile,
                    str(img),
                    str(out),
                    block,
                    '-exhaustive',
                ])

            # -------- PC --------
            pc_out_dir = PC_ASSETS / rel.parent
            pc_out_dir.mkdir(parents=True, exist_ok=True)

            # No compression for now.
            out = pc_out_dir / img.name
            print(f'[PC] Copying {img} -> {out}')
            shutil.copy2(img, out)
            strip_exif_if_requested(out, strip_exif)


def compile_shaders(debug):
    out_dir = PC_ASSETS / 'shaders'
    out_dir.mkdir(parents=True, exist_ok=True)
    out_dir_android = ANDROID_ASSETS / 'shaders'
    out_dir_android.mkdir(parents=True, exist_ok=True)

    for base_dir in SHADER_DIRS:
        for shader in base_dir.rglob('*'):
            stage = shader.suffix.lstrip('.')
            if stage not in SHADER_STAGES:
                continue

            out = out_dir / f'{shader.stem}-{stage}.spv'
            out_android = out_dir_android / f'{shader.stem}-{stage}.spv'

            print(f'[SHADER] Compiling {shader} -> {out}')
            
            params = ['glslc', '--target-env=vulkan1.1', str(shader), '-o', str(out)]
            if debug:
                params.extend(['-g', '-O0'])
            else:
                params.append('-O')

            if run(params):
                print(f'[SHADER] Copying {out} -> {out_android}')
                shutil.copy2(out, out_android)


def main():
    parser = argparse.ArgumentParser(description='Unlit asset pipeline.')
    parser.add_argument('--compress-android', action='store_true')
    parser.add_argument('--strip-exif', action='store_true')
    parser.add_argument('--debug', action='store_true')

    args = parser.parse_args()

    # TODO: Wipe the current asset output directory.

    process_textures(
        compress_android=args.compress_android,
        strip_exif=args.strip_exif,
    )
    print('Textures done.')

    compile_shaders(debug=args.debug)
    print('Shaders compiled.')


if __name__ == "__main__":
    main()
