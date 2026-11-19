#!/usr/bin/env python3
"""
Cross-compiler build & packaging script for AROMA-Next-Installer.
Supports Zig (`zig cc`) to produce static Linux/Android recovery binaries
for arm64-v8a (aarch64), armeabi-v7a (arm), and x86_64, plus flashable ZIPs.
"""

import argparse
import datetime
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent

CORE_SRCS = [
    # zlib
    "libs/zlib/adler32.c",
    "libs/zlib/crc32.c",
    "libs/zlib/infback.c",
    "libs/zlib/inffast.c",
    "libs/zlib/inflate.c",
    "libs/zlib/inftrees.c",
    "libs/zlib/zutil.c",
    # png
    "libs/png/png.c",
    "libs/png/pngerror.c",
    "libs/png/pnggccrd.c",
    "libs/png/pngget.c",
    "libs/png/pngmem.c",
    "libs/png/pngpread.c",
    "libs/png/pngread.c",
    "libs/png/pngrio.c",
    "libs/png/pngrtran.c",
    "libs/png/pngrutil.c",
    "libs/png/pngset.c",
    "libs/png/pngtrans.c",
    "libs/png/pngvcrd.c",
    # minutf8 & minzip
    "libs/minutf8/minutf8.c",
    "libs/minzip/DirUtil.c",
    "libs/minzip/Hash.c",
    "libs/minzip/Inlines.c",
    "libs/minzip/SysUtil.c",
    "libs/minzip/Zip.c",
    # freetype
    "libs/freetype/autofit/autofit.c",
    "libs/freetype/base/basepic.c",
    "libs/freetype/base/ftapi.c",
    "libs/freetype/base/ftbase.c",
    "libs/freetype/base/ftbbox.c",
    "libs/freetype/base/ftbitmap.c",
    "libs/freetype/base/ftglyph.c",
    "libs/freetype/base/ftinit.c",
    "libs/freetype/base/ftpic.c",
    "libs/freetype/base/ftstroke.c",
    "libs/freetype/base/ftsynth.c",
    "libs/freetype/base/ftsystem.c",
    "libs/freetype/base/ftlcdfil.c",
    "libs/freetype/cff/cff.c",
    "libs/freetype/pshinter/pshinter.c",
    "libs/freetype/psnames/psnames.c",
    "libs/freetype/raster/raster.c",
    "libs/freetype/sfnt/sfnt.c",
    "libs/freetype/smooth/smooth.c",
    "libs/freetype/truetype/truetype.c",
    # edify
    "src/edify/expr.c",
    "src/edify/lex.yy.c",
    "src/edify/parser.c",
    # controls
    "src/controls/aroma_controls.c",
    "src/controls/aroma_control_button.c",
    "src/controls/aroma_control_check.c",
    "src/controls/aroma_control_checkbox.c",
    "src/controls/aroma_control_menubox.c",
    "src/controls/aroma_control_checkopt.c",
    "src/controls/aroma_control_optbox.c",
    "src/controls/aroma_control_textbox.c",
    "src/controls/aroma_control_threads.c",
    "src/controls/aroma_control_imgbutton.c",
    # libs
    "src/libs/aroma_array.c",
    "src/libs/aroma_freetype.c",
    "src/libs/aroma_graph.c",
    "src/libs/graph_drm.c",
    "src/libs/aroma_input.c",
    "src/libs/aroma_languages.c",
    "src/libs/aroma_libs.c",
    "src/libs/aroma_memory.c",
    "src/libs/aroma_png.c",
    "src/libs/aroma_zip.c",
    # main
    "src/main/aroma_ui.c",
    "src/main/aroma_installer.c",
    "src/main/aroma.c",
]

TARGETS = {
    "arm64": {
        "zig_target": "aarch64-linux-musl",
        "out_name": "aroma_installer-arm64",
        "cflags": ["-D__ARM_NEON__"],
        "extra_srcs": [],
    },
    "arm32": {
        "zig_target": "arm-linux-musleabihf",
        "out_name": "aroma_installer-arm32",
        "cflags": ["-mcpu=cortex_a7", "-D__ARM_NEON__"],
        "extra_srcs": [],
    },
    "x86_64": {
        "zig_target": "x86_64-linux-musl",
        "out_name": "aroma_installer-x86_64",
        "cflags": [],
        "extra_srcs": [],
    },
}

BUILD_VERSION = datetime.datetime.now().strftime("%y.%m")
BUILD_NUM = datetime.datetime.now().strftime("%d.%H")

COMMON_CFLAGS = [
    "-O2",
    "-static",
    "-std=gnu11",
    "-DFT2_BUILD_LIBRARY=1",
    "-DDARWIN_NO_CARBON",
    "-D_AROMA_NODEBUG",
    "-DAROMA_NAME=\"AROMA-Next-Installer\"",
    f"-DAROMA_VERSION=\"{BUILD_VERSION}\"",
    f"-DAROMA_BUILD=\"{BUILD_NUM}\"",
    "-DAROMA_BUILD_CN=\"NextGen\"",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wno-deprecated-non-prototype",
    "-Wno-implicit-function-declaration",
    "-Wno-int-conversion",
    "-Wno-pointer-sign",
    "-Wno-incompatible-pointer-types",
]


def find_zig(explicit: str | None) -> str:
    if explicit:
        return explicit
    env = os.environ.get("ZIG_EXE")
    if env and Path(env).exists():
        return env
    found = shutil.which("zig")
    if found:
        return found
    raise SystemExit("zig executable not found; pass --zig <path> or set ZIG_EXE")


def compile_target(zig: str, arch: str, cfg: dict) -> Path:
    out_dir = ROOT / "out" / "bin"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_bin = out_dir / cfg["out_name"]

    srcs = [str(ROOT / s) for s in CORE_SRCS + cfg["extra_srcs"]]
    cmd = [
        zig,
        "cc",
        "-target",
        cfg["zig_target"],
        *COMMON_CFLAGS,
        *cfg["cflags"],
        f"-I{ROOT / 'include'}",
        f"-I{ROOT / 'src'}",
        *srcs,
        "-Wl,--gc-sections",
        "-Wl,-s",
        "-lm",
        "-lpthread",
        "-o",
        str(out_bin),
    ]
    print(f"[{arch}] Building {out_bin.name} ({cfg['zig_target']})...")
    res = subprocess.run(cmd, cwd=str(ROOT))
    if res.returncode != 0:
        raise SystemExit(f"Build failed for {arch} (exit {res.returncode})")
    print(f"[{arch}] OK: {out_bin} ({out_bin.stat().st_size} bytes)")
    return out_bin


def build_zip(out_zip: Path, include_magisk_template: bool, bins: dict[str, Path]) -> None:
    out_zip.parent.mkdir(parents=True, exist_ok=True)
    assets_dir = ROOT / "assets"
    magisk_dir = ROOT / "magisk-template"

    entries: dict[str, tuple[bytes, int]] = {}

    for p in sorted(assets_dir.rglob("*")):
        if p.is_file():
            rel = p.relative_to(assets_dir).as_posix()
            if rel in ("META-INF/CERT.RSA", "META-INF/CERT.SF", "META-INF/MANIFEST.MF"):
                continue
            if include_magisk_template and rel == "META-INF/com/google/android/update-binary-installer":
                continue
            mode = 0o755 if rel.endswith((".sh", "update-binary", "update-binary-installer", "sleep")) else 0o644
            entries[rel] = (p.read_bytes(), mode)

    for _, bin_path in bins.items():
        arc = f"META-INF/com/google/android/bin/{bin_path.name}"
        entries[arc] = (bin_path.read_bytes(), 0o755)

    if include_magisk_template and magisk_dir.exists():
        for p in sorted(magisk_dir.rglob("*")):
            if p.is_file():
                rel = p.relative_to(magisk_dir).as_posix()
                mode = 0o755 if rel.endswith((".sh", "magisk-installer")) else 0o644
                entries[rel] = (p.read_bytes(), mode)

    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for arc in sorted(entries.keys()):
            data, mode = entries[arc]
            zi = zipfile.ZipInfo(arc)
            zi.external_attr = mode << 16
            zf.writestr(zi, data, compress_type=zipfile.ZIP_DEFLATED)

    print(f"[zip] Created {out_zip} ({out_zip.stat().st_size} bytes, {len(entries)} entries)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--zig", help="Path to zig executable")
    ap.add_argument("--arch", nargs="+", default=["arm64", "arm32"], choices=list(TARGETS.keys()))
    ap.add_argument("--no-zip", action="store_true")
    args = ap.parse_args()

    zig = find_zig(args.zig)
    built: dict[str, Path] = {}
    for a in args.arch:
        built[a] = compile_target(zig, a, TARGETS[a])

    if not args.no_zip:
        build_zip(ROOT / "dist" / "AROMA-Next-Installer.zip", False, built)
        build_zip(ROOT / "dist" / "AROMA-Next-Magisk-Template.zip", True, built)


if __name__ == "__main__":
    main()
