/*
 * Copyright (C) 2011-2012 Ahmad Amarullah ( http://amarullz.com/ )
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
 * AROMA UI: Main AROMA UI Window
 *
 */
#include <sched.h>
#include <time.h>
#include "../aroma.h"

static long long aw_now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((long long) ts.tv_sec * 1000000LL) + ((long long) ts.tv_nsec / 1000LL);
}

static inline float aw_ease_out_cubic(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  float u = 1.0f - t;
  return 1.0f - (u * u * u);
}

static inline void aw_anim_pace(long long frame_start_us) {
  /* DRM Atomic KMS commits sync natively to panel VSYNC (60/90/120Hz+).
   * On fbdev (which may not VSYNC-block), cap pacing at ~120Hz. */
  if (ag_backend() != 2) {
    long long dt = aw_now_us() - frame_start_us;
    if (dt < 8000LL) {
      usleep((useconds_t)(8000LL - dt));
    }
  }
}

/***************************[ GLOBAL VARIABLES ]**************************/
static AC_CONFIG acfg_var;
static byte      on_dialog_window = 0;

/***************************[ CONFIG FUNCTIONS ]**************************/
AC_CONFIG * acfg() {
  return &acfg_var;
}
void acfg_reset_text() {
  snprintf(acfg_var.text_ok, 64, "OK");
  snprintf(acfg_var.text_next, 64, "Next");
  snprintf(acfg_var.text_back, 64, "Back");
  snprintf(acfg_var.text_yes, 64, "Yes");
  snprintf(acfg_var.text_no, 64, "No");
  snprintf(acfg_var.text_about, 64, "About");
  snprintf(acfg_var.text_calibrating, 64, "Calibrating Tools");
  snprintf(acfg_var.text_quit, 64, "Quit Installation");
  snprintf(acfg_var.text_quit_msg, 128, "Are you sure to quit the installer?");
  snprintf(acfg_var.icon_next, 128, "@next");
  snprintf(acfg_var.icon_back, 128, "@back");
}
void acfg_init_ex(byte themeonly) {
  acfg_var.winbg        = ag_rgb(0xf0, 0xf0, 0xf0);
  acfg_var.winbg_g      = ag_rgb(0xee, 0xee, 0xee);
  acfg_var.dialogbg     = acfg_var.winbg;
  acfg_var.dialogbg_g   = acfg_var.winbg_g;
  acfg_var.textbg       = ag_rgb(0xff, 0xff, 0xff);
  acfg_var.textfg       = ag_rgb(0x00, 0x00, 0x00);
  acfg_var.textfg_gray  = ag_rgb(0x88, 0x88, 0x88);
  acfg_var.winfg_gray   = acfg_var.textfg_gray;
  acfg_var.winfg        = acfg_var.textfg;
  acfg_var.dialogfg     = acfg_var.textfg;
  acfg_var.controlbg    = ag_rgb(0xf5, 0xf5, 0xf5);
  acfg_var.controlbg_g  = ag_rgb(0xdf, 0xdf, 0xdf);
  acfg_var.controlfg    = ag_rgb(0x44, 0x44, 0x44);
  acfg_var.selectbg     = ag_rgb(158, 228, 32);
  acfg_var.selectbg_g   = ag_rgb(106, 160, 44);
  acfg_var.selectfg     = ag_rgb(0xff, 0xff, 0xff);
  acfg_var.titlebg      = ag_rgb(0x22, 0x22, 0x22);
  acfg_var.titlebg_g    = ag_rgb(0x11, 0x11, 0x11);
  acfg_var.titlefg      = ag_rgb(0xff, 0xff, 0xff);
  acfg_var.dlgtitlebg   = acfg_var.titlebg;
  acfg_var.dlgtitlebg_g = acfg_var.titlebg_g;
  acfg_var.dlgtitlefg   = acfg_var.titlefg;
  acfg_var.navbg        = ag_rgb(0xf5, 0xf5, 0xf5);
  acfg_var.navbg_g      = ag_rgb(0xdf, 0xdf, 0xdf);
  acfg_var.scrollbar    = ag_rgb(0x66, 0x66, 0x66);
  acfg_var.border       = ag_rgb(0xcc, 0xcc, 0xcc);
  acfg_var.border_g     = ag_rgb(0x99, 0x99, 0x99);
  acfg_var.progressglow = acfg_var.selectbg;
  acfg_var.winroundsz   = 3;
  acfg_var.roundsz      = 2;
  acfg_var.btnroundsz   = 2;
  acfg_var.fadeframes   = 5;
  acfg_var.vibrate      = 5;
  set_vibrate_rate(acfg()->vibrate);
  snprintf(acfg_var.themename, 64, "");
  
  if (themeonly == 0) {
    acfg_reset_text();
    snprintf(acfg_var.rom_name, 128, AROMA_NAME);
    snprintf(acfg_var.rom_version, 128, AROMA_VERSION);
    snprintf(acfg_var.rom_author, 128, AROMA_BUILD_A);
    snprintf(acfg_var.rom_device, 128, "Not Defined");
    snprintf(acfg_var.rom_date, 128, AROMA_BUILD);
    acfg_var.ckey_up      = 0;
    acfg_var.ckey_down    = 0;
    acfg_var.ckey_select  = 0;
    acfg_var.ckey_back    = 0;
    acfg_var.ckey_menu    = 0;
  }
  
  atheme_releaseall();
}
void acfg_init() {
  acfg_init_ex(0);
}

/***************************[ THEME ]**************************/
static char theme_name[AROMA_THEME_CNT][27] = {
  "img.background",
  "img.titlebar",
  "img.navbar",
  "img.dialog",
  "img.dialog.titlebar",
  "img.progress",
  "img.prograss.fill",
  "img.selection",
  "img.selection.push",
  "img.button",
  "img.button.focus",
  "img.button.push",
  "img.checkbox",
  "img.checkbox.focus",
  "img.checkbox.push",
  "img.checkbox.on",
  "img.checkbox.on.focus",
  "img.checkbox.on.push",
  "img.radio",
  "img.radio.focus",
  "img.radio.push",
  "img.radio.on",
  "img.radio.on.focus",
  "img.radio.on.push"
};
void atheme_releaseall() {
  int i = 0;
  
  for (i = 0; i < AROMA_THEME_CNT; i++) {
    if (acfg_var.theme[i] != NULL) {
      apng_close(acfg_var.theme[i]);
      free(acfg_var.theme[i]);
    }
    
    acfg_var.theme[i]   = NULL;
    acfg_var.theme_9p[i] = 0;
  }
}
void atheme_release(char * key) {
  int i = 0;
  
  for (i = 0; i < AROMA_THEME_CNT; i++) {
    if (strcmp(theme_name[i], key) == 0) {
      if (acfg_var.theme[i] != NULL) {
        apng_close(acfg_var.theme[i]);
        free(acfg_var.theme[i]);
        acfg_var.theme[i] = NULL;
        acfg_var.theme_9p[i] = 0;
      }
      
      return;
    }
  }
  
  return;
}
PNGCANVASP atheme_create(char * key, char * path) {
  int id = atheme_id(key);
  
  if (id != -1) {
    PNGCANVAS * ap = malloc(sizeof(PNGCANVAS));
    
    if (apng_load(ap, path)) {
      if (acfg_var.theme[id] != NULL) {
        apng_close(acfg_var.theme[id]);
        free(acfg_var.theme[id]);
        acfg_var.theme[id] = NULL;
        acfg_var.theme_9p[id] = 0;
      }
      
      acfg_var.theme[id]  = ap;
      int ln = strlen(path) - 1;
      acfg_var.theme_9p[id] = 0;
      
      if (ln > 2) {
        if ((path[ln] == '9') && (path[ln - 1] == '.')) {
          acfg_var.theme_9p[id] = 1;
        }
      }
      
      return ap;
    }
    
    free(ap);
  }
  
  return NULL;
}
byte atheme_draw(char * key, CANVAS * _b, int x, int y, int w, int h) {
  return atheme_id_draw(atheme_id(key), _b, x, y, w, h);
}
byte atheme_id_draw(int id, CANVAS * _b, int x, int y, int w, int h) {
  if (id < 0) {
    return 0;
  }
  
  if (id >= AROMA_THEME_CNT) {
    return 0;
  }
  
  if (acfg_var.theme[id] != NULL) {
    if (acfg_var.theme_9p[id]) {
      return apng9_draw(_b, acfg_var.theme[id], x, y, w, h, NULL, 1);
    }
    else {
      return apng_stretch(
               _b,
               acfg_var.theme[id],
               x, y, w, h,
               0, 0, acfg_var.theme[id]->w, acfg_var.theme[id]->h);
    }
  }
  
  return 0;
}
PNGCANVASP atheme(char * key) {
  int i = 0;
  
  for (i = 0; i < AROMA_THEME_CNT; i++) {
    if (strcmp(theme_name[i], key) == 0) {
      return acfg_var.theme[i];
    }
  }
  
  return NULL;
}
int atheme_id(char * key) {
  int i = 0;
  
  for (i = 0; i < AROMA_THEME_CNT; i++) {
    if (strcmp(theme_name[i], key) == 0) {
      return i;
    }
  }
  
  return -1;
}
char * atheme_key(int id) {
  if (id < 0) {
    return NULL;
  }
  
  if (id >= AROMA_THEME_CNT) {
    return NULL;
  }
  
  return theme_name[id];
}


/***************************[ WINDOW FUNCTIONS ]**************************/
//-- CREATE WINDOW
AWINDOWP aw(CANVAS * bg) {
  ag_setbusy();
  //sleep(4);
  //-- Create Window
  AWINDOWP win = (AWINDOWP) malloc(sizeof(AWINDOW));
  
  if (win == NULL) {
    return NULL;
  }
  
  //-- Create Canvas & Draw BG
  ag_canvas(&win->c, agw(), agh());
  ag_draw(&win->c, bg, 0, 0);
  //-- Initializing Variables
  win->bg           = bg;
  win->controls     = NULL;
  win->controln     = 0;
  win->threadnum    = 0;
  win->focusIndex   = -1;
  win->touchIndex   = -1;
  win->isActived    = 0;
  //-- RETURN
  return win;
}
void aw_set_on_dialog(byte d) {
  on_dialog_window = d;
}
//-- DESTROY WINDOW
void aw_destroy(AWINDOWP win) {
  ag_setbusy();
  //-- Set To Unactive
  win->isActived = 0;
  //-- Wait Thread To Closed
  int threadwait_n = 0;
  
  while (win->threadnum > 0) {
    usleep(500);
    
    if (threadwait_n++ > 1000) {
      break;
    }
  }
  
  //-- Cleanup Controls
  if (win->controln > 0) {
    int i;
    ACONTROLP * controls = (ACONTROLP *) win->controls;
    
    for (i = win->controln - 1; i >= 0; i--) {
      controls[i]->ondestroy((void *) controls[i]);
      free(controls[i]);
    }
    
    free(win->controls);
  }
  
  //-- Cleanup Window
  ag_ccanvas(&win->c);
  free(win);
}

//-- Add Control Into Window
void aw_add(AWINDOWP win, ACONTROLP ctl) {
  if (win->controln > 0) {
    int i;
    void ** tmpctls   = win->controls;
    win->controls     = malloc( sizeof(ACONTROLP) * (win->controln + 1) );
    
    for (i = 0; i < win->controln; i++) {
      win->controls[i] = tmpctls[i];
    }
    
    win->controls[win->controln] = (void *) ctl;
    free(tmpctls);
  }
  else {
    win->controls    = malloc(sizeof(ACONTROLP));
    win->controls[0] = (void *) ctl;
  }
  
  win->controln++;
}

//-- Draw Window
void aw_draw(AWINDOWP win) {
  if (!win->isActived) {
    return;
  }
  
  ag_draw(NULL, &win->c, 0, 0);
  ag_sync();
}

//-- Redraw Window & Controls
void aw_redraw_ex(AWINDOWP win, byte syncnow) {
  if (!win->isActived) {
    return;
  }
  
  if (win->controln > 0) {
    int i;
    
    for (i = 0; i < win->controln; i++) {
      ACONTROLP ctl = (ACONTROLP) win->controls[i];
      
      if (ctl->ondraw != NULL) {
        ctl->ondraw(ctl);
      }
    }
  }
  
  if (syncnow) {
    ag_draw(NULL, &win->c, 0, 0);
  }
}

void aw_redraw(AWINDOWP win) {
  aw_redraw_ex(win, 1);
}

//-- Show Window
void aw_show_ex2(AWINDOWP win, byte anitype, int x, int pos, int w, int h, ACONTROLP firstFocus) {
  win->threadnum    = 0;
  win->isActived    = 1;
  
  //-- Find First Focus
  if (win->controln > 0) {
    int i;
    
    if (firstFocus != NULL) {
      for (i = 0; i < win->controln; i++) {
        ACONTROLP ctl = (ACONTROLP) win->controls[i];
        
        if (firstFocus == ctl) {
          if (ctl->onfocus != NULL) {
            if (ctl->onfocus(ctl)) {
              win->focusIndex = i;
              break;
            }
          }
        }
      }
    }
    
    if (win->focusIndex == -1) {
      for (i = 0; i < win->controln; i++) {
        ACONTROLP ctl = (ACONTROLP) win->controls[i];
        
        if (ctl->onfocus != NULL) {
          if (ctl->onfocus(ctl)) {
            win->focusIndex = i;
            break;
          }
        }
      }
    }
  }
  
  if (acfg()->fadeframes > 0 && strcmp(aui_get_user_transition(), "none") != 0) {
    if (anitype == 0) {
      //-- Smooth 60-120Hz VSYNC Crossfade
      aw_redraw_ex(win, 0);
      CANVAS cbg;
      CANVAS tmpb;
      ag_canvas(&cbg, agw(), agh());
      ag_canvas(&tmpb, agw(), agh());
      ag_draw(&cbg, agc(), 0, 0);
      long long t0 = aw_now_us();
      const long long dur_us = 170000LL;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float ease = aw_ease_out_cubic((float) elapsed / (float) dur_us);
        byte alpha = (byte)(20.0f + (235.0f * ease));
        ag_draw(&tmpb, &cbg, 0, 0);
        ag_draw_strecth_ex(
          &tmpb,
          &win->c,
          0, 0, agw(), agh(),
          0, 0, agw(), agh(), alpha, 1
        );
        ag_draw(NULL, &tmpb, 0, 0);
        ag_sync();
        aw_anim_pace(f0);
      }
      ag_ccanvas(&tmpb);
      ag_ccanvas(&cbg);
      ag_draw(NULL, &win->c, 0, 0);
      ag_sync();
    }
    else if (anitype == 1) {
      //-- Bottom Top (Smooth time-based VSYNC slide)
      aw_redraw_ex(win, 0);
      int drawh = agh() - pos;
      long long t0 = aw_now_us();
      const long long dur_us = 180000LL;
      int last_off = -1;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float ease = aw_ease_out_cubic((float) elapsed / (float) dur_us);
        int off = (int)(ease * (float) drawh);
        if (off < 1) off = 1;
        if (off >= drawh) break;
        if (off != last_off) {
          ag_draw_ex(NULL, &win->c, 0, agh() - off, 0, pos, agw(), drawh);
          ag_sync();
          last_off = off;
        }
        aw_anim_pace(f0);
      }
      ag_draw(NULL, &win->c, 0, 0);
      ag_sync();
    }
    else if (anitype == 2) {
      //-- Right Left (Smooth time-based VSYNC slide)
      aw_redraw_ex(win, 0);
      int drawh = agh() - pos;
      int sw = agw();
      CANVAS cbg;
      ag_canvas(&cbg, agw(), agh());
      ag_draw(&cbg, agc(), 0, 0);
      long long t0 = aw_now_us();
      const long long dur_us = 200000LL;
      int last_off = -1;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float ease = aw_ease_out_cubic((float) elapsed / (float) dur_us);
        int off = (int)(ease * (float) sw);
        if (off < 1) off = 1;
        if (off >= sw) break;
        if (off != last_off) {
          ag_draw_ex(NULL, &cbg, -off, pos, 0, pos, sw, drawh);
          ag_draw_ex(NULL, &win->c, sw - off, pos, 0, pos, sw, drawh);
          ag_sync();
          last_off = off;
        }
        aw_anim_pace(f0);
      }
      ag_ccanvas(&cbg);
      ag_draw(NULL, &win->c, 0, 0);
      ag_sync();
    }
    else if (anitype == 3) {
      //-- Left Right (Smooth time-based VSYNC slide)
      aw_redraw_ex(win, 0);
      int drawh = agh() - pos;
      int sw = agw();
      CANVAS cbg;
      ag_canvas(&cbg, agw(), agh());
      ag_draw(&cbg, agc(), 0, 0);
      long long t0 = aw_now_us();
      const long long dur_us = 200000LL;
      int last_off = -1;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float ease = aw_ease_out_cubic((float) elapsed / (float) dur_us);
        int off = (int)(ease * (float) sw);
        if (off < 1) off = 1;
        if (off >= sw) break;
        if (off != last_off) {
          ag_draw_ex(NULL, &cbg, off, pos, 0, pos, sw, drawh);
          ag_draw_ex(NULL, &win->c, -(sw - off), pos, 0, pos, sw, drawh);
          ag_sync();
          last_off = off;
        }
        aw_anim_pace(f0);
      }
      ag_ccanvas(&cbg);
      ag_draw(NULL, &win->c, 0, 0);
      ag_sync();
    }
    else if (anitype == 5) {
      //-- Dialog Scale + Fade (Single reused canvas, zero pre-render freeze)
      ag_sync();
      aw_redraw_ex(win, 0);
      CANVAS cbg;
      CANVAS tmpb;
      ag_canvas(&cbg, agw(), agh());
      ag_canvas(&tmpb, w, h);
      ag_draw(&cbg, agc(), 0, 0);
      int xc = w / 2;
      int yc = h / 2;
      long long t0 = aw_now_us();
      const long long dur_us = 160000LL;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float ease = aw_ease_out_cubic((float) elapsed / (float) dur_us);
        float sc = 0.86f + (0.14f * ease);
        int wtarget = (int)((float) w * sc);
        int htarget = (int)((float) h * sc);
        if (wtarget < 2) wtarget = 2;
        if (htarget < 2) htarget = 2;
        byte alpha = (byte)(40.0f + (215.0f * ease));
        ag_draw_ex(&tmpb, &cbg, 0, 0, x, pos, w, h);
        ag_draw_strecth_ex(
          &tmpb,
          &win->c,
          xc - wtarget / 2, yc - htarget / 2, wtarget, htarget,
          x, pos, w, h, alpha, 1
        );
        ag_draw(NULL, &tmpb, x, pos);
        ag_sync();
        aw_anim_pace(f0);
      }
      ag_ccanvas(&tmpb);
      ag_ccanvas(&cbg);
      ag_draw(NULL, &win->c, 0, 0);
      ag_sync();
    }
    else if (anitype == 6) {
      //-- Stack Right to Left (Single reused canvas, time-based)
      ag_sync();
      aw_redraw_ex(win, 0);
      CANVAS cbg;
      CANVAS tmpb;
      ag_canvas(&cbg, agw(), agh());
      ag_canvas(&tmpb, w, h);
      ag_draw(&cbg, agc(), 0, 0);
      int yc = h / 2;
      long long t0 = aw_now_us();
      const long long dur_us = 190000LL;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float ease = aw_ease_out_cubic((float) elapsed / (float) dur_us);
        float sc = 0.85f + (0.15f * ease);
        int wtarget = (int)((float) w * sc);
        int htarget = (int)((float) h * sc);
        int xtarget = (int)((float) w * ease);
        if (wtarget < 2) wtarget = 2;
        if (htarget < 2) htarget = 2;
        byte alpha = (byte)(ease * 255.0f);
        ag_draw_strecth_ex(
          &tmpb,
          &win->c,
          w - wtarget, yc - htarget / 2, wtarget, htarget,
          x, pos, w, h, alpha, 0
        );
        if (w - xtarget > 0) {
          ag_draw_ex(&tmpb, &cbg, 0, 0, x + xtarget, pos, w - xtarget, h);
        }
        ag_draw(NULL, &tmpb, x, pos);
        ag_sync();
        aw_anim_pace(f0);
      }
      ag_ccanvas(&tmpb);
      ag_ccanvas(&cbg);
      ag_draw(NULL, &win->c, 0, 0);
      ag_sync();
    }
    else if (anitype == 7) {
      //-- Stack Left to Right (Single reused canvas, time-based)
      ag_sync();
      aw_redraw_ex(win, 0);
      CANVAS cbg;
      CANVAS tmpb;
      ag_canvas(&cbg, agw(), agh());
      ag_canvas(&tmpb, w, h);
      ag_draw(&cbg, agc(), 0, 0);
      int yc = h / 2;
      long long t0 = aw_now_us();
      const long long dur_us = 190000LL;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float ease = 1.0f - aw_ease_out_cubic((float) elapsed / (float) dur_us);
        float sc = 0.85f + (0.15f * ease);
        int wtarget = (int)((float) w * sc);
        int htarget = (int)((float) h * sc);
        int xtarget = (int)((float) w * ease);
        if (wtarget < 2) wtarget = 2;
        if (htarget < 2) htarget = 2;
        byte alpha = (byte)(ease * 255.0f);
        ag_draw_strecth_ex(
          &tmpb,
          &cbg,
          w - wtarget, yc - htarget / 2, wtarget, htarget,
          x, pos, w, h, alpha, 0
        );
        if (w - xtarget > 0) {
          ag_draw_ex(&tmpb, &win->c, 0, 0, x + xtarget, pos, w - xtarget, h);
        }
        ag_draw(NULL, &tmpb, x, pos);
        ag_sync();
        aw_anim_pace(f0);
      }
      ag_ccanvas(&tmpb);
      ag_ccanvas(&cbg);
      ag_draw(NULL, &win->c, 0, 0);
      ag_sync();
    }
    else if (anitype == 8 || anitype == 9) {
      //-- Material Zoom + Fade (8 = Zoom In Forward, 9 = Zoom Out Back)
      ag_sync();
      aw_redraw_ex(win, 0);
      CANVAS cbg;
      CANVAS tmpb;
      ag_canvas(&cbg, agw(), agh());
      ag_canvas(&tmpb, w, h);
      ag_draw(&cbg, agc(), 0, 0);
      int xc = w / 2;
      int yc = h / 2;
      long long t0 = aw_now_us();
      const long long dur_us = 180000LL;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float ease = aw_ease_out_cubic((float) elapsed / (float) dur_us);
        float sc = (anitype == 8) ? (0.90f + (0.10f * ease)) : (1.08f - (0.08f * ease));
        int wtarget = (int)((float) w * sc);
        int htarget = (int)((float) h * sc);
        if (wtarget < 2) wtarget = 2;
        if (htarget < 2) htarget = 2;
        byte alpha = (byte)(30.0f + (225.0f * ease));
        ag_draw_ex(&tmpb, &cbg, 0, 0, x, pos, w, h);
        ag_draw_strecth_ex(
          &tmpb,
          &win->c,
          xc - wtarget / 2, yc - htarget / 2, wtarget, htarget,
          x, pos, w, h, alpha, 1
        );
        ag_draw(NULL, &tmpb, x, pos);
        ag_sync();
        aw_anim_pace(f0);
      }
      ag_ccanvas(&tmpb);
      ag_ccanvas(&cbg);
      ag_draw(NULL, &win->c, 0, 0);
      ag_sync();
    }
    else {
      //-- No Effect
      aw_redraw(win);
      ag_sync();
    }
  }
  else {
    //-- No Effect
    aw_redraw(win);
    ag_sync();
  }
  
  ui_clear_key_queue_ex();
}
void aw_show_ex(AWINDOWP win, byte anitype, int pos, ACONTROLP firstFocus) {
  aw_show_ex2(win, anitype, 0, pos, agw(), agh(), firstFocus);
}
//-- Show Window
void aw_show(AWINDOWP win) {
  aw_show_ex(win, 0, 0, NULL);
}

//-- Show Window
/*
void aw_show(AWINDOWP win) {
  win->threadnum    = 0;
  win->isActived    = 1;

  //-- Find First Focus
  if (win->controln > 0) {
    int i;

    for (i = 0; i < win->controln; i++) {
      ACONTROLP ctl = (ACONTROLP) win->controls[i];

      if (ctl->onfocus != NULL) {
        if (ctl->onfocus(ctl)) {
          win->focusIndex = i;
          break;
        }
      }
    }
  }

  aw_redraw(win);
  ag_sync_fade(acfg_var.fadeframes);
}
*/

//-- Post Message
void aw_post(dword msg) {
  atouch_send_message(msg);
}

//-- Check Mouse Event
byte aw_touchoncontrol(ACONTROLP ctl, int x, int y) {
  int wx  = ctl->x;
  int wx2 = wx + ctl->w;
  int wy  = ctl->y;
  int wy2 = wy + ctl->h;
  
  if ((x >= wx) && (x < wx2) && (y >= wy) && (y < wy2)) {
    return 1;
  }
  
  return 0;
}

//-- Set Focus
byte aw_setfocus(AWINDOWP win, ACONTROLP ctl) {
  if (!win->isActived) {
    return 0;
  }
  
  int i;
  
  for (i = 0; i < win->controln; i++) {
    ACONTROLP fctl = (ACONTROLP) win->controls[i];
    
    if (fctl == ctl) {
      if (fctl->onfocus != NULL) {
        if (fctl->onfocus(fctl)) {
          int pf = win->focusIndex;
          win->focusIndex = i;
          
          if ((pf != -1) && (pf != i)) {
            ACONTROLP pctl = (ACONTROLP) win->controls[pf];
            pctl->onblur(pctl);
          }
          
          aw_draw(win);
          return 1;
        }
      }
    }
  }
  
  return 0;
}

//-- Dispatch Messages
dword aw_dispatch(AWINDOWP win) {
  dword msg;
  int i;
  // ui_clear_key_queue();
  
  while (1) {
    //-- Wait For Event
    ATEV        atev;
    int action  = atouch_wait(&atev);
    //-- Reset Message Value
    msg         = aw_msg(0, 0, 0, 0);
    
    //-- Check an Action Value
    switch (action) {
      case ATEV_MESSAGE: {
          msg = atev.msg;
        }
        break;
        
      case ATEV_MENU: {
          if (!atev.d) {
            if (!on_dialog_window) {
              byte resmenu = aw_showmenu(win);
              
              if (resmenu == 2) {
                msg = aw_msg(4, 0, 0, 0);
              }
              else if (resmenu == 3) {
                msg = aw_msg(7, 0, 0, 0);
              }
            }
            else if (on_dialog_window == 2) {
              msg = aw_msg(5, 0, 0, 0);
            }
          }
        }
        break;
        
      case ATEV_BACK: {
          if (!atev.d) {
            msg = aw_msg(5, 0, 0, 0);
          }
        }
        break;
        
      case ATEV_DOWN:
      case ATEV_RIGHT:
        if (!atev.d) {
          if (win->focusIndex != -1) {
            ACONTROLP ctl = (ACONTROLP) win->controls[win->focusIndex];
            
            if (ctl->oninput != NULL) {
              msg = ctl->oninput((void *)ctl, action, &atev);
            }
            
            if (aw_gl(msg) == 0) {
              for (i = win->focusIndex + 1; i < win->controln; i++) {
                ACONTROLP fctl = (ACONTROLP) win->controls[i];
                
                if (fctl->onfocus != NULL) {
                  if (fctl->onfocus(fctl)) {
                    win->focusIndex = i;
                    ctl->onblur(ctl);
                    aw_draw(win);
                    break;
                  }
                }
              }
            }
          }
        }
        
        break;
        
      case ATEV_UP:
      case ATEV_LEFT:
        if (!atev.d) {
          if (win->focusIndex != -1) {
            ACONTROLP ctl = (ACONTROLP) win->controls[win->focusIndex];
            
            if (ctl->oninput != NULL) {
              msg = ctl->oninput((void *)ctl, action, &atev);
            }
            
            if (aw_gl(msg) == 0) {
              for (i = win->focusIndex - 1; i >= 0; i--) {
                ACONTROLP fctl = (ACONTROLP) win->controls[i];
                
                if (fctl->onfocus != NULL) {
                  if (fctl->onfocus(fctl)) {
                    win->focusIndex = i;
                    ctl->onblur(ctl);
                    aw_draw(win);
                    break;
                  }
                }
              }
            }
          }
        }
        
        break;
        
      case ATEV_SELECT: {
          if (win->focusIndex != -1) {
            ACONTROLP ctl = (ACONTROLP) win->controls[win->focusIndex];
            
            if (ctl->oninput != NULL) {
              msg = ctl->oninput((void *)ctl, action, &atev);
            }
          }
        }
        break;
        
      case ATEV_MOUSEDN: {
          if (win->controln > 0) {
            int i;
            
            for (i = win->controln - 1; i >= 0; i--) {
              ACONTROLP ctl = (ACONTROLP) win->controls[i];
              
              if (aw_touchoncontrol(ctl, atev.x, atev.y)) {
                if (ctl->oninput != NULL) {
                  msg             = ctl->oninput((void *)ctl, action, &atev);
                  win->touchIndex = i;
                  break;
                }
              }
            }
          }
        }
        break;
        
      case ATEV_MOUSEUP: {
          if (win->touchIndex != -1) {
            ACONTROLP ctl = (ACONTROLP) win->controls[win->touchIndex];
            
            if (ctl->oninput != NULL) {
              msg             = ctl->oninput((void *)ctl, action, &atev);
            }
            
            win->touchIndex   = -1;
          }
        }
        break;
        
      case ATEV_MOUSEMV: {
          if (win->touchIndex != -1) {
            ACONTROLP ctl = (ACONTROLP) win->controls[win->touchIndex];
            
            if (ctl->oninput != NULL) {
              msg             = ctl->oninput((void *)ctl, action, &atev);
            }
          }
        }
        break;
    }
    
    if (aw_gm(msg) == 200) {
      if (!atev.d) {
        if (!on_dialog_window) {
          byte resmenu = aw_showmenu(win);
          
          if (resmenu == 2) {
            msg = aw_msg(4, 0, 0, 0);
          }
          else if (resmenu == 3) {
            msg = aw_msg(7, 0, 0, 0);
          }
        }
        else if (on_dialog_window == 2) {
          msg = aw_msg(5, 0, 0, 0);
        }
      }
    }
    
    if (aw_gd(msg) == 1) {
      aw_draw(win);
    }
    
    if (aw_gm(msg) != 0) {
      return msg;
    }
  }
  
  return msg;
}
CANVAS * aw_muteparent(AWINDOWP win) {
  if (win == NULL) {
    //-- Set Temporary
    CANVAS * tmpbg = (CANVAS *) malloc(sizeof(CANVAS));
    ag_canvas(tmpbg, agw(), agh());
    ag_draw(tmpbg, agc(), 0, 0);
    return tmpbg;
  }
  else {
    win->isActived = 0;
    return NULL;
  }
}
CANVAS * aw_maskparent() {
  CANVAS * tmpbg = (CANVAS *) malloc(sizeof(CANVAS));
  ag_canvas(tmpbg, agw(), agh());
  ag_draw(tmpbg, agc(), 0, 0);
  ag_rectopa(tmpbg, 0, 0, agw(), agh(), 0x0000, 180);
  ag_draw(agc(), tmpbg, 0, 0);
  return tmpbg;
}
void aw_unmuteparent(AWINDOWP win, CANVAS * p) {
  if (win == NULL) {
    if (p != NULL) {
      ag_draw(NULL, p, 0, 0);
      //ag_sync_fade(acfg_var.fadeframes);
      ag_sync();
      ag_ccanvas(p);
      free(p);
    }
  }
  else {
    if (p != NULL) {
      ag_ccanvas(p);
      free(p);
    }
    
    win->isActived = 1;
    ag_draw(NULL, &win->c, 0, 0);
    //ag_sync_fade(acfg_var.fadeframes);
    ag_sync();
  }
}
void aw_unmaskparent(AWINDOWP win, CANVAS * p, CANVAS * maskc, int x, int y, int w, int h) {
  if (maskc != NULL) {
    CANVAS * wincanvas = NULL;
    
    if (win == NULL) {
      if (p != NULL) {
        wincanvas = p;
      }
      else {
        if (maskc != NULL) {
          ag_ccanvas(maskc);
          free(maskc);
        }
        
        aw_unmuteparent(win, p);
        return;
      }
    }
    else {
      wincanvas = &win->c;
    }
    
    int fadesz = (strcmp(aui_get_user_transition(), "none") == 0) ? 0 : acfg()->fadeframes;
    
    if (fadesz > 0) {
      //-- Smooth time-based dialog dismiss (single reused canvas)
      CANVAS cbg;
      CANVAS tmpb;
      ag_canvas(&cbg, agw(), agh());
      ag_canvas(&tmpb, w, h);
      ag_draw(&cbg, agc(), 0, 0);
      int xc = w / 2;
      int yc = h / 2;
      long long t0 = aw_now_us();
      const long long dur_us = 130000LL;
      while (1) {
        long long f0 = aw_now_us();
        long long elapsed = f0 - t0;
        if (elapsed >= dur_us) {
          break;
        }
        float u = (float) elapsed / (float) dur_us;
        float inv = 1.0f - u;
        float ease = inv * inv; /* smooth ease-in shrink */
        float sc = 0.86f + (0.14f * ease);
        int wtarget = (int)((float) w * sc);
        int htarget = (int)((float) h * sc);
        if (wtarget < 2) wtarget = 2;
        if (htarget < 2) htarget = 2;
        byte alpha = (byte)(ease * 255.0f);
        ag_draw_ex(&tmpb, maskc, 0, 0, x, y, w, h);
        ag_draw_strecth_ex(
          &tmpb,
          &cbg,
          xc - wtarget / 2, yc - htarget / 2, wtarget, htarget,
          x, y, w, h, alpha, 1
        );
        ag_draw(NULL, &tmpb, x, y);
        ag_sync();
        aw_anim_pace(f0);
      }
      ag_ccanvas(&tmpb);
      ag_ccanvas(&cbg);
    }
    
    ag_ccanvas(maskc);
    free(maskc);
    aw_unmuteparent(win, p);
  }
  else {
    aw_unmuteparent(win, p);
  }
}
static void aw_draw_dialog_border(CANVAS * c, PNGCANVASP winp, int winX, int winY, int winW, int winH, int titH) {
  (void) winp;
  if (aui_is_m3()) {
    return;
  }
  int dlg_id = atheme_id("img.dialog");
  if (dlg_id >= 0 && acfg_var.theme_9p[dlg_id]) {
    return;
  }
  if (acfg_var.border == acfg_var.dialogbg) {
    return;
  }
  int bw = agdp() / 3;
  if (bw < 2) bw = 2;
  int bh = (bw > 2) ? 2 : bw;
  ag_rect(c, winX, winY, winW, bw, acfg_var.border);
  ag_rect(c, winX, winY + winH - bw, winW, bw, acfg_var.border);
  ag_rect(c, winX, winY, bw, winH, acfg_var.border);
  ag_rect(c, winX + winW - bw, winY, bw, winH, acfg_var.border);
  if (titH > bw) {
    ag_rect(c, winX, winY + titH - bh, winW, bh, acfg_var.border);
  }
}

void aw_textdialog(AWINDOWP parent, char * titlev, char * text, char * ok_text) {
  // actext(hWin,txtX,txtY,txtW,txtH,text,0);
  CANVAS * tmpc = aw_muteparent(parent);
  //-- Set Mask
  on_dialog_window = 1;
  CANVAS * maskc = aw_maskparent();
  //ag_sync();
  //ag_sync_fade(acfg_var.fadeframes);
  char title[64];
  snprintf(title, 64, "%s", titlev);
  int pad   = agdp() * 4;
  int winW  = agw() - (pad * 2);
  int txtW  = winW - (pad * 2);
  int txtX  = pad * 2;
  int btnH  = agdp() * 20;
  int titW  = ag_txtwidth(title, 1);
  int titH  = ag_fontheight(1) + (pad * 2);
  PNGCANVASP winp = atheme("img.dialog");
  PNGCANVASP titp = atheme("img.dialog.titlebar");
  APNG9      winv;
  APNG9      titv;
  int vtitY = -1;
  int vpadB = pad;
  int vimgX = pad * 2;
  
  if (titp != NULL) {
    if (apng9_calc(titp, &titv, 1)) {
      int tmptitH = titH - (pad * 2);
      titH        = tmptitH + (titv.t + titv.b);
      vtitY       = titv.t;
    }
  }
  
  if (winp != NULL) {
    if (apng9_calc(winp, &winv, 1)) {
      txtW = winW - (winv.l + winv.r);
      txtX = pad  + (winv.l);
      vimgX = pad  + (winv.l);
      vpadB = winv.b;
    }
  }
  
  byte imgE = 0;
  int imgW = 0;
  int imgH = 0;
  int txtH    = agh() / 2;
  int infH    = txtH;
  //-- Calculate Window Size & Position
  int winH    = titH + infH + btnH + (pad * 2) + vpadB;
  int winX    = pad;
  int winY    = (agh() / 2) - (winH / 2);
  //-- Calculate Title Size & Position
  int titX    = (agw() / 2) - (titW / 2);
  int titY    = winY + pad;
  
  if (vtitY != -1) {
    titY = winY + vtitY;
  }
  
  //-- Calculate Text Size & Position
  int infY    = winY + titH + pad;
  int txtY    = infY;
  //-- Calculate Button Size & Position
  int btnW    = aui_is_m3() ? txtW : (winW / 2);
  int btnY    = infY + infH + pad;
  int btnX    = aui_is_m3() ? txtX : ((agw() / 2) - (btnW / 2));
  //-- Initializing Canvas
  CANVAS alertbg;
  ag_canvas(&alertbg, agw(), agh());
  ag_draw(&alertbg, agc(), 0, 0);
  
  if (aui_is_m3()) {
    int m3r = agdp() * 10;
    ag_roundgrad(&alertbg, winX - 1, winY - 1, winW + 2, winH + 2, acfg_var.border, acfg_var.border, m3r + 1);
    ag_roundgrad(&alertbg, winX, winY, winW, winH, acfg_var.dialogbg, acfg_var.dialogbg, m3r);
    ag_roundgrad_ex(&alertbg, winX, winY, winW, titH, acfg_var.dlgtitlebg, acfg_var.dlgtitlebg, m3r, 1, 1, 0, 0);
    ag_rect(&alertbg, winX, winY + titH - 1, winW, 1, acfg_var.border);
    ag_text(&alertbg, titW, titX, titY, title, acfg_var.dlgtitlefg, 1);
  }
  else {
    //-- Draw Window
    if (!atheme_draw("img.dialog", &alertbg, winX, winY, winW, winH)) {
      ag_roundgrad(&alertbg, winX - 1, winY - 1, winW + 2, winH + 2, acfg_var.border, acfg_var.border_g, (acfg_var.roundsz * agdp()) + 1);
      ag_roundgrad(&alertbg, winX, winY, winW, winH, acfg_var.dialogbg, acfg_var.dialogbg_g, acfg_var.roundsz * agdp());
    }
    
    //-- Draw Title
    if (!atheme_draw("img.dialog.titlebar", &alertbg, winX, winY, winW, titH)) {
      ag_roundgrad_ex(&alertbg, winX, winY, winW, titH, acfg_var.dlgtitlebg, acfg_var.dlgtitlebg_g, acfg_var.roundsz * agdp(), 1, 1, 0, 0);
    }
    aw_draw_dialog_border(&alertbg, winp, winX, winY, winW, winH, titH);
    
    ag_textf(&alertbg, titW, titX + 1, titY + 1, title, acfg_var.dlgtitlebg_g, 1);
    ag_text(&alertbg, titW, titX, titY, title, acfg_var.dlgtitlefg, 1);
  }
  AWINDOWP hWin   = aw(&alertbg);
  actext(hWin, txtX, txtY, txtW, txtH, text, 0);
  ACONTROLP okbtn = acbutton(hWin, btnX, btnY, btnW, btnH, (ok_text == NULL ? acfg_var.text_ok : ok_text), 0, 5);
  // aw_show_ex(hWin, 5, 0, okbtn);
  aw_show_ex2(hWin, 5, winX - 1, winY - 1, winW + 2, winH + 2, okbtn);
  /*
  aw_show(hWin);
  aw_setfocus(hWin, okbtn);
  */
  byte ondispatch = 1;
  
  while (ondispatch) {
    dword msg = aw_dispatch(hWin);
    
    switch (aw_gm(msg)) {
      case 5:
        ondispatch = 0;
        break;
    }
  }
  
  aw_destroy(hWin);
  ag_ccanvas(&alertbg);
  on_dialog_window = 0;
  aw_unmaskparent(parent, tmpc, maskc, winX - 1, winY - 1, winW + 2, winH + 2);
}
void aw_alert(AWINDOWP parent, char * titlev, char * textv, char * img, char * ok_text) {
  CANVAS * tmpc = aw_muteparent(parent);
  //-- Set Mask
  on_dialog_window = 1;
  CANVAS * maskc = aw_maskparent();
  //ag_sync();
  //ag_sync_fade(acfg_var.fadeframes);
  char title[96];
  char text[2048];
  snprintf(title, sizeof(title), "%s", titlev);
  snprintf(text, sizeof(text), "%s", textv);
  int pad   = agdp() * 4;
  int winW  = agw() - (pad * 2);
  int txtW  = winW - (pad * 2);
  int txtX  = pad * 2;
  int btnH  = agdp() * 20;
  int titW  = ag_txtwidth(title, 1);
  int titH  = ag_fontheight(1) + (pad * 2);
  PNGCANVASP winp = atheme("img.dialog");
  PNGCANVASP titp = atheme("img.dialog.titlebar");
  APNG9      winv;
  APNG9      titv;
  int vtitY = -1;
  int vpadB = -1;
  int vimgX = pad * 2;
  
  if (titp != NULL) {
    if (apng9_calc(titp, &titv, 1)) {
      int tmptitH = titH - (pad * 2);
      titH        = tmptitH + (titv.t + titv.b);
      vtitY       = titv.t;
    }
  }
  
  if (winp != NULL) {
    if (apng9_calc(winp, &winv, 1)) {
      txtW = winW - (winv.l + winv.r);
      txtX = pad  + (winv.l);
      vimgX = pad  + (winv.l);
      vpadB = winv.b;
    }
  }
  
  //-- Load Icon
  PNGCANVAS ap;
  byte imgE = 0;
  int imgW = 0;
  int imgH = 0;
  
  if (apng_load(&ap, img)) {
    imgE      = 1;
    imgW      = aui_is_m3() ? (agdp() * 22) : min(ap.w, agdp() * 30);
    imgH      = aui_is_m3() ? (agdp() * 22) : min(ap.h, agdp() * 30);
    int imgA  = pad + imgW;
    txtX     += imgA;
    txtW     -= imgA;
  }
  
  int txtH    = ag_txtheight(txtW, text, 0);
  int infH    = ((imgE) && (txtH < imgH)) ? imgH : txtH;
  //-- Calculate Window Size & Position
  int winH    = titH + infH + btnH + (pad * 3);
  
  if (vpadB != -1) {
    winH    = titH + infH + btnH + (pad * 2) + vpadB;
  }
  
  int winX    = pad;
  int winY    = (agh() / 2) - (winH / 2);
  //-- Calculate Title Size & Position
  int titX    = (agw() / 2) - (titW / 2);
  int titY    = winY + pad;
  
  if (vtitY != -1) {
    titY = winY + vtitY;
  }
  
  //-- Calculate Text Size & Position
  int infY    = winY + titH + pad;
  int txtY    = infY + ((infH - txtH) / 2);
  int imgY    = infY;
  //-- Calculate Button Size & Position
  int btnW    = aui_is_m3() ? (winW - (pad * 2)) : (winW / 2);
  int btnY    = infY + infH + pad;
  int btnX    = aui_is_m3() ? (winX + pad) : ((agw() / 2) - (btnW / 2));
  //-- Initializing Canvas
  CANVAS alertbg;
  ag_canvas(&alertbg, agw(), agh());
  ag_draw(&alertbg, agc(), 0, 0);
  
  if (aui_is_m3()) {
    int m3r = agdp() * 10;
    ag_roundgrad(&alertbg, winX - 1, winY - 1, winW + 2, winH + 2, acfg_var.border, acfg_var.border, m3r + 1);
    ag_roundgrad(&alertbg, winX, winY, winW, winH, acfg_var.dialogbg, acfg_var.dialogbg, m3r);
    ag_roundgrad_ex(&alertbg, winX, winY, winW, titH, acfg_var.dlgtitlebg, acfg_var.dlgtitlebg, m3r, 1, 1, 0, 0);
    ag_rect(&alertbg, winX, winY + titH - 1, winW, 1, acfg_var.border);
    ag_text(&alertbg, titW, titX, titY, title, acfg_var.dlgtitlefg, 1);
  }
  else {
    //-- Draw Window
    if (!atheme_draw("img.dialog", &alertbg, winX, winY, winW, winH)) {
      ag_roundgrad(&alertbg, winX - 1, winY - 1, winW + 2, winH + 2, acfg_var.border, acfg_var.border_g, (acfg_var.roundsz * agdp()) + 1);
      ag_roundgrad(&alertbg, winX, winY, winW, winH, acfg_var.dialogbg, acfg_var.dialogbg_g, acfg_var.roundsz * agdp());
    }
    
    //-- Draw Title
    if (!atheme_draw("img.dialog.titlebar", &alertbg, winX, winY, winW, titH)) {
      ag_roundgrad_ex(&alertbg, winX, winY, winW, titH, acfg_var.dlgtitlebg, acfg_var.dlgtitlebg_g, acfg_var.roundsz * agdp(), 1, 1, 0, 0);
    }
    aw_draw_dialog_border(&alertbg, winp, winX, winY, winW, winH, titH);
    
    ag_textf(&alertbg, titW, titX + 1, titY + 1, title, acfg_var.dlgtitlebg_g, 1);
    ag_text(&alertbg, titW, titX, titY, title, acfg_var.dlgtitlefg, 1);
  }
  
  //-- Draw Image
  if (imgE) {
    apng_stretch(&alertbg, &ap, vimgX, imgY, imgW, imgH, 0, 0, ap.w, ap.h);
    apng_close(&ap);
  }
  
  //-- Draw Text
  if (!aui_is_m3()) {
    ag_textf(&alertbg, txtW, txtX + 1, txtY + 1, text, acfg_var.dialogbg, 0);
  }
  ag_text(&alertbg, txtW, txtX, txtY, text, acfg_var.dialogfg, 0);
  AWINDOWP hWin   = aw(&alertbg);
  ACONTROLP okbtn = acbutton(hWin, btnX, btnY, btnW, btnH, (ok_text == NULL ? acfg_var.text_ok : ok_text), 0, 5);
  // aw_show_ex(hWin, 5, 0, NULL);
  aw_show_ex2(hWin, 5, winX - 1, winY - 1, winW + 2, winH + 2, aui_is_m3() ? okbtn : NULL);
  /*
  aw_show(hWin);
  */
  byte ondispatch = 1;
  
  while (ondispatch) {
    dword msg = aw_dispatch(hWin);
    
    switch (aw_gm(msg)) {
      case 5:
        ondispatch = 0;
        break;
    }
  }
  
  aw_destroy(hWin);
  ag_ccanvas(&alertbg);
  on_dialog_window = 0;
  aw_unmaskparent(parent, tmpc, maskc, winX - 1, winY - 1, winW + 2, winH + 2);
}
byte aw_confirm(AWINDOWP parent, char * titlev, char * textv, char * img, char * yes_text, char * no_text) {
  CANVAS * tmpc = aw_muteparent(parent);
  //-- Set Mask
  on_dialog_window = 1;
  CANVAS * maskc = aw_maskparent();
  //ag_sync();
  //ag_sync_fade(acfg_var.fadeframes);
  char title[96];
  char text[2048];
  snprintf(title, sizeof(title), "%s", titlev);
  snprintf(text, sizeof(text), "%s", textv);
  int pad   = agdp() * 4;
  int winW  = agw() - (pad * 2);
  int txtW  = winW - (pad * 2);
  int txtX  = pad * 2;
  int btnH  = agdp() * 20;
  int titW  = ag_txtwidth(title, 1);
  int titH  = ag_fontheight(1) + (pad * 2);
  PNGCANVASP winp = atheme("img.dialog");
  PNGCANVASP titp = atheme("img.dialog.titlebar");
  APNG9      winv;
  APNG9      titv;
  int vtitY = -1;
  int vpadB = -1;
  int vimgX = pad * 2;
  
  if (titp != NULL) {
    if (apng9_calc(titp, &titv, 1)) {
      int tmptitH = titH - (pad * 2);
      titH        = tmptitH + (titv.t + titv.b);
      vtitY       = titv.t;
    }
  }
  
  if (winp != NULL) {
    if (apng9_calc(winp, &winv, 1)) {
      txtW = winW - (winv.l + winv.r);
      txtX = pad  + (winv.l);
      vimgX = pad  + (winv.l);
      vpadB = winv.b;
    }
  }
  
  //-- Load Icon
  PNGCANVAS ap;
  byte imgE = 0;
  int imgW = 0;
  int imgH = 0;
  
  if (apng_load(&ap, img)) {
    imgE      = 1;
    imgW      = aui_is_m3() ? (agdp() * 22) : min(ap.w, agdp() * 30);
    imgH      = aui_is_m3() ? (agdp() * 22) : min(ap.h, agdp() * 30);
    int imgA  = pad + imgW;
    txtX     += imgA;
    txtW     -= imgA;
  }
  
  int txtH    = ag_txtheight(txtW, text, 0);
  int infH    = ((imgE) && (txtH < imgH)) ? imgH : txtH;
  //-- Calculate Window Size & Position
  int winH    = titH + infH + btnH + (pad * 3);
  
  if (vpadB != -1) {
    winH    = titH + infH + btnH + (pad * 2) + vpadB;
  }
  
  int winX    = pad;
  int winY    = (agh() / 2) - (winH / 2);
  //-- Calculate Title Size & Position
  int titX    = (agw() / 2) - (titW / 2);
  int titY    = winY + pad;
  
  if (vtitY != -1) {
    titY = winY + vtitY;
  }
  
  //-- Calculate Text Size & Position
  int infY    = winY + titH + pad;
  int txtY    = infY + ((infH - txtH) / 2);
  int imgY    = infY;
  //-- Calculate Button Size & Position
  int btnW    = aui_is_m3() ? ((winW - (pad * 3)) / 2) : ((txtW / 2) - (pad / 2));
  int btnY    = infY + infH + pad;
  int btnX    = aui_is_m3() ? (winX + pad) : txtX;
  int btnX2   = aui_is_m3() ? (btnX + btnW + pad) : (txtX + (txtW / 2) + (pad / 2));
  //-- Initializing Canvas
  CANVAS alertbg;
  ag_canvas(&alertbg, agw(), agh());
  ag_draw(&alertbg, agc(), 0, 0);
  
  if (aui_is_m3()) {
    int m3r = agdp() * 10;
    ag_roundgrad(&alertbg, winX - 1, winY - 1, winW + 2, winH + 2, acfg_var.border, acfg_var.border, m3r + 1);
    ag_roundgrad(&alertbg, winX, winY, winW, winH, acfg_var.dialogbg, acfg_var.dialogbg, m3r);
    ag_roundgrad_ex(&alertbg, winX, winY, winW, titH, acfg_var.dlgtitlebg, acfg_var.dlgtitlebg, m3r, 1, 1, 0, 0);
    ag_rect(&alertbg, winX, winY + titH - 1, winW, 1, acfg_var.border);
    ag_text(&alertbg, titW, titX, titY, title, acfg_var.dlgtitlefg, 1);
  }
  else {
    //-- Draw Window
    if (!atheme_draw("img.dialog", &alertbg, winX - 1, winY - 1, winW + 2, winH + 2)) {
      ag_roundgrad(&alertbg, winX - 1, winY - 1, winW + 2, winH + 2, acfg_var.border, acfg_var.border_g, (acfg_var.roundsz * agdp()) + 1);
      ag_roundgrad(&alertbg, winX, winY, winW, winH, acfg_var.dialogbg, acfg_var.dialogbg_g, acfg_var.roundsz * agdp());
    }
    
    //-- Draw Title
    if (!atheme_draw("img.dialog.titlebar", &alertbg, winX, winY, winW, titH)) {
      ag_roundgrad_ex(&alertbg, winX, winY, winW, titH, acfg_var.dlgtitlebg, acfg_var.dlgtitlebg_g, acfg_var.roundsz * agdp(), 1, 1, 0, 0);
    }
    aw_draw_dialog_border(&alertbg, winp, winX - 1, winY - 1, winW + 2, winH + 2, titH + 1);
    
    ag_textf(&alertbg, titW, titX + 1, titY + 1, title, acfg_var.dlgtitlebg_g, 1);
    ag_text(&alertbg, titW, titX, titY, title, acfg_var.dlgtitlefg, 1);
  }
  
  //-- Draw Image
  if (imgE) {
    apng_stretch(&alertbg, &ap, vimgX, imgY, imgW, imgH, 0, 0, ap.w, ap.h);
    apng_close(&ap);
  }
  
  //-- Draw Text
  if (!aui_is_m3()) {
    ag_textf(&alertbg, txtW, txtX + 1, txtY + 1, text, acfg_var.dialogbg, 0);
  }
  ag_text(&alertbg, txtW, txtX, txtY, text, acfg_var.dialogfg, 0);
  AWINDOWP hWin   = aw(&alertbg);
  ACONTROLP yesbtn = acbutton(hWin, btnX, btnY, btnW, btnH, (yes_text == NULL ? acfg_var.text_yes : yes_text), 0, 6);
  acbutton(hWin, btnX2, btnY, btnW, btnH, (no_text == NULL ? acfg_var.text_no : no_text), 0, 5);
  //aw_show_ex(hWin, 5, 0, NULL);
  aw_show_ex2(hWin, 5, winX - 1, winY - 1, winW + 2, winH + 2, aui_is_m3() ? yesbtn : NULL);
  byte ondispatch = 1;
  byte res = 0;
  
  while (ondispatch) {
    dword msg = aw_dispatch(hWin);
    
    switch (aw_gm(msg)) {
      case 6:
        res = 1;
        ondispatch = 0;
        break;
        
      case 5:
        ondispatch = 0;
        break;
    }
  }
  
  aw_destroy(hWin);
  ag_ccanvas(&alertbg);
  on_dialog_window = 0;
  aw_unmaskparent(parent, tmpc, maskc, winX - 1, winY - 1, winW + 2, winH + 2);
  return res;
}
void aw_help_dialog(AWINDOWP parent) {
}
byte aw_calibdraw(CANVAS * c,
                  int id, int * xpos, int * ypos, int * xtch, int * ytch) {
  return 0;
  /*
  ag_draw(agc(), c, 0, 0);
  // usleep(500000);
  int sz = agdp() * 10;
  int ty2 = 0;
  
  if (id == -2) {
    char * txt  =
      "<@center><b>Fine Tuning</b>\n"
      "Use volume button to fine tuning your calibration.\n"
      "Increase if it flicked while hold your finger.\n"
      "Reduce if it sticked while release your finger.\n"
      "<b>NOTE:</b> Smaller value mean more responsive</@>\n\n";
    char * txt2 = "Press Power Key or Other Keys To Continue";
    int txh = ag_txtheight(agw(), txt, 0);
    int tw = ag_txtwidth(txt, 0);
    int tw2 = ag_txtwidth(txt2, 0);
    int tx = (agw() / 2) - (tw / 2);
    int tx2 = (agw() / 2) - (tw2 / 2);
    int ty = (agh() / 2) + (sz * 2);
    ty2 = (ty + ag_fontheight(0) + agdp());
    ty -= txh - ag_fontheight(0);
    ag_text(agc(), agw(), 1, ty + 1, txt, 0x0000, 0);
    ag_text(agc(), agw(), 0, ty, txt, 0xffff, 0);
    ag_text(agc(), tw2, tx2 + 1, ty2 + 1, txt2, 0x0000, 0);
    ag_text(agc(), tw2, tx2, ty2, txt2, 0xffff, 0);
  }
  else if (id != -1) {
    char txt[128];
    snprintf(txt, 128, "Step %i: Tap The Circle To Calibrate", id + 1);
    char * txt2 = "Press Power Key or Other Keys To Cancel";
    int tw = ag_txtwidth(txt, 0);
    int tw2 = ag_txtwidth(txt2, 0);
    int tx = (agw() / 2) - (tw / 2);
    int tx2 = (agw() / 2) - (tw2 / 2);
    int ty = (agh() / 2) + (sz * 2);
    ty2 = (ty + ag_fontheight(0) + agdp());
    ag_text(agc(), tw, tx + 1, ty + 1, txt, 0x0000, 0);
    ag_text(agc(), tw, tx, ty, txt, 0xffff, 0);
    ag_text(agc(), tw2, tx2 + 1, ty2 + 1, txt2, 0x0000, 0);
    ag_text(agc(), tw2, tx2, ty2, txt2, 0xffff, 0);
  }
  else {
    char * txt  = "Tap The Screen to Test Calibrated Data";
    char * txt2 = "Press Power or Other Keys To Continue";
    int tw = ag_txtwidth(txt, 0);
    int tw2 = ag_txtwidth(txt2, 0);
    int tx = (agw() / 2) - (tw / 2);
    int tx2 = (agw() / 2) - (tw2 / 2);
    int ty = (agh() / 2) + (sz * 2);
    ty2 = (ty + ag_fontheight(0) + agdp());
    ag_text(agc(), tw, tx + 1, ty + 1, txt, 0x0000, 0);
    ag_text(agc(), tw, tx, ty, txt, 0xffff, 0);
    ag_text(agc(), tw2, tx2 + 1, ty2 + 1, txt2, 0x0000, 0);
    ag_text(agc(), tw2, tx2, ty2, txt2, 0xffff, 0);
  }
  
  CANVAS bg;
  ag_canvas(&bg, agw(), agh());
  ag_draw(&bg, agc(), 0, 0);
  ag_sync();
  int rx = 0;
  int ry = 0;
  
  if ((id != -1) && (id != -2)) {
    int x  = xpos[id];
    int y  = ypos[id];
    rx     = x - (sz / 2);
    ry     = y - (sz / 2);
  
    if (id > 0) {
      int prx = xpos[id - 1];
      int pry = ypos[id - 1];
      int px  = prx - (sz / 2);
      int py  = pry - (sz / 2);
      int i;
      int addx = floor((rx - prx) / 10.0);
      int addy = floor((ry - pry) / 10.0);
  
      for (i = 0; i < 9; i++) {
        usleep(5000);
        ag_draw(agc(), &bg, 0, 0);
        prx += addx;
        pry += addy;
        ag_roundgrad(agc(), prx, pry, sz, sz,
                     0xffff,
                     ag_rgb(200, 200, 200),
                     sz / 2);
        ag_sync();
      }
  
      usleep(50000);
      ag_draw(agc(), &bg, 0, 0);
    }
  
    ag_roundgrad(
      agc(), rx, ry, sz, sz,
      0xffff,
      ag_rgb(200, 200, 200),
      sz / 2);
    ag_sync();
  }
  
  if (id == -2) {
    char txt3[256];
    snprintf(txt3, 256, "<b>Fine Tuning Value : %i</b>", *xpos);
    int ty3 = agh() - (ag_fontheight(0) + (agdp() * 6));
    int tw3 = ag_txtwidth(txt3, 0);
    int tx3 = (agw() / 2) - (tw3 / 2);
    ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
    ag_sync();
  }
  
  byte res = 1;
  byte ond = 1;
  byte onp = 0;
  ui_clear_key_queue();
  
  while (ond) {
    ATEV atev;
    // ui_clear_key_queue();
    int action = atouch_wait_ex(&atev, 1);
  
    switch (action) {
      case ATEV_MOUSEDN: {
          onp = 1;
  
          if ((id == -1) || (id == -2)) {
            ag_draw(agc(), &bg, 0, 0);
            int vz = agdp() * 30;
            int vx = atev.x - (vz / 2);
            int vy = atev.y - (vz / 2);
            char txt3[256];
            snprintf(txt3, 256, "<b>Coordinate: %i x %i</b>", atev.x, atev.y);
            int ty3 = ty2 + ag_fontheight(0) + (agdp() * 6);
            int tw3 = ag_txtwidth(txt3, 0);
            int tx3 = (agw() / 2) - (tw3 / 2);
            ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
            ag_rect(agc(), 0, atev.y, agw(), 1, 0xffff);
            ag_rect(agc(), atev.x, 0, 1, agh(), 0xffff);
            ag_roundgrad(agc(), vx, vy, vz, vz, 0xffff, ag_rgb(180, 180, 180), (vz / 2));
  
            if (id == -2) {
              snprintf(txt3, 256, "<b>Fine Tuning Value : %i</b>", *xpos);
              ty3 = agh() - (ag_fontheight(0) + (agdp() * 6));
              tw3 = ag_txtwidth(txt3, 0);
              tx3 = (agw() / 2) - (tw3 / 2);
              ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
            }
  
            ag_sync();
          }
          else {
            ag_draw(agc(), &bg, 0, 0);
            int agpl = agdp() * 10;
            ag_roundgrad(
              agc(), rx - agpl, ry - agpl, sz + (agpl * 2), sz + (agpl * 2),
              0xffff,
              ag_rgb(200, 200, 200),
              (sz / 2) + agpl);
            agpl = agdp() * 7;
            ag_roundgrad(
              agc(), rx - agpl, ry - agpl, sz + (agpl * 2), sz + (agpl * 2),
              ag_rgb(40, 40, 40),
              0x0000,
              (sz / 2) + agpl);
            agpl = 0;
            ag_roundgrad(
              agc(), rx - agpl, ry - agpl, sz + (agpl * 2), sz + (agpl * 2),
              0xffff,
              ag_rgb(200, 200, 200),
              (sz / 2) + agpl);
            ag_sync();
          }
        }
        break;
  
      case ATEV_MOUSEMV: {
          if (onp) {
            if ((id != -1) && (id != -2)) {
              xtch[id] = atev.x;
              ytch[id] = atev.y;
            }
            else {
              ag_draw(agc(), &bg, 0, 0);
              int vz = agdp() * 30;
              int vx = atev.x - (vz / 2);
              int vy = atev.y - (vz / 2);
              char txt3[256];
              snprintf(txt3, 256, "<b>Coordinate: %i x %i</b>", atev.x, atev.y);
              int ty3 = ty2 + ag_fontheight(0) + (agdp() * 6);
              int tw3 = ag_txtwidth(txt3, 0);
              int tx3 = (agw() / 2) - (tw3 / 2);
              ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
              ag_rect(agc(), 0, atev.y, agw(), 1, 0xffff);
              ag_rect(agc(), atev.x, 0, 1, agh(), 0xffff);
              ag_roundgrad(agc(), vx, vy, vz, vz, 0xffff, ag_rgb(180, 180, 180), (vz / 2));
  
              if (id == -2) {
                snprintf(txt3, 256, "<b>Fine Tuning Value : %i</b>", *xpos);
                ty3 = agh() - (ag_fontheight(0) + (agdp() * 6));
                tw3 = ag_txtwidth(txt3, 0);
                tx3 = (agw() / 2) - (tw3 / 2);
                ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
              }
  
              ag_sync();
            }
          }
        }
        break;
  
      case ATEV_MOUSEUP: {
          if ((id != -1) && (id != -2)) {
            if (onp) {
              if ((xtch[id] > 0) && (ytch[id] > 0)) {
                ond = 0;
              }
  
              onp = 0;
            }
          }
          else {
            ag_draw(agc(), &bg, 0, 0);
            int vz = agdp() * 40;
            int vx = atev.x - (vz / 2);
            int vy = atev.y - (vz / 2);
            char txt3[256];
            snprintf(txt3, 256, "<b>Release Coordinate: %i x %i</b>", atev.x, atev.y);
            int ty3 = ty2 + ag_fontheight(0) + (agdp() * 6);
            int tw3 = ag_txtwidth(txt3, 0);
            int tx3 = (agw() / 2) - (tw3 / 2);
            ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
  
            if (id == -2) {
              snprintf(txt3, 256, "<b>Fine Tuning Value : %i</b>", *xpos);
              ty3 = agh() - (ag_fontheight(0) + (agdp() * 6));
              tw3 = ag_txtwidth(txt3, 0);
              tx3 = (agw() / 2) - (tw3 / 2);
              ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
            }
  
            // ag_roundgrad(agc(),vx,vy,vz,vz,0xffff,ag_rgb(180,180,180),(vz/2));
            ag_sync();
          }
        }
        break;
  
      case ATEV_DOWN: {
          if (id == -2) {
            *xpos -= 1;
  
            if (*xpos < 1) {
              *xpos = 1;
            }
  
            atouch_sethack(*xpos);
            ag_draw(agc(), &bg, 0, 0);
            char txt3[256];
            snprintf(txt3, 256, "<b>Fine Tuning Value : %i</b>", *xpos);
            int ty3 = agh() - (ag_fontheight(0) + (agdp() * 6));
            int tw3 = ag_txtwidth(txt3, 0);
            int tx3 = (agw() / 2) - (tw3 / 2);
            ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
            ag_sync();
          }
        }
        break;
  
      case ATEV_UP: {
          if (id == -2) {
            *xpos += 1;
  
            if (*xpos > 100) {
              *xpos = 100;
            }
  
            atouch_sethack(*xpos);
            ag_draw(agc(), &bg, 0, 0);
            char txt3[256];
            snprintf(txt3, 256, "<b>Fine Tuning Value : %i</b>", *xpos);
            int ty3 = agh() - (ag_fontheight(0) + (agdp() * 6));
            int tw3 = ag_txtwidth(txt3, 0);
            int tx3 = (agw() / 2) - (tw3 / 2);
            ag_text(agc(), tw3, tx3, ty3, txt3, 0xffff, 0);
            ag_sync();
          }
        }
        break;
  
      case ATEV_SELECT:
      case ATEV_BACK:
      case ATEV_MENU: {
          if (atev.d == 0) {
            ond = 0;
            res = 0;
          }
        }
        break;
    }
  }
  
  ag_ccanvas(&bg);
  return res;
  */
}

byte aw_calibmatrix(AW_CALIBPOINTP displayPtr, AW_CALIBPOINTP screenPtr, AW_CALIBMATRIXP matrixPtr) {
  return 0;
  /*
  byte retValue = 1;
  matrixPtr->Divider = ((screenPtr[0].x - screenPtr[2].x) * (screenPtr[1].y - screenPtr[2].y)) -
                       ((screenPtr[1].x - screenPtr[2].x) * (screenPtr[0].y - screenPtr[2].y)) ;
  
  if ( matrixPtr->Divider == 0 ) {
    retValue = 0;
  }
  else {
    matrixPtr->An = ((displayPtr[0].x - displayPtr[2].x) * (screenPtr[1].y - screenPtr[2].y)) -
                    ((displayPtr[1].x - displayPtr[2].x) * (screenPtr[0].y - screenPtr[2].y)) ;
    matrixPtr->Bn = ((screenPtr[0].x - screenPtr[2].x) * (displayPtr[1].x - displayPtr[2].x)) -
                    ((displayPtr[0].x - displayPtr[2].x) * (screenPtr[1].x - screenPtr[2].x)) ;
    matrixPtr->Cn = (screenPtr[2].x * displayPtr[1].x - screenPtr[1].x * displayPtr[2].x) * screenPtr[0].y +
                    (screenPtr[0].x * displayPtr[2].x - screenPtr[2].x * displayPtr[0].x) * screenPtr[1].y +
                    (screenPtr[1].x * displayPtr[0].x - screenPtr[0].x * displayPtr[1].x) * screenPtr[2].y ;
    matrixPtr->Dn = ((displayPtr[0].y - displayPtr[2].y) * (screenPtr[1].y - screenPtr[2].y)) -
                    ((displayPtr[1].y - displayPtr[2].y) * (screenPtr[0].y - screenPtr[2].y)) ;
    matrixPtr->En = ((screenPtr[0].x - screenPtr[2].x) * (displayPtr[1].y - displayPtr[2].y)) -
                    ((displayPtr[0].y - displayPtr[2].y) * (screenPtr[1].x - screenPtr[2].x)) ;
    matrixPtr->Fn = (screenPtr[2].x * displayPtr[1].y - screenPtr[1].x * displayPtr[2].y) * screenPtr[0].y +
                    (screenPtr[0].x * displayPtr[2].y - screenPtr[2].x * displayPtr[0].y) * screenPtr[1].y +
                    (screenPtr[1].x * displayPtr[0].y - screenPtr[0].x * displayPtr[1].y) * screenPtr[2].y ;
  }
  
  return retValue; */
} /* end of setCalibrationMatrix() */

byte aw_calibtools(AWINDOWP parent) {
  aw_alert(parent,
           "No Need Calibration",
           "This version doesn't need any calibration data...",
           NULL,
           NULL);
  return 1;
}
byte aw_calibtools_(AWINDOWP parent) {
  return 0;
  /*
  int USE_HACK = aw_confirm(
                   parent,
                   "Use alternative touch",
                   "Do you want to use alternative touch?\n  Only use if the default method does not work.\n\nPress the volume keys to select Yes or No.",
                   "@alert",
                   acfg_var.text_no,
                   acfg_var.text_yes
                 );
  int current_hack = atouch_gethack();
  int new_hack_val = 0;
  
  if (!USE_HACK) {
    atouch_sethack(20);
    new_hack_val = 20;
  }
  else {
    atouch_sethack(0);
  }
  
  //-- Set Mask
  CANVAS * tmpc = aw_muteparent(parent);
  on_dialog_window = 1;
  ag_rectopa(agc(), 0, 0, agw(), agh(), 0x0000, 220);
  ag_sync();
  byte isvalid = 0;
  //-- Initializing Canvas
  CANVAS ccv;
  ag_canvas(&ccv, agw(), agh());
  ag_blur(&ccv, agc(), agdp() * 2);
  atouch_plaincalibrate();
  int dp10    = agdp() * 20;
  int xpos[3] = {
    dp10, agw() - dp10, agw() / 2
  };
  int ypos[3] = {
    dp10, agh() / 2, agh() - dp10
  };
  int xtch[3] = { 0, 0, 0 };
  int ytch[3] = { 0, 0, 0 };
  AW_CALIBPOINT dPoint[3];
  AW_CALIBPOINT tPoint[3];
  int i;
  
  for (i = 0; i < 3; i++) {
    if (!aw_calibdraw(&ccv, i, xpos, ypos, xtch, ytch)) {
      goto doneit;
    }
  
    dPoint[i].x = ((float) xpos[i]) / 2;
    dPoint[i].y = ((float) ypos[i]) / 2;
    tPoint[i].x = ((float) xtch[i]) / 2;
    tPoint[i].y = ((float) ytch[i]) / 2;
  }
  
  AW_CALIBMATRIX matrix;
  
  if (aw_calibmatrix(dPoint, tPoint, &matrix)) {
    atouch_matrix_calibrate(&matrix);
  
    if (new_hack_val) {
      aw_calibdraw(&ccv, -2, &new_hack_val, ypos, xtch, ytch);
    }
    else {
      aw_calibdraw(&ccv, -1, xpos, ypos, xtch, ytch);
    }
  
    isvalid       = 1;
  }
  else {
    aw_alert(parent,
             "Calibrated Data",
             "Calibrated data not valid, please try again...",
             "@alert",
             NULL);
  }
  
  doneit:
  ag_ccanvas(&ccv);
  on_dialog_window = 0;
  aw_unmuteparent(parent, tmpc);
  byte dont_restore_caldata = 0;
  
  if (isvalid) {
    char data_calib[256];
    char datax[256];
    snprintf(datax, 256,
             "calibrate_matrix(\n"
             " \"%01.2f\",\"%01.2f\",\"%01.2f\",\"%01.2f\",\n"
             " \"%01.2f\",\"%01.2f\",\"%01.2f\",\"%i\"\n"
             ");",
             matrix.An, matrix.Bn, matrix.Cn,
             matrix.Dn, matrix.En, matrix.Fn,
             matrix.Divider, new_hack_val);
    snprintf(data_calib, 256,
             "Use/replace this command in <#009>aroma-config</#>:\n\n"
             "<#060>%s</#>\n\nLook at Recovery Log to copy this values.", datax);
    printf("\n\n\nCALIBRATION DATA\n================\nUse/replace this command in aroma-config\n%s\n\n\n",
           datax);
    aw_alert(parent,
             "Calibrated Data",
             data_calib,
             "@info",
             NULL);
    char msg_calib[256];
    snprintf(msg_calib, 256, "Do you want to use the current calibrated data in the current process?");
    dont_restore_caldata = aw_confirm(
                             parent,
                             "Calibration Data",
                             msg_calib,
                             "@info",
                             NULL,
                             NULL
                           );
  }
  
  if (!dont_restore_caldata) {
    atouch_sethack(current_hack);
    atouch_restorecalibrate();
  }
  
  return isvalid;*/
}
static const char * aw_theme_display_name(const char * id) {
  if (!id || id[0] == '\0' || strcmp(id, "generic") == 0) return "Generic";
  if (strcmp(id, "amoled") == 0)      return "AMOLED Modern";
  if (strcmp(id, "blacknwhite") == 0) return "Black & White";
  if (strcmp(id, "ics") == 0)         return "ICS Theme";
  if (strcmp(id, "miui4") == 0)       return "MIUI 4 Theme";
  if (strcmp(id, "miui") == 0)        return "MIUI Theme";
  if (strcmp(id, "touchwiz") == 0)    return "TouchWiz";
  if (strcmp(id, "franzyroy") == 0)   return "Franzyroy";
  return id;
}

static const char * aw_accent_display_name(const char * id) {
  if (!id || id[0] == '\0' || strcmp(id, "default") == 0) return "Theme Default";
  if (strcmp(id, "blue") == 0)    return "Pixel Blue";
  if (strcmp(id, "mono") == 0)    return "Monochrome White";
  if (strcmp(id, "emerald") == 0) return "Emerald Green";
  if (strcmp(id, "crimson") == 0) return "Crimson Red";
  if (strcmp(id, "amber") == 0)   return "Warm Amber";
  if (strcmp(id, "violet") == 0)  return "Electric Violet";
  if (strcmp(id, "cyan") == 0)    return "Ice Cyan";
  return id;
}

static const char * aw_font_display_name(const char * id) {
  if (!id || id[0] == '\0')                return "Google Sans";
  if (strcmp(id, "googlesans") == 0)       return "Google Sans";
  if (strcmp(id, "roboto") == 0)           return "Roboto";
  if (strcmp(id, "inter") == 0)            return "Inter";
  if (strcmp(id, "plusjakartasans") == 0)  return "Plus Jakarta Sans";
  if (strcmp(id, "outfit") == 0)           return "Outfit";
  if (strcmp(id, "ubuntu") == 0)           return "Ubuntu";
  if (strcmp(id, "jetbrainsmono") == 0)    return "JetBrains Mono";
  return id;
}

static const char * aw_transition_display_name(const char * id) {
  if (!id || id[0] == '\0')        return "Fade";
  if (strcmp(id, "fade") == 0)     return "Fade";
  if (strcmp(id, "slide") == 0)    return "Slide";
  if (strcmp(id, "stack") == 0)    return "Stack";
  if (strcmp(id, "zoom") == 0)     return "Zoom";
  if (strcmp(id, "none") == 0)     return "None (Instant)";
  return id;
}

void aw_about_dialog(AWINDOWP parent) {
  char unchkmsg[2048];
  char slot[16] = {0};
  aui_get_slot_suffix(slot, sizeof(slot));
  const char * backend_str = (ag_backend() == 2) ? "DRM/KMS" : ((ag_backend() == 1) ? "fbdev" : "Headless");

  snprintf(unchkmsg, sizeof(unchkmsg),
           "<b>%s %s</b>\n"
           "%s\n\n"
           "<b>Build:</b> <#selectbg_g>%s</#> (%s)\n"
           "<b>Origin:</b> %s\n"
           "<b>Support:</b> %s\n"
           "<u>%s</u>\n\n"
           "<b>Display:</b> %s  •  %dx%d (dp=%d)%s%s\n"
           "<b>Appearance:</b> %s  •  %s  •  %s\n\n"
           "<b>Package Name:</b>  <#selectbg_g>%s</#>\n"
           "<b>Version:</b>  <#selectbg_g>%s</#>\n"
           "<b>Author:</b>  <#selectbg_g>%s</#>\n"
           "<b>Device:</b>  <#selectbg_g>%s</#>\n"
           "<b>Date / Update:</b>  <#selectbg_g>%s</#>",
           AROMA_NAME,
           AROMA_VERSION,
           AROMA_COPY,
           AROMA_BUILD,
           AROMA_BUILD_CN,
           AROMA_BUILD_L,
           AROMA_BUILD_A,
           AROMA_BUILD_URL,
           backend_str, agw(), agh(), agdp(),
           slot[0] ? "  •  Slot " : "",
           slot[0] ? ((strcmp(slot, "_a") == 0) ? "A" : ((strcmp(slot, "_b") == 0) ? "B" : slot)) : "",
           aw_theme_display_name(acfg()->themename),
           aw_accent_display_name(aui_get_user_accent()),
           aw_font_display_name(aui_get_user_font()),
           acfg()->rom_name,
           acfg()->rom_version,
           acfg()->rom_author,
           acfg()->rom_device,
           acfg()->rom_date
          );
  aw_alert(parent,
           AROMA_NAME " " AROMA_VERSION,
           unchkmsg,
           "@info",
           NULL);
}

/* Stay-Open Live Preview Picker Dialog:
 * Tapping any item applies it LIVE & redraws the dialog in-place without closing,
 * preserving scroll position so the user can browse & try every option freely!
 * Only tapping "Done" closes the picker. Returns 1 if any change was applied. */
typedef void (*AW_LIVE_APPLY_FN)(int pick_idx, void * ctx);
static void aw_draw_bottom_sheet_bg(CANVAS * cv, int winX, int winY, int winW, int winH);

static byte aw_live_picker_loop(
  AWINDOWP parent,
  const char * title_str,
  const char ** titles,
  const char ** descs,
  int count,
  int active_idx,
  int height_pct,
  AW_LIVE_APPLY_FN apply_fn,
  void * ctx
) {
  if (count <= 0) {
    return 0;
  }

  CANVAS * tmpc = aw_muteparent(parent);
  on_dialog_window = 1;
  CANVAS * maskc = aw_maskparent();

  int saved_scroll = 0;
  byte any_applied = 0;
  byte first_show = 1;
  byte stay_open = 1;
  int last_winX = 0, last_winY = 0, last_winW = agw(), last_winH = agh();

  while (stay_open) {
    char title[64];
    snprintf(title, sizeof(title), "%s", title_str);
    int titW = ag_txtwidth(title, 1);
    int btnH = agdp() * 20;

    CANVAS alertbg;
    ag_canvas(&alertbg, agw(), agh());
    ag_draw(&alertbg, maskc, 0, 0);

    int listX, listY, listW, listH, btnX, btnY, btnW;

    if (aui_is_m3()) {
      /* Unified Material 3 Tall Modal Bottom Sheet (matches 3-dot & Appearance hub 1:1!) */
      int pad    = agdp() * 5;
      int topPad = agdp() * 10;
      int titH   = ag_fontheight(1) + (agdp() * 5);
      int maxLH  = agh() - aui_statusbar_height() - topPad - titH - btnH - (pad * 3) - (agdp() * 16);
      listH      = (agh() * height_pct) / 100;
      if (listH > maxLH && maxLH > agdp() * 40) listH = maxLH;

      int winW = agw();
      int winH = topPad + titH + listH + pad + btnH + pad;
      int winX = 0;
      int winY = agh() - winH;
      last_winX = winX; last_winY = winY; last_winW = winW; last_winH = winH;

      aw_draw_bottom_sheet_bg(&alertbg, winX, winY, winW, winH);
      ag_text(&alertbg, titW, (agw() - titW) / 2, winY + topPad, title, acfg_var.dlgtitlefg, 1);

      /* Align acmenu cards pixel-for-pixel with Done button (compensating acmenu's internal agdp()*4 margin) */
      int m3_inset = pad - (agdp() * 4);
      if (m3_inset < 0) m3_inset = 0;
      listX = m3_inset;
      listW = winW - (m3_inset * 2);
      listY = winY + topPad + titH;
      btnX  = pad;
      btnW  = winW - (pad * 2);
      btnY  = listY + listH + pad;
    }
    else {
      int pad  = agdp() * 4;
      int winW = agw() - (pad * 2);
      int titH = ag_fontheight(1) + (pad * 2);

      PNGCANVASP winp = atheme("img.dialog");
      PNGCANVASP titp = atheme("img.dialog.titlebar");
      APNG9 winv, titv;
      int vtitY = -1;
      int vpadB = pad;
      listX = pad * 2;
      listW = winW - (pad * 2);
      int tit_id = atheme_id("img.dialog.titlebar");
      int dlg_id = atheme_id("img.dialog");

      if (titp != NULL && tit_id >= 0 && acfg_var.theme_9p[tit_id] && apng9_calc(titp, &titv, 1)) {
        int tmptitH = titH - (pad * 2);
        titH  = tmptitH + (titv.t + titv.b);
        vtitY = titv.t;
      }
      if (winp != NULL && dlg_id >= 0 && acfg_var.theme_9p[dlg_id] && apng9_calc(winp, &winv, 1)) {
        listW = winW - (winv.l + winv.r);
        listX = pad + winv.l;
        vpadB = winv.b;
      }

      listH = (agh() * height_pct) / 100;
      int winH  = titH + listH + btnH + (pad * 2) + vpadB;
      int winX  = pad;
      int winY  = (agh() / 2) - (winH / 2);
      int titX  = (agw() / 2) - (titW / 2);
      int titY  = (vtitY != -1) ? (winY + vtitY) : (winY + pad);
      listY = winY + titH + pad;
      btnX  = listX;
      btnW  = listW;
      btnY  = listY + listH + pad;
      last_winX = winX; last_winY = winY; last_winW = winW; last_winH = winH;

      if (!atheme_draw("img.dialog", &alertbg, winX, winY, winW, winH)) {
        ag_roundgrad(&alertbg, winX - 1, winY - 1, winW + 2, winH + 2, acfg_var.border, acfg_var.border_g, (acfg_var.roundsz * agdp()) + 1);
        ag_roundgrad(&alertbg, winX, winY, winW, winH, acfg_var.dialogbg, acfg_var.dialogbg_g, acfg_var.roundsz * agdp());
      }
      if (!atheme_draw("img.dialog.titlebar", &alertbg, winX, winY, winW, titH)) {
        ag_roundgrad_ex(&alertbg, winX, winY, winW, titH, acfg_var.dlgtitlebg, acfg_var.dlgtitlebg_g, acfg_var.roundsz * agdp(), 1, 1, 0, 0);
      }
      aw_draw_dialog_border(&alertbg, winp, winX, winY, winW, winH, titH);
      ag_textf(&alertbg, titW, titX + 1, titY + 1, title, acfg_var.dlgtitlebg_g, 1);
      ag_text(&alertbg, titW, titX, titY, title, acfg_var.dlgtitlefg, 1);
    }

    AWINDOWP hWin = aw(&alertbg);
    ACONTROLP menu1 = acmenu(hWin, listX, listY, listW, listH, 6);
    if (aui_is_m3()) {
      acmenu_set_m3_colors(menu1, acfg_var.dialogbg, acfg_var.controlbg);
    }
    int i;
    for (i = 0; i < count; i++) {
      char tbuf[128];
      if (i == active_idx) {
        snprintf(tbuf, sizeof(tbuf), "<b>%s  • Active</b>", titles[i]);
      }
      else {
        snprintf(tbuf, sizeof(tbuf), "%s", titles[i]);
      }
      acmenu_add(menu1, tbuf, (char *) descs[i], "");
    }
    acmenu_set_active_index(menu1, active_idx);
    acmenu_setscroll(menu1, saved_scroll);
    ACONTROLP donebtn = acbutton(hWin, btnX, btnY, btnW, btnH, "Done", 0, 5);
    aw_show_ex2(hWin, first_show ? 5 : 4, 0, 0, agw(), agh(), donebtn);
    first_show = 0;

    byte ondispatch = 1;
    while (ondispatch) {
      dword msg = aw_dispatch(hWin);
      switch (aw_gm(msg)) {
        case 6: {
          int sel = acmenu_getselectedindex(menu1);
          saved_scroll = acmenu_getscroll(menu1);
          if (sel >= 0 && sel < count) {
            active_idx = sel;
            if (apply_fn) {
              apply_fn(sel, ctx);
              aui_sync_theme_and_font();
              any_applied = 1;
            }
          }
          ondispatch = 0; /* Rebuild dialog immediately with new live look! */
        } break;
        case 5:
          stay_open = 0;
          ondispatch = 0;
          break;
      }
    }

    aw_destroy(hWin);
    ag_ccanvas(&alertbg);
  }

  on_dialog_window = 0;
  aw_unmaskparent(parent, tmpc, maskc, 0, 0, agw(), agh());
  return any_applied;
}

static void aw_apply_theme_cb(int pick, void * ctx) {
  const char ** ids = (const char **) ctx;
  aui_set_user_theme(ids[pick]);
}

static byte aw_theme_dialog(AWINDOWP parent) {
  static const struct {
    const char * id;
    const char * title;
    const char * desc;
  } known_themes[] = {
    { "amoled",      "AMOLED Modern", "Pure OLED Black & Material 3 You Surfaces" },
    { "blacknwhite", "Black & White", "High-Contrast Monochrome by Miustone" },
    { "ics",         "ICS Theme",     "Ice Cream Sandwich by DemonWav & amarullz" },
    { "miui4",       "MIUI 4 Theme",  "MIUI 4/ICS by amarullz & Lennox" },
    { "miui",        "MIUI Theme",    "Classic MIUI by mickey-r & amarullz" },
    { "touchwiz",    "TouchWiz",      "TouchWiz by titanic_fanatic" },
    { "franzyroy",   "Franzyroy",     "Franzyroy by franzyroy" },
    { "",            "Generic",       "Unthemed Classic AROMA" }
  };

  const char * avail_ids[10];
  const char * avail_titles[10];
  const char * avail_descs[10];
  int avail_cnt = 0;
  int active_idx = -1;
  int i;
  for (i = 0; i < (int)(sizeof(known_themes) / sizeof(known_themes[0])); i++) {
    if (aui_theme_exists(known_themes[i].id)) {
      if (strcmp(acfg_var.themename, known_themes[i].id) == 0) {
        active_idx = avail_cnt;
      }
      avail_ids[avail_cnt]    = known_themes[i].id;
      avail_titles[avail_cnt] = known_themes[i].title;
      avail_descs[avail_cnt]  = known_themes[i].desc;
      avail_cnt++;
    }
  }
  return aw_live_picker_loop(parent, "Themes (Live Preview)", avail_titles, avail_descs, avail_cnt, active_idx, 58, aw_apply_theme_cb, (void *) avail_ids);
}

static void aw_apply_accent_cb(int pick, void * ctx) {
  const char ** ids = (const char **) ctx;
  aui_set_user_accent(ids[pick]);
}

static byte aw_accent_dialog(AWINDOWP parent) {
  static const struct {
    const char * id;
    const char * title;
    const char * desc;
  } accents[] = {
    { "default", "Theme Default",    "Use the active theme's native highlight colors" },
    { "blue",    "Pixel Blue",       "#3B82F6 — Modern Material You Blue" },
    { "mono",    "Monochrome White", "#EAEAEA — Crisp High-Contrast Silver/White" },
    { "emerald", "Emerald Green",    "#10B981 — Vibrant Material Emerald" },
    { "crimson", "Crimson Red",      "#EF4444 — Bold Red Accent" },
    { "amber",   "Warm Amber",       "#F59E0B — Golden Amber Highlight" },
    { "violet",  "Electric Violet",  "#8B5CF6 — Deep Material You Purple" },
    { "cyan",    "Ice Cyan",         "#06B6D4 — Cool Cyan Highlight" }
  };
  int cnt = (int)(sizeof(accents) / sizeof(accents[0]));
  const char * ids[10];
  const char * titles[10];
  const char * descs[10];
  const char * cur = aui_get_user_accent();
  int active_idx = 0;
  int i;
  for (i = 0; i < cnt; i++) {
    ids[i]    = accents[i].id;
    titles[i] = accents[i].title;
    descs[i]  = accents[i].desc;
    if (cur && strcmp(cur, accents[i].id) == 0) {
      active_idx = i;
    }
  }
  return aw_live_picker_loop(parent, "Accent Color (Live Preview)", titles, descs, cnt, active_idx, 58, aw_apply_accent_cb, (void *) ids);
}

typedef struct {
  const char ** ids;
  const char ** files;
} AW_FONT_CTX;

static void aw_apply_font_cb(int pick, void * ctx) {
  AW_FONT_CTX * fc = (AW_FONT_CTX *) ctx;
  aui_set_user_font(fc->ids[pick], fc->files[pick]);
}

static byte aw_font_dialog(AWINDOWP parent) {
  static const struct {
    const char * id;
    const char * file;
    const char * title;
    const char * desc;
  } known_fonts[] = {
    { "googlesans",      "GoogleSans-Regular.ttf",      "Google Sans",       "Modern Sans by Google (Default)" },
    { "roboto",          "Roboto-Regular.ttf",          "Roboto",            "Classic Android System Font" },
    { "inter",           "Inter-Regular.ttf",           "Inter",             "Clean High-Legibility UI Font" },
    { "plusjakartasans", "PlusJakartaSans-Regular.ttf", "Plus Jakarta Sans", "Geometric Modern Display Sans" },
    { "outfit",          "Outfit-Regular.ttf",          "Outfit",            "Minimalist Geometric Sans" },
    { "ubuntu",          "Ubuntu-Regular.ttf",          "Ubuntu",            "Humanist Sans by Canonical" },
    { "jetbrainsmono",   "JetBrainsMono-Regular.ttf",   "JetBrains Mono",    "Developer Monospace Font" }
  };

  const char * avail_ids[8];
  const char * avail_files[8];
  const char * avail_titles[8];
  const char * avail_descs[8];
  int avail_cnt = 0;
  int active_idx = -1;
  const char * cur_font = aui_get_user_font();
  int i;
  for (i = 0; i < (int)(sizeof(known_fonts) / sizeof(known_fonts[0])); i++) {
    if (aui_font_exists(known_fonts[i].file)) {
      if (cur_font && strcmp(cur_font, known_fonts[i].id) == 0) {
        active_idx = avail_cnt;
      }
      avail_ids[avail_cnt]    = known_fonts[i].id;
      avail_files[avail_cnt]  = known_fonts[i].file;
      avail_titles[avail_cnt] = known_fonts[i].title;
      avail_descs[avail_cnt]  = known_fonts[i].desc;
      avail_cnt++;
    }
  }
  AW_FONT_CTX fctx = { avail_ids, avail_files };
  return aw_live_picker_loop(parent, "Fonts (Live Preview)", avail_titles, avail_descs, avail_cnt, active_idx, 56, aw_apply_font_cb, &fctx);
}

static void aw_apply_transition_cb(int pick, void * ctx) {
  const char ** ids = (const char **) ctx;
  aui_set_user_transition(ids[pick]);
}

static byte aw_transition_dialog(AWINDOWP parent) {
  static const struct {
    const char * id;
    const char * title;
    const char * desc;
  } styles[] = {
    { "fade",  "Fade",           "Ultra-clean 60-120Hz smooth crossfade (Default)" },
    { "slide", "Slide",          "Classic smooth horizontal slide" },
    { "stack", "Stack",          "Depth card stack with scale & overlay" },
    { "zoom",  "Zoom",           "Modern Material-style zoom & fade transition" },
    { "none",  "None (Instant)", "Disable animations for instant screen switching" }
  };
  int cnt = (int)(sizeof(styles) / sizeof(styles[0]));
  const char * ids[8];
  const char * titles[8];
  const char * descs[8];
  const char * cur_tr = aui_get_user_transition();
  int active_idx = 0;
  int i;
  for (i = 0; i < cnt; i++) {
    ids[i]    = styles[i].id;
    titles[i] = styles[i].title;
    descs[i]  = styles[i].desc;
    if (cur_tr && strcmp(cur_tr, styles[i].id) == 0) {
      active_idx = i;
    }
  }
  return aw_live_picker_loop(parent, "Transitions (Live)", titles, descs, cnt, active_idx, 48, aw_apply_transition_cb, (void *) ids);
}

static void aw_draw_bottom_sheet_bg(CANVAS * cv, int winX, int winY, int winW, int winH) {
  if (aui_is_m3()) {
    int r = agdp() * 10;
    ag_roundgrad_ex(cv, winX, winY - 1, winW, winH + 1, acfg_var.border, acfg_var.border, r, 1, 1, 0, 0);
    ag_roundgrad_ex(cv, winX, winY, winW, winH, acfg_var.dialogbg, acfg_var.dialogbg, r, 1, 1, 0, 0);
    /* Material 3 centered drag-handle pill */
    int hw = agdp() * 24;
    int hh = max(agdp() * 2, 4);
    int hx = (agw() - hw) / 2;
    int hy = winY + (agdp() * 4);
    ag_roundgrad(cv, hx, hy, hw, hh, acfg_var.border, acfg_var.border, hh / 2);
  }
  else {
    ag_roundgrad_ex(cv, winX - 1, winY - 1, winW + 2, winH + 2, acfg_var.border, acfg_var.border_g, 0, 1, 1, 0, 0);
    ag_roundgrad_ex(cv, winX, winY, winW, winH, acfg_var.navbg, acfg_var.navbg_g, 0, 1, 1, 0, 0);
  }
}

static byte aw_themes_submenu(AWINDOWP parent) {
  byte any_changed = 0;
  byte keep_hub = 1;

  while (keep_hub) {
    CANVAS * tmpc = aw_muteparent(parent);
    on_dialog_window = 2;
    ag_rectopa(agc(), 0, 0, agw(), agh(), 0x0000, 180);

    int item_cnt = 5;
    int btnH   = agdp() * 20;
    int pad    = aui_is_m3() ? (agdp() * 5) : (agdp() * 2);
    int topPad = aui_is_m3() ? (agdp() * 10) : (agdp() * 2);
    int vpad   = aui_is_m3() ? (agdp() * 3) : (agdp() * 1);
    int winH   = topPad + ((btnH + vpad) * item_cnt) + pad;
    int winW   = agw();
    int winX   = 0;
    int winY   = agh() - winH;
    int btnY   = winY + topPad;
    int btnX   = winX + pad;
    int btnW   = winW - (pad * 2);

    CANVAS alertbg;
    ag_canvas(&alertbg, agw(), agh());
    ag_draw(&alertbg, agc(), 0, 0);
    aw_draw_bottom_sheet_bg(&alertbg, winX, winY, winW, winH);

    char lbl_theme[96], lbl_accent[96], lbl_font[96], lbl_tr[96];
    snprintf(lbl_theme,  sizeof(lbl_theme),  "Theme: %s",        aw_theme_display_name(acfg_var.themename));
    snprintf(lbl_accent, sizeof(lbl_accent), "Accent Color: %s", aw_accent_display_name(aui_get_user_accent()));
    snprintf(lbl_font,   sizeof(lbl_font),   "Font: %s",         aw_font_display_name(aui_get_user_font()));
    snprintf(lbl_tr,     sizeof(lbl_tr),     "Transition: %s",   aw_transition_display_name(aui_get_user_transition()));

    AWINDOWP hWin = aw(&alertbg);
    int row = 0;
    acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, lbl_theme,  0, 15);
    acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, lbl_accent, 0, 19);
    acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, lbl_font,   0, 16);
    acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, lbl_tr,     0, 18);
    ACONTROLP backbtn = acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, "Done", 0, 5);
    aw_show_ex(hWin, 1, winY - 1, backbtn);

    byte ondispatch = 1;
    byte pick = 0;
    while (ondispatch) {
      dword msg = aw_dispatch(hWin);
      switch (aw_gm(msg)) {
        case 5:  ondispatch = 0; break;
        case 15: pick = 1; ondispatch = 0; break;
        case 19: pick = 4; ondispatch = 0; break;
        case 16: pick = 2; ondispatch = 0; break;
        case 18: pick = 3; ondispatch = 0; break;
      }
    }

    aw_destroy(hWin);
    ag_ccanvas(&alertbg);
    on_dialog_window = 0;
    aw_unmuteparent(parent, tmpc);

    if (pick == 1) {
      if (aw_theme_dialog(parent)) any_changed = 1;
    }
    else if (pick == 4) {
      if (aw_accent_dialog(parent)) any_changed = 1;
    }
    else if (pick == 2) {
      if (aw_font_dialog(parent)) any_changed = 1;
    }
    else if (pick == 3) {
      aw_transition_dialog(parent);
    }
    else {
      keep_hub = 0;
    }
  }
  return any_changed ? 3 : 0;
}

byte aw_showmenu(AWINDOWP parent) {
  CANVAS * tmpc = aw_muteparent(parent);
  //-- Set Mask
  on_dialog_window = 2;
  ag_rectopa(agc(),   0, 0, agw(), agh(), 0x0000, 180);
  byte in_fm = afm_is_active();
  int item_cnt = in_fm ? 4 : 5;
  int btnH   = agdp() * 20;
  int pad    = aui_is_m3() ? (agdp() * 5) : (agdp() * 2);
  int topPad = aui_is_m3() ? (agdp() * 10) : (agdp() * 2);
  int vpad   = aui_is_m3() ? (agdp() * 3) : (agdp() * 1);
  int winH   = topPad + ((btnH + vpad) * item_cnt) + pad;
  int winW   = agw();
  int winX   = 0;
  int winY   = agh() - winH;
  int btnY   = winY + topPad;
  int btnX   = winX + pad;
  int btnW   = winW - (pad * 2);
  //-- Initializing Canvas
  CANVAS alertbg;
  ag_canvas(&alertbg, agw(), agh());
  ag_draw(&alertbg, agc(), 0, 0);
  //-- Draw Window Background (Material 3 Modal Bottom Sheet on AMOLED)
  aw_draw_bottom_sheet_bg(&alertbg, winX, winY, winW, winH);
  //-- Create Window (Top -> Bottom: About, Appearance, Filemanager, Exit, Back)
  AWINDOWP hWin   = aw(&alertbg);
  int row = 0;
  acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, acfg_var.text_about, 0, 11);
  acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, "Appearance", 0, 15);
  if (!in_fm) {
    acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, "Filemanager", 0, 17);
  }
  acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, "Exit", 0, 14);
  ACONTROLP backbtn = acbutton(hWin, btnX, btnY + ((btnH + vpad) * row++), btnW, btnH, acfg_var.text_back, 0, 5);
  /* Focus Back button subtly instead of screaming highlight on About */
  aw_show_ex(hWin, 1, winY - 1, backbtn);
  byte ondispatch = 1;
  byte res        = 0;
  
  while (ondispatch) {
    dword msg = aw_dispatch(hWin);
    
    switch (aw_gm(msg)) {
      case 5:
        ondispatch = 0;
        break;
        
      case 11:
        res = 1;
        ondispatch = 0;
        break;
        
      case 12:
        res = 2;
        ondispatch = 0;
        break;
        
      case 13:
        res = 3;
        ondispatch = 0;
        break;
        
      case 14:
        res = 4;
        ondispatch = 0;
        break;
        
      case 15:
        res = 5;
        ondispatch = 0;
        break;

      case 17:
        res = 7;
        ondispatch = 0;
        break;
    }
  }
  
  aw_destroy(hWin);
  ag_ccanvas(&alertbg);
  on_dialog_window = 0;
  aw_unmuteparent(parent, tmpc);
  
  if (res == 1) {
    aw_about_dialog(parent);
  }
  else if (res == 2) {
    aw_help_dialog(parent);
  }
  else if (res == 3) {
    aw_calibtools(parent);
  }
  else if (res == 4) {
    byte res = aw_confirm(parent, AROMA_NAME " " AROMA_VERSION, acfg_var.text_quit_msg, "@alert", NULL, NULL);
    
    if (res) {
      return 2;
    }
  }
  else if (res == 5) {
    return aw_themes_submenu(parent);
  }
  else if (res == 7) {
    afm_run(parent, NULL);
    return 3;
  }
  
  return 0;
}