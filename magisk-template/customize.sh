# AROMA-Next Systemless & Optional Pre-RW Module customize.sh
# Reads Touch GUI choices from ${AROMA_TMP:-/tmp/aroma}/*.prop (in Recovery)
# or uses safe Systemless defaults when installed from Magisk/KernelSU/APatch App.

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

# Initialize modding backend (with safe fallback to systemless if RO/EROFS)
aroma_mod_init "$REQ_MODE" "$MODPATH" 1 || abort "! Failed to initialize AROMA modding mode"

if [ "$OPT_TWEAKS" = "1" ]; then
  ui_print "- Applying system.prop tweaks..."
  aroma_mod_edit_prop "ro.config.media_vol_steps" "30"
  aroma_mod_edit_prop "profiler.force_disable_err_rpt" "1"
fi

if [ "$OPT_BOOTSOUND" = "1" ]; then
  ui_print "- Disabling boot sound..."
  aroma_mod_edit_prop "ro.config.play.bootsound" "0"
fi

if [ "$OPT_DEBLOAT" = "1" ]; then
  ui_print "- Example debloat (uncomment real package paths in customize.sh):"
  # Example: aroma_mod_delete "system/app/ExampleBloat" "dir"
  ui_print "  (No destructive paths enabled in template by default)"
fi

# Remove helper META-INF artifacts if unpacked inside $MODPATH
rm -rf "$MODPATH/META-INF" 2>/dev/null || true
set_perm_recursive "$MODPATH" 0 0 0755 0644
