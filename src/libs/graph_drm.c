/*
 * Copyright (C) 2026 AROMA-Next-Installer Contributors
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
 * Pure-ioctl Linux DRM/KMS dumb-buffer backend for AROMA-Next.
 * Zero external libdrm dependency so static arm64-v8a / armeabi-v7a
 * recovery binaries work directly against /dev/dri/card*.
 *
 * Supports Atomic KMS (double-buffered dumb buffers, disabling/restoring
 * recovery overlay planes, clean restore before RMFB so parent recovery
 * never hits -EINVAL on handback) with fallback to legacy SETCRTC + DIRTYFB,
 * Exynos/gs201 pixel format quirk handling, and inherited DRM master FDs.
 */

#ifndef __AROMA_GRAPH_DRM_C__
#define __AROMA_GRAPH_DRM_C__

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif
#ifndef DRM_MODE_TYPE_PREFERRED
#define DRM_MODE_TYPE_PREFERRED (1 << 3)
#endif

#define AROMA_DRM_MAX_EXTRA_PLANES 16

typedef struct {
  uint32_t plane_id;
  uint32_t prop_fb_id;
  uint32_t prop_crtc_id;
  uint32_t prop_src_x;
  uint32_t prop_src_y;
  uint32_t prop_src_w;
  uint32_t prop_src_h;
  uint32_t prop_crtc_x;
  uint32_t prop_crtc_y;
  uint32_t prop_crtc_w;
  uint32_t prop_crtc_h;
  uint64_t saved_fb_id;
  uint64_t saved_crtc_id;
  uint64_t saved_src_x;
  uint64_t saved_src_y;
  uint64_t saved_src_w;
  uint64_t saved_src_h;
  uint64_t saved_crtc_x;
  uint64_t saved_crtc_y;
  uint64_t saved_crtc_w;
  uint64_t saved_crtc_h;
} AROMA_DRM_PLANE_SAVED;

typedef struct {
  int                     fd;
  uint32_t                conn_id;
  uint32_t                crtc_id;
  uint32_t                crtc_idx;
  uint32_t                pixel_format;
  uint32_t                fb_ids[2];
  uint32_t                handles[2];
  uint32_t                pitch;
  uint32_t                width;
  uint32_t                height;
  uint64_t                size;
  unsigned char         * maps[2];
  unsigned char           has_buf1;
  int                     front_idx;
  struct drm_mode_modeinfo mode;
  struct drm_mode_crtc    saved_crtc;
  unsigned char           has_saved_crtc;
  unsigned char           is_master;
  unsigned char           inherited_fd;
  unsigned char           use_atomic;
  unsigned char           atomic_mode;
  unsigned char           is_exynos;
  uint32_t                mode_blob_id;
  uint32_t                conn_prop_crtc;
  uint32_t                crtc_prop_active;
  uint32_t                crtc_prop_mode_id;
  uint32_t                prim_plane_id;
  uint32_t                saved_prim_fb_id;
  uint32_t                prim_fb_id;
  uint32_t                prim_crtc_id;
  uint32_t                prim_src_x;
  uint32_t                prim_src_y;
  uint32_t                prim_src_w;
  uint32_t                prim_src_h;
  uint32_t                prim_crtc_x;
  uint32_t                prim_crtc_y;
  uint32_t                prim_crtc_w;
  uint32_t                prim_crtc_h;
  AROMA_DRM_PLANE_SAVED   extra_planes[AROMA_DRM_MAX_EXTRA_PLANES];
  int                     extra_plane_count;
  unsigned char           r_shift;
  unsigned char           g_shift;
  unsigned char           b_shift;
  unsigned char           a_shift;
} AROMA_DRM_CTX;

static AROMA_DRM_CTX ag_drm = { .fd = -1 };
static pthread_mutex_t ag_drm_lock = PTHREAD_MUTEX_INITIALIZER;

static int ag_drm_ioctl(int fd, unsigned long req, void * arg) {
  int r;
  do {
    r = ioctl(fd, req, arg);
  } while (r == -1 && (errno == EINTR || errno == EAGAIN));
  return r;
}

static uint32_t ag_drm_find_prop(int fd, uint32_t obj_id, uint32_t obj_type,
                                 const char * target_name, uint64_t * out_val) {
  struct drm_mode_obj_get_properties op;
  memset(&op, 0, sizeof(op));
  op.obj_id   = obj_id;
  op.obj_type = obj_type;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) != 0 || op.count_props == 0) {
    return 0;
  }

  uint32_t * props = calloc(op.count_props, sizeof(uint32_t));
  uint64_t * vals  = calloc(op.count_props, sizeof(uint64_t));
  if (!props || !vals) {
    free(props);
    free(vals);
    return 0;
  }

  op.props_ptr       = (uint64_t)(uintptr_t) props;
  op.prop_values_ptr = (uint64_t)(uintptr_t) vals;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) != 0) {
    free(props);
    free(vals);
    return 0;
  }

  uint32_t found_id = 0;
  uint32_t i;
  for (i = 0; i < op.count_props; i++) {
    struct drm_mode_get_property gp;
    memset(&gp, 0, sizeof(gp));
    gp.prop_id = props[i];
    if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &gp) == 0) {
      if (strcmp(gp.name, target_name) == 0) {
        found_id = props[i];
        if (out_val) {
          *out_val = vals[i];
        }
        break;
      }
    }
  }

  free(props);
  free(vals);
  return found_id;
}

static void ag_drm_push_prop(uint32_t * prop_ids, uint64_t * prop_vals, int * n_props,
                             int max_props, uint32_t id, uint64_t val) {
  if (id != 0 && *n_props < max_props) {
    prop_ids[*n_props]  = id;
    prop_vals[*n_props] = val;
    (*n_props)++;
  }
}

static int ag_drm_atomic_commit_internal(uint32_t fb_id,
                                         unsigned char set_plane_geom,
                                         unsigned char include_crtc,
                                         unsigned char include_extra,
                                         unsigned char restore_saved,
                                         uint32_t flags) {
  enum { MAX_PROPS = 256 };
  uint32_t objs[4 + AROMA_DRM_MAX_EXTRA_PLANES];
  uint32_t cnts[4 + AROMA_DRM_MAX_EXTRA_PLANES];
  uint32_t prop_ids[MAX_PROPS];
  uint64_t prop_vals[MAX_PROPS];
  int n_objs  = 0;
  int n_props = 0;

  if (ag_drm.prim_plane_id == 0 || ag_drm.prim_fb_id == 0) {
    errno = EINVAL;
    return -1;
  }

  if (include_crtc) {
    if (ag_drm.conn_prop_crtc != 0 && ag_drm.crtc_prop_active != 0 &&
        ag_drm.crtc_prop_mode_id != 0 && ag_drm.mode_blob_id != 0) {
      int sp = n_props;
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.conn_prop_crtc, ag_drm.crtc_id);
      objs[n_objs] = ag_drm.conn_id;
      cnts[n_objs] = (uint32_t)(n_props - sp);
      n_objs++;

      sp = n_props;
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.crtc_prop_active, 1);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.crtc_prop_mode_id, ag_drm.mode_blob_id);
      objs[n_objs] = ag_drm.crtc_id;
      cnts[n_objs] = (uint32_t)(n_props - sp);
      n_objs++;
    }
  }

  if (include_extra) {
    int k;
    for (k = 0; k < ag_drm.extra_plane_count; k++) {
      AROMA_DRM_PLANE_SAVED * ep = &ag_drm.extra_planes[k];
      if (ep->prop_fb_id == 0 || ep->prop_crtc_id == 0) {
        continue;
      }
      int sp = n_props;
      if (restore_saved) {
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_fb_id, ep->saved_fb_id);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_crtc_id, ep->saved_crtc_id);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_src_x, ep->saved_src_x);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_src_y, ep->saved_src_y);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_src_w, ep->saved_src_w);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_src_h, ep->saved_src_h);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_crtc_x, ep->saved_crtc_x);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_crtc_y, ep->saved_crtc_y);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_crtc_w, ep->saved_crtc_w);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_crtc_h, ep->saved_crtc_h);
      }
      else {
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_fb_id, 0);
        ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ep->prop_crtc_id, 0);
      }
      objs[n_objs] = ep->plane_id;
      cnts[n_objs] = (uint32_t)(n_props - sp);
      n_objs++;
    }
  }

  /* Primary plane properties */
  {
    int sp = n_props;
    objs[n_objs] = ag_drm.prim_plane_id;
    ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_fb_id, fb_id);
    if (set_plane_geom) {
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_crtc_id, ag_drm.crtc_id);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_src_x, 0);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_src_y, 0);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_src_w, ((uint64_t) ag_drm.width) << 16);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_src_h, ((uint64_t) ag_drm.height) << 16);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_crtc_x, 0);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_crtc_y, 0);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_crtc_w, ag_drm.width);
      ag_drm_push_prop(prop_ids, prop_vals, &n_props, MAX_PROPS, ag_drm.prim_crtc_h, ag_drm.height);
    }
    cnts[n_objs] = (uint32_t)(n_props - sp);
    n_objs++;
  }

  struct drm_mode_atomic atom;
  memset(&atom, 0, sizeof(atom));
  atom.flags           = flags;
  atom.count_objs      = (uint32_t) n_objs;
  atom.objs_ptr        = (uint64_t)(uintptr_t) objs;
  atom.count_props_ptr = (uint64_t)(uintptr_t) cnts;
  atom.props_ptr       = (uint64_t)(uintptr_t) prop_ids;
  atom.prop_values_ptr = (uint64_t)(uintptr_t) prop_vals;

  return ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_ATOMIC, &atom);
}

static unsigned char ag_drm_alloc_buf(int fd, int idx, uint32_t pref_fmt);

static void ag_drm_destroy_bufs(int fd) {
  int i;
  for (i = 0; i < 2; i++) {
    if (ag_drm.maps[i] != NULL && ag_drm.maps[i] != MAP_FAILED && ag_drm.size > 0) {
      munmap(ag_drm.maps[i], (size_t) ag_drm.size);
    }
    ag_drm.maps[i] = NULL;
    if (ag_drm.fb_ids[i] != 0) {
      uint32_t fb_id = ag_drm.fb_ids[i];
      ag_drm_ioctl(fd, DRM_IOCTL_MODE_RMFB, &fb_id);
      ag_drm.fb_ids[i] = 0;
    }
    if (ag_drm.handles[i] != 0) {
      struct drm_mode_destroy_dumb dreq;
      memset(&dreq, 0, sizeof(dreq));
      dreq.handle = ag_drm.handles[i];
      ag_drm_ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
      ag_drm.handles[i] = 0;
    }
  }
  ag_drm.has_buf1 = 0;
}

static uint32_t ag_drm_pick_plane_format(int fd, uint32_t plane_id, uint32_t cur_fmt) {
  struct drm_mode_get_plane p;
  memset(&p, 0, sizeof(p));
  p.plane_id = plane_id;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p) != 0 || p.count_format_types == 0) {
    return cur_fmt;
  }
  uint32_t * fmts = calloc(p.count_format_types, sizeof(uint32_t));
  if (!fmts) {
    return cur_fmt;
  }
  p.format_type_ptr = (uint64_t)(uintptr_t) fmts;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p) != 0) {
    free(fmts);
    return cur_fmt;
  }

  uint32_t j;
  for (j = 0; j < p.count_format_types; j++) {
    if (fmts[j] == cur_fmt) {
      free(fmts);
      return cur_fmt;
    }
  }

  static const uint32_t candidates[] = {
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_XBGR8888,
    DRM_FORMAT_ABGR8888,
#ifdef DRM_FORMAT_RGBA8888
    DRM_FORMAT_RGBA8888,
#endif
  };
  size_t c;
  for (c = 0; c < sizeof(candidates) / sizeof(candidates[0]); c++) {
    for (j = 0; j < p.count_format_types; j++) {
      if (fmts[j] == candidates[c]) {
        free(fmts);
        return candidates[c];
      }
    }
  }

  free(fmts);
  return cur_fmt;
}

static unsigned char ag_drm_setup_atomic(uint32_t * io_fb_id) {
  int fd = ag_drm.fd;
  struct drm_set_client_cap cap;

  memset(&cap, 0, sizeof(cap));
  cap.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES;
  cap.value      = 1;
  ag_drm_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);

  memset(&cap, 0, sizeof(cap));
  cap.capability = DRM_CLIENT_CAP_ATOMIC;
  cap.value      = 1;
  if (ag_drm_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap) != 0) {
    return 0;
  }

  struct drm_mode_get_plane_res pres;
  memset(&pres, 0, sizeof(pres));
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) != 0 || pres.count_planes == 0) {
    return 0;
  }

  uint32_t * plane_ids = calloc(pres.count_planes, sizeof(uint32_t));
  if (!plane_ids) {
    return 0;
  }
  pres.plane_id_ptr = (uint64_t)(uintptr_t) plane_ids;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) != 0) {
    free(plane_ids);
    return 0;
  }

  uint32_t i;
  uint32_t active_prim_id = 0, active_prim_fb = 0;
  uint32_t active_any_id  = 0, active_any_fb  = 0;
  uint32_t poss_prim_id   = 0, poss_prim_fb   = 0;
  ag_drm.prim_plane_id     = 0;
  ag_drm.saved_prim_fb_id  = 0;
  ag_drm.extra_plane_count = 0;

  /* Pass 1: find the plane already driving this CRTC so Qualcomm/SDE and
   * multi-primary-plane drivers don't pick an unused/incompatible SSPP plane.
   */
  for (i = 0; i < pres.count_planes; i++) {
    struct drm_mode_get_plane p;
    memset(&p, 0, sizeof(p));
    p.plane_id = plane_ids[i];
    if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p) != 0) {
      continue;
    }

    uint64_t ptype = 0;
    ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "type", &ptype);
    if (p.crtc_id == ag_drm.crtc_id && p.fb_id != 0) {
      if (ptype == 1 && active_prim_id == 0) {
        active_prim_id = p.plane_id;
        active_prim_fb = p.fb_id;
      }
      else if (active_any_id == 0) {
        active_any_id = p.plane_id;
        active_any_fb = p.fb_id;
      }
    }
    if (ptype == 1 && (p.possible_crtcs & (1u << ag_drm.crtc_idx)) && poss_prim_id == 0) {
      poss_prim_id = p.plane_id;
      poss_prim_fb = p.fb_id;
    }
  }

  if (active_prim_id != 0) {
    ag_drm.prim_plane_id    = active_prim_id;
    ag_drm.saved_prim_fb_id = active_prim_fb;
  }
  else if (active_any_id != 0) {
    ag_drm.prim_plane_id    = active_any_id;
    ag_drm.saved_prim_fb_id = active_any_fb;
  }
  else {
    ag_drm.prim_plane_id    = poss_prim_id;
    ag_drm.saved_prim_fb_id = poss_prim_fb;
  }

  /* Pass 2: record any remaining active planes on this CRTC as overlays */
  for (i = 0; i < pres.count_planes; i++) {
    if (plane_ids[i] == ag_drm.prim_plane_id) {
      continue;
    }
    struct drm_mode_get_plane p;
    memset(&p, 0, sizeof(p));
    p.plane_id = plane_ids[i];
    if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p) != 0) {
      continue;
    }
    if (p.crtc_id == ag_drm.crtc_id && p.fb_id != 0 &&
        ag_drm.extra_plane_count < AROMA_DRM_MAX_EXTRA_PLANES) {
      AROMA_DRM_PLANE_SAVED * ep = &ag_drm.extra_planes[ag_drm.extra_plane_count];
      memset(ep, 0, sizeof(*ep));
      ep->plane_id     = p.plane_id;
      ep->prop_fb_id   = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", &ep->saved_fb_id);
      ep->prop_crtc_id = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", &ep->saved_crtc_id);
      ep->prop_src_x   = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X", &ep->saved_src_x);
      ep->prop_src_y   = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y", &ep->saved_src_y);
      ep->prop_src_w   = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W", &ep->saved_src_w);
      ep->prop_src_h   = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H", &ep->saved_src_h);
      ep->prop_crtc_x  = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X", &ep->saved_crtc_x);
      ep->prop_crtc_y  = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y", &ep->saved_crtc_y);
      ep->prop_crtc_w  = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W", &ep->saved_crtc_w);
      ep->prop_crtc_h  = ag_drm_find_prop(fd, p.plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H", &ep->saved_crtc_h);
      if (ep->prop_fb_id && ep->prop_crtc_id) {
        ag_drm.extra_plane_count++;
      }
    }
  }
  free(plane_ids);

  if (ag_drm.prim_plane_id == 0) {
    fprintf(stdout, "aroma/w: DRM no suitable plane found for crtc=%u (idx=%u)\n",
            ag_drm.crtc_id, ag_drm.crtc_idx);
    return 0;
  }

  /* Ensure dumb buffer format is actually supported by the selected plane */
  uint32_t wanted_fmt = ag_drm_pick_plane_format(fd, ag_drm.prim_plane_id, ag_drm.pixel_format);
  if (wanted_fmt != 0 && wanted_fmt != ag_drm.pixel_format) {
    fprintf(stdout, "aroma/s: DRM switching buffer format 0x%08x -> 0x%08x for plane=%u\n",
            ag_drm.pixel_format, wanted_fmt, ag_drm.prim_plane_id);
    ag_drm_destroy_bufs(fd);
    if (!ag_drm_alloc_buf(fd, 0, wanted_fmt)) {
      return 0;
    }
    ag_drm.has_buf1  = ag_drm_alloc_buf(fd, 1, ag_drm.pixel_format);
    ag_drm.front_idx = 0;
    *io_fb_id        = ag_drm.fb_ids[0];
  }

  ag_drm.prim_fb_id   = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", NULL);
  ag_drm.prim_crtc_id = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", NULL);
  ag_drm.prim_src_x   = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X", NULL);
  ag_drm.prim_src_y   = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y", NULL);
  ag_drm.prim_src_w   = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W", NULL);
  ag_drm.prim_src_h   = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H", NULL);
  ag_drm.prim_crtc_x  = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X", NULL);
  ag_drm.prim_crtc_y  = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y", NULL);
  ag_drm.prim_crtc_w  = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W", NULL);
  ag_drm.prim_crtc_h  = ag_drm_find_prop(fd, ag_drm.prim_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H", NULL);

  ag_drm.conn_prop_crtc    = ag_drm_find_prop(fd, ag_drm.conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", NULL);
  ag_drm.crtc_prop_active  = ag_drm_find_prop(fd, ag_drm.crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", NULL);
  ag_drm.crtc_prop_mode_id = ag_drm_find_prop(fd, ag_drm.crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", NULL);

  struct drm_mode_create_blob b;
  memset(&b, 0, sizeof(b));
  b.data   = (uint64_t)(uintptr_t) &ag_drm.mode;
  b.length = sizeof(ag_drm.mode);
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &b) == 0) {
    ag_drm.mode_blob_id = b.blob_id;
  }

  struct {
    unsigned char set_geom;
    unsigned char inc_crtc;
    unsigned char inc_extra;
    uint32_t      flags;
  } attempts[4] = {
    { 1, 1, 1, DRM_MODE_ATOMIC_ALLOW_MODESET },
    { 1, 0, 1, 0 },
    { 1, 0, 0, 0 },
    { 0, 0, 0, 0 },
  };
  unsigned char a;
  unsigned char ok = 0;
  for (a = 0; a < 4; a++) {
    if (a == 3 && ag_drm.saved_prim_fb_id == 0) {
      continue;
    }
    if (ag_drm_atomic_commit_internal(*io_fb_id, attempts[a].set_geom, attempts[a].inc_crtc,
                                      attempts[a].inc_extra, 0, attempts[a].flags) == 0) {
      ag_drm.atomic_mode = a;
      ok = 1;
      break;
    }
    fprintf(stdout, "aroma/w: DRM Atomic commit attempt %u failed (errno=%d: %s)\n",
            a, errno, strerror(errno));
  }
  if (!ok) {
    fprintf(stdout, "aroma/w: DRM Atomic failed all attempts (plane=%u, saved_fb=%u, overlays=%d), falling back to SETCRTC\n",
            ag_drm.prim_plane_id, ag_drm.saved_prim_fb_id, ag_drm.extra_plane_count);
    return 0;
  }

  ag_drm.use_atomic = 1;
  fprintf(stdout, "aroma/s: DRM: Atomic KMS active (amode=%u, plane=%u, saved_fb=%u, overlays=%d, doublebuf=%u)\n",
          ag_drm.atomic_mode, ag_drm.prim_plane_id, ag_drm.saved_prim_fb_id, ag_drm.extra_plane_count, ag_drm.has_buf1);
  return 1;
}

static unsigned char ag_drm_alloc_buf(int fd, int idx, uint32_t pref_fmt) {
  struct drm_mode_create_dumb creq;
  memset(&creq, 0, sizeof(creq));
  creq.width  = ag_drm.width;
  creq.height = ag_drm.height;
  creq.bpp    = 32;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
    if (idx == 0) {
      fprintf(stdout, "aroma/w: DRM CREATE_DUMB[%d] failed (errno=%d: %s)\n",
              idx, errno, strerror(errno));
    }
    return 0;
  }
  ag_drm.handles[idx] = creq.handle;
  if (idx == 0) {
    ag_drm.pitch = creq.pitch;
    ag_drm.size  = creq.size;
  }

  /* Exynos/Google Tensor (gs201) DPU defines formats in byte-order rather than
   * little-endian bit order: RGBA8888=[R,G,B,A] (0,8,16,24) and XRGB8888=[X,R,G,B] (8,16,24,0).
   */
  struct {
    uint32_t fmt;
    unsigned char r, g, b, a;
  } fmts_exynos[] = {
#ifdef DRM_FORMAT_RGBA8888
    { DRM_FORMAT_RGBA8888, 0,  8,  16, 24 },
#endif
    { DRM_FORMAT_XRGB8888, 8,  16, 24, 0  },
    { DRM_FORMAT_ARGB8888, 8,  16, 24, 0  },
    { DRM_FORMAT_ABGR8888, 0,  8,  16, 24 },
  }, fmts_std[] = {
    { DRM_FORMAT_XRGB8888, 16, 8,  0,  24 },
    { DRM_FORMAT_XBGR8888, 0,  8,  16, 24 },
    { DRM_FORMAT_ABGR8888, 0,  8,  16, 24 },
    { DRM_FORMAT_ARGB8888, 16, 8,  0,  24 },
#ifdef DRM_FORMAT_RGBA8888
    { DRM_FORMAT_RGBA8888, 24, 16, 8,  0  },
#endif
  };

  size_t count = ag_drm.is_exynos ? (sizeof(fmts_exynos) / sizeof(fmts_exynos[0]))
                                  : (sizeof(fmts_std) / sizeof(fmts_std[0]));
  unsigned char fb_ok = 0;
  size_t fi;
  for (fi = 0; fi < count; fi++) {
    uint32_t fmt      = ag_drm.is_exynos ? fmts_exynos[fi].fmt : fmts_std[fi].fmt;
    unsigned char rs  = ag_drm.is_exynos ? fmts_exynos[fi].r   : fmts_std[fi].r;
    unsigned char gs  = ag_drm.is_exynos ? fmts_exynos[fi].g   : fmts_std[fi].g;
    unsigned char bs  = ag_drm.is_exynos ? fmts_exynos[fi].b   : fmts_std[fi].b;
    unsigned char as  = ag_drm.is_exynos ? fmts_exynos[fi].a   : fmts_std[fi].a;

    if (pref_fmt != 0 && fmt != pref_fmt) {
      continue;
    }
    struct drm_mode_fb_cmd2 fb2;
    memset(&fb2, 0, sizeof(fb2));
    fb2.width        = ag_drm.width;
    fb2.height       = ag_drm.height;
    fb2.pixel_format = fmt;
    fb2.handles[0]   = ag_drm.handles[idx];
    fb2.pitches[0]   = creq.pitch;
    if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb2) == 0) {
      ag_drm.fb_ids[idx] = fb2.fb_id;
      if (idx == 0) {
        ag_drm.pixel_format = fmt;
        ag_drm.r_shift      = rs;
        ag_drm.g_shift      = gs;
        ag_drm.b_shift      = bs;
        ag_drm.a_shift      = as;
      }
      fb_ok = 1;
      break;
    }
  }
  if (!fb_ok && pref_fmt == 0) {
    struct drm_mode_fb_cmd fbreq;
    memset(&fbreq, 0, sizeof(fbreq));
    fbreq.width  = ag_drm.width;
    fbreq.height = ag_drm.height;
    fbreq.pitch  = creq.pitch;
    fbreq.bpp    = 32;
    fbreq.depth  = 24;
    fbreq.handle = ag_drm.handles[idx];
    if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbreq) == 0) {
      ag_drm.fb_ids[idx]  = fbreq.fb_id;
      ag_drm.pixel_format = 0;
      ag_drm.r_shift      = 16;
      ag_drm.g_shift      = 8;
      ag_drm.b_shift      = 0;
      ag_drm.a_shift      = 24;
      fb_ok = 1;
    }
  }
  if (!fb_ok) {
    if (idx == 0) {
      fprintf(stdout, "aroma/w: DRM ADDFB/ADDFB2 failed (errno=%d: %s)\n", errno, strerror(errno));
    }
    return 0;
  }

  struct drm_mode_map_dumb mreq;
  memset(&mreq, 0, sizeof(mreq));
  mreq.handle = ag_drm.handles[idx];
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
    if (idx == 0) {
      fprintf(stdout, "aroma/w: DRM MAP_DUMB[%d] failed (errno=%d: %s)\n", idx, errno, strerror(errno));
    }
    return 0;
  }

  ag_drm.maps[idx] = (unsigned char *) mmap(0, (size_t) creq.size, PROT_READ | PROT_WRITE,
                                            MAP_SHARED, fd, (off_t) mreq.offset);
  if (ag_drm.maps[idx] == MAP_FAILED) {
    ag_drm.maps[idx] = NULL;
    if (idx == 0) {
      fprintf(stdout, "aroma/w: DRM mmap[%d] failed (errno=%d: %s)\n", idx, errno, strerror(errno));
    }
    return 0;
  }
  memset(ag_drm.maps[idx], 0, (size_t) creq.size);
  return 1;
}

static void ag_drm_free_ctx(void) {
  int i;
  for (i = 0; i < 2; i++) {
    if (ag_drm.maps[i] != NULL && ag_drm.maps[i] != MAP_FAILED && ag_drm.size > 0) {
      munmap(ag_drm.maps[i], (size_t) ag_drm.size);
    }
    ag_drm.maps[i] = NULL;
  }

  if (ag_drm.fd >= 0) {
    /* Restore parent recovery's scanout FB & planes BEFORE RMFB, otherwise
     * RMFB unbinds the active plane + CRTC and breaks recovery's Atomic commits.
     */
    if (ag_drm.use_atomic && ag_drm.saved_prim_fb_id != 0) {
      unsigned char s_geom  = (ag_drm.atomic_mode <= 2) ? 1 : 0;
      unsigned char s_crtc  = (ag_drm.atomic_mode == 0) ? 1 : 0;
      unsigned char s_extra = (ag_drm.atomic_mode <= 1) ? 1 : 0;
      uint32_t      s_flags = (ag_drm.atomic_mode == 0) ? DRM_MODE_ATOMIC_ALLOW_MODESET : 0;
      if (ag_drm_atomic_commit_internal(ag_drm.saved_prim_fb_id, s_geom, s_crtc, s_extra, 1, s_flags) != 0) {
        if (ag_drm_atomic_commit_internal(ag_drm.saved_prim_fb_id, 0, 0, 0, 1, 0) != 0) {
          fprintf(stdout, "aroma/w: DRM restore atomic commit failed (errno=%d: %s)\n",
                  errno, strerror(errno));
        }
      }
    }
    else if (ag_drm.has_saved_crtc && ag_drm.is_master && ag_drm.saved_crtc.fb_id != 0) {
      ag_drm.saved_crtc.set_connectors_ptr = (uint64_t)(uintptr_t) &ag_drm.conn_id;
      ag_drm.saved_crtc.count_connectors   = 1;
      ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_SETCRTC, &ag_drm.saved_crtc);
    }

    if (ag_drm.mode_blob_id != 0) {
      struct drm_mode_destroy_blob db;
      memset(&db, 0, sizeof(db));
      db.blob_id = ag_drm.mode_blob_id;
      ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_DESTROYPROPBLOB, &db);
      ag_drm.mode_blob_id = 0;
    }
    for (i = 0; i < 2; i++) {
      if (ag_drm.fb_ids[i] != 0) {
        uint32_t fb_id = ag_drm.fb_ids[i];
        ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_RMFB, &fb_id);
        ag_drm.fb_ids[i] = 0;
      }
      if (ag_drm.handles[i] != 0) {
        struct drm_mode_destroy_dumb dreq;
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = ag_drm.handles[i];
        ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        ag_drm.handles[i] = 0;
      }
    }
    if (!ag_drm.inherited_fd) {
      if (ag_drm.is_master) {
        ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_DROP_MASTER, NULL);
      }
      close(ag_drm.fd);
    }
    ag_drm.is_master = 0;
    ag_drm.fd = -1;
  }
  memset(&ag_drm, 0, sizeof(ag_drm));
  ag_drm.fd = -1;
}

static unsigned char ag_drm_setup_card(int fd, unsigned char inherited) {
  if (!inherited) {
    if (ag_drm_ioctl(fd, DRM_IOCTL_SET_MASTER, NULL) != 0) {
      fprintf(stdout, "aroma/w: DRM_IOCTL_SET_MASTER failed (errno=%d: %s)\n",
              errno, strerror(errno));
      return 0;
    }
  }
  ag_drm.is_master = 1;

  char drv_name[64];
  memset(drv_name, 0, sizeof(drv_name));
  struct drm_version ver;
  memset(&ver, 0, sizeof(ver));
  ver.name_len = sizeof(drv_name) - 1;
  ver.name     = drv_name;
  if (ag_drm_ioctl(fd, DRM_IOCTL_VERSION, &ver) == 0) {
    drv_name[sizeof(drv_name) - 1] = '\0';
    fprintf(stdout, "aroma/s: DRM: driver='%s'\n", drv_name);
    if (strstr(drv_name, "exynos") != NULL || strstr(drv_name, "dpu") != NULL) {
      ag_drm.is_exynos = 1;
    }
  }

  struct drm_mode_card_res res;
  memset(&res, 0, sizeof(res));
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
    fprintf(stdout, "aroma/w: DRM_IOCTL_MODE_GETRESOURCES failed (errno=%d: %s)\n",
            errno, strerror(errno));
    return 0;
  }
  if (res.count_connectors == 0 || res.count_crtcs == 0) {
    fprintf(stdout, "aroma/w: DRM card has no connectors/crtcs\n");
    return 0;
  }

  uint32_t * conns = calloc(res.count_connectors, sizeof(uint32_t));
  uint32_t * crtcs = calloc(res.count_crtcs, sizeof(uint32_t));
  uint32_t * encs  = res.count_encoders ? calloc(res.count_encoders, sizeof(uint32_t)) : NULL;
  uint32_t * fbs   = res.count_fbs ? calloc(res.count_fbs, sizeof(uint32_t)) : NULL;
  if (!conns || !crtcs) {
    free(conns);
    free(crtcs);
    free(encs);
    free(fbs);
    return 0;
  }

  res.connector_id_ptr = (uint64_t)(uintptr_t) conns;
  res.crtc_id_ptr      = (uint64_t)(uintptr_t) crtcs;
  res.encoder_id_ptr   = (uint64_t)(uintptr_t) encs;
  res.fb_id_ptr        = (uint64_t)(uintptr_t) fbs;

  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
    free(conns);
    free(crtcs);
    free(encs);
    free(fbs);
    return 0;
  }

  unsigned char found = 0;
  uint32_t i;
  for (i = 0; i < res.count_connectors && !found; i++) {
    struct drm_mode_get_connector conn;
    memset(&conn, 0, sizeof(conn));
    conn.connector_id = conns[i];
    if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) {
      continue;
    }
    if (conn.connection != DRM_MODE_CONNECTED || conn.count_modes == 0) {
      continue;
    }

    struct drm_mode_modeinfo * modes = calloc(conn.count_modes, sizeof(struct drm_mode_modeinfo));
    uint32_t * c_encs = conn.count_encoders ? calloc(conn.count_encoders, sizeof(uint32_t)) : NULL;
    uint32_t * props  = conn.count_props ? calloc(conn.count_props, sizeof(uint32_t)) : NULL;
    uint64_t * pvals  = conn.count_props ? calloc(conn.count_props, sizeof(uint64_t)) : NULL;
    if (!modes) {
      free(c_encs);
      free(props);
      free(pvals);
      continue;
    }

    conn.modes_ptr       = (uint64_t)(uintptr_t) modes;
    conn.encoders_ptr    = (uint64_t)(uintptr_t) c_encs;
    conn.props_ptr       = (uint64_t)(uintptr_t) props;
    conn.prop_values_ptr = (uint64_t)(uintptr_t) pvals;

    if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) == 0 && conn.count_modes > 0) {
      uint32_t selected_mode = 0;
      uint32_t m;
      for (m = 0; m < conn.count_modes; m++) {
        if (modes[m].type & DRM_MODE_TYPE_PREFERRED) {
          selected_mode = m;
          break;
        }
      }

      uint32_t chosen_crtc = 0;
      uint32_t chosen_crtc_idx = 0;
      if (conn.encoder_id != 0) {
        struct drm_mode_get_encoder enc;
        memset(&enc, 0, sizeof(enc));
        enc.encoder_id = conn.encoder_id;
        if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id != 0) {
          uint32_t c;
          chosen_crtc = enc.crtc_id;
          for (c = 0; c < res.count_crtcs; c++) {
            if (crtcs[c] == chosen_crtc) {
              chosen_crtc_idx = c;
              break;
            }
          }
        }
      }
      if (chosen_crtc == 0 && c_encs != NULL) {
        uint32_t e;
        for (e = 0; e < conn.count_encoders && chosen_crtc == 0; e++) {
          struct drm_mode_get_encoder enc;
          memset(&enc, 0, sizeof(enc));
          enc.encoder_id = c_encs[e];
          if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
            uint32_t c;
            for (c = 0; c < res.count_crtcs; c++) {
              if (enc.possible_crtcs & (1u << c)) {
                chosen_crtc = crtcs[c];
                chosen_crtc_idx = c;
                break;
              }
            }
          }
        }
      }

      if (chosen_crtc != 0 && modes[selected_mode].hdisplay > 0 && modes[selected_mode].vdisplay > 0) {
        ag_drm.conn_id  = conn.connector_id;
        ag_drm.crtc_id  = chosen_crtc;
        ag_drm.crtc_idx = chosen_crtc_idx;
        ag_drm.mode     = modes[selected_mode];
        ag_drm.width    = ag_drm.mode.hdisplay;
        ag_drm.height   = ag_drm.mode.vdisplay;
        found = 1;
      }
    }

    free(modes);
    free(c_encs);
    free(props);
    free(pvals);
  }

  free(conns);
  free(crtcs);
  free(encs);
  free(fbs);

  if (!found) {
    fprintf(stdout, "aroma/w: DRM no connected display mode found\n");
    return 0;
  }

  memset(&ag_drm.saved_crtc, 0, sizeof(ag_drm.saved_crtc));
  ag_drm.saved_crtc.crtc_id = ag_drm.crtc_id;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &ag_drm.saved_crtc) == 0) {
    ag_drm.has_saved_crtc = 1;
    if (ag_drm.saved_crtc.mode_valid &&
        ag_drm.saved_crtc.mode.hdisplay > 0 &&
        ag_drm.saved_crtc.mode.vdisplay > 0) {
      ag_drm.mode   = ag_drm.saved_crtc.mode;
      ag_drm.width  = ag_drm.saved_crtc.mode.hdisplay;
      ag_drm.height = ag_drm.saved_crtc.mode.vdisplay;
    }
  }

  if (!ag_drm_alloc_buf(fd, 0, 0)) {
    return 0;
  }
  ag_drm.has_buf1 = ag_drm_alloc_buf(fd, 1, ag_drm.pixel_format);
  ag_drm.front_idx = 0;

  if (ag_drm_setup_atomic(&ag_drm.fb_ids[0])) {
    return 1;
  }

  struct drm_mode_crtc setcrtc;
  memset(&setcrtc, 0, sizeof(setcrtc));
  setcrtc.crtc_id            = ag_drm.crtc_id;
  setcrtc.fb_id              = ag_drm.fb_ids[0];
  setcrtc.set_connectors_ptr = (uint64_t)(uintptr_t) &ag_drm.conn_id;
  setcrtc.count_connectors   = 1;
  setcrtc.mode               = ag_drm.mode;
  setcrtc.mode_valid         = 1;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &setcrtc) != 0) {
    fprintf(stdout, "aroma/w: DRM_IOCTL_MODE_SETCRTC failed (errno=%d: %s)\n", errno, strerror(errno));
    return 0;
  }

  return 1;
}

static int ag_drm_find_inherited_fd(char * out_path, size_t out_sz) {
  DIR * d = opendir("/proc/self/fd");
  if (!d) {
    return -1;
  }
  struct dirent * de;
  char lp[64], target[128];
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] < '0' || de->d_name[0] > '9') {
      continue;
    }
    int fd = atoi(de->d_name);
    if (fd <= 2) {
      continue;
    }
    snprintf(lp, sizeof(lp), "/proc/self/fd/%d", fd);
    ssize_t len = readlink(lp, target, sizeof(target) - 1);
    if (len > 0) {
      target[len] = '\0';
      if (strncmp(target, "/dev/dri/card", 13) == 0) {
        snprintf(out_path, out_sz, "%s", target);
        closedir(d);
        return fd;
      }
    }
  }
  closedir(d);
  return -1;
}

unsigned char ag_drm_init(int * out_w, int * out_h, int * out_pitch, unsigned char ** out_map, unsigned char * out_shifts) {
  char path[128];
  int inh_fd = ag_drm_find_inherited_fd(path, sizeof(path));
  if (inh_fd >= 0) {
    fprintf(stdout, "aroma/s: DRM: Found inherited fd=%d (%s)\n", inh_fd, path);
    memset(&ag_drm, 0, sizeof(ag_drm));
    ag_drm.fd           = inh_fd;
    ag_drm.inherited_fd = 1;
    if (ag_drm_setup_card(inh_fd, 1)) {
      *out_w        = (int) ag_drm.width;
      *out_h        = (int) ag_drm.height;
      *out_pitch    = (int) ag_drm.pitch;
      *out_map      = ag_drm.maps[0];
      out_shifts[0] = ag_drm.r_shift;
      out_shifts[1] = ag_drm.g_shift;
      out_shifts[2] = ag_drm.b_shift;
      out_shifts[3] = ag_drm.a_shift;
      fprintf(stdout, "aroma/s: DRM: Initialized via inherited fd=%d (fmt=0x%08x, %ux%u, pitch=%u, shifts=%u/%u/%u/%u)\n",
              inh_fd, ag_drm.pixel_format, ag_drm.width, ag_drm.height, ag_drm.pitch,
              ag_drm.r_shift, ag_drm.g_shift, ag_drm.b_shift, ag_drm.a_shift);
      return 1;
    }
    ag_drm_free_ctx();
  }

  int card;
  for (card = 0; card < 4; card++) {
    snprintf(path, sizeof(path), "/dev/dri/card%d", card);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }
    memset(&ag_drm, 0, sizeof(ag_drm));
    ag_drm.fd           = fd;
    ag_drm.inherited_fd = 0;
    if (ag_drm_setup_card(fd, 0)) {
      *out_w        = (int) ag_drm.width;
      *out_h        = (int) ag_drm.height;
      *out_pitch    = (int) ag_drm.pitch;
      *out_map      = ag_drm.maps[0];
      out_shifts[0] = ag_drm.r_shift;
      out_shifts[1] = ag_drm.g_shift;
      out_shifts[2] = ag_drm.b_shift;
      out_shifts[3] = ag_drm.a_shift;
      fprintf(stdout, "aroma/s: DRM: Initialized %s (fmt=0x%08x, %ux%u, pitch=%u, shifts=%u/%u/%u/%u)\n",
              path, ag_drm.pixel_format, ag_drm.width, ag_drm.height, ag_drm.pitch,
              ag_drm.r_shift, ag_drm.g_shift, ag_drm.b_shift, ag_drm.a_shift);
      return 1;
    }
    ag_drm_free_ctx();
  }
  return 0;
}

void ag_drm_flush(void) {
  pthread_mutex_lock(&ag_drm_lock);
  if (ag_drm.fd < 0 || ag_drm.fb_ids[0] == 0) {
    pthread_mutex_unlock(&ag_drm_lock);
    return;
  }

  int next_idx = ag_drm.has_buf1 ? (ag_drm.front_idx ^ 1) : 0;
  if (next_idx == 1 && ag_drm.maps[1] != NULL && ag_drm.maps[0] != NULL) {
    memcpy(ag_drm.maps[1], ag_drm.maps[0], (size_t) ag_drm.size);
  }
  ag_drm.front_idx = next_idx;
  uint32_t fb_id = ag_drm.fb_ids[next_idx];

  if (ag_drm.use_atomic) {
    if (ag_drm_atomic_commit_internal(fb_id, 0, 0, 0, 0, 0) != 0 && errno == EINVAL) {
      unsigned char f_geom  = (ag_drm.atomic_mode <= 2) ? 1 : 0;
      unsigned char f_crtc  = (ag_drm.atomic_mode == 0) ? 1 : 0;
      unsigned char f_extra = (ag_drm.atomic_mode <= 1) ? 1 : 0;
      uint32_t      f_flags = (ag_drm.atomic_mode == 0) ? DRM_MODE_ATOMIC_ALLOW_MODESET : 0;
      ag_drm_atomic_commit_internal(fb_id, f_geom, f_crtc, f_extra, 0, f_flags);
    }
  }
  else {
    struct drm_mode_fb_dirty_cmd dirty;
    struct drm_clip_rect clip;
    memset(&dirty, 0, sizeof(dirty));
    clip.x1 = 0;
    clip.y1 = 0;
    clip.x2 = (unsigned short) ag_drm.width;
    clip.y2 = (unsigned short) ag_drm.height;
    dirty.fb_id     = fb_id;
    dirty.num_clips = 1;
    dirty.clips_ptr = (uint64_t)(uintptr_t) &clip;
    ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
  }
  pthread_mutex_unlock(&ag_drm_lock);
}

void ag_drm_close(void) {
  pthread_mutex_lock(&ag_drm_lock);
  ag_drm_free_ctx();
  pthread_mutex_unlock(&ag_drm_lock);
}

#endif /* __AROMA_GRAPH_DRM_C__ */
