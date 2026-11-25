/*
 * Copyright (C) 2026 Miustone (AROMA-Next-Installer)
 * Based on AROMA Installer / AROMA Filemanager (C) 2011-2013 Ahmad Amarullah
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
 * Descriptions:
 * -------------
 * AROMA-Next Integrated File Manager & Root Module Manager
 * Shares the AROMA-Next 64-bit DRM/KMS, Touch, Theme, and Font engine.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../aroma.h"

#define AFM_PATH_MAX      1024
#define AFM_MAX_ENTRIES   1024
#define AFM_MAX_EDIT_SZ   (256 * 1024)
#define AFM_MAX_LINES     4096

#define AFM_VIEW_MODULES  0
#define AFM_VIEW_FILES    1

#define AFM_CLIP_NONE     0
#define AFM_CLIP_COPY     1
#define AFM_CLIP_CUT      2

typedef struct {
  char   name[256];
  char   fullpath[AFM_PATH_MAX];
  mode_t mode;
  off_t  size;
  byte   is_dir;
  byte   is_link;
  byte   is_parent;
} AFM_ENTRY;

typedef struct {
  char id[256];
  char path[AFM_PATH_MAX];
  char name[128];
  char version[64];
  char author[64];
  char desc[256];
  byte disabled;
  byte remove_flag;
  byte update_flag;
} AFM_MODULE;

static byte afm_active       = 0;
static byte afm_show_hidden  = 0;
static byte afm_clip_mode    = AFM_CLIP_NONE;
static char afm_clip_path[AFM_PATH_MAX] = {0};
static char afm_clip_name[256]          = {0};

byte afm_is_active(void) {
  return afm_active;
}

/*********************************[ HELPERS ]**********************************/

static void afm_join_path(char * out, size_t out_sz, const char * dir, const char * name) {
  if (dir == NULL || dir[0] == '\0' || strcmp(dir, "/") == 0) {
    snprintf(out, out_sz, "/%s", name);
  }
  else {
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/') {
      snprintf(out, out_sz, "%s%s", dir, name);
    }
    else {
      snprintf(out, out_sz, "%s/%s", dir, name);
    }
  }
}

static void afm_parent_path(const char * path, char * out, size_t out_sz) {
  if (path == NULL || path[0] == '\0' || strcmp(path, "/") == 0) {
    snprintf(out, out_sz, "/");
    return;
  }
  snprintf(out, out_sz, "%s", path);
  size_t len = strlen(out);
  while (len > 1 && out[len - 1] == '/') {
    out[--len] = '\0';
  }
  char * slash = strrchr(out, '/');
  if (slash == NULL || slash == out) {
    snprintf(out, out_sz, "/");
  }
  else {
    *slash = '\0';
  }
}

static void afm_format_size(off_t sz, char * out, size_t out_sz) {
  if (sz < 1024) {
    snprintf(out, out_sz, "%lld B", (long long) sz);
  }
  else if (sz < 1024LL * 1024LL) {
    snprintf(out, out_sz, "%.1f KB", (double) sz / 1024.0);
  }
  else if (sz < 1024LL * 1024LL * 1024LL) {
    snprintf(out, out_sz, "%.2f MB", (double) sz / (1024.0 * 1024.0));
  }
  else {
    snprintf(out, out_sz, "%.2f GB", (double) sz / (1024.0 * 1024.0 * 1024.0));
  }
}

static void afm_format_mode(mode_t m, char * out, size_t out_sz) {
  char p[10];
  p[0] = (m & S_IRUSR) ? 'r' : '-';
  p[1] = (m & S_IWUSR) ? 'w' : '-';
  p[2] = (m & S_IXUSR) ? 'x' : '-';
  p[3] = (m & S_IRGRP) ? 'r' : '-';
  p[4] = (m & S_IWGRP) ? 'w' : '-';
  p[5] = (m & S_IXGRP) ? 'x' : '-';
  p[6] = (m & S_IROTH) ? 'r' : '-';
  p[7] = (m & S_IWOTH) ? 'w' : '-';
  p[8] = (m & S_IXOTH) ? 'x' : '-';
  p[9] = '\0';
  snprintf(out, out_sz, "%s (%04o)", p, (unsigned)(m & 07777));
}

static byte afm_path_exists(const char * p) {
  struct stat st;
  return (p != NULL && lstat(p, &st) == 0) ? 1 : 0;
}

static byte afm_dir_exists(const char * p) {
  struct stat st;
  return (p != NULL && stat(p, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
}

static void afm_detect_storage_path(char * out, size_t out_sz) {
  if (afm_dir_exists("/sdcard")) {
    snprintf(out, out_sz, "/sdcard");
  }
  else if (afm_dir_exists("/data/media/0")) {
    snprintf(out, out_sz, "/data/media/0");
  }
  else {
    snprintf(out, out_sz, "/");
  }
}

/* Query /proc/mounts for longest-prefix mountpoint covering `path`.
 * Returns 1 if mounted RW, 0 if RO or unknown. */
static byte afm_mount_info(const char * path, char * out_mp, size_t mp_sz,
                           char * out_fs, size_t fs_sz) {
  if (out_mp && mp_sz > 0) snprintf(out_mp, mp_sz, "/");
  if (out_fs && fs_sz > 0) snprintf(out_fs, fs_sz, "unknown");

  FILE * fp = fopen("/proc/mounts", "r");
  if (!fp) {
    return 0;
  }

  char line[1024];
  size_t best_len = 0;
  byte best_rw = 0;
  char best_mp[256] = "/";
  char best_fs[64]  = "unknown";
  size_t path_len = strlen(path);

  while (fgets(line, sizeof(line), fp) != NULL) {
    char dev[256], mp[256], fs[64], opts[512];
    if (sscanf(line, "%255s %255s %63s %511s", dev, mp, fs, opts) != 4) {
      continue;
    }
    size_t mplen = strlen(mp);
    byte match = 0;
    if (mplen == 1 && mp[0] == '/') {
      match = 1;
    }
    else if (path_len >= mplen && strncmp(path, mp, mplen) == 0 &&
             (path[mplen] == '\0' || path[mplen] == '/')) {
      match = 1;
    }
    if (match && mplen >= best_len) {
      best_len = mplen;
      snprintf(best_mp, sizeof(best_mp), "%s", mp);
      snprintf(best_fs, sizeof(best_fs), "%s", fs);
      best_rw = 0;
      char * tok = opts;
      while (tok && *tok) {
        char * comma = strchr(tok, ',');
        size_t tlen = comma ? (size_t)(comma - tok) : strlen(tok);
        if (tlen == 2 && strncmp(tok, "rw", 2) == 0) {
          best_rw = 1;
          break;
        }
        tok = comma ? (comma + 1) : NULL;
      }
    }
  }
  fclose(fp);

  if (out_mp && mp_sz > 0) snprintf(out_mp, mp_sz, "%s", best_mp);
  if (out_fs && fs_sz > 0) snprintf(out_fs, fs_sz, "%s", best_fs);
  return best_rw;
}

static byte afm_try_remount_rw(const char * path, char * msg, size_t msg_sz) {
  char mp[256], fs[64];
  byte is_rw = afm_mount_info(path, mp, sizeof(mp), fs, sizeof(fs));
  if (is_rw) {
    snprintf(msg, msg_sz, "%s (%s) is already mounted RW.", mp, fs);
    return 1;
  }
  if (strcmp(fs, "erofs") == 0) {
    snprintf(msg, msg_sz,
             "%s uses EROFS (read-only by kernel design).\n\n"
             "Use Systemless Modules (/data/adb/modules) or convert partitions beforehand with RO2RW / SystemRW.",
             mp);
    return 0;
  }

  /* Attempt direct mount syscall first */
  if (mount(NULL, mp, NULL, MS_REMOUNT, NULL) == 0) {
    if (afm_mount_info(path, mp, sizeof(mp), fs, sizeof(fs))) {
      snprintf(msg, msg_sz, "Successfully remounted %s (%s) as RW.", mp, fs);
      return 1;
    }
  }

  /* Fallback to mount helper binary */
  static const char * mount_bins[] = {
    "/sbin/mount", "/system/bin/mount", "/bin/mount", "/vendor/bin/mount", NULL
  };
  const char * mbin = NULL;
  int i;
  for (i = 0; mount_bins[i] != NULL; i++) {
    if (access(mount_bins[i], X_OK) == 0) {
      mbin = mount_bins[i];
      break;
    }
  }
  if (mbin != NULL) {
    pid_t pid = fork();
    if (pid == 0) {
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
      }
      execl(mbin, "mount", "-o", "remount,rw", mp, (char *) NULL);
      _exit(127);
    }
    else if (pid > 0) {
      int st = 0;
      waitpid(pid, &st, 0);
    }
  }

  if (afm_mount_info(path, mp, sizeof(mp), fs, sizeof(fs))) {
    snprintf(msg, msg_sz, "Successfully remounted %s (%s) as RW.", mp, fs);
    return 1;
  }

  snprintf(msg, msg_sz,
           "Could not remount %s (%s) as RW.\n\n"
           "The partition may be protected by shared_blocks ext4, dm-verity, or EROFS.",
           mp, fs);
  return 0;
}

/****************************[ FILE OPERATIONS ]*******************************/

static byte afm_copy_file(const char * src, const char * dst) {
  struct stat st;
  if (stat(src, &st) != 0) {
    return 0;
  }
  int in_fd = open(src, O_RDONLY);
  if (in_fd < 0) {
    return 0;
  }
  int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
  if (out_fd < 0) {
    close(in_fd);
    return 0;
  }

  char * buf = (char *) malloc(65536);
  if (!buf) {
    close(in_fd);
    close(out_fd);
    return 0;
  }

  byte ok = 1;
  ssize_t r;
  while ((r = read(in_fd, buf, 65536)) > 0) {
    ssize_t off = 0;
    while (off < r) {
      ssize_t w = write(out_fd, buf + off, (size_t)(r - off));
      if (w <= 0) {
        ok = 0;
        break;
      }
      off += w;
    }
    if (!ok) break;
  }
  if (r < 0) ok = 0;

  free(buf);
  close(in_fd);
  close(out_fd);
  if (ok) {
    chmod(dst, st.st_mode & 07777);
  }
  return ok;
}

static byte afm_is_subpath(const char * parent, const char * child) {
  size_t plen = strlen(parent);
  if (plen == 0) return 0;
  if (strncmp(parent, child, plen) != 0) return 0;
  return (child[plen] == '\0' || child[plen] == '/') ? 1 : 0;
}

static byte afm_copy_recursive(const char * src, const char * dst) {
  if (strcmp(src, dst) == 0 || afm_is_subpath(src, dst)) {
    errno = EINVAL;
    return 0;
  }
  struct stat st;
  if (lstat(src, &st) != 0) {
    return 0;
  }

  if (S_ISLNK(st.st_mode)) {
    char target[AFM_PATH_MAX];
    ssize_t n = readlink(src, target, sizeof(target) - 1);
    if (n < 0) return 0;
    target[n] = '\0';
    unlink(dst);
    return (symlink(target, dst) == 0) ? 1 : 0;
  }
  if (S_ISREG(st.st_mode)) {
    return afm_copy_file(src, dst);
  }
  if (S_ISDIR(st.st_mode)) {
    if (mkdir(dst, st.st_mode & 0777) != 0 && errno != EEXIST) {
      return 0;
    }
    DIR * d = opendir(src);
    if (!d) return 0;
    struct dirent * de;
    byte ok = 1;
    while ((de = readdir(d)) != NULL) {
      if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
        continue;
      }
      char csrc[AFM_PATH_MAX], cdst[AFM_PATH_MAX];
      afm_join_path(csrc, sizeof(csrc), src, de->d_name);
      afm_join_path(cdst, sizeof(cdst), dst, de->d_name);
      if (!afm_copy_recursive(csrc, cdst)) {
        ok = 0;
        break;
      }
    }
    closedir(d);
    chmod(dst, st.st_mode & 07777);
    return ok;
  }
  return 0;
}

static byte afm_delete_recursive(const char * path) {
  if (path == NULL || path[0] == '\0' || strcmp(path, "/") == 0) {
    errno = EPERM;
    return 0;
  }
  struct stat st;
  if (lstat(path, &st) != 0) {
    return 0;
  }
  if (!S_ISDIR(st.st_mode)) {
    return (unlink(path) == 0) ? 1 : 0;
  }
  DIR * d = opendir(path);
  if (!d) return 0;
  struct dirent * de;
  byte ok = 1;
  while ((de = readdir(d)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
      continue;
    }
    char child[AFM_PATH_MAX];
    afm_join_path(child, sizeof(child), path, de->d_name);
    if (!afm_delete_recursive(child)) {
      ok = 0;
      break;
    }
  }
  closedir(d);
  if (!ok) return 0;
  return (rmdir(path) == 0) ? 1 : 0;
}

static byte afm_move_item(const char * src, const char * dst) {
  if (strcmp(src, dst) == 0 || afm_is_subpath(src, dst)) {
    errno = EINVAL;
    return 0;
  }
  if (rename(src, dst) == 0) {
    return 1;
  }
  if (errno == EXDEV) {
    if (!afm_copy_recursive(src, dst)) {
      return 0;
    }
    return afm_delete_recursive(src);
  }
  return 0;
}

/*************************[ DIALOG / UI HELPERS ]******************************/

static void afm_draw_modal_chrome(CANVAS * bg, int winX, int winY, int winW, int winH,
                                  int titH, const char * title) {
  if (!atheme_draw("img.dialog", bg, winX, winY, winW, winH)) {
    ag_roundgrad(bg, winX - 1, winY - 1, winW + 2, winH + 2,
                 acfg()->border, acfg()->border_g, (acfg()->roundsz * agdp()) + 1);
    ag_roundgrad(bg, winX, winY, winW, winH,
                 acfg()->dialogbg, acfg()->dialogbg_g, acfg()->roundsz * agdp());
  }
  if (!atheme_draw("img.dialog.titlebar", bg, winX, winY, winW, titH)) {
    ag_roundgrad_ex(bg, winX, winY, winW, titH,
                    acfg()->dlgtitlebg, acfg()->dlgtitlebg_g,
                    acfg()->roundsz * agdp(), 1, 1, 0, 0);
  }
  int dlg_id = atheme_id("img.dialog");
  if (!(dlg_id >= 0 && acfg()->theme_9p[dlg_id]) && acfg()->border != acfg()->dialogbg) {
    int bw = agdp() / 3;
    if (bw < 2) bw = 2;
    int bh = (bw > 2) ? 2 : bw;
    ag_rect(bg, winX, winY, winW, bw, acfg()->border);
    ag_rect(bg, winX, winY + winH - bw, winW, bw, acfg()->border);
    ag_rect(bg, winX, winY, bw, winH, acfg()->border);
    ag_rect(bg, winX + winW - bw, winY, bw, winH, acfg()->border);
    if (titH > bw) {
      ag_rect(bg, winX, winY + titH - bh, winW, bh, acfg()->border);
    }
  }
  int titW = ag_txtwidth((char *) title, 1);
  int titX = winX + (winW - titW) / 2;
  int titY = winY + (titH - ag_fontheight(1)) / 2;
  ag_textf(bg, titW, titX + 1, titY + 1, (char *) title, acfg()->dlgtitlebg_g, 1);
  ag_text(bg, titW, titX, titY, (char *) title, acfg()->dlgtitlefg, 1);
}

/* Modal picker menu returning 0-based selected index, or -1 if cancelled. */
static int afm_action_sheet(AWINDOWP parent, const char * title,
                            const char ** items, const char ** descs, int count) {
  if (count <= 0) return -1;

  int pad  = agdp() * 4;
  int winW = agw() - (pad * 2);
  int btnH = agdp() * 20;
  int titH = ag_fontheight(1) + (pad * 2);
  int maxListH = (agh() * 58) / 100;
  int estItemH = ag_fontheight(0) * 3 + (agdp() * 6);
  int listH = estItemH * count;
  if (listH > maxListH) listH = maxListH;
  if (listH < (agh() * 25) / 100) listH = (agh() * 25) / 100;

  int winH  = titH + listH + btnH + (pad * 3);
  int winX  = pad;
  int winY  = (agh() - winH) / 2;
  int listX = winX + pad;
  int listW = winW - (pad * 2);
  int listY = winY + titH + pad;
  int btnY  = listY + listH + pad;

  CANVAS bg;
  ag_canvas(&bg, agw(), agh());
  ag_draw(&bg, agc(), 0, 0);
  ag_rectopa(&bg, 0, 0, agw(), agh(), 0x0000, 175);
  afm_draw_modal_chrome(&bg, winX, winY, winW, winH, titH, title);

  AWINDOWP hWin = aw(&bg);
  ACONTROLP menu = acmenu(hWin, listX, listY, listW, listH, 6);
  int i;
  for (i = 0; i < count; i++) {
    acmenu_add(menu, (char *) items[i], (char *)(descs && descs[i] ? descs[i] : ""), "");
  }
  acbutton(hWin, listX, btnY, listW, btnH, acfg()->text_back, 0, 5);

  aw_set_on_dialog(1);
  aw_show_ex(hWin, 4, 0, NULL);

  int picked = -1;
  byte loop = 1;
  while (loop) {
    dword msg = aw_dispatch(hWin);
    switch (aw_gm(msg)) {
      case 6:
        picked = acmenu_getselectedindex(menu);
        loop = 0;
        break;
      case 5:
        picked = -1;
        loop = 0;
        break;
    }
  }

  aw_destroy(hWin);
  ag_ccanvas(&bg);
  aw_set_on_dialog(0);
  if (parent != NULL) {
    aw_draw(parent);
  }
  return picked;
}

/***********************[ ON-SCREEN TOUCH KEYBOARD ]***************************/

static void afm_utf8_pop_char(char * buf) {
  size_t len = strlen(buf);
  if (len == 0) return;
  size_t pos = len - 1;
  while (pos > 0 && (((unsigned char) buf[pos]) & 0xC0u) == 0x80u) {
    pos--;
  }
  buf[pos] = '\0';
}

/* Full touch QWERTY + symbol keyboard dialog.
 * Returns 1 if user pressed OK (out_buf updated), 0 if Cancelled. */
static byte afm_input_dialog(AWINDOWP parent, const char * title,
                             const char * initial, char * out_buf, size_t out_sz) {
  static const char * rows_lower[3][10] = {
    { "q","w","e","r","t","y","u","i","o","p" },
    { "a","s","d","f","g","h","j","k","l","-" },
    { "z","x","c","v","b","n","m",".","_","/" }
  };
  static const char * rows_upper[3][10] = {
    { "Q","W","E","R","T","Y","U","I","O","P" },
    { "A","S","D","F","G","H","J","K","L","=" },
    { "Z","X","C","V","B","N","M",",",":","/" }
  };
  static const char * rows_sym[3][10] = {
    { "1","2","3","4","5","6","7","8","9","0" },
    { "!","@","#","$","%","&","*","(",")","-" },
    { "+","=","[","]","\"","'","~",";","?","\\" }
  };

  char text[512];
  snprintf(text, sizeof(text), "%s", (initial != NULL) ? initial : "");

  byte mode = 0; /* 0 = lower, 1 = upper, 2 = symbols */
  byte result = 0;
  byte running = 1;

  int pad   = agdp() * 3;
  int winW  = agw() - (pad * 2);
  int titH  = ag_fontheight(1) + (pad * 2);
  int boxH  = ag_fontheight(0) * 2 + (pad * 2);
  int keyH  = agdp() * 18;
  int kpad  = agdp();
  if (kpad < 2) kpad = 2;
  int btnH  = agdp() * 19;
  int winH  = titH + boxH + (keyH * 4) + btnH + (kpad * 5) + (pad * 4);
  if (winH > agh() - (pad * 2)) {
    keyH = agdp() * 14;
    btnH = agdp() * 16;
    winH = titH + boxH + (keyH * 4) + btnH + (kpad * 5) + (pad * 4);
  }
  int winX  = pad;
  int winY  = (agh() - winH) / 2;
  int inX   = winX + pad;
  int inW   = winW - (pad * 2);
  int boxY  = winY + titH + pad;
  int kbY   = boxY + boxH + pad;
  int keyW  = (inW - (kpad * 9)) / 10;

  while (running) {
    CANVAS bg;
    ag_canvas(&bg, agw(), agh());
    ag_draw(&bg, agc(), 0, 0);
    ag_rectopa(&bg, 0, 0, agw(), agh(), 0x0000, 185);
    afm_draw_modal_chrome(&bg, winX, winY, winW, winH, titH, title);

    /* Input preview box */
    ag_roundgrad(&bg, inX - 1, boxY - 1, inW + 2, boxH + 2,
                 acfg()->border, acfg()->border_g, agdp() * 2);
    ag_roundgrad(&bg, inX, boxY, inW, boxH,
                 acfg()->textbg, acfg()->textbg, agdp() * 2);

    char disp[520];
    snprintf(disp, sizeof(disp), "%s_", text);
    /* Trim left side for preview if text exceeds input box width */
    const char * show = disp;
    int maxTxtW = inW - (pad * 2);
    while (*show && ag_txtwidth((char *) show, 0) > maxTxtW) {
      show++;
    }
    ag_text(&bg, maxTxtW, inX + pad, boxY + (boxH - ag_fontheight(0)) / 2,
            (char *) show, acfg()->textfg, 0);

    AWINDOWP hWin = aw(&bg);

    /* 3 x 10 character rows -> touchmsg 100..129 */
    int r, c;
    for (r = 0; r < 3; r++) {
      int ry = kbY + r * (keyH + kpad);
      for (c = 0; c < 10; c++) {
        const char * lbl = (mode == 0) ? rows_lower[r][c] :
                           (mode == 1) ? rows_upper[r][c] : rows_sym[r][c];
        int kx = inX + c * (keyW + kpad);
        acbutton(hWin, kx, ry, keyW, keyH, (char *) lbl, 0, (byte)(100 + r * 10 + c));
      }
    }

    /* Row 4: Shift/ABC | 123/abc | Space | Clear | Del */
    int r4y = kbY + 3 * (keyH + kpad);
    int wShift = (inW * 18) / 100;
    int wSym   = (inW * 18) / 100;
    int wClr   = (inW * 16) / 100;
    int wDel   = (inW * 18) / 100;
    int wSpc   = inW - (wShift + wSym + wClr + wDel + kpad * 4);
    int curx   = inX;

    acbutton(hWin, curx, r4y, wShift, keyH, (mode == 1) ? "abc" : "ABC", 0, 60);
    curx += wShift + kpad;
    acbutton(hWin, curx, r4y, wSym, keyH, (mode == 2) ? "abc" : "123", 0, 61);
    curx += wSym + kpad;
    acbutton(hWin, curx, r4y, wSpc, keyH, "Space", 0, 62);
    curx += wSpc + kpad;
    acbutton(hWin, curx, r4y, wClr, keyH, "Clr", 0, 63);
    curx += wClr + kpad;
    acbutton(hWin, curx, r4y, wDel, keyH, "Del", 0, 64);

    /* Bottom row: Cancel (left) | OK (right) */
    int actY = r4y + keyH + pad;
    int actW = (inW - pad) / 2;
    acbutton(hWin, inX, actY, actW, btnH, acfg()->text_back, 0, 5);
    acbutton(hWin, inX + actW + pad, actY, actW, btnH, acfg()->text_ok, 0, 6);

    aw_set_on_dialog(1);
    aw_show_ex(hWin, 4, 0, NULL);

    dword msg = aw_dispatch(hWin);
    byte m = aw_gm(msg);
    if (m >= 100 && m < 130) {
      int idx = m - 100;
      const char * ch = (mode == 0) ? rows_lower[idx / 10][idx % 10] :
                        (mode == 1) ? rows_upper[idx / 10][idx % 10] :
                                      rows_sym[idx / 10][idx % 10];
      size_t curlen = strlen(text);
      if (curlen + strlen(ch) + 1 < sizeof(text)) {
        strcat(text, ch);
      }
    }
    else if (m == 60) {
      mode = (mode == 1) ? 0 : 1;
    }
    else if (m == 61) {
      mode = (mode == 2) ? 0 : 2;
    }
    else if (m == 62) {
      size_t curlen = strlen(text);
      if (curlen + 2 < sizeof(text)) {
        text[curlen] = ' ';
        text[curlen + 1] = '\0';
      }
    }
    else if (m == 63) {
      text[0] = '\0';
    }
    else if (m == 64) {
      afm_utf8_pop_char(text);
    }
    else if (m == 6) {
      snprintf(out_buf, out_sz, "%s", text);
      result = 1;
      running = 0;
    }
    else if (m == 5) {
      result = 0;
      running = 0;
    }

    aw_destroy(hWin);
    ag_ccanvas(&bg);
    aw_set_on_dialog(0);
  }

  if (parent != NULL) {
    aw_draw(parent);
  }
  return result;
}

/************************[ TEXT VIEWER & LINE EDITOR ]*************************/

static byte afm_save_lines(const char * path, char ** lines, int line_cnt, mode_t orig_mode) {
  FILE * fp = fopen(path, "wb");
  if (!fp) {
    return 0;
  }
  int i;
  for (i = 0; i < line_cnt; i++) {
    fputs(lines[i], fp);
    fputc('\n', fp);
  }
  int ok = (ferror(fp) == 0) ? 1 : 0;
  if (fclose(fp) != 0) ok = 0;
  if (ok && orig_mode != 0) {
    chmod(path, orig_mode & 07777);
  }
  return (byte) ok;
}

static void afm_text_editor(AWINDOWP parent, const char * filepath) {
  struct stat st;
  mode_t orig_mode = 0644;
  byte exists = (stat(filepath, &st) == 0) ? 1 : 0;
  if (exists) {
    orig_mode = st.st_mode;
    if (st.st_size > AFM_MAX_EDIT_SZ) {
      aw_alert(parent, "File Too Large",
               "Text editor supports files up to 256 KB.\nThis file exceeds the limit.",
               "@alert", NULL);
      return;
    }
  }

  char ** lines = (char **) calloc(AFM_MAX_LINES, sizeof(char *));
  if (!lines) return;
  int line_cnt = 0;

  if (exists) {
    FILE * fp = fopen(filepath, "rb");
    if (!fp) {
      free(lines);
      char err[256];
      snprintf(err, sizeof(err), "Cannot open file:\n%s\n(%s)", filepath, strerror(errno));
      aw_alert(parent, "Read Error", err, "@alert", NULL);
      return;
    }
    size_t fsz = (size_t) st.st_size;
    char * raw = (char *) malloc(fsz + 1);
    if (!raw) {
      fclose(fp);
      free(lines);
      return;
    }
    size_t nread = fread(raw, 1, fsz, fp);
    fclose(fp);
    raw[nread] = '\0';

    /* Reject binary files (NUL byte in first 512 bytes) */
    size_t chk = (nread < 512) ? nread : 512;
    size_t k;
    for (k = 0; k < chk; k++) {
      if (raw[k] == '\0') {
        free(raw);
        free(lines);
        aw_alert(parent, "Binary File",
                 "This appears to be a binary file and cannot be edited as plain text.",
                 "@alert", NULL);
        return;
      }
    }

    char * p = raw;
    while (*p && line_cnt < AFM_MAX_LINES) {
      char * nl = strchr(p, '\n');
      size_t llen = nl ? (size_t)(nl - p) : strlen(p);
      while (llen > 0 && p[llen - 1] == '\r') {
        llen--;
      }
      lines[line_cnt] = (char *) malloc(llen + 1);
      if (lines[line_cnt]) {
        memcpy(lines[line_cnt], p, llen);
        lines[line_cnt][llen] = '\0';
        line_cnt++;
      }
      if (!nl) break;
      p = nl + 1;
    }
    free(raw);
  }

  if (line_cnt == 0) {
    lines[0] = strdup("");
    line_cnt = 1;
  }

  byte dirty = 0;
  byte running = 1;
  const char * base = strrchr(filepath, '/');
  base = base ? (base + 1) : filepath;

  while (running) {
    int pad  = agdp() * 2;
    int capH = ag_fontheight(1) + (pad * 2);
    int navH = agdp() * 24;
    int subH = ag_fontheight(0) + (pad * 2);
    int listY = capH + subH;
    int listH = agh() - listY - navH;

    CANVAS bg;
    ag_canvas(&bg, agw(), agh());
    ag_rect(&bg, 0, 0, agw(), agh(), acfg()->winbg);
    if (!atheme_id_draw(0, &bg, 0, 0, agw(), agh())) {
      ag_roundgrad(&bg, 0, 0, agw(), agh(), acfg()->winbg, acfg()->winbg_g, 0);
    }
    if (!atheme_id_draw(1, &bg, 0, 0, agw(), capH)) {
      ag_roundgrad_ex(&bg, 0, 0, agw(), capH, acfg()->titlebg, acfg()->titlebg_g, 0, 1, 1, 0, 0);
    }
    if (!atheme_id_draw(2, &bg, 0, agh() - navH, agw(), navH)) {
      ag_roundgrad_ex(&bg, 0, agh() - navH, agw(), navH, acfg()->navbg, acfg()->navbg_g, 0, 0, 0, 1, 1);
    }

    char hdr[160];
    snprintf(hdr, sizeof(hdr), "%s%s", base, dirty ? " *" : "");
    int hdrW = ag_txtwidth(hdr, 1);
    ag_text(&bg, hdrW, (agw() - hdrW) / 2, pad, hdr, acfg()->titlefg, 1);

    char sub[256];
    snprintf(sub, sizeof(sub), "%d lines • Tap any line to edit/insert/delete", line_cnt);
    ag_rect(&bg, 0, capH, agw(), subH, acfg()->controlbg);
    ag_text(&bg, agw() - pad * 2, pad, capH + pad, sub, acfg()->controlfg, 0);

    AWINDOWP hWin = aw(&bg);
    ACONTROLP menu = acmenu(hWin, 0, listY, agw(), listH, 10);
    int i;
    for (i = 0; i < line_cnt; i++) {
      char lnum[32];
      snprintf(lnum, sizeof(lnum), "Line %03d", i + 1);
      acmenu_add(menu, lines[i][0] ? lines[i] : "(empty line)", lnum, "");
    }

    int btnY = agh() - navH + pad;
    int btnH = navH - (pad * 2);
    int btnW = (agw() - (pad * 4)) / 3;
    acbutton(hWin, pad, btnY, btnW, btnH, acfg()->text_back, 0, 5);
    acbutton(hWin, pad * 2 + btnW, btnY, btnW, btnH, "+ Add Line", 0, 11);
    acbutton(hWin, pad * 3 + btnW * 2, btnY, btnW, btnH, dirty ? "Save *" : "Save", 0, 12);

    aw_show_ex(hWin, 4, 0, NULL);
    dword msg = aw_dispatch(hWin);
    byte m = aw_gm(msg);

    if (m == 10) {
      int sel = acmenu_getselectedindex(menu);
      if (sel >= 0 && sel < line_cnt) {
        const char * acts[] = { "Edit Line", "Insert Line Below", "Delete Line" };
        const char * desc[] = { "Modify this line with keyboard", "Insert a new line below", "Remove this line" };
        char ltitle[48];
        snprintf(ltitle, sizeof(ltitle), "Line %d", sel + 1);
        int pick = afm_action_sheet(hWin, ltitle, acts, desc, 3);
        if (pick == 0) {
          char edited[512];
          if (afm_input_dialog(hWin, ltitle, lines[sel], edited, sizeof(edited))) {
            free(lines[sel]);
            lines[sel] = strdup(edited);
            dirty = 1;
          }
        }
        else if (pick == 1 && line_cnt < AFM_MAX_LINES) {
          char added[512];
          if (afm_input_dialog(hWin, "Insert Line", "", added, sizeof(added))) {
            int j;
            for (j = line_cnt; j > sel + 1; j--) {
              lines[j] = lines[j - 1];
            }
            lines[sel + 1] = strdup(added);
            line_cnt++;
            dirty = 1;
          }
        }
        else if (pick == 2) {
          free(lines[sel]);
          int j;
          for (j = sel; j < line_cnt - 1; j++) {
            lines[j] = lines[j + 1];
          }
          line_cnt--;
          if (line_cnt == 0) {
            lines[0] = strdup("");
            line_cnt = 1;
          }
          dirty = 1;
        }
      }
    }
    else if (m == 11 && line_cnt < AFM_MAX_LINES) {
      char added[512];
      if (afm_input_dialog(hWin, "Add New Line", "", added, sizeof(added))) {
        lines[line_cnt++] = strdup(added);
        dirty = 1;
      }
    }
    else if (m == 12) {
      if (afm_save_lines(filepath, lines, line_cnt, orig_mode)) {
        dirty = 0;
        aw_alert(hWin, "Saved", "File saved successfully.", "@info", NULL);
      }
      else {
        char err[256];
        snprintf(err, sizeof(err),
                 "Failed to save file:\n%s\n\nError: %s\n(Check if partition is RW)",
                 filepath, strerror(errno));
        aw_alert(hWin, "Save Failed", err, "@alert", NULL);
      }
    }
    else if (m == 5 || m == 4) {
      if (dirty) {
        if (aw_confirm(hWin, "Unsaved Changes",
                       "Save changes before closing?", "@alert", "Save", "Discard")) {
          afm_save_lines(filepath, lines, line_cnt, orig_mode);
        }
      }
      running = 0;
    }

    aw_destroy(hWin);
    ag_ccanvas(&bg);
  }

  int i;
  for (i = 0; i < line_cnt; i++) {
    free(lines[i]);
  }
  free(lines);
  if (parent != NULL) {
    aw_draw(parent);
  }
}

/**************************[ MODULE MANAGER CORE ]*****************************/

static void afm_parse_module_prop(AFM_MODULE * mod) {
  char ppath[AFM_PATH_MAX];
  afm_join_path(ppath, sizeof(ppath), mod->path, "module.prop");
  snprintf(mod->name, sizeof(mod->name), "%s", mod->id);
  mod->version[0] = '\0';
  mod->author[0]  = '\0';
  mod->desc[0]    = '\0';

  FILE * fp = fopen(ppath, "r");
  if (!fp) return;
  char line[512];
  while (fgets(line, sizeof(line), fp) != NULL) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }
    if (strncmp(line, "name=", 5) == 0 && line[5]) {
      snprintf(mod->name, sizeof(mod->name), "%s", line + 5);
    }
    else if (strncmp(line, "version=", 8) == 0) {
      snprintf(mod->version, sizeof(mod->version), "%s", line + 8);
    }
    else if (strncmp(line, "author=", 7) == 0) {
      snprintf(mod->author, sizeof(mod->author), "%s", line + 7);
    }
    else if (strncmp(line, "description=", 12) == 0) {
      snprintf(mod->desc, sizeof(mod->desc), "%s", line + 12);
    }
  }
  fclose(fp);
}

static int afm_cmp_modules(const void * a, const void * b) {
  const AFM_MODULE * ma = (const AFM_MODULE *) a;
  const AFM_MODULE * mb = (const AFM_MODULE *) b;
  return strcasecmp(ma->name, mb->name);
}

static int afm_scan_modules(AFM_MODULE * mods, int max_mods) {
  const char * root = "/data/adb/modules";
  DIR * d = opendir(root);
  if (!d) return 0;

  int cnt = 0;
  struct dirent * de;
  while ((de = readdir(d)) != NULL && cnt < max_mods) {
    if (de->d_name[0] == '.') continue;
    char mpath[AFM_PATH_MAX];
    afm_join_path(mpath, sizeof(mpath), root, de->d_name);
    if (!afm_dir_exists(mpath)) continue;

    AFM_MODULE * m = &mods[cnt];
    memset(m, 0, sizeof(*m));
    snprintf(m->id, sizeof(m->id), "%s", de->d_name);
    snprintf(m->path, sizeof(m->path), "%s", mpath);
    afm_parse_module_prop(m);

    char flagp[AFM_PATH_MAX];
    afm_join_path(flagp, sizeof(flagp), mpath, "disable");
    m->disabled = afm_path_exists(flagp);
    afm_join_path(flagp, sizeof(flagp), mpath, "remove");
    m->remove_flag = afm_path_exists(flagp);
    afm_join_path(flagp, sizeof(flagp), mpath, "update");
    m->update_flag = afm_path_exists(flagp);
    cnt++;
  }
  closedir(d);

  if (cnt > 1) {
    qsort(mods, (size_t) cnt, sizeof(AFM_MODULE), afm_cmp_modules);
  }
  return cnt;
}

static void afm_touch_file(const char * p) {
  int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) close(fd);
}

/**************************[ FILE BROWSER CORE ]*******************************/

static int afm_cmp_entries(const void * a, const void * b) {
  const AFM_ENTRY * ea = (const AFM_ENTRY *) a;
  const AFM_ENTRY * eb = (const AFM_ENTRY *) b;
  if (ea->is_parent != eb->is_parent) return ea->is_parent ? -1 : 1;
  if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
  return strcasecmp(ea->name, eb->name);
}

static int afm_scan_dir(const char * dirpath, AFM_ENTRY * entries, int max_entries) {
  int cnt = 0;
  if (strcmp(dirpath, "/") != 0 && cnt < max_entries) {
    AFM_ENTRY * pe = &entries[cnt++];
    memset(pe, 0, sizeof(*pe));
    snprintf(pe->name, sizeof(pe->name), "..");
    afm_parent_path(dirpath, pe->fullpath, sizeof(pe->fullpath));
    pe->is_dir    = 1;
    pe->is_parent = 1;
  }

  DIR * d = opendir(dirpath);
  if (!d) return cnt;

  struct dirent * de;
  while ((de = readdir(d)) != NULL && cnt < max_entries) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
      continue;
    }
    if (!afm_show_hidden && de->d_name[0] == '.') {
      continue;
    }
    AFM_ENTRY * e = &entries[cnt];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", de->d_name);
    afm_join_path(e->fullpath, sizeof(e->fullpath), dirpath, de->d_name);

    struct stat lst, st;
    if (lstat(e->fullpath, &lst) == 0) {
      e->mode    = lst.st_mode;
      e->size    = lst.st_size;
      e->is_link = S_ISLNK(lst.st_mode) ? 1 : 0;
      if (e->is_link && stat(e->fullpath, &st) == 0) {
        e->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
      }
      else {
        e->is_dir = S_ISDIR(lst.st_mode) ? 1 : 0;
      }
    }
    cnt++;
  }
  closedir(d);

  if (cnt > 1) {
    qsort(entries, (size_t) cnt, sizeof(AFM_ENTRY), afm_cmp_entries);
  }
  return cnt;
}

static void afm_chmod_dialog(AWINDOWP parent, const AFM_ENTRY * e) {
  const char * items[] = {
    "0644 (rw-r--r--) — Standard File",
    "0755 (rwxr-xr-x) — Executable / Folder",
    "0777 (rwxrwxrwx) — Full Access",
    "0600 (rw-------) — Private File",
    "Custom Octal Mode..."
  };
  const char * descs[] = {
    "Owner read/write, others read",
    "Owner full, others read/execute",
    "Read/write/execute for all",
    "Owner read/write only",
    "Enter custom octal (e.g. 0664)"
  };
  int pick = afm_action_sheet(parent, "Set Permissions (chmod)", items, descs, 5);
  mode_t mode = 0;
  if (pick == 0) mode = 0644;
  else if (pick == 1) mode = 0755;
  else if (pick == 2) mode = 0777;
  else if (pick == 3) mode = 0600;
  else if (pick == 4) {
    char oct[32];
    char cur[16];
    snprintf(cur, sizeof(cur), "%04o", (unsigned)(e->mode & 07777));
    if (!afm_input_dialog(parent, "Octal Mode (e.g. 0644)", cur, oct, sizeof(oct))) {
      return;
    }
    char * endp = NULL;
    unsigned long v = strtoul(oct, &endp, 8);
    if (endp == oct || *endp != '\0' || v > 07777) {
      aw_alert(parent, "Invalid Mode", "Please enter a valid octal permission (0000 - 0777).", "@alert", NULL);
      return;
    }
    mode = (mode_t) v;
  }
  else {
    return;
  }

  if (chmod(e->fullpath, mode) == 0) {
    char okmsg[192];
    snprintf(okmsg, sizeof(okmsg), "Permissions for %s updated to %04o.", e->name, (unsigned) mode);
    aw_alert(parent, "Permissions Updated", okmsg, "@info", NULL);
  }
  else {
    char err[256];
    snprintf(err, sizeof(err), "chmod failed for %s:\n%s", e->name, strerror(errno));
    aw_alert(parent, "chmod Failed", err, "@alert", NULL);
  }
}

static void afm_show_properties(AWINDOWP parent, const AFM_ENTRY * e) {
  struct stat lst;
  if (lstat(e->fullpath, &lst) != 0) {
    aw_alert(parent, "Properties", "Cannot stat item.", "@alert", NULL);
    return;
  }
  char szbuf[64], modebuf[64], mp[256], fs[64];
  afm_format_size(lst.st_size, szbuf, sizeof(szbuf));
  afm_format_mode(lst.st_mode, modebuf, sizeof(modebuf));
  byte rw = afm_mount_info(e->fullpath, mp, sizeof(mp), fs, sizeof(fs));

  char linkinfo[320] = "";
  if (S_ISLNK(lst.st_mode)) {
    char tgt[256];
    ssize_t n = readlink(e->fullpath, tgt, sizeof(tgt) - 1);
    if (n >= 0) {
      tgt[n] = '\0';
      snprintf(linkinfo, sizeof(linkinfo), "\n<b>Target:</b> %s", tgt);
    }
  }

  char info[1024];
  snprintf(info, sizeof(info),
           "<b>Path:</b> %s\n"
           "<b>Type:</b> %s%s\n"
           "<b>Size:</b> %s (%lld bytes)\n"
           "<b>Mode:</b> %s\n"
           "<b>Owner:</b> UID %u : GID %u\n"
           "<b>Mount:</b> %s (%s, %s)",
           e->fullpath,
           e->is_link ? "Symlink" : (e->is_dir ? "Directory" : "File"),
           linkinfo,
           szbuf, (long long) lst.st_size,
           modebuf,
           (unsigned) lst.st_uid, (unsigned) lst.st_gid,
           mp, fs, rw ? "RW" : "RO");
  aw_alert(parent, "Item Properties", info, "@info", NULL);
}

/* Item action sheet for a file or folder. Returns 1 if directory should reload. */
static byte afm_handle_item_actions(AWINDOWP parent, const char * cur_dir,
                                    const AFM_ENTRY * e, char * nav_target, size_t nav_sz) {
  const char * items[8];
  const char * descs[8];
  int codes[8];
  int n = 0;

  if (e->is_dir) {
    items[n] = "Open Folder";
    descs[n] = "Browse contents of this folder";
    codes[n++] = 1;
  }
  else {
    items[n] = "View / Edit Text";
    descs[n] = "Open in built-in line viewer & editor";
    codes[n++] = 2;
  }

  items[n] = "Copy";
  descs[n] = "Copy to clipboard (use Tools -> Paste Here)";
  codes[n++] = 3;

  items[n] = "Cut (Move)";
  descs[n] = "Cut to clipboard (use Tools -> Paste Here)";
  codes[n++] = 4;

  items[n] = "Rename";
  descs[n] = "Change name using touch keyboard";
  codes[n++] = 5;

  items[n] = "Permissions (chmod)";
  descs[n] = "Change read/write/execute permissions";
  codes[n++] = 6;

  items[n] = "Delete";
  descs[n] = e->is_dir ? "Recursively delete folder and contents" : "Delete this file permanently";
  codes[n++] = 7;

  items[n] = "Properties";
  descs[n] = "Path, size, permissions, mount status";
  codes[n++] = 8;

  int pick = afm_action_sheet(parent, e->name, items, descs, n);
  if (pick < 0 || pick >= n) return 0;

  switch (codes[pick]) {
    case 1:
      if (nav_target && nav_sz > 0) {
        snprintf(nav_target, nav_sz, "%s", e->fullpath);
      }
      return 2;
    case 2:
      afm_text_editor(parent, e->fullpath);
      return 1;
    case 3:
      afm_clip_mode = AFM_CLIP_COPY;
      snprintf(afm_clip_path, sizeof(afm_clip_path), "%s", e->fullpath);
      snprintf(afm_clip_name, sizeof(afm_clip_name), "%s", e->name);
      return 1;
    case 4:
      afm_clip_mode = AFM_CLIP_CUT;
      snprintf(afm_clip_path, sizeof(afm_clip_path), "%s", e->fullpath);
      snprintf(afm_clip_name, sizeof(afm_clip_name), "%s", e->name);
      return 1;
    case 5: {
      char newname[256];
      if (afm_input_dialog(parent, "Rename Item", e->name, newname, sizeof(newname))) {
        if (newname[0] != '\0' && strchr(newname, '/') == NULL) {
          char dst[AFM_PATH_MAX];
          afm_join_path(dst, sizeof(dst), cur_dir, newname);
          if (rename(e->fullpath, dst) != 0) {
            char err[256];
            snprintf(err, sizeof(err), "Rename failed:\n%s", strerror(errno));
            aw_alert(parent, "Rename Error", err, "@alert", NULL);
          }
          return 1;
        }
      }
      return 0;
    }
    case 6:
      afm_chmod_dialog(parent, e);
      return 1;
    case 7: {
      char q[320];
      snprintf(q, sizeof(q), "Permanently delete:\n<b>%s</b>\n\nThis cannot be undone!", e->fullpath);
      if (aw_confirm(parent, "Confirm Delete", q, "@alert", "Delete", "Cancel")) {
        if (!afm_delete_recursive(e->fullpath)) {
          char err[256];
          snprintf(err, sizeof(err), "Delete failed:\n%s", strerror(errno));
          aw_alert(parent, "Delete Error", err, "@alert", NULL);
        }
        else if (afm_clip_mode != AFM_CLIP_NONE && strcmp(afm_clip_path, e->fullpath) == 0) {
          afm_clip_mode = AFM_CLIP_NONE;
          afm_clip_path[0] = '\0';
        }
        return 1;
      }
      return 0;
    }
    case 8:
      afm_show_properties(parent, e);
      return 0;
  }
  return 0;
}

static void afm_system_partition_picker(AWINDOWP parent, char * out_path, size_t out_sz, byte * out_picked) {
  static const char * candidates[] = {
    "/", "/system", "/system_root", "/vendor", "/product",
    "/system_ext", "/odm", "/data", "/data/adb", "/cache", "/tmp", NULL
  };
  const char * items[12];
  const char * descs[12];
  char desc_bufs[12][128];
  int cnt = 0;
  int i;

  for (i = 0; candidates[i] != NULL && cnt < 12; i++) {
    if (!afm_dir_exists(candidates[i])) continue;
    char mp[256], fs[64];
    byte rw = afm_mount_info(candidates[i], mp, sizeof(mp), fs, sizeof(fs));
    items[cnt] = candidates[i];
    snprintf(desc_bufs[cnt], sizeof(desc_bufs[cnt]), "[%s] %s (mount: %s)",
             rw ? "RW" : "RO", fs, mp);
    descs[cnt] = desc_bufs[cnt];
    cnt++;
  }

  int pick = afm_action_sheet(parent, "System & Root Partitions", items, descs, cnt);
  if (pick >= 0 && pick < cnt) {
    snprintf(out_path, out_sz, "%s", items[pick]);
    *out_picked = 1;
  }
}

/**************************[ MAIN FILE MANAGER LOOP ]**************************/

byte afm_run(AWINDOWP parent, const char * start_path) {
  if (afm_active) {
    return 0;
  }
  afm_active = 1;
  aui_sync_theme_and_font();

  byte view_mode = AFM_VIEW_MODULES;
  char cur_path[AFM_PATH_MAX];
  afm_detect_storage_path(cur_path, sizeof(cur_path));

  if (start_path != NULL && start_path[0] != '\0') {
    if (strcmp(start_path, "modules") == 0) {
      view_mode = AFM_VIEW_MODULES;
    }
    else if (afm_dir_exists(start_path)) {
      view_mode = AFM_VIEW_FILES;
      snprintf(cur_path, sizeof(cur_path), "%s", start_path);
    }
  }
  else if (!afm_dir_exists("/data/adb/modules")) {
    view_mode = AFM_VIEW_FILES;
  }

  AFM_ENTRY  * entries = (AFM_ENTRY *) calloc(AFM_MAX_ENTRIES, sizeof(AFM_ENTRY));
  AFM_MODULE * mods    = (AFM_MODULE *) calloc(256, sizeof(AFM_MODULE));
  if (!entries || !mods) {
    free(entries);
    free(mods);
    afm_active = 0;
    return 0;
  }

  byte running = 1;
  byte first_draw = 1;

  while (running) {
    aui_sync_theme_and_font();

    int pad   = agdp() * 2;
    int capH  = ag_fontheight(1) + (pad * 2);
    int barH  = agdp() * 19;
    int indH  = agdp() * 3;
    int subH  = ag_fontheight(0) + (pad * 2);
    int navH  = agdp() * 24;
    int tbY   = capH + (pad / 2);
    int indY  = tbY + barH + (agdp() > 1 ? agdp() : 1);
    int subY  = indY + indH + (pad / 2);
    int listY = subY + subH;
    int listH = agh() - listY - navH;

    int active_tab = 2; /* 0 = Modules, 1 = Storage, 2 = System */
    if (view_mode == AFM_VIEW_MODULES) {
      active_tab = 0;
    }
    else if (strncmp(cur_path, "/sdcard", 7) == 0 ||
             strncmp(cur_path, "/storage", 8) == 0 ||
             strncmp(cur_path, "/data/media", 11) == 0) {
      active_tab = 1;
    }

    CANVAS bg;
    ag_canvas(&bg, agw(), agh());
    ag_rect(&bg, 0, 0, agw(), agh(), acfg()->winbg);
    if (!atheme_id_draw(0, &bg, 0, 0, agw(), agh())) {
      ag_roundgrad(&bg, 0, 0, agw(), agh(), acfg()->winbg, acfg()->winbg_g, 0);
    }
    if (!atheme_id_draw(1, &bg, 0, 0, agw(), capH)) {
      ag_roundgrad_ex(&bg, 0, 0, agw(), capH, acfg()->titlebg, acfg()->titlebg_g, 0, 1, 1, 0, 0);
    }
    if (!atheme_id_draw(2, &bg, 0, agh() - navH, agw(), navH)) {
      ag_roundgrad_ex(&bg, 0, agh() - navH, agw(), navH, acfg()->navbg, acfg()->navbg_g, 0, 0, 0, 1, 1);
    }

    char title[128];
    char sub[320];
    char mp[256], fs[64];

    if (view_mode == AFM_VIEW_MODULES) {
      byte rw = afm_mount_info("/data/adb/modules", mp, sizeof(mp), fs, sizeof(fs));
      snprintf(title, sizeof(title), "AROMA-Next-Filemanager");
      if (afm_dir_exists("/data/adb/modules")) {
        snprintf(sub, sizeof(sub), "Modules • /data/adb/modules [%s]", rw ? "RW" : "RO");
      }
      else {
        snprintf(sub, sizeof(sub), "Modules • /data/adb/modules not found (or /data encrypted)");
      }
    }
    else {
      byte rw = afm_mount_info(cur_path, mp, sizeof(mp), fs, sizeof(fs));
      snprintf(title, sizeof(title), "AROMA-Next-Filemanager");
      if (afm_clip_mode != AFM_CLIP_NONE) {
        snprintf(sub, sizeof(sub), "%s [%s] • %s: %s",
                 cur_path, rw ? "RW" : "RO",
                 (afm_clip_mode == AFM_CLIP_COPY) ? "COPY" : "CUT",
                 afm_clip_name);
      }
      else {
        snprintf(sub, sizeof(sub), "%s [%s • %s]", cur_path, rw ? "RW" : "RO", fs);
      }
    }

    int titW = ag_txtwidth(title, 1);
    ag_textf(&bg, titW, (agw() - titW) / 2 + 1, pad + 1, title, acfg()->titlebg_g, 1);
    ag_text(&bg, titW, (agw() - titW) / 2, pad, title, acfg()->titlefg, 1);

    /* Active tab underline indicator (clearly visible for Modules / Storage / System) */
    int tbW = (agw() - (pad * 5)) / 4;
    int actX = pad + active_tab * (tbW + pad);
    color ind_col = (acfg()->selectbg != acfg()->winbg) ? acfg()->selectbg : acfg()->titlefg;
    color ind_col_g = (acfg()->selectbg_g != acfg()->winbg) ? acfg()->selectbg_g : ind_col;
    ag_roundgrad(&bg, actX, indY, tbW, indH, ind_col, ind_col_g, agdp());

    /* Status / path bar */
    ag_rect(&bg, 0, subY, agw(), subH, acfg()->controlbg);
    ag_text(&bg, agw() - (pad * 2), pad, subY + pad, sub, acfg()->controlfg, 0);

    AWINDOWP hWin = aw(&bg);

    /* Quick-Access Top Bar: Modules | Storage | System | Tools */
    ACONTROLP btn_mod = acbutton(hWin, pad,               tbY, tbW, barH, (active_tab == 0) ? "<b>Modules</b>" : "Modules", 0, 21);
    ACONTROLP btn_sto = acbutton(hWin, pad * 2 + tbW,     tbY, tbW, barH, (active_tab == 1) ? "<b>Storage</b>" : "Storage", 0, 22);
    ACONTROLP btn_sys = acbutton(hWin, pad * 3 + tbW * 2, tbY, tbW, barH, (active_tab == 2) ? "<b>System</b>"  : "System",  0, 23);
    acbutton(hWin, pad * 4 + tbW * 3, tbY, tbW, barH, "Tools", 0, 24);
    ACONTROLP active_tab_btn = (active_tab == 0) ? btn_mod : (active_tab == 1) ? btn_sto : btn_sys;

    ACONTROLP menu = acmenu(hWin, 0, listY, agw(), listH, 10);
    int mod_cnt = 0;
    int ent_cnt = 0;
    int i;

    if (view_mode == AFM_VIEW_MODULES) {
      mod_cnt = afm_scan_modules(mods, 256);
      if (mod_cnt == 0) {
        acmenu_add(menu, "No Root Modules Found",
                   "Tap Storage or System above to browse files, or decrypt /data first.", "");
      }
      else {
        for (i = 0; i < mod_cnt; i++) {
          const char * status = "ACTIVE";
          if (mods[i].remove_flag) status = "REMOVE PENDING";
          else if (mods[i].disabled) status = "DISABLED";
          else if (mods[i].update_flag) status = "UPDATE PENDING";

          char mtitle[256];
          char mdesc[320];
          if (mods[i].version[0]) {
            snprintf(mtitle, sizeof(mtitle), "[%s] %s (%s)", status, mods[i].name, mods[i].version);
          }
          else {
            snprintf(mtitle, sizeof(mtitle), "[%s] %s", status, mods[i].name);
          }
          snprintf(mdesc, sizeof(mdesc), "ID: %s%s%s",
                   mods[i].id,
                   mods[i].author[0] ? " • by " : "",
                   mods[i].author);
          const char * micon = (mods[i].remove_flag || mods[i].disabled) ? "@alert" : "@apps";
          acmenu_add(menu, mtitle, mdesc, (char *) micon);
        }
      }
    }
    else {
      ent_cnt = afm_scan_dir(cur_path, entries, AFM_MAX_ENTRIES);
      if (ent_cnt == 0) {
        acmenu_add(menu, "(Empty or Unreadable Directory)", cur_path, "");
      }
      else {
        for (i = 0; i < ent_cnt; i++) {
          char etitle[300];
          char edesc[256];
          if (entries[i].is_parent) {
            snprintf(etitle, sizeof(etitle), "..");
            snprintf(edesc, sizeof(edesc), "Parent Directory");
          }
          else if (entries[i].is_dir) {
            char mbuf[32];
            afm_format_mode(entries[i].mode, mbuf, sizeof(mbuf));
            snprintf(etitle, sizeof(etitle), "[DIR] %s", entries[i].name);
            snprintf(edesc, sizeof(edesc), "%s%s", entries[i].is_link ? "Symlink Dir • " : "Folder • ", mbuf);
          }
          else {
            char szbuf[32], mbuf[32];
            afm_format_size(entries[i].size, szbuf, sizeof(szbuf));
            afm_format_mode(entries[i].mode, mbuf, sizeof(mbuf));
            snprintf(etitle, sizeof(etitle), "%s", entries[i].name);
            snprintf(edesc, sizeof(edesc), "%s%s • %s",
                     entries[i].is_link ? "Symlink • " : "", szbuf, mbuf);
          }
          acmenu_add(menu, etitle, edesc, entries[i].is_parent ? "@back" : "");
        }
      }
    }

    /* Bottom Navigation Bar: 1:1 matched to AROMA-Next-Installer (Back/Up | Vertical 3-Dot Menu | Exit) */
    int nPad    = agdp() * 2;
    int nHeight = navH;
    int nWidth  = (agw() - (nPad * 2) - nHeight) / 2;
    int nY      = agh() - navH;
    imgbtn(hWin, nPad, nY, nWidth, nHeight, aui_back_icon(),
           (view_mode == AFM_VIEW_FILES && strcmp(cur_path, "/") != 0) ? "Up" : acfg()->text_back, 4, 5);
    imgbtn(hWin, nPad + nWidth, nY, nHeight, nHeight, aui_menu_icon(), NULL, 4, 200);
    imgbtn(hWin, nPad + nWidth + nHeight, nY, nWidth, nHeight, aui_next_icon(), "Exit", 5, 4);

    aw_show_ex(hWin, first_draw ? 5 : 4, 0, active_tab_btn);
    first_draw = 0;

    dword msg = aw_dispatch(hWin);
    byte m = aw_gm(msg);

    if (m == 21) {
      view_mode = AFM_VIEW_MODULES;
    }
    else if (m == 22) {
      view_mode = AFM_VIEW_FILES;
      afm_detect_storage_path(cur_path, sizeof(cur_path));
    }
    else if (m == 23) {
      byte picked = 0;
      char chosen[AFM_PATH_MAX];
      afm_system_partition_picker(hWin, chosen, sizeof(chosen), &picked);
      if (picked) {
        view_mode = AFM_VIEW_FILES;
        snprintf(cur_path, sizeof(cur_path), "%s", chosen);
      }
    }
    else if (m == 24) {
      /* Context Tools Sheet */
      const char * titems[9];
      const char * tdescs[9];
      int tcodes[9];
      int tn = 0;

      if (view_mode == AFM_VIEW_FILES && afm_clip_mode != AFM_CLIP_NONE) {
        titems[tn] = (afm_clip_mode == AFM_CLIP_COPY) ? "Paste Here (Copy)" : "Paste Here (Move)";
        tdescs[tn] = afm_clip_path;
        tcodes[tn++] = 1;
      }
      if (view_mode == AFM_VIEW_FILES) {
        titems[tn] = "New Folder";
        tdescs[tn] = "Create a new directory here";
        tcodes[tn++] = 2;

        titems[tn] = "New Text File";
        tdescs[tn] = "Create and open a new file in text editor";
        tcodes[tn++] = 3;

        if (ent_cnt > 0) {
          titems[tn] = "Folder / Item Actions...";
          tdescs[tn] = "Copy, Move, Rename, Chmod or Delete any folder/file here";
          tcodes[tn++] = 4;
        }
      }
      titems[tn] = afm_show_hidden ? "Hide Hidden Files (.dotfiles)" : "Show Hidden Files (.dotfiles)";
      tdescs[tn] = "Toggle visibility of hidden entries";
      tcodes[tn++] = 5;

      titems[tn] = "Remount Partition RW";
      tdescs[tn] = "Try remounting current mountpoint read-write (if supported)";
      tcodes[tn++] = 6;

      titems[tn] = "Go to Path...";
      tdescs[tn] = "Enter any custom directory path";
      tcodes[tn++] = 7;

      titems[tn] = "Refresh";
      tdescs[tn] = "Rescan current view";
      tcodes[tn++] = 8;

      int tpick = afm_action_sheet(hWin, "Filemanager Tools", titems, tdescs, tn);
      if (tpick >= 0 && tpick < tn) {
        switch (tcodes[tpick]) {
          case 1: {
            char dst[AFM_PATH_MAX];
            afm_join_path(dst, sizeof(dst), cur_path, afm_clip_name);
            byte ok = (afm_clip_mode == AFM_CLIP_COPY)
                      ? afm_copy_recursive(afm_clip_path, dst)
                      : afm_move_item(afm_clip_path, dst);
            if (!ok) {
              char err[256];
              snprintf(err, sizeof(err), "Paste failed:\n%s\n(%s)", dst, strerror(errno));
              aw_alert(hWin, "Paste Failed", err, "@alert", NULL);
            }
            else if (afm_clip_mode == AFM_CLIP_CUT) {
              afm_clip_mode = AFM_CLIP_NONE;
              afm_clip_path[0] = '\0';
            }
          } break;
          case 2: {
            char dname[256];
            if (afm_input_dialog(hWin, "New Folder Name", "", dname, sizeof(dname))) {
              if (dname[0] != '\0' && strchr(dname, '/') == NULL) {
                char dst[AFM_PATH_MAX];
                afm_join_path(dst, sizeof(dst), cur_path, dname);
                if (mkdir(dst, 0755) != 0) {
                  char err[256];
                  snprintf(err, sizeof(err), "Cannot create folder:\n%s", strerror(errno));
                  aw_alert(hWin, "Error", err, "@alert", NULL);
                }
              }
            }
          } break;
          case 3: {
            char fname[256];
            if (afm_input_dialog(hWin, "New File Name", "", fname, sizeof(fname))) {
              if (fname[0] != '\0' && strchr(fname, '/') == NULL) {
                char dst[AFM_PATH_MAX];
                afm_join_path(dst, sizeof(dst), cur_path, fname);
                afm_text_editor(hWin, dst);
              }
            }
          } break;
          case 4: {
            const char * nlist[128];
            const char * dlist[128];
            int mapidx[128];
            int ncnt = 0;
            for (i = 0; i < ent_cnt && ncnt < 128; i++) {
              if (entries[i].is_parent) continue;
              nlist[ncnt] = entries[i].name;
              dlist[ncnt] = entries[i].is_dir ? "Folder" : "File";
              mapidx[ncnt] = i;
              ncnt++;
            }
            int ipick = afm_action_sheet(hWin, "Select Item for Action", nlist, dlist, ncnt);
            if (ipick >= 0 && ipick < ncnt) {
              char nav[AFM_PATH_MAX] = "";
              if (afm_handle_item_actions(hWin, cur_path, &entries[mapidx[ipick]], nav, sizeof(nav)) == 2) {
                snprintf(cur_path, sizeof(cur_path), "%s", nav);
              }
            }
          } break;
          case 5:
            afm_show_hidden = afm_show_hidden ? 0 : 1;
            break;
          case 6: {
            const char * target = (view_mode == AFM_VIEW_MODULES) ? "/data/adb/modules" : cur_path;
            char rmsg[384];
            byte rok = afm_try_remount_rw(target, rmsg, sizeof(rmsg));
            aw_alert(hWin, rok ? "Remount RW" : "Read-Only Partition", rmsg, rok ? "@info" : "@alert", NULL);
          } break;
          case 7: {
            char target[AFM_PATH_MAX];
            if (afm_input_dialog(hWin, "Go to Directory Path", cur_path, target, sizeof(target))) {
              if (afm_dir_exists(target)) {
                view_mode = AFM_VIEW_FILES;
                snprintf(cur_path, sizeof(cur_path), "%s", target);
              }
              else {
                aw_alert(hWin, "Path Not Found", "Directory does not exist or is not accessible.", "@alert", NULL);
              }
            }
          } break;
          case 8:
            break;
        }
      }
    }
    else if (m == 10) {
      int sel = acmenu_getselectedindex(menu);
      if (view_mode == AFM_VIEW_MODULES) {
        if (sel >= 0 && sel < mod_cnt) {
          AFM_MODULE * mod = &mods[sel];
          const char * mitems[6];
          const char * mdescs[6];
          mitems[0] = mod->disabled ? "Enable Module" : "Disable Module";
          mdescs[0] = mod->disabled ? "Remove disable flag (active next boot)" : "Create disable flag (keeps files safe)";
          mitems[1] = mod->remove_flag ? "Cancel Removal" : "Mark for Removal on Reboot";
          mdescs[1] = mod->remove_flag ? "Remove pending uninstall flag" : "Magisk/KSU will cleanly uninstall on next boot";
          mitems[2] = "Browse Module Files";
          mdescs[2] = mod->path;
          mitems[3] = "Edit module.prop";
          mdescs[3] = "View or edit module metadata";
          mitems[4] = "Edit system.prop";
          mdescs[4] = "View, create, or edit module system.prop overrides";
          mitems[5] = "Delete Module Now";
          mdescs[5] = "Immediately delete entire module directory";

          int mpick = afm_action_sheet(hWin, mod->name, mitems, mdescs, 6);
          char fpath[AFM_PATH_MAX];
          if (mpick == 0) {
            afm_join_path(fpath, sizeof(fpath), mod->path, "disable");
            if (mod->disabled) unlink(fpath);
            else afm_touch_file(fpath);
          }
          else if (mpick == 1) {
            afm_join_path(fpath, sizeof(fpath), mod->path, "remove");
            if (mod->remove_flag) unlink(fpath);
            else afm_touch_file(fpath);
          }
          else if (mpick == 2) {
            view_mode = AFM_VIEW_FILES;
            snprintf(cur_path, sizeof(cur_path), "%s", mod->path);
          }
          else if (mpick == 3) {
            afm_join_path(fpath, sizeof(fpath), mod->path, "module.prop");
            afm_text_editor(hWin, fpath);
          }
          else if (mpick == 4) {
            afm_join_path(fpath, sizeof(fpath), mod->path, "system.prop");
            afm_text_editor(hWin, fpath);
          }
          else if (mpick == 5) {
            char q[320];
            snprintf(q, sizeof(q), "Immediately delete module:\n<b>%s</b>\n(%s)", mod->name, mod->path);
            if (aw_confirm(hWin, "Delete Module", q, "@alert", "Delete", "Cancel")) {
              if (!afm_delete_recursive(mod->path)) {
                char err[256];
                snprintf(err, sizeof(err), "Failed to delete module:\n%s", strerror(errno));
                aw_alert(hWin, "Delete Error", err, "@alert", NULL);
              }
            }
          }
        }
      }
      else {
        if (sel >= 0 && sel < ent_cnt) {
          AFM_ENTRY * e = &entries[sel];
          if (e->is_dir) {
            snprintf(cur_path, sizeof(cur_path), "%s", e->fullpath);
          }
          else {
            char nav[AFM_PATH_MAX] = "";
            if (afm_handle_item_actions(hWin, cur_path, e, nav, sizeof(nav)) == 2) {
              snprintf(cur_path, sizeof(cur_path), "%s", nav);
            }
          }
        }
      }
    }
    else if (m == 5) {
      if (view_mode == AFM_VIEW_FILES && strcmp(cur_path, "/") != 0) {
        char up[AFM_PATH_MAX];
        afm_parent_path(cur_path, up, sizeof(up));
        snprintf(cur_path, sizeof(cur_path), "%s", up);
      }
      else {
        running = 0;
      }
    }
    else if (m == 4) {
      running = 0;
    }
    else if (m == 7) {
      /* Live Theme / Font change triggered from 3-dot menu -> loop redraws cleanly! */
      aui_sync_theme_and_font();
    }

    aw_destroy(hWin);
    ag_ccanvas(&bg);
  }

  free(entries);
  free(mods);
  afm_active = 0;
  if (parent != NULL) {
    aw_draw(parent);
  }
  return 1;
}
