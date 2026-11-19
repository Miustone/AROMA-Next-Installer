# Changelog

All notable changes in **AROMA-Next-Installer** compared to the upstream fork base (`amarullz/AROMA-Installer`, commit `de82ccc`).

## [3.00-next] — 2026-11-19

### Added
- **Pure-ioctl Linux DRM/KMS Display Backend (`src/libs/graph_drm.c`):**
  - Scans `/dev/dri/card0`..`/dev/dri/card3`, acquires DRM master (`DRM_IOCTL_SET_MASTER`), discovers connected modes, allocates 32-bit `XRGB8888` dumb buffers, calls `DRM_IOCTL_MODE_SETCRTC`, and flushes updates via `DRM_IOCTL_MODE_DIRTYFB` (for command-mode OLED panels) without requiring external `libdrm.so`.
  - Restores the previous CRTC and drops DRM master cleanly on exit (`ag_drm_close()`).
- **Display Backend Auto-Selection & Safe Exit (`src/libs/aroma_graph.c`, `src/main/aroma.c`, `src/aroma.h`):**
  - Added `ag_backend()` (`0 = none`, `1 = fbdev`, `2 = drm`) and `AROMA_DISPLAY=drm|fbdev` environment override.
  - Added `AROMA_EXIT_NO_DISPLAY` (`77`): if neither `fbdev` nor DRM master can be initialized, AROMA-Next resumes the parent recovery (`SIGCONT`) and exits cleanly instead of hanging TWRP.
- **Universal Multi-Arch Recovery & Magisk/KernelSU Dispatcher (`assets/META-INF/com/google/android/update-binary`):**
  - Detects `BOOTMODE` (`true` in Magisk/KernelSU/APatch App vs. `false` in Recovery).
  - Selects `aroma_installer-arm64` (`aarch64`), `aroma_installer-arm32` (`armv7l`/`armv8l`), or `aroma_installer-x86_64` at runtime in recovery and triggers the non-GUI fallback on exit code `77`.
  - Automatically skips the recovery Touch GUI in `BOOTMODE=true` and delegates directly to `magisk-installer` / `customize.sh`.
- **Magisk / KernelSU Module Template (`magisk-template/`):**
  - Added `module.prop`, `customize.sh` (reads `${AROMA_TMP:-/tmp/aroma}/*.prop` from Touch GUI or falls back to defaults in App mode), and `META-INF/com/google/android/magisk-installer`.
- **Modern Build Tooling & CI:**
  - Added `Application.mk` (`APP_ABI := arm64-v8a armeabi-v7a`, `APP_PLATFORM := android-35`).
  - Added `build.py` (cross-compiling static `arm64` / `arm32` / `x86_64` binaries via Zig `cc` and packaging `AROMA-Next-Installer.zip` + `AROMA-Next-Magisk-Template.zip`).
  - Added `.github/workflows/build.yml` for automated Android NDK r27c builds and artifact packaging.
  - Added `README.md`, `.gitignore`, and `.gitattributes` (enforcing LF line endings for shell scripts, C sources, and Makefiles).

### Changed & Fixed
- **64-bit (`arm64-v8a`) & Modern C11 Compatibility:**
  - `src/libs/aroma_memory.c`: Replaced 32-bit `(long)` / `%i` / `atoi` pointer tracking with `uintptr_t` / `%llx` / `strtoull`.
  - `src/libs/aroma_graph.c`: Fixed `int` ↔ `void *` pointer casts in `ag_sync_fade_thread()`, `ag_sync_fade()`, and `ag_sync_fade_wait()` via `intptr_t`; clamped `ag_dp >= 1`.
  - `src/libs/neon/blt_neon.c`: Fixed uninitialized loop index `i` in `aBlt32_neon` fallback branch and enabled NEON C intrinsics on `arm64-v8a`.
  - `libs/minzip/Zip.c` & `libs/minzip/inline_magic.h`: Fixed `(int)` / `(void *)` pointer casts using `intptr_t` and switched `INLINE` to `static __inline__` for C11 linkage.
  - `Android.mk`: Removed legacy AOSP `LOCAL_STATIC_LIBRARIES := libm libc` (replaced with `LOCAL_LDLIBS := -lm` and `LOCAL_LDFLAGS += -static -Wl,--gc-sections -Wl,-s`), removed `LOCAL_MODULE_PATH` so multi-ABI outputs stay separated in `libs/<abi>/`, excluded 32-bit `.s` assembly on `arm64-v8a`, enabled `LOCAL_ARM_NEON := true` for `armeabi-v7a`, and added Clang C11 compatibility warning flags.
- **Framebuffer (`fbdev`) Fixes (`src/libs/aroma_graph.c`):**
  - Added `/dev/fb0` fallback path alongside `/dev/graphics/fb0`, `MAP_FAILED` validation, and `ag_fb` initialization to `-1`.
  - Read real `red`/`green`/`blue`/`transp` bit offsets from `FBIOGET_VSCREENINFO` instead of hardcoding `16/8/0/24`.
- **Touch & Input Fixes (`src/libs/input/input_driver.c`, `src/libs/input/input_translate/translate_touch.c`, `src/libs/aroma_input.c`):**
  - Raised `INDR_MAXDEV` from `15` (`0xf`) to `64` and `dev->file` buffer from `10` to `64`.
  - Added Linux Multitouch Protocol Type-B (`ABS_MT_SLOT`) filtering so secondary fingers do not corrupt slot-0 touch coordinates.
  - Moved touch-release state from shared `static` globals into per-device `INDR_POS`, added `BTN_TOUCH` up handling, fixed `(1 << 31)` unsigned shift, removed the `x != 0 && y != 0` guard, and added `ABS_X`/`ABS_Y` calibration fallback.
  - Allowed non-touch `EV_KEY` events on combo touch+key input devices to fall through to `INDR_translate_keyboard()`.
  - Added `/sys/class/leds/vibrator/{duration,activate}` haptic fallback in `vibrate()`.
- **Core Bugfixes (`src/libs/aroma_array.c`, `src/libs/aroma_zip.c`, `src/libs/aroma_libs.c`, `src/main/aroma_installer.c`, `src/main/aroma_ui.c`):**
  - `aroma_array.c`: Fixed `sizeof(AARRAY)` → `sizeof(AARRAY_ITEM)` allocation bug and added `NULL` key guards after `aarray_del()`.
  - `aroma_zip.c`: Fixed `mkdir(AROMA_TMP, 755)` octal permission (`0755`).
  - `aroma_libs.c`: Fixed `abs()` on floating-point `p->velocity` → `fabs()`.
  - `aroma_installer.c`: Added `META-INF/com/google/android/magisk-installer` fallback, `chmod(..., 0755)`, and `NULL` checks before writing `.install.log`.
  - `aroma_ui.c`: Replaced hardcoded `/sbin/mount` and `/sbin/umount` with dynamic lookup across `/sbin`, `/system/bin`, `/bin`, and `/vendor/bin`.
