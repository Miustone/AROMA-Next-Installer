/*
 * Copyright (C) 2026 Miustone (AROMA-Next-Installer)
 * Based on AROMA Installer (C) 2011-2013 Ahmad Amarullah
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Native Volume Key Selector Engine (BOOTMODE=1 / Android App / Headless Fallback)
 * Renders any standard AROMA-Next `aroma-config` interactively via Volume Keys
 * (KEY_VOLUMEUP / KEY_VOLUMEDOWN) without touching DRM/KMS or framebuffer displays.
 * Module developers author ONE aroma-config and automatically get:
 *   - Recovery (BOOTMODE=0): Full 60-120Hz AROMA-Next Touch GUI
 *   - Android  (BOOTMODE=1): Native Volume Key Selector inside Magisk / KernelSU / APatch
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../aroma.h"

#define AVK_MAX_DEVS   64
#define AVK_KEY_NONE   0
#define AVK_KEY_UP     1
#define AVK_KEY_DOWN   2
#define AVK_KEY_BACK   3

static byte g_vk_mode         = 0;
static byte g_vk_config_only  = 0;
static byte g_vk_plain_out    = 0;
static byte g_vk_done_install = 0;
static byte g_vk_use_stdin    = 0;
static char g_vk_preset_path[256] = {0};
static char * g_vk_preset_buf = NULL;

static struct pollfd g_vk_fds[AVK_MAX_DEVS];
static int           g_vk_fd_cnt = 0;
static long long     g_vk_last_ms = 0;

/* Mode accessors */
void a_set_volkey_mode(byte enabled)      { g_vk_mode = enabled ? 1 : 0; }
byte a_volkey_mode(void)                  { return g_vk_mode; }
void a_set_config_only_mode(byte enabled) { g_vk_config_only = enabled ? 1 : 0; }
byte a_config_only_mode(void)             { return g_vk_config_only; }
void a_set_plain_output(byte enabled)     { g_vk_plain_out = enabled ? 1 : 0; }
byte a_plain_output(void)                 { return g_vk_plain_out; }
byte a_volkey_install_stopped(void)       { return g_vk_done_install; }

void a_set_preset_file(const char * path) {
  if (g_vk_preset_buf) {
    free(g_vk_preset_buf);
    g_vk_preset_buf = NULL;
  }
  if (path && path[0]) {
    snprintf(g_vk_preset_path, sizeof(g_vk_preset_path), "%s", path);
    g_vk_preset_buf = aui_readfromfs(g_vk_preset_path);
  }
  else {
    g_vk_preset_path[0] = '\0';
  }
}

const char * a_get_preset_file(void) {
  return g_vk_preset_path;
}

static byte avk_has_preset(void) {
  return (g_vk_preset_buf != NULL) ? 1 : 0;
}

/* Lookup `propname:key` first, then fallback to `key` inside loaded --preset file */
static char * avk_preset_lookup(const char * prop_name, const char * key) {
  if (!g_vk_preset_buf || !key) return NULL;
  if (prop_name && prop_name[0]) {
    char scoped[256];
    snprintf(scoped, sizeof(scoped), "%s:%s", prop_name, key);
    char * v = aui_parsepropstring(g_vk_preset_buf, scoped);
    if (v != NULL) return v;
  }
  return aui_parsepropstring(g_vk_preset_buf, (char *) key);
}

static long long avk_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long) ts.tv_sec * 1000LL + (long long) (ts.tv_nsec / 1000000L);
}

/* Output helpers: supports both Recovery/Magisk OUTFD (`ui_print`) and direct stdout (KSU/APatch) */
static void avk_emit_single_line(const char * line) {
  FILE * out = apipe();
  if (!out) {
    out = stdout;
  }
  if (g_vk_plain_out) {
    fprintf(out, "%s\n", line ? line : "");
  }
  else {
    if (line && line[0] != '\0') {
      fprintf(out, "ui_print %s\nui_print\n", line);
    }
    else {
      fprintf(out, "ui_print  \nui_print\n");
    }
  }
  fflush(out);
}

#define AVK_WRAP_COLS  48

static void avk_emit_wrapped_line(const char * line, size_t len) {
  if (len == 0) {
    avk_emit_single_line("");
    return;
  }

  /* Detect leading indentation (spaces + optional bullet prefix) for wrapped continuations */
  size_t indent = 0;
  while (indent < len && line[indent] == ' ' && indent < 10) {
    indent++;
  }
  size_t cont_indent = indent;
  /* Extra indent if line starts with a bullet or numbered prefix */
  if (indent + 2 < len && ((line[indent] == '-' && line[indent + 1] == ' ') ||
                           (isdigit((unsigned char)line[indent]) && line[indent + 1] == '.'))) {
    cont_indent = indent + 3;
    if (cont_indent > 10) cont_indent = 10;
  }

  /* Preserve short lines and horizontal rules as-is */
  if (len <= AVK_WRAP_COLS || (len > 4 && (line[0] == '=' || line[0] == '-'))) {
    char buf[512];
    size_t c = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, line, c);
    buf[c] = '\0';
    avk_emit_single_line(buf);
    return;
  }

  size_t pos = 0;
  int first = 1;
  while (pos < len) {
    while (!first && pos < len && line[pos] == ' ') {
      pos++;
    }
    if (pos >= len) {
      break;
    }

    size_t pad = first ? 0 : cont_indent;
    size_t avail = (AVK_WRAP_COLS > pad + 8) ? (AVK_WRAP_COLS - pad) : 20;
    size_t rem = len - pos;
    size_t take = rem;

    if (rem > avail) {
      /* Find last space within avail columns (UTF-8 safe since ASCII space < 0x80) */
      size_t split = avail;
      while (split > 0 && line[pos + split] != ' ') {
        split--;
      }
      if (split > 0) {
        take = split;
      }
      else {
        /* Single word longer than avail: avoid cutting inside UTF-8 continuation bytes */
        take = avail;
        while (take < rem && (((unsigned char)line[pos + take]) & 0xC0) == 0x80) {
          take++;
        }
      }
    }

    char buf[512];
    size_t w = 0;
    size_t k;
    for (k = 0; k < pad && w < sizeof(buf) - 1; k++) {
      buf[w++] = ' ';
    }
    if (w + take >= sizeof(buf)) {
      take = sizeof(buf) - 1 - w;
    }
    memcpy(buf + w, line + pos, take);
    w += take;
    while (w > pad && buf[w - 1] == ' ') {
      w--;
    }
    buf[w] = '\0';
    avk_emit_single_line(buf);

    pos += take;
    first = 0;
  }
}

void avk_print_lines(const char * text) {
  if (!text) {
    avk_emit_single_line("");
    return;
  }
  const char * p = text;
  while (*p) {
    const char * nl = strchr(p, '\n');
    if (!nl) {
      size_t len = strlen(p);
      while (len > 0 && p[len - 1] == '\r') len--;
      avk_emit_wrapped_line(p, len);
      break;
    }
    size_t len = (size_t) (nl - p);
    if (len > 0 && p[len - 1] == '\r') {
      len--;
    }
    avk_emit_wrapped_line(p, len);
    p = nl + 1;
  }
}

void avk_printf(const char * fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  avk_print_lines(buf);
}

/* Strip AROMA rich-text markup (<#color>, </#>, <b>, </b>, <i>, </i>, <u>, </u>, <@..>, <*>) */
static void avk_strip_markup(const char * src, char * dst, size_t dst_sz) {
  if (!dst || dst_sz == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t w = 0;
  const char * p = src;
  while (*p && w + 1 < dst_sz) {
    if (*p == '<') {
      const char * end = strchr(p, '>');
      if (end && (size_t)(end - p) < 24) {
        char c1 = p[1];
        if (c1 == '#' || c1 == '/' || c1 == '@' || c1 == '*' ||
            ((c1 == 'b' || c1 == 'i' || c1 == 'u') && p[2] == '>')) {
          p = end + 1;
          continue;
        }
      }
    }
    dst[w++] = *p++;
  }
  dst[w] = '\0';
}

static void avk_print_rich(const char * raw) {
  if (!raw || raw[0] == '\0') {
    return;
  }
  size_t len = strlen(raw) + 1;
  char * clean = (char *) malloc(len);
  if (!clean) {
    avk_print_lines(raw);
    return;
  }
  avk_strip_markup(raw, clean, len);
  avk_print_lines(clean);
  free(clean);
}

static void avk_print_header(const char * title, const char * desc) {
  char ctitle[256];
  avk_strip_markup(title ? title : "", ctitle, sizeof(ctitle));
  avk_emit_single_line("");
  avk_emit_single_line("========================================");
  avk_printf("  %s", ctitle);
  avk_emit_single_line("========================================");
  if (desc && desc[0] != '\0') {
    avk_print_rich(desc);
  }
}

/* Linux evdev Volume Key scanner */
static int avk_test_bit(int bit, const unsigned char * arr) {
  return (arr[bit / 8] & (1 << (bit % 8))) != 0;
}

int avk_init_input(void) {
  const char * stdin_env = getenv("AROMA_VK_STDIN");
  if (stdin_env && strcmp(stdin_env, "1") == 0) {
    g_vk_use_stdin = 1;
    return 1;
  }

  g_vk_fd_cnt = 0;
  DIR * dir = opendir("/dev/input");
  if (!dir) {
    return 0;
  }

  struct dirent * de;
  while ((de = readdir(dir)) != NULL && g_vk_fd_cnt < AVK_MAX_DEVS) {
    if (strncmp(de->d_name, "event", 5) != 0) {
      continue;
    }
    char devpath[64];
    snprintf(devpath, sizeof(devpath), "/dev/input/%s", de->d_name);
    int fd = open(devpath, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      continue;
    }

    unsigned char ev_bits[(EV_MAX + 7) / 8];
    unsigned char key_bits[(KEY_MAX + 7) / 8];
    memset(ev_bits, 0, sizeof(ev_bits));
    memset(key_bits, 0, sizeof(key_bits));

    int has_vol = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0 &&
        avk_test_bit(EV_KEY, ev_bits) &&
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) {
      if (avk_test_bit(KEY_VOLUMEUP, key_bits) || avk_test_bit(KEY_VOLUMEDOWN, key_bits)) {
        has_vol = 1;
      }
    }

    if (has_vol) {
      /* Drain any queued events */
      struct input_event ev;
      while (read(fd, &ev, sizeof(ev)) == (ssize_t) sizeof(ev)) {}
      g_vk_fds[g_vk_fd_cnt].fd = fd;
      g_vk_fds[g_vk_fd_cnt].events = POLLIN;
      g_vk_fds[g_vk_fd_cnt].revents = 0;
      g_vk_fd_cnt++;
    }
    else {
      close(fd);
    }
  }
  closedir(dir);
  g_vk_last_ms = avk_now_ms();
  return g_vk_fd_cnt > 0;
}

void avk_close_input(void) {
  int i;
  for (i = 0; i < g_vk_fd_cnt; i++) {
    if (g_vk_fds[i].fd >= 0) {
      close(g_vk_fds[i].fd);
      g_vk_fds[i].fd = -1;
    }
  }
  g_vk_fd_cnt = 0;
}

static void avk_drain_events(void) {
  int i;
  struct input_event ev;
  for (i = 0; i < g_vk_fd_cnt; i++) {
    while (read(g_vk_fds[i].fd, &ev, sizeof(ev)) == (ssize_t) sizeof(ev)) {}
  }
}

int avk_wait_key(void) {
  if (avk_has_preset()) {
    return AVK_KEY_NONE;
  }
  if (g_vk_use_stdin) {
    int ch;
    while ((ch = getchar()) != EOF) {
      if (ch == '+' || ch == 'u' || ch == 'U' || ch == '1') {
        return AVK_KEY_UP;
      }
      if (ch == 'b' || ch == 'B') {
        return AVK_KEY_BACK;
      }
      if (ch == '-' || ch == 'd' || ch == 'D' || ch == '2' || ch == '\n') {
        return AVK_KEY_DOWN;
      }
    }
    return AVK_KEY_NONE;
  }

  if (g_vk_fd_cnt <= 0) {
    avk_emit_single_line("! Volume keys unavailable — applying default choice.");
    return AVK_KEY_NONE;
  }

  avk_drain_events();

  while (1) {
    int r = poll(g_vk_fds, (nfds_t) g_vk_fd_cnt, -1);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return AVK_KEY_NONE;
    }

    int i;
    for (i = 0; i < g_vk_fd_cnt; i++) {
      if (!(g_vk_fds[i].revents & POLLIN)) {
        continue;
      }
      struct input_event ev;
      while (read(g_vk_fds[i].fd, &ev, sizeof(ev)) == (ssize_t) sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
          long long now = avk_now_ms();
          if (now - g_vk_last_ms < 120) {
            continue;
          }
          if (ev.code == KEY_VOLUMEUP) {
            g_vk_last_ms = now;
            return AVK_KEY_UP;
          }
          if (ev.code == KEY_VOLUMEDOWN) {
            /* Detect VOL- long-press (>= 600ms) as instant Back */
            long long press_start = now;
            int is_long = 0;
            while (1) {
              long long elapsed = avk_now_ms() - press_start;
              if (elapsed >= 600) {
                is_long = 1;
                break;
              }
              int rem = (int)(600 - elapsed);
              int pr = poll(&g_vk_fds[i], 1, rem);
              if (pr <= 0) {
                if (pr == 0) {
                  is_long = 1;
                  break;
                }
                continue;
              }
              struct input_event rev;
              int released = 0;
              while (read(g_vk_fds[i].fd, &rev, sizeof(rev)) == (ssize_t) sizeof(rev)) {
                if (rev.type == EV_KEY && rev.code == KEY_VOLUMEDOWN && rev.value == 0) {
                  released = 1;
                }
              }
              if (released) {
                break;
              }
            }
            g_vk_last_ms = avk_now_ms();
            return is_long ? AVK_KEY_BACK : AVK_KEY_DOWN;
          }
        }
      }
    }
  }
}

/* ============================================================================
 * Interactive Widget Renderers (1:1 .prop parity with AROMA-Next Touch GUI)
 * ============================================================================ */

void avk_viewbox(
    byte isplain, int argc, char ** args,
    byte can_back, byte * out_back, byte * out_checked) {
  *out_back = 0;
  *out_checked = 0;

  avk_print_header(args[0], args[1]);
  avk_emit_single_line("");

  if (isplain) {
    const char * next_lbl = acfg()->text_next[0] ? acfg()->text_next : "Next";
    if (avk_has_preset()) {
      avk_printf("  [PRESET] -> %s", next_lbl);
      return;
    }
    if (can_back) {
      avk_printf("  [VOL+] %s   |   [VOL-] Back", next_lbl);
      int k = avk_wait_key();
      if (k == AVK_KEY_DOWN || k == AVK_KEY_BACK) {
        *out_back = 1;
        avk_emit_single_line("  <- Back");
      }
      else {
        avk_printf("  -> %s", next_lbl);
      }
    }
    else {
      avk_printf("  [VOL+ / VOL-] %s", next_lbl);
      avk_wait_key();
    }
  }
  else {
    byte defchk = (argc > 4 && atoi(args[4]) != 0) ? 1 : 0;
    char cchk[256];
    avk_strip_markup(args[3], cchk, sizeof(cchk));
    if (avk_has_preset()) {
      char * pv = (argc == 6) ? avk_preset_lookup(NULL, args[5]) : NULL;
      if (pv) {
        *out_checked = (atoi(pv) != 0) ? 1 : 0;
        free(pv);
      }
      else {
        *out_checked = defchk;
      }
      avk_printf("  [PRESET] %s -> %s", cchk, *out_checked ? "YES" : "NO");
      return;
    }
    avk_printf("  Option: %s  [Default: %s]", cchk, defchk ? "YES" : "NO");
    avk_emit_single_line("  [VOL+] Yes (Enable)   |   [VOL-] No (Skip)");
    int k = avk_wait_key();
    if (k == AVK_KEY_BACK && can_back) {
      *out_back = 1;
      avk_emit_single_line("  <- Back");
      return;
    }
    if (k == AVK_KEY_NONE) {
      *out_checked = defchk;
    }
    else {
      *out_checked = (k == AVK_KEY_UP) ? 1 : 0;
    }
    avk_printf("  -> %s", *out_checked ? "YES" : "NO");
  }
}

void avk_textbox(
    byte isplain, int argc, char ** args,
    byte can_back, byte * out_back, byte * out_exit) {
  *out_back = 0;
  *out_exit = 0;

  avk_print_header(args[0], args[1]);
  if (args[3] && args[3][0] != '\0') {
    avk_emit_single_line("----------------------------------------");
    avk_print_rich(args[3]);
    avk_emit_single_line("----------------------------------------");
  }
  avk_emit_single_line("");

  if (avk_has_preset()) {
    avk_emit_single_line("  [PRESET] -> Agree & Continue");
    return;
  }

  if (isplain) {
    if (can_back) {
      avk_emit_single_line("  [VOL+] Continue   |   [VOL-] Back");
      int k = avk_wait_key();
      if (k == AVK_KEY_DOWN || k == AVK_KEY_BACK) {
        *out_back = 1;
      }
    }
    else {
      avk_emit_single_line("  [VOL+ / VOL-] Continue");
      avk_wait_key();
    }
  }
  else {
    char cagree[256];
    avk_strip_markup(args[4], cagree, sizeof(cagree));
    avk_printf("  %s", cagree);
    avk_emit_single_line("  [VOL+] Agree & Continue   |   [VOL-] Decline (Exit)");
    int k = avk_wait_key();
    if (k == AVK_KEY_BACK && can_back) {
      *out_back = 1;
    }
    else if (k == AVK_KEY_DOWN) {
      *out_exit = 1;
    }
  }
}

void avk_alert(int argc, char ** args) {
  avk_print_header(args[0], args[1]);
  const char * ok_txt = (argc > 3 && args[3] && args[3][0]) ? args[3] : "OK";
  if (avk_has_preset()) {
    avk_printf("  [PRESET] %s", ok_txt);
    return;
  }
  avk_printf("\n  [VOL+ / VOL-] %s", ok_txt);
  avk_wait_key();
}

byte avk_confirm(int argc, char ** args) {
  avk_print_header(args[0], args[1]);
  const char * yes_txt = (argc > 3 && args[3] && args[3][0]) ? args[3] : "Yes";
  const char * no_txt  = (argc > 4 && args[4] && args[4][0]) ? args[4] : "No";
  if (avk_has_preset()) {
    avk_printf("  [PRESET] -> %s", no_txt);
    return 0;
  }
  avk_printf("\n  [VOL+] %s   |   [VOL-] %s", yes_txt, no_txt);
  int k = avk_wait_key();
  byte res = (k == AVK_KEY_UP) ? 1 : 0;
  avk_printf("  -> %s", res ? yes_txt : no_txt);
  return res;
}

void avk_textdialog(int argc, char ** args) {
  avk_print_header(args[0], args[1]);
  const char * ok_txt = (argc > 2 && args[2] && args[2][0]) ? args[2] : "OK";
  if (avk_has_preset()) {
    avk_printf("  [PRESET] %s", ok_txt);
    return;
  }
  avk_printf("\n  [VOL+ / VOL-] %s", ok_txt);
  avk_wait_key();
}

/* Helper: cycle through a list of options with VOL+ and confirm with VOL-.
 * Returns -2 when user chooses Go Back (either explicit entry or VOL- long-press). */
static int avk_pick_from_list(
    const char * group_title,
    char ** titles, char ** descs,
    int count, int initial_idx, byte can_back) {
  if (count <= 0) {
    return -1;
  }
  int cur = (initial_idx >= 0 && initial_idx < count) ? initial_idx : 0;
  int total = can_back ? (count + 1) : count;

  if (group_title && group_title[0]) {
    char cg[256];
    avk_strip_markup(group_title, cg, sizeof(cg));
    avk_emit_single_line("");
    avk_printf("  -- %s --", cg);
  }

  int i;
  for (i = 0; i < count; i++) {
    char ct[256], cd[256];
    avk_strip_markup(titles[i] ? titles[i] : "", ct, sizeof(ct));
    avk_strip_markup(descs[i] ? descs[i] : "", cd, sizeof(cd));
    avk_printf("  %d. %s%s", i + 1, ct, (i == cur) ? " [*]" : "");
    if (cd[0]) {
      avk_printf("     %s", cd);
    }
  }
  if (can_back) {
    avk_printf("  %d. <- Go Back", count + 1);
    avk_emit_single_line("  [VOL+] Next  |  [VOL-] Select  |  [Hold VOL-] Back");
  }
  else {
    avk_emit_single_line("  [VOL+] Next option   |   [VOL-] Select");
  }

  while (1) {
    if (can_back && cur == count) {
      avk_printf("  > [%d/%d] <- Go Back", cur + 1, total);
    }
    else {
      char ct[256];
      avk_strip_markup(titles[cur] ? titles[cur] : "", ct, sizeof(ct));
      avk_printf("  > [%d/%d] %s", cur + 1, total, ct);
    }
    int k = avk_wait_key();
    if (k == AVK_KEY_BACK && can_back) {
      avk_emit_single_line("  <- Back");
      return -2;
    }
    if (k == AVK_KEY_UP) {
      cur = (cur + 1) % total;
    }
    else {
      if (can_back && cur == count) {
        avk_emit_single_line("  <- Back");
        return -2;
      }
      char ct[256];
      avk_strip_markup(titles[cur] ? titles[cur] : "", ct, sizeof(ct));
      avk_printf("  [*] Selected: %s", ct);
      return cur;
    }
  }
}

void avk_menubox(int argc, char ** args, const char * prop_path, byte can_back, byte * out_back) {
  if (out_back) *out_back = 0;
  avk_print_header(args[0], args[1]);

  char * titles[64];
  char * descs[64];
  int cnt = 0;
  int i;
  for (i = 4; i < argc && cnt < 64; i += 3) {
    if (args[i] && args[i][0] != '\0') {
      titles[cnt] = args[i];
      descs[cnt]  = args[i + 1];
      cnt++;
    }
  }

  int sel = 0;
  if (avk_has_preset()) {
    char * pv = avk_preset_lookup(args[3], "selected");
    if (pv) {
      int idx = atoi(pv) - 1;
      if (idx >= 0 && idx < cnt) sel = idx;
      free(pv);
    }
    char ct[256];
    avk_strip_markup((cnt > 0 && titles[sel]) ? titles[sel] : "", ct, sizeof(ct));
    avk_printf("  [PRESET] selected=%d (%s)", sel + 1, ct);
  }
  else {
    sel = avk_pick_from_list(NULL, titles, descs, cnt, 0, can_back);
    if (sel == -2) {
      if (out_back) *out_back = 1;
      return;
    }
    if (sel < 0) sel = 0;
  }

  FILE * fp = fopen(prop_path, "wb");
  if (fp) {
    fprintf(fp, "selected=%d\n", sel + 1);
    fclose(fp);
  }
}

void avk_selectbox(int argc, char ** args, const char * prop_path, byte can_back, byte * out_back) {
  if (out_back) *out_back = 0;
  avk_print_header(args[0], args[1]);

  typedef struct {
    char * title;
    char * item_titles[64];
    char * item_descs[64];
    int    item_cnt;
    int    def_idx;
    int    chosen_idx;
  } VK_GROUP;

  VK_GROUP * groups = (VK_GROUP *) calloc(32, sizeof(VK_GROUP));
  if (!groups) {
    return;
  }
  int g_cnt = 1; /* group 0 = root (ungrouped) */
  int cur_g = 0;
  int i;

  for (i = 4; i < argc; i += 3) {
    int defchk = atoi(args[i + 2]);
    if (defchk == 2) {
      if (g_cnt < 32) {
        cur_g = g_cnt++;
        groups[cur_g].title = args[i];
        groups[cur_g].def_idx = -1;
      }
    }
    else if (defchk != 3) {
      VK_GROUP * g = &groups[cur_g];
      if (g->item_cnt < 64) {
        int idx = g->item_cnt++;
        g->item_titles[idx] = args[i];
        g->item_descs[idx]  = args[i + 1];
        if (defchk == 1) {
          g->def_idx = idx;
        }
      }
    }
  }

  byte went_back = 0;
  for (i = 0; i < g_cnt; i++) {
    VK_GROUP * g = &groups[i];
    if (g->item_cnt <= 0) {
      continue;
    }
    int def = (g->def_idx >= 0) ? g->def_idx : 0;
    if (avk_has_preset()) {
      char pkey[32];
      snprintf(pkey, sizeof(pkey), "selected.%d", i);
      char * pv = avk_preset_lookup(args[3], pkey);
      int sel = def;
      if (pv) {
        int idx = atoi(pv) - 1;
        if (idx >= 0 && idx < g->item_cnt) sel = idx;
        free(pv);
      }
      g->chosen_idx = sel;
      char ct[256];
      avk_strip_markup(g->item_titles[sel] ? g->item_titles[sel] : "", ct, sizeof(ct));
      avk_printf("  [PRESET] %s=%d (%s)", pkey, sel + 1, ct);
    }
    else {
      int sel = avk_pick_from_list(g->title, g->item_titles, g->item_descs, g->item_cnt, def, can_back);
      if (sel == -2) {
        went_back = 1;
        break;
      }
      if (sel < 0) sel = def;
      g->chosen_idx = sel;
    }
  }

  if (went_back) {
    if (out_back) *out_back = 1;
    free(groups);
    return;
  }

  FILE * fp = fopen(prop_path, "wb");
  for (i = 0; i < g_cnt; i++) {
    VK_GROUP * g = &groups[i];
    if (g->item_cnt <= 0) continue;
    if (fp) {
      fprintf(fp, "selected.%d=%d\n", i, g->chosen_idx + 1);
    }
  }
  if (fp) {
    fclose(fp);
  }
  free(groups);
}

void avk_checkbox(int argc, char ** args, const char * prop_path, byte can_back, byte * out_back) {
  if (out_back) *out_back = 0;
  avk_print_header(args[0], args[1]);
  if (!avk_has_preset()) {
    if (can_back) {
      avk_emit_single_line("  [VOL+] ON  |  [VOL-] OFF  |  [Hold VOL-] Back");
    }
    else {
      avk_emit_single_line("  [VOL+] Enable (ON)   |   [VOL-] Disable (OFF)");
    }
  }

  int out_g[128], out_idx[128], out_val[128];
  int out_cnt = 0;
  int group_id = 0;
  int idx = 0;
  int i;
  byte went_back = 0;

  for (i = 4; i < argc; i += 3) {
    int defchk = atoi(args[i + 2]);
    if (defchk == 2) {
      group_id++;
      idx = 0;
      char cg[256];
      avk_strip_markup(args[i], cg, sizeof(cg));
      avk_emit_single_line("");
      avk_printf("  -- %s --", cg);
    }
    else if (defchk != 3) {
      idx++;
      char ct[256], cd[256];
      avk_strip_markup(args[i], ct, sizeof(ct));
      avk_strip_markup(args[i + 1], cd, sizeof(cd));
      int val = defchk ? 1 : 0;
      if (avk_has_preset()) {
        char pkey[32];
        snprintf(pkey, sizeof(pkey), "item.%d.%d", group_id, idx);
        char * pv = avk_preset_lookup(args[3], pkey);
        if (pv) {
          val = (atoi(pv) != 0) ? 1 : 0;
          free(pv);
        }
        avk_printf("  [PRESET] %s: %s", ct, val ? "ON" : "OFF");
      }
      else {
        avk_emit_single_line("");
        avk_printf("  [?] %s  [Default: %s]", ct, defchk ? "ON" : "OFF");
        if (cd[0]) {
          avk_printf("      %s", cd);
        }
        int k = avk_wait_key();
        if (k == AVK_KEY_BACK && can_back) {
          avk_emit_single_line("  <- Back");
          went_back = 1;
          break;
        }
        val = (k == AVK_KEY_NONE) ? (defchk ? 1 : 0) : ((k == AVK_KEY_UP) ? 1 : 0);
        avk_printf("  -> %s: %s", ct, val ? "ON" : "OFF");
      }
      if (out_cnt < 128) {
        out_g[out_cnt]   = group_id;
        out_idx[out_cnt] = idx;
        out_val[out_cnt] = val;
        out_cnt++;
      }
    }
  }

  if (went_back) {
    if (out_back) *out_back = 1;
    return;
  }

  FILE * fp = fopen(prop_path, "wb");
  if (fp) {
    for (i = 0; i < out_cnt; i++) {
      fprintf(fp, "item.%d.%d=%d\n", out_g[i], out_idx[i], out_val[i]);
    }
    fclose(fp);
  }
}

void avk_form(int argc, char ** args, const char * prop_path, byte can_back, byte * out_back) {
  if (out_back) *out_back = 0;
  avk_print_header(args[0], args[1]);

  typedef struct {
    char * iid;
    char * title;
    char * opt_iids[64];
    char * opt_titles[64];
    char * opt_descs[64];
    int    opt_cnt;
    int    opt_def;
    int    opt_chosen;
    char * chk_iids[64];
    char * chk_titles[64];
    char * chk_descs[64];
    byte   chk_defs[64];
    byte   chk_vals[64];
    int    chk_cnt;
  } VK_FGROUP;

  VK_FGROUP * groups = (VK_FGROUP *) calloc(32, sizeof(VK_FGROUP));
  if (!groups) {
    return;
  }
  groups[0].iid = "root";
  int g_cnt = 1;
  int cur_g = 0;
  int i;

  for (i = 4; i < argc; i += 4) {
    const char * vtype = args[i + 3];
    if (strcmp(vtype, "group") == 0) {
      if (g_cnt < 32) {
        cur_g = g_cnt++;
        groups[cur_g].iid     = args[i];
        groups[cur_g].title   = args[i + 1];
        groups[cur_g].opt_def = -1;
      }
    }
    else if (strcmp(vtype, "hide") == 0) {
      continue;
    }
    else if (strcmp(vtype, "select") == 0 || strcmp(vtype, "select.selected") == 0) {
      VK_FGROUP * g = &groups[cur_g];
      if (g->opt_cnt < 64) {
        int idx = g->opt_cnt++;
        g->opt_iids[idx]   = args[i];
        g->opt_titles[idx] = args[i + 1];
        g->opt_descs[idx]  = args[i + 2];
        if (strcmp(vtype, "select.selected") == 0) {
          g->opt_def = idx;
        }
      }
    }
    else {
      VK_FGROUP * g = &groups[cur_g];
      if (g->chk_cnt < 64) {
        int idx = g->chk_cnt++;
        g->chk_iids[idx]   = args[i];
        g->chk_titles[idx] = args[i + 1];
        g->chk_descs[idx]  = args[i + 2];
        g->chk_defs[idx]   = (strcmp(vtype, "check.checked") == 0) ? 1 : 0;
      }
    }
  }

  byte went_back = 0;
  for (i = 0; i < g_cnt && !went_back; i++) {
    VK_FGROUP * g = &groups[i];
    if (g->opt_cnt > 0) {
      int def = (g->opt_def >= 0) ? g->opt_def : 0;
      if (avk_has_preset()) {
        char * pv = avk_preset_lookup(args[3], g->iid);
        int sel = def;
        if (pv) {
          int k;
          for (k = 0; k < g->opt_cnt; k++) {
            if (strcmp(g->opt_iids[k], pv) == 0) {
              sel = k;
              break;
            }
          }
          free(pv);
        }
        g->opt_chosen = sel;
        avk_printf("  [PRESET] %s=%s", g->iid, g->opt_iids[sel]);
      }
      else {
        int sel = avk_pick_from_list(g->title, g->opt_titles, g->opt_descs, g->opt_cnt, def, can_back);
        if (sel == -2) {
          went_back = 1;
          break;
        }
        if (sel < 0) sel = def;
        g->opt_chosen = sel;
      }
    }
    if (g->chk_cnt > 0 && !went_back) {
      if (g->opt_cnt == 0 && g->title && g->title[0]) {
        char cg[256];
        avk_strip_markup(g->title, cg, sizeof(cg));
        avk_emit_single_line("");
        avk_printf("  -- %s --", cg);
      }
      int j;
      for (j = 0; j < g->chk_cnt; j++) {
        char ct[256], cd[256];
        avk_strip_markup(g->chk_titles[j], ct, sizeof(ct));
        avk_strip_markup(g->chk_descs[j], cd, sizeof(cd));
        int val = g->chk_defs[j];
        if (avk_has_preset()) {
          char * pv = avk_preset_lookup(args[3], g->chk_iids[j]);
          if (pv) {
            val = (atoi(pv) != 0) ? 1 : 0;
            free(pv);
          }
          avk_printf("  [PRESET] %s: %s", ct, val ? "ON" : "OFF");
        }
        else {
          avk_emit_single_line("");
          avk_printf("  [?] %s  [Default: %s]", ct, g->chk_defs[j] ? "ON" : "OFF");
          if (cd[0]) {
            avk_printf("      %s", cd);
          }
          avk_emit_single_line(can_back
            ? "  [VOL+] ON  |  [VOL-] OFF  |  [Hold VOL-] Back"
            : "  [VOL+] Enable (ON)   |   [VOL-] Disable (OFF)");
          int k = avk_wait_key();
          if (k == AVK_KEY_BACK && can_back) {
            avk_emit_single_line("  <- Back");
            went_back = 1;
            break;
          }
          val = (k == AVK_KEY_NONE) ? g->chk_defs[j] : ((k == AVK_KEY_UP) ? 1 : 0);
          avk_printf("  -> %s: %s", ct, val ? "ON" : "OFF");
        }
        g->chk_vals[j] = (byte) val;
      }
    }
  }

  if (went_back) {
    if (out_back) *out_back = 1;
    free(groups);
    return;
  }

  FILE * fp = fopen(prop_path, "wb");
  if (fp) {
    for (i = 0; i < g_cnt; i++) {
      VK_FGROUP * g = &groups[i];
      if (g->opt_cnt > 0) {
        fprintf(fp, "%s=%s\n", g->iid, g->opt_iids[g->opt_chosen]);
      }
      int j;
      for (j = 0; j < g->chk_cnt; j++) {
        fprintf(fp, "%s=%d\n", g->chk_iids[j], g->chk_vals[j]);
      }
    }
    fclose(fp);
  }
  free(groups);
}

void avk_slider(int argc, char ** args, const char * prop_path, byte can_back, byte * out_back) {
  if (out_back) *out_back = 0;
  avk_print_header(args[0], args[1]);

  const char * key  = args[4];
  int min_v  = atoi(args[5]);
  int max_v  = atoi(args[6]);
  int step_v = atoi(args[7]);
  int cur_v  = atoi(args[8]);
  const char * unit = (argc >= 10 && args[9]) ? args[9] : "";
  if (max_v < min_v) { int t = max_v; max_v = min_v; min_v = t; }
  if (step_v < 1) step_v = 1;
  if (cur_v < min_v) cur_v = min_v;
  if (cur_v > max_v) cur_v = max_v;

  if (avk_has_preset()) {
    char * pv = avk_preset_lookup(args[3], key);
    if (pv) {
      cur_v = atoi(pv);
      if (cur_v < min_v) cur_v = min_v;
      if (cur_v > max_v) cur_v = max_v;
      free(pv);
    }
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "%d", cur_v);
    aui_set_prop_key(prop_path, key, vbuf);
    avk_printf("  [PRESET] %s=%d%s%s", key, cur_v, unit[0] ? " " : "", unit);
    return;
  }

  avk_printf("  Range: %d .. %d (step %d)%s%s", min_v, max_v, step_v, unit[0] ? " " : "", unit);
  avk_emit_single_line(can_back
    ? "  [VOL+] +Step  |  [VOL-] Confirm  |  [Hold VOL-] Back"
    : "  [VOL+] +Step  |  [VOL-] Confirm");

  while (1) {
    char bar[21];
    int span = max_v - min_v;
    int fill = (span > 0) ? (((cur_v - min_v) * 16) / span) : 16;
    if (fill < 0) fill = 0;
    if (fill > 16) fill = 16;
    int b;
    for (b = 0; b < 16; b++) {
      bar[b] = (b < fill) ? '#' : '-';
    }
    bar[16] = '\0';
    avk_printf("  > [%s] %d%s%s", bar, cur_v, unit[0] ? " " : "", unit);

    int k = avk_wait_key();
    if (k == AVK_KEY_BACK && can_back) {
      avk_emit_single_line("  <- Back");
      if (out_back) *out_back = 1;
      return;
    }
    if (k == AVK_KEY_UP) {
      cur_v += step_v;
      if (cur_v > max_v) {
        cur_v = min_v;
      }
    }
    else {
      avk_printf("  [*] Set: %d%s%s", cur_v, unit[0] ? " " : "", unit);
      break;
    }
  }

  char vbuf[32];
  snprintf(vbuf, sizeof(vbuf), "%d", cur_v);
  aui_set_prop_key(prop_path, key, vbuf);
}

int avk_run_install(const char * title, const char * desc) {
  avk_print_header(title, desc);

  /* Config-only mode (invoked from customize.sh in Magisk / KernelSU / APatch app):
   * Stop cleanly here and leave AROMA_TMP prop files intact for customize.sh to apply. */
  if (g_vk_config_only) {
    g_vk_done_install = 1;
    avk_emit_single_line("- AROMA-Next configuration captured; continuing module install...");
    return 0;
  }

  /* Full VolKey mode (e.g. Recovery EXIT 77 fallback via update-binary):
   * Extract and run magisk-installer (or update-binary-installer) directly. */
  char binary[256];
  snprintf(binary, sizeof(binary), "%s/update-binary", aroma_tmp_dir());

  int res = az_extract(AROMA_ORIB, binary);
  if (res == 0) {
    res = az_extract(AROMA_MAGISK_ORIB, binary);
  }
  if (res == 0) {
    avk_emit_single_line("! No backend installer found in archive.");
    return 1;
  }
  chmod(binary, 0755);

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    return 1;
  }

  char pipestr[16];
  snprintf(pipestr, sizeof(pipestr), "%d", pipefd[1]);
  char * argscmd[5] = { binary, getArgv(0), pipestr, getArgv(1), NULL };

  az_close();

  pid_t pid = fork();
  if (pid == 0) {
    setenv("UPDATE_PACKAGE", getArgv(1), 1);
    setenv("AROMA_TMP",      aroma_tmp_dir(), 1);
    setenv("AROMA_DONE",     "1", 1);
    setenv("AROMA_VERSION",  AROMA_VERSION, 1);
    setenv("AROMA_BUILD",    AROMA_BUILD, 1);
    setenv("AROMA_BUILD_CN", AROMA_BUILD_CN, 1);
    setenv("AROMA_NAME",     AROMA_NAME, 1);
    setenv("AROMA_COPY",     AROMA_COPY, 1);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    execv(binary, argscmd);
    _exit(127);
  }

  close(pipefd[1]);
  FILE * from_child = fdopen(pipefd[0], "r");
  if (from_child) {
    char line[1024];
    while (fgets(line, sizeof(line), from_child) != NULL) {
      size_t len = strlen(line);
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
      }
      if (strncmp(line, "ui_print ", 9) == 0) {
        const char * msg = line + 9;
        if (msg[0] == '@') {
          msg++;
        }
        avk_emit_single_line(msg);
      }
      else if (strcmp(line, "ui_print") == 0 ||
               strncmp(line, "set_progress ", 13) == 0 ||
               strncmp(line, "progress ", 9) == 0) {
        continue;
      }
      else if (len > 0) {
        avk_emit_single_line(line);
      }
    }
    fclose(from_child);
  }

  int status = 0;
  waitpid(pid, &status, 0);
  az_init(getArgv(1));
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
