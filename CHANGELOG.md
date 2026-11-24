# Changelog

All notable changes in **AROMA-Next-Installer** are documented per release below, starting from the fork off `amarullz/AROMA-Installer` (commit `de82ccc`).

---

## [V26.11-RC3-PRE1] — 2026-11-24 *(Pre-Release)*

Focus: Full modernization of all vendored C libraries (`zlib`, `libpng`, `FreeType`) from 2011/2012 baselines to 2026 upstream releases, plus repository licensing & layout cleanup.

### Changed & Upgraded
- **Vendored Core Libraries (`include/` & `libs/`):**
  - **zlib:** Upgraded `1.2.5.2-motley` (2011) → **`1.4`** (2026), replacing legacy 32-bit ARM assembly with clean C11 sources.
  - **libpng:** Upgraded `1.2.46` (2011) → **`1.6.59`** (2026) with native ARM64 & ARM32 NEON C intrinsics (`libs/png/arm/`), official `pnglibconf.h`, and migrated `src/libs/aroma_png.c` (`apng_load` / `apng_loadfont`) to libpng 1.6 accessor APIs (`png_get_*`).
  - **FreeType:** Upgraded `2.4.10` (2012) → **`2.15.0`** (2026) with updated module configuration (`autofit`, `truetype`, `cff`, `psaux`, `psnames`, `pshinter`, `sfnt`, `smooth`, `raster`) and subpixel LCD filtering enabled.
  - Synchronized source lists in `Android.mk` and `build.py`.
- **Smooth 60/90/120Hz+ Window Animations & Direct DRM Backbuffer Pipeline (`src/libs/graph_drm.c`, `src/libs/aroma_graph.c`, `src/controls/aroma_controls.c`):**
  - Eliminated the slow uncached/write-combining DRM dumb-buffer read (`memcpy(maps[1], maps[0])`) on alternating frames and bypassed the redundant 18 MB intermediate `ag_bf32` copy by blitting RGB565 directly to the active DRM backbuffer (`ag_drm_backbuf()`) in a single ARM NEON pass.
  - Replaced legacy 5-frame linear animations and upfront multi-canvas pre-allocation freezes with monotonic time-based cubic ease-out VSYNC-locked transitions (scaling smoothly to 60Hz, 90Hz, and 120Hz+ panels).
  - Stopped the idle background refresh thread from competing with animations when neither the busy bar nor text caret is active; fixed height-clipping typo in `ag_draw_strecth` / `ag_draw_strecth_ex`.
- **UI Polish (`src/controls/aroma_controls.c`):**
  - Swapped button order in the **Select Theme** dialog so **Back** is on the left and **OK** is on the right, matching all other installer screens.
- **Repository Housekeeping:**
  - Replaced legacy 2013 `README` file with a standard Apache 2.0 `LICENSE` file (auto-detected by GitHub) and updated license links in `README.md`.
  - Removed tracked `out/out_folder.txt` placeholder (`out/` is generated automatically by CI and `build.py`).

---

## [V26.11-RC2] — 2026-11-24

Focus: AERA Recovery / Qualcomm `msm_drm` release promotion, visual README showcase, and legacy repository cleanup.

### Changed & Added
- **Documentation (`README.md`):**
  - Redesigned `README.md` with a centered hero header, live GitHub Actions / Release / Download badges, and a directly visible 7-step clickable screenshot gallery (using fast `screenshots/thumbs/` previews linked to full-resolution PNGs).
  - Highlighted **AERA Recovery** support (Qualcomm `msm_drm` Dual-LM Source-Split Atomic KMS & High-FD `OUTFD >= 10` pipe compatibility, introduced in `V26.11-RC2-PRE2`) alongside Pixel 7 Pro / OrangeFox R12.0.

### Removed
- **Legacy 2013 Windows Build Tooling:**
  - Removed obsolete root batch scripts (`clean.bat`, `deploy.bat`, `make.bat`, `makeall.bat`, `push.bat`) and the `tools/` directory (`7z.*`, `AStyle.exe`, `beauty.bat`, `android_building.sh`), which were unused by GitHub Actions / `build.py` and incompatible with the modern multi-arch `update-binary` dispatcher.
  - Removed the dead legacy `aroma_installer.zip` target from `Android.mk`.

---

## [V26.11-RC2-PRE2] — 2026-11-20 *(Pre-Release)*

Focus: Qualcomm `msm_drm` / Dual-LM Source-Split support (AERA Recovery on OnePlus 15), high-FD pipe compatibility, and CI/repo polish.

### Fixed
- **Qualcomm `msm_drm` & Dual-LM Source-Split Atomic KMS (`src/libs/graph_drm.c`):**
  - Prioritizes the plane already active on the target CRTC (`Pass 1`) instead of picking an idle/incompatible SSPP plane (`-EINVAL`).
  - Validates and switches dumb-buffer pixel formats against `DRM_IOCTL_MODE_GETPLANE` supported formats.
  - Adopts `saved_crtc.mode` when valid and adds progressive non-modeset / `FB_ID`-only atomic commit fallbacks (`amode=0..3`) for strict drivers.
  - Saves and restores the primary plane's original `SRC_X/Y/W/H`, `CRTC_X/Y/W/H`, `CRTC_ID`, and `FB_ID` alongside overlay planes in `ag_drm_free_ctx()` before `RMFB` and `SIGCONT` — preventing `_sde_crtc_validate_src_split_order` `-EINVAL` failures and UI freezes when returning to AERA Recovery (OnePlus 15).
  - Retries full plane+overlay restore (without and with `ALLOW_MODESET`) before falling back to `FB_ID`-only restore.
- **High File Descriptor (`OUTFD >= 10`) & Shell Fallbacks:**
  - `assets/META-INF/com/google/android/update-binary`: Replaced `>&"$OUTFD"` with `> "/proc/self/fd/$OUTFD"` (fixes `mksh`/Toybox `illegal file descriptor name` when recovery passes e.g. `OUTFD=26`) and fixed fallback extraction under Toybox/BusyBox `unzip`.
  - `magisk-template/META-INF/com/google/android/magisk-installer`: Updated `ui_print()` to use `> "/proc/self/fd/$OUTFD"` as well.

### Changed & Added
- **CI Release Rules (`.github/workflows/build.yml`):** Tag pushes containing `-PRE` or `-TEST` are automatically marked as `prerelease` without overwriting `latest`.
- **Docs & Repo:** Added compressed screenshots for `README.md` / `XDA_THREAD.txt` and added `.github/FUNDING.yml`.

---

## [V26.11-RC1] — 2026-11-19

Initial **AROMA-Next-Installer** release vs. `amarullz/AROMA-Installer` (`de82ccc`), verified on Google Pixel 7 Pro + OrangeFox R12.0.

### Added
- **Pure-ioctl Linux DRM/KMS & Atomic KMS Backend (`src/libs/graph_drm.c`):**
  - Detects and reuses inherited DRM master FDs via `/proc/self/fd` when `/dev/dri/card0` is inherited without `O_CLOEXEC`.
  - Implements Atomic KMS (`DRM_IOCTL_MODE_ATOMIC`) with double-buffered dumb buffers, disables/restores active recovery overlay planes, and restores saved scanout FBs before `RMFB`.
  - Queries `DRM_IOCTL_VERSION` (`exynos` / `dpu`) for native `RGBA8888` byte-order shifts on Exynos/Tensor panels and standard `XRGB8888`/`ABGR8888` shifts elsewhere, with legacy `SETCRTC` + `DIRTYFB` fallback.
- **Display Backend Selection & Safe Exit (`src/libs/aroma_graph.c`, `src/main/aroma.c`, `src/aroma.h`):**
  - Added `ag_backend()` (`0 = none`, `1 = fbdev`, `2 = drm`) and `AROMA_DISPLAY=drm|fbdev` override.
  - Added `AROMA_EXIT_NO_DISPLAY` (`77`) so AROMA-Next resumes parent recovery and triggers the non-GUI fallback instead of hanging when no display backend is available.
  - Walks ancestor PIDs up to `recovery`/`twrp` to `SIGSTOP` / `SIGCONT` both shell dispatchers and the recovery compositor cleanly in reverse order.
- **Universal Multi-Arch Dispatcher (`assets/META-INF/com/google/android/update-binary`):**
  - Detects `BOOTMODE` (`true` in Magisk/KernelSU/APatch app vs. `false` in recovery), picks `arm64` / `arm32` / `x86_64` at runtime, and falls back to `magisk-installer` / `update-binary-installer` when `BOOTMODE=true` or exit code is `77`.
- **Magisk / KernelSU / APatch Module Template (`magisk-template/`):**
  - Added `aroma-mod-lib.sh` (`aroma_mod_init`, `aroma_probe_rw`, `aroma_mod_copy`, `aroma_mod_delete`, `aroma_mod_edit_prop`, `aroma_mod_patch_file`) defaulting to safe **Systemless Overlay Mode** with optional **Direct RW Mode** for pre-converted `RO2RW`/`SystemRW` partitions.
  - Added `aroma-env-check.sh` (probes root managers & partition writability at startup), `aroma-config`, `customize.sh`, `module.prop`, `files/custom_config.example`, and `magisk-installer` (with standalone `/data/adb/modules` recovery fallback).
- **UI, Themes & Screenshot Feedback (`src/controls/aroma_controls.c`, `src/main/aroma_ui.c`, `src/main/aroma_installer.c`):**
  - Added `blacknwhite` as the default theme and **Select Theme** in the 3-Dot Menu with live page theme reloading (`blacknwhite`, `ics`, `miui4`, `miui`, `touchwiz`, `franzyroy`, `generic`).
  - Added `aui_ensure_ready()` so `alert()`, `confirm()`, and `textdialog()` initialize theme/fonts even before the first `viewbox()`.
  - Added `aw_draw_dialog_border()`, `ag_getcolorset()` contrast fallbacks, `blacknwhite` 9-patch progress bar + monochrome button icons, bounded install glow animation, and camera-shutter flash feedback in `ag_takescreenshoot()`.
- **Build Tooling, Versioning & CI:**
  - Dynamic `%y.%m` (`26.11`) `AROMA_VERSION` and `%d.%H` `AROMA_BUILD` (`src/aroma.h`, `Android.mk`, `build.py`).
  - Added `Application.mk` (`arm64-v8a`, `armeabi-v7a`, `android-35`), `build.py` (Zig cross-compile + ZIP packager), `.github/workflows/build.yml` (NDK r27c + automated GitHub Releases), `README.md`, `XDA_THREAD.txt`, `.gitignore`, and `.gitattributes`.

### Changed & Fixed
- **64-bit (`arm64-v8a`) & C11 Compatibility:**
  - `src/libs/aroma_memory.c`: Replaced 32-bit pointer tracking with `uintptr_t` / `%llx` / `strtoull`.
  - `src/libs/aroma_graph.c`: Fixed `int` ↔ `void *` casts via `intptr_t`; clamped `ag_dp >= 1`.
  - `src/libs/neon/blt_neon.c`: Fixed uninitialized loop index `i` in `aBlt32_neon` fallback and enabled NEON intrinsics on `arm64-v8a`.
  - `libs/minzip/Zip.c` & `libs/minzip/inline_magic.h`: Fixed pointer casts via `intptr_t` and switched `INLINE` to `static __inline__`.
  - `Android.mk`: Removed legacy `libm`/`libc` static lib deps (using `-lm` + `-static`), removed `LOCAL_MODULE_PATH`, excluded 32-bit `.s` assembly on `arm64-v8a`, enabled NEON on `armeabi-v7a`.
- **Framebuffer (`fbdev`) Fixes (`src/libs/aroma_graph.c`):**
  - Added `/dev/fb0` fallback, `MAP_FAILED` check, `ag_fb = -1` init, and reading actual `FBIOGET_VSCREENINFO` channel bit offsets.
- **Touch, Key & Haptic Fixes (`src/libs/input/input_driver.c`, `translate_touch.c`, `src/libs/aroma_input.c`):**
  - Raised `INDR_MAXDEV` (`15` → `64`) and `dev->file` buffer (`10` → `64`).
  - Added Type-B multitouch (`ABS_MT_SLOT`) filtering, per-device release state, `BTN_TOUCH` up handling, unsigned `(1U << 31)` shift, `ABS_X`/`ABS_Y` calibration fallback, and non-touch `EV_KEY` passthrough on combo devices.
  - Deferred `KEY_VOLUMEDOWN` / `KEY_POWER` until release with mutual combo suppression so `Vol-Down + Power` screenshots don't trigger accidental UI actions.
  - Added `/sys/class/leds/vibrator/{duration,activate}` haptic fallback in `vibrate()`.
- **Core Bugfixes:**
  - `src/libs/aroma_array.c`: Fixed `sizeof(AARRAY)` → `sizeof(AARRAY_ITEM)` allocation and `NULL` key guards after `aarray_del()`.
  - `src/libs/aroma_zip.c`: Fixed `mkdir(AROMA_TMP, 755)` → `0755`.
  - `src/libs/aroma_libs.c`: Fixed `abs()` → `fabs()` on float `p->velocity`.
  - `src/main/aroma_installer.c`: Added `magisk-installer` extraction fallback, `chmod(..., 0755)`, and `NULL` guards for `.install.log`.
  - `src/main/aroma_ui.c`: Replaced hardcoded `/sbin/mount` and `/sbin/umount` with dynamic path lookup.
