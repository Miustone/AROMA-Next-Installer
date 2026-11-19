#!/sbin/sh
# AROMA-Next Recovery Environment Probe
# Detects Magisk, KernelSU/APatch, and pre-converted RO2RW/SystemRW partitions,
# writing capabilities to ${AROMA_TMP:-/tmp/aroma}/env.prop.

OUT="${AROMA_TMP:-/tmp/aroma}/env.prop"
mkdir -p "${AROMA_TMP:-/tmp/aroma}"

has_magisk=0
has_ksu_apatch=0
can_systemless=0
can_rw=0
can_modify=0
data_encrypted=0
env_summary="Preview Only (No Mod Target)"

if ! grep -q " /data " /proc/mounts 2>/dev/null; then
  mount /data >/dev/null 2>&1 || true
fi

if [ -d /data/adb/magisk ] || [ -f /data/adb/magisk.db ]; then
  has_magisk=1
fi
if [ -d /data/adb/ksu ] || [ -f /data/adb/ksu/modules.img ] || [ -d /data/adb/ap ] || [ -d /data/adb/modules ]; then
  has_ksu_apatch=1
fi
if [ "$has_magisk" = "1" ] || [ "$has_ksu_apatch" = "1" ] || { [ -d /data/adb ] && [ -w /data/adb ]; }; then
  can_systemless=1
elif [ ! -d /data/system ] && [ ! -d /data/adb ]; then
  data_encrypted=1
fi

for mp in /system_root /mnt/system /system; do
  if [ -d "$mp" ] && grep -q " $mp " /proc/mounts 2>/dev/null; then
    fs="$(awk -v m="$mp" '$2==m {print $3; exit}' /proc/mounts 2>/dev/null)"
    case "$fs" in
      erofs|squashfs) continue ;;
    esac
    mount -o rw,remount "$mp" >/dev/null 2>&1 || true
    probe="${mp%/}/.aroma_env_probe_$$"
    if touch "$probe" >/dev/null 2>&1; then
      rm -f "$probe" >/dev/null 2>&1 || true
      can_rw=1
      break
    fi
  fi
done

if [ "$can_systemless" = "1" ] || [ "$can_rw" = "1" ]; then
  can_modify=1
  if [ "$has_magisk" = "1" ] && [ "$can_rw" = "1" ]; then
    env_summary="Magisk + RW Partitions"
  elif [ "$has_magisk" = "1" ]; then
    env_summary="Magisk (Systemless)"
  elif [ "$has_ksu_apatch" = "1" ] && [ "$can_rw" = "1" ]; then
    env_summary="KernelSU / APatch + RW"
  elif [ "$has_ksu_apatch" = "1" ]; then
    env_summary="KernelSU / APatch (Systemless)"
  elif [ "$can_rw" = "1" ]; then
    env_summary="Direct RW (RO2RW / SystemRW)"
  else
    env_summary="/data/adb Ready (Systemless)"
  fi
fi

cat > "$OUT" <<EOF
can_modify=$can_modify
can_systemless=$can_systemless
can_rw=$can_rw
has_magisk=$has_magisk
has_ksu_apatch=$has_ksu_apatch
data_encrypted=$data_encrypted
env_summary=$env_summary
EOF
exit 0
