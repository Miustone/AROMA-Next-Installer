# AROMA-Next-Installer

Modern `arm64-v8a` + `armeabi-v7a` port of Ahmad Amarullah's **AROMA Installer** (Apache 2.0), updated for modern Android recoveries (TWRP, OrangeFox, PitchBlack) and dual-mode **Magisk / KernelSU / APatch** module packaging.

## What Changed vs. Upstream `amarullz/AROMA-Installer`

1. **64-bit (`arm64-v8a`) & Modern C11 Portability:**
   - Fixed 64-bit pointer truncation in `src/libs/aroma_memory.c` (`uintptr_t` / `%llx`) and `intptr_t` thread argument casts in `src/libs/aroma_graph.c`.
   - Fixed `sizeof(AARRAY)` allocation bug and `NULL`-key crash after deletion in `src/libs/aroma_array.c`.
   - Fixed `mkdir(..., 755)` octal bug (`0755`) in `src/libs/aroma_zip.c`, `fabs()` in `src/libs/aroma_libs.c`, uninitialized loop index in `src/libs/neon/blt_neon.c`, and C11 inline linkage in `libs/minzip/inline_magic.h`.
   - Updated `Android.mk` + added `Application.mk` so `arm64-v8a` compiles with C NEON intrinsics while excluding 32-bit-only `.s` assembly.

2. **Display Engine (`fbdev` + Pure-ioctl `DRM/KMS`):**
   - Added `src/libs/graph_drm.c`: pure Linux kernel ioctl DRM/KMS dumb-buffer backend (`/dev/dri/card0..3`, `DIRTYFB` flush for command-mode OLED panels) with zero external `libdrm.so` dependency.
   - Enhanced `fbdev` backend in `src/libs/aroma_graph.c`: supports `/dev/graphics/fb0` and `/dev/fb0`, validates `mmap != MAP_FAILED`, and reads real `red/green/blue/transp` channel offsets from `FBIOGET_VSCREENINFO` instead of hardcoding `16/8/0/24`.
   - Clean exit (`AROMA_EXIT_NO_DISPLAY = 77`) + parent `SIGCONT` if neither `fbdev` nor DRM master can be acquired — prevents recovery freezes.
   - Note on stock TWRP/OrangeFox DRM: On recoveries that use DRM/KMS without releasing DRM master while a child `update-binary` runs, `DRM_IOCTL_SET_MASTER` can still return `EBUSY`/`EACCES`. In that case `AROMA-Next` exits cleanly with code `77` and the dispatcher automatically runs the non-GUI fallback installer instead of hanging the recovery.

3. **Modern Touch & Input (`src/libs/input/`):**
   - Raised `INDR_MAXDEV` from `15` to `64` so touchscreens on `/dev/input/event15+` are detected.
   - Added Linux Multitouch Protocol Type-B (`ABS_MT_SLOT`) filtering so secondary fingers don't corrupt slot-0 coordinates.
   - Replaced shared `static` touch-release globals with per-device state in `INDR_POS`, added `BTN_TOUCH` up handling, fixed `ABS_MT_POSITION` sign shift, and added `ABS_X`/`ABS_Y` calibration fallback.
   - Added modern `/sys/class/leds/vibrator/{duration,activate}` haptic fallback in `src/libs/aroma_input.c`.

4. **Universal Multi-Arch Recovery + Magisk Module Dual-Mode:**
   - `assets/META-INF/com/google/android/update-binary` is a universal dispatcher:
     - **Recovery (`BOOTMODE=false`):** Detects `uname -m` (`aarch64` / `armv7l` / `x86_64`), launches the matching static `aroma_installer` Touch GUI binary, and falls back gracefully on exit `77`.
     - **Magisk / KernelSU / APatch App (`BOOTMODE=true`):** Skips the recovery Touch GUI (since Android SurfaceFlinger owns the display) and automatically invokes `magisk-installer` / `customize.sh` with default selections.
   - `magisk-template/` contains a ready-to-use `module.prop`, `customize.sh`, and `META-INF/com/google/android/magisk-installer` bridge.

## Building

### Option 1: GitHub Actions
Push to `master`/`main` and download the `aroma-next-binaries-and-zip` artifact built with Android NDK r27c via `.github/workflows/build.yml`.

### Option 2: Local Android NDK
```sh
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

### Option 3: Local Cross-Compile with Zig (`zig cc`)
```sh
python build.py --arch arm64 arm32 x86_64
```
Outputs static binaries to `out/bin/` and flashable packages to `dist/AROMA-Next-Installer.zip` and `dist/AROMA-Next-Magisk-Template.zip`.

## License
Apache License 2.0 (see `README`). Original AROMA Installer by Ahmad Amarullah (`amarullz`).
