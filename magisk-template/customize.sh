# AROMA-Next Systemless & Optional Pre-RW Module customize.sh
# Reads Touch GUI choices from ${AROMA_TMP:-/tmp/aroma}/*.prop (in Recovery)
# or uses safe Systemless defaults when installed from Magisk/KernelSU/APatch App.
#
# All examples below use aroma-mod-lib.sh. Edit or delete any block you don't need!

SKIPUNZIP=0

AROMA_PROP_DIR="${AROMA_TMP:-/tmp/aroma}"

# Load helper library (from unpacked zip or extract on demand)
MODLIB_PATH=""
for cand in \
  "$MODPATH/META-INF/com/google/android/aroma-mod-lib.sh" \
  "$TMPDIR/META-INF/com/google/android/aroma-mod-lib.sh"; do
  if [ -f "$cand" ]; then
    MODLIB_PATH="$cand"
    break
  fi
done
if [ -z "$MODLIB_PATH" ] && [ -n "$ZIPFILE" ]; then
  unzip -o "$ZIPFILE" "META-INF/com/google/android/aroma-mod-lib.sh" -d "$TMPDIR" >/dev/null 2>&1
  MODLIB_PATH="$TMPDIR/META-INF/com/google/android/aroma-mod-lib.sh"
fi
[ -f "$MODLIB_PATH" ] && . "$MODLIB_PATH"

read_aroma_prop() {
  local file="$AROMA_PROP_DIR/$1"
  local key="$2"
  local def="$3"
  if [ -f "$file" ]; then
    local val
    val="$(grep -E "^${key}=" "$file" | head -n1 | cut -d'=' -f2- | tr -d '\r')"
    if [ -n "$val" ]; then
      echo "$val"
      return
    fi
  fi
  echo "$def"
}

ui_print "****************************************"
ui_print "  AROMA-Next Module Installer           "
ui_print "****************************************"

MODE_CHOICE="$(read_aroma_prop "mode.prop" "selected.1" "1")"
OPT_TWEAKS="$(read_aroma_prop "customize.prop" "item.1.1" "1")"
OPT_BOOTSOUND="$(read_aroma_prop "customize.prop" "item.1.2" "0")"
OPT_DEBLOAT="$(read_aroma_prop "customize.prop" "item.2.1" "0")"

REQ_MODE="systemless"
[ "$MODE_CHOICE" = "2" ] && REQ_MODE="rw"

# Initialize modding backend (falls back safely to systemless if partitions are EROFS/RO)
aroma_mod_init "$REQ_MODE" "$MODPATH" 1 || abort "! Failed to initialize AROMA modding mode"

# ==============================================================================
# [PLACEHOLDER EXAMPLES] - Customize or delete any section below for your module
# ==============================================================================

# 1) Edit or add system properties (writes $MODPATH/system.prop in Systemless mode,
#    or updates build.prop in Direct RW mode if pre-converted with RO2RW/SystemRW)
if [ "$OPT_TWEAKS" = "1" ]; then
  ui_print "- Applying system.prop tweaks..."
  aroma_mod_edit_prop "ro.config.media_vol_steps" "30"
  aroma_mod_edit_prop "profiler.force_disable_err_rpt" "1"
fi

if [ "$OPT_BOOTSOUND" = "1" ]; then
  ui_print "- Disabling boot sound..."
  aroma_mod_edit_prop "ro.config.play.bootsound" "0"
fi

# 2) Copy/replace files into system/vendor/product/system_ext/odm
#    (In Systemless mode, placed under $MODPATH/system/...; in RW mode, copied directly)
# if [ -f "$MODPATH/files/custom_config.example" ]; then
#   aroma_mod_copy "$MODPATH/files/custom_config.example" "system/etc/custom_config.conf" 0644
# fi

# 3) Hide/debloat apps or delete files
#    (In Systemless mode: creates .replace or KernelSU 0:0 whiteout; in RW mode: deletes)
if [ "$OPT_DEBLOAT" = "1" ]; then
  ui_print "- Debloat placeholder active (uncomment real paths in customize.sh):"
  # aroma_mod_delete "system/app/ExampleBloat" "dir"
  # aroma_mod_delete "product/app/ExampleApp" "dir"
  # aroma_mod_delete "system/media/bootanimation.zip" "file"
fi

# 4) Patch an existing read-only system file without touching the real partition
#    (Reads original from /system_root or /system, copies into $MODPATH, runs sed)
# aroma_mod_patch_file "system/etc/hosts" "s/localhost/localhost/"

# ==============================================================================
# Clean up installer helper files from $MODPATH so only module payload remains
# ==============================================================================
rm -rf "$MODPATH/META-INF" "$MODPATH/files" 2>/dev/null || true
set_perm_recursive "$MODPATH" 0 0 0755 0644
