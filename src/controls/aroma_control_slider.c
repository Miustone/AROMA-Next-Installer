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
 * Descriptions:
 * -------------
 * AROMA-Next UI: Interactive Touch Slider Window Control
 */
#include "../aroma.h"

typedef struct {
  CANVAS  control;
  int     min_val;
  int     max_val;
  int     step;
  int     val;
  char    unit[32];
  byte    dragging;
  byte    focused;
  int     track_x;
  int     track_y;
  int     track_w;
  int     track_h;
} ACSLIDERD, * ACSLIDERDP;

static int acslider_clamp_snap(ACSLIDERDP d, int v) {
  if (d->step < 1) {
    d->step = 1;
  }
  if (v <= d->min_val) {
    return d->min_val;
  }
  if (v >= d->max_val) {
    return d->max_val;
  }
  int offset = v - d->min_val;
  int snapped = d->min_val + ((offset + (d->step / 2)) / d->step) * d->step;
  if (snapped < d->min_val) snapped = d->min_val;
  if (snapped > d->max_val) snapped = d->max_val;
  return snapped;
}

static void acslider_render(ACONTROLP ctl) {
  ACSLIDERDP d = (ACSLIDERDP) ctl->d;
  int w = ctl->w;
  int h = ctl->h;
  int pad = agdp() * 6;
  int rsz = max(acfg()->roundsz * agdp(), agdp() * 4);

  /* Copy parent window background cleanly */
  ag_draw_ex(&d->control, ctl->win->bg, 0, 0, ctl->x, ctl->y, w, h);

  /* Elevated modern card container */
  color card_border = d->dragging ? acfg()->selectbg : acfg()->border;
  color card_bg     = acfg()->dialogbg;
  color card_bg_g   = acfg()->dialogbg_g;
  if (card_bg == acfg()->winbg && card_bg_g == acfg()->winbg_g) {
    card_bg   = acfg()->controlbg;
    card_bg_g = acfg()->controlbg_g;
  }
  ag_roundgrad(&d->control, 0, 0, w, h, card_border, acfg()->border_g, rsz);
  ag_roundgrad(&d->control, 1, 1, w - 2, h - 2, card_bg, card_bg_g, max(rsz - 1, 1));

  /* Large live value readout centered at top */
  char valbuf[96];
  if (d->unit[0] != '\0') {
    snprintf(valbuf, sizeof(valbuf), "%d %s", d->val, d->unit);
  }
  else {
    snprintf(valbuf, sizeof(valbuf), "%d", d->val);
  }
  int valW = ag_txtwidth(valbuf, 1);
  int valH = ag_fontheight(1);
  int valX = (w - valW) / 2;
  int valY = pad;
  color accent = (acfg()->selectbg != acfg()->winbg) ? acfg()->selectbg : acfg()->winfg;
  color accent_g = (acfg()->progressglow != acfg()->winbg) ? acfg()->progressglow : accent;
  ag_text(&d->control, valW, valX, valY, valbuf, acfg()->dialogfg, 1);

  /* Track geometry */
  int thumb_r = agdp() * 7;
  int track_h = agdp() * 4;
  if (track_h < 6) track_h = 6;
  int track_x = pad + thumb_r;
  int track_w = w - ((pad + thumb_r) * 2);
  if (track_w < agdp() * 20) track_w = agdp() * 20;
  int track_y = valY + valH + (agdp() * 11);

  d->track_x = track_x;
  d->track_y = track_y;
  d->track_w = track_w;
  d->track_h = track_h;

  /* Inactive track */
  ag_roundgrad(&d->control, track_x - 1, track_y - 1, track_w + 2, track_h + 2,
               acfg()->border, acfg()->border_g, (track_h / 2) + 1);
  ag_roundgrad(&d->control, track_x, track_y, track_w, track_h,
               acfg()->controlbg, acfg()->controlbg_g, track_h / 2);

  /* Active filled track */
  int span = d->max_val - d->min_val;
  float ratio = (span > 0) ? ((float)(d->val - d->min_val) / (float)span) : 0.0f;
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  int fill_w = (int)(ratio * (float)track_w + 0.5f);
  if (fill_w > 0) {
    ag_roundgrad(&d->control, track_x, track_y, fill_w, track_h, accent, accent_g, track_h / 2);
  }

  /* Tactile thumb knob */
  int thumb_cx = track_x + fill_w;
  int thumb_cy = track_y + (track_h / 2);
  int tr = d->dragging ? (thumb_r + agdp()) : thumb_r;
  ag_roundgrad(&d->control, thumb_cx - tr, thumb_cy - tr, tr * 2, tr * 2,
               acfg()->border, acfg()->border_g, tr);
  ag_roundgrad(&d->control, thumb_cx - tr + 1, thumb_cy - tr + 1, (tr * 2) - 2, (tr * 2) - 2,
               accent, accent_g, max(tr - 1, 1));
  int inner_r = max(tr / 3, 2);
  ag_roundgrad(&d->control, thumb_cx - inner_r, thumb_cy - inner_r, inner_r * 2, inner_r * 2,
               acfg()->selectfg, acfg()->selectfg, inner_r);

  /* Min / Step / Max labels beneath track */
  char minbuf[48], maxbuf[48], stepbuf[48];
  if (d->unit[0] != '\0') {
    snprintf(minbuf, sizeof(minbuf), "%d %s", d->min_val, d->unit);
    snprintf(maxbuf, sizeof(maxbuf), "%d %s", d->max_val, d->unit);
  }
  else {
    snprintf(minbuf, sizeof(minbuf), "%d", d->min_val);
    snprintf(maxbuf, sizeof(maxbuf), "%d", d->max_val);
  }
  snprintf(stepbuf, sizeof(stepbuf), "Step: %d", d->step);

  int lblY = track_y + track_h + thumb_r + (agdp() * 3);
  int minW = ag_txtwidth(minbuf, 0);
  int maxW = ag_txtwidth(maxbuf, 0);
  int stpW = ag_txtwidth(stepbuf, 0);
  ag_text(&d->control, minW, pad, lblY, minbuf, acfg()->textfg_gray, 0);
  ag_text(&d->control, stpW, (w - stpW) / 2, lblY, stepbuf, acfg()->textfg_gray, 0);
  ag_text(&d->control, maxW, w - pad - maxW, lblY, maxbuf, acfg()->textfg_gray, 0);
}

static void acslider_update_from_x(ACONTROLP ctl, int touch_x) {
  ACSLIDERDP d = (ACSLIDERDP) ctl->d;
  int rel_x = touch_x - ctl->x - d->track_x;
  if (rel_x < 0) rel_x = 0;
  if (rel_x > d->track_w) rel_x = d->track_w;
  int span = d->max_val - d->min_val;
  int raw = d->min_val;
  if (d->track_w > 0 && span > 0) {
    raw = d->min_val + (int)(((double)rel_x / (double)d->track_w) * (double)span + 0.5);
  }
  int next = acslider_clamp_snap(d, raw);
  if (next != d->val) {
    d->val = next;
  }
}

void acslider_ondraw(void * x) {
  ACONTROLP ctl = (ACONTROLP) x;
  ACSLIDERDP d  = (ACSLIDERDP) ctl->d;
  acslider_render(ctl);
  ag_draw(&ctl->win->c, &d->control, ctl->x, ctl->y);
}

dword acslider_oninput(void * x, int action, ATEV * atev) {
  ACONTROLP ctl = (ACONTROLP) x;
  ACSLIDERDP d  = (ACSLIDERDP) ctl->d;
  dword msg = 0;

  switch (action) {
    case ATEV_MOUSEDN: {
      vibrate(20);
      d->dragging = 1;
      acslider_update_from_x(ctl, atev->x);
      ctl->ondraw(ctl);
      msg = aw_msg(0, 1, 1, 0);
    } break;

    case ATEV_MOUSEMV: {
      if (d->dragging) {
        acslider_update_from_x(ctl, atev->x);
        ctl->ondraw(ctl);
        msg = aw_msg(0, 1, 1, 0);
      }
    } break;

    case ATEV_MOUSEUP: {
      if (d->dragging) {
        acslider_update_from_x(ctl, atev->x);
        d->dragging = 0;
        ctl->ondraw(ctl);
        msg = aw_msg(0, 1, 0, 0);
      }
    } break;

    case ATEV_LEFT:
    case ATEV_DOWN: {
      if (!atev->d) {
        acslider_step(ctl, -1);
        msg = aw_msg(0, 1, 0, 0);
      }
    } break;

    case ATEV_RIGHT:
    case ATEV_UP: {
      if (!atev->d) {
        acslider_step(ctl, 1);
        msg = aw_msg(0, 1, 0, 0);
      }
    } break;
  }

  return msg;
}

void acslider_ondestroy(void * x) {
  ACONTROLP ctl = (ACONTROLP) x;
  ACSLIDERDP d  = (ACSLIDERDP) ctl->d;
  ag_ccanvas(&d->control);
  free(ctl->d);
}

byte acslider_onfocus(void * x) {
  ACONTROLP ctl = (ACONTROLP) x;
  ACSLIDERDP d  = (ACSLIDERDP) ctl->d;
  d->focused = 1;
  ctl->ondraw(ctl);
  return 1;
}

void acslider_onblur(void * x) {
  ACONTROLP ctl = (ACONTROLP) x;
  ACSLIDERDP d  = (ACSLIDERDP) ctl->d;
  d->focused = 0;
  ctl->ondraw(ctl);
}

int acslider_getvalue(ACONTROLP ctl) {
  if (ctl == NULL || ctl->d == NULL) return 0;
  return ((ACSLIDERDP) ctl->d)->val;
}

void acslider_setvalue(ACONTROLP ctl, int val) {
  if (ctl == NULL || ctl->d == NULL) return;
  ACSLIDERDP d = (ACSLIDERDP) ctl->d;
  d->val = acslider_clamp_snap(d, val);
  ctl->ondraw(ctl);
}

void acslider_step(ACONTROLP ctl, int delta_steps) {
  if (ctl == NULL || ctl->d == NULL) return;
  ACSLIDERDP d = (ACSLIDERDP) ctl->d;
  int next = d->val + (delta_steps * d->step);
  d->val = acslider_clamp_snap(d, next);
  ctl->ondraw(ctl);
}

ACONTROLP acslider(
  AWINDOWP win,
  int x, int y, int w, int h,
  int min_val, int max_val, int step, int initial_val,
  const char * unit
) {
  if (max_val < min_val) {
    int tmp = max_val;
    max_val = min_val;
    min_val = tmp;
  }
  if (step < 1) {
    step = 1;
  }

  ACSLIDERDP d = (ACSLIDERDP) calloc(1, sizeof(ACSLIDERD));
  d->min_val = min_val;
  d->max_val = max_val;
  d->step    = step;
  d->val     = acslider_clamp_snap(d, initial_val);
  if (unit != NULL) {
    snprintf(d->unit, sizeof(d->unit), "%s", unit);
  }

  ag_canvas(&d->control, w, h);

  ACONTROLP ctl = (ACONTROLP) calloc(1, sizeof(ACONTROL));
  ctl->ondestroy = &acslider_ondestroy;
  ctl->oninput   = &acslider_oninput;
  ctl->ondraw    = &acslider_ondraw;
  ctl->onblur    = &acslider_onblur;
  ctl->onfocus   = &acslider_onfocus;
  ctl->win       = win;
  ctl->x         = x;
  ctl->y         = y;
  ctl->w         = w;
  ctl->h         = h;
  ctl->forceNS   = 1;
  ctl->d         = (void *) d;

  acslider_render(ctl);
  aw_add(win, ctl);
  return ctl;
}
