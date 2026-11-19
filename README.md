# AROMA-Next-Installer

Modern `arm64-v8a` + `armeabi-v7a` port of Ahmad Amarullah's **AROMA Installer** (Apache 2.0), updated for modern Android smartphones, custom recoveries (TWRP, OrangeFox, PitchBlack), and **Systemless-First Magisk / KernelSU / APatch** module packaging.

---

## Downloads (GitHub Releases)

Prebuilt flashable ZIPs and static binaries (`arm64-v8a` & `armeabi-v7a`) are published automatically via GitHub Actions on the **Releases** page:
- **`AROMA-Next-Installer.zip`** — Universal Recovery demo/installer ZIP (`arm64` + `arm32` Touch GUI).
- **`AROMA-Next-Magisk-Template.zip`** — Ready-to-use Magisk / KernelSU / APatch module template with Touch GUI in Recovery, App-mode fallback, and `aroma-mod-lib.sh`.
- **`aroma_installer-arm64`** / **`aroma_installer-arm32`** — Standalone static ELF binaries.

---

## Why Legacy AROMA Broke & What Changed

1. **64-bit (`arm64-v8a`) & C11 Cleanliness:**
   - Fixed 64-bit pointer truncation (`uintptr_t` in `src/libs/aroma_memory.c`, `intptr_t` casts in `src/libs/aroma_graph.c` and `libs/minzip/Zip.c`), `sizeof(AARRAY_ITEM)` allocation & `NULL`-key checks in `src/libs/aroma_array.c`, `0755` permissions in `src/libs/aroma_zip.c`, `fabs()` in `src/libs/aroma_libs.c`, and C11 inline linkage in `libs/minzip/inline_magic.h`.
   - Updated `Android.mk` + added `Application.mk` (`APP_PLATFORM := android-35`) for Android NDK r27c (`arm64-v8a` + `armeabi-v7a`).

2. **Dual Display Engine (`fbdev` + Pure-ioctl `DRM/KMS`):**
   - Added `src/libs/graph_drm.c`: pure Linux kernel ioctl DRM/KMS dumb-buffer backend (`/dev/dri/card0..3`, `DRM_IOCTL_MODE_SETCRTC` + `DIRTYFB` flush for command-mode OLED panels) with zero external `libdrm.so` dependency.
   - Enhanced `fbdev` in `src/libs/aroma_graph.c`: tries `/dev/graphics/fb0` and `/dev/fb0`, validates `mmap != MAP_FAILED`, and reads real `red/green/blue/transp` channel offsets from `FBIOGET_VSCREENINFO`.
   - **Anti-Freeze Recovery Safety (`EXIT 77`):** If neither `fbdev` nor DRM master (`DRM_IOCTL_SET_MASTER`) can be acquired (for example when a recovery holds DRM master across `SIGSTOP`), AROMA-Next immediately resumes the parent recovery (`SIGCONT`) and exits with code `77` (`AROMA_EXIT_NO_DISPLAY`) so the dispatcher runs the non-GUI fallback instead of hanging TWRP.

3. **Modern Touch & Input (`src/libs/input/`):**
   - Raised `INDR_MAXDEV` from `15` to `64` so touchscreens on `/dev/input/event15+` are detected.
   - Added Linux Multitouch Protocol Type-B (`ABS_MT_SLOT`) filtering so secondary fingers don't corrupt slot-0 coordinates.
   - Replaced shared `static` touch globals with per-device state in `INDR_POS`, added `BTN_TOUCH` up handling, fixed `(1 << 31)` shift, and added `ABS_X`/`ABS_Y` calibration fallback.
   - Added `/sys/class/leds/vibrator/{duration,activate}` haptic fallback in `src/libs/aroma_input.c`.
   - Dynamic `mount`/`umount` lookup (`/sbin`, `/system/bin`, `/bin`, `/vendor/bin`) in `src/main/aroma_ui.c`.

---

## Systemless-First Modding + Optional Pre-RW (`RO2RW` / `SystemRW`)

On modern Android (10–16+), `/system`, `/vendor`, `/product`, and `/system_ext` typically use **EROFS** (read-only by kernel design) or deduplicated `shared_blocks` `ext4` inside dynamic `super` partitions protected by `dm-verity`.

**AROMA-Next** brings back classic Touch-GUI file modding safely in two modes:

1. **Systemless Overlay Mode (Default & Recommended):**
   - Generates a standard Magisk / KernelSU / APatch module (`$MODPATH`, or directly `/data/adb/modules/<id>` when flashed in Recovery).
   - Works on **EROFS**, `shared_blocks` `ext4`, Virtual A/B, and active `dm-verity` with zero bootloop risk.
2. **Direct Partition RW Mode (Optional for Power Users):**
   - Intended **only** if you already converted your partitions to RW beforehand using an external tool like **RO2RW** or **SystemRW** and rebooted prior to running AROMA-Next.
   - `aroma_probe_rw()` verifies actual partition writability before touching anything; if partitions are still EROFS or locked RO, it safely falls back to Systemless Mode (or aborts cleanly).

### Dual-Mode Flow (`Recovery` vs. `App`)
- **In Recovery (`TWRP` / `OrangeFox`, `BOOTMODE=false`):** `update-binary` detects `uname -m` (`aarch64` / `armv7l`), launches the Touch GUI (`aroma-config`), writes user choices to `/tmp/aroma/*.prop`, and calls `magisk-installer` -> `customize.sh`.
- **In Magisk / KernelSU / APatch App (`BOOTMODE=true`):** Android's SurfaceFlinger owns the display, so `update-binary` skips the recovery Touch GUI and runs `magisk-installer` -> `customize.sh` directly using safe Systemless defaults.

### Developer Quickstart (`aroma-mod-lib.sh` Examples)

Use `magisk-template/` as your starting point (`module.prop`, `customize.sh`, `META-INF/com/google/android/aroma-config`). Inside `customize.sh`, source `aroma-mod-lib.sh` and use these helpers (edit or remove what you don't need):

```sh
# 1. Initialize backend ("systemless", "rw", or "auto"; 3rd arg = fallback to systemless 1|0)
aroma_mod_init "$REQ_MODE" "$MODPATH" 1

# 2. Set/override build.prop entries (writes $MODPATH/system.prop in Systemless mode)
aroma_mod_edit_prop "ro.config.media_vol_steps" "30"

# 3. Copy/replace files into system, vendor, product, system_ext, or odm
aroma_mod_copy "$MODPATH/files/custom_config.example" "system/etc/custom_config.conf" 0644

# 4. Debloat / delete apps or files (.replace / KernelSU 0:0 whiteouts in Systemless mode)
aroma_mod_delete "system/app/ExampleBloat" "dir"
aroma_mod_delete "system/media/bootanimation.zip" "file"

# 5. Patch an existing read-only system file inside the overlay module
aroma_mod_patch_file "system/etc/hosts" "s/old_entry/new_entry/"
```

---

## Building from Source

### Option 1: GitHub Actions (Automatic Releases)
Push to `master`/`main` (updates the `latest` pre-release) or push a tag `v*` (creates a full versioned GitHub Release) using `.github/workflows/build.yml` with Android NDK r27c.

### Option 2: Local Android NDK
```sh
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk -j4
```

### Option 3: Local Cross-Compile with Zig (`zig cc`)
```sh
python build.py --arch arm64 arm32
```
Outputs static binaries to `out/bin/` and flashable ZIPs to `dist/AROMA-Next-Installer.zip` and `dist/AROMA-Next-Magisk-Template.zip`.

---

## License
Apache License 2.0 (see `README`). Original AROMA Installer by Ahmad Amarullah (`amarullz`).
