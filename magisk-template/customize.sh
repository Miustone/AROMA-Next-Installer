# AROMA-Next Magisk/KernelSU Module customize.sh template
# When flashed in TWRP/OrangeFox via AROMA-Next, user selections are saved in:
#   ${AROMA_TMP:-/tmp/aroma}/*.prop

SKIPUNZIP=0

AROMA_PROP_DIR="${AROMA_TMP:-/tmp/aroma}"

read_aroma_prop() {
  local file="$AROMA_PROP_DIR/$1"
  local key="$2"
  local def="$3"
  if [ -f "$file" ]; then
    local val
    val="$(grep -E "^${key}=" "$file" | head -n1 | cut -d'=' -f2-)"
    if [ -n "$val" ]; then
      echo "$val"
      return
    fi
  fi
  echo "$def"
}

ui_print "****************************************"
ui_print "  AROMA-Next Magisk Module Installer    "
ui_print "****************************************"

if [ -d "$AROMA_PROP_DIR" ] && ls "$AROMA_PROP_DIR"/*.prop >/dev/null 2>&1; then
  ui_print "- Loaded Touch GUI selections from $AROMA_PROP_DIR"
  FEATURE_A="$(read_aroma_prop "customize.prop" "item.1.1" "1")"
  FEATURE_B="$(read_aroma_prop "customize.prop" "item.1.2" "0")"
else
  ui_print "- Running in App / Headless fallback mode (using defaults)"
  FEATURE_A="1"
  FEATURE_B="0"
fi

ui_print "- Option A: $FEATURE_A"
ui_print "- Option B: $FEATURE_B"

# Place your module installation logic here using $MODPATH
set_perm_recursive "$MODPATH" 0 0 0755 0644
