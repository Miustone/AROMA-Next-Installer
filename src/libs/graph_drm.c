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
 */

#ifndef __AROMA_GRAPH_DRM_C__
#define __AROMA_GRAPH_DRM_C__

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif
#ifndef DRM_MODE_TYPE_PREFERRED
#define DRM_MODE_TYPE_PREFERRED (1 << 3)
#endif

typedef struct {
  int                    fd;
  uint32_t               conn_id;
  uint32_t               crtc_id;
  uint32_t               fb_id;
  uint32_t               handle;
  uint32_t               pitch;
  uint32_t               width;
  uint32_t               height;
  uint64_t               size;
  unsigned char        * map;
  struct drm_mode_modeinfo mode;
  struct drm_mode_crtc     saved_crtc;
  unsigned char          has_saved_crtc;
  unsigned char          is_master;
} AROMA_DRM_CTX;

static AROMA_DRM_CTX ag_drm = { .fd = -1 };

static int ag_drm_ioctl(int fd, unsigned long req, void * arg) {
  int r;
  do {
    r = ioctl(fd, req, arg);
  } while (r == -1 && (errno == EINTR || errno == EAGAIN));
  return r;
}

static void ag_drm_free_ctx(void) {
  if (ag_drm.map != NULL && ag_drm.map != MAP_FAILED && ag_drm.size > 0) {
    munmap(ag_drm.map, (size_t) ag_drm.size);
  }
  ag_drm.map = NULL;

  if (ag_drm.fd >= 0) {
    if (ag_drm.has_saved_crtc && ag_drm.is_master) {
      ag_drm.saved_crtc.set_connectors_ptr = (uint64_t)(uintptr_t) &ag_drm.conn_id;
      ag_drm.saved_crtc.count_connectors = 1;
      ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_SETCRTC, &ag_drm.saved_crtc);
    }
    if (ag_drm.fb_id != 0) {
      uint32_t fb_id = ag_drm.fb_id;
      ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_RMFB, &fb_id);
      ag_drm.fb_id = 0;
    }
    if (ag_drm.handle != 0) {
      struct drm_mode_destroy_dumb dreq;
      memset(&dreq, 0, sizeof(dreq));
      dreq.handle = ag_drm.handle;
      ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
      ag_drm.handle = 0;
    }
    if (ag_drm.is_master) {
      ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_DROP_MASTER, NULL);
      ag_drm.is_master = 0;
    }
    close(ag_drm.fd);
    ag_drm.fd = -1;
  }
  memset(&ag_drm, 0, sizeof(ag_drm));
  ag_drm.fd = -1;
}

static unsigned char ag_drm_setup_card(int fd) {
  if (ag_drm_ioctl(fd, DRM_IOCTL_SET_MASTER, NULL) != 0) {
    fprintf(stdout, "aroma/w: DRM_IOCTL_SET_MASTER failed (errno=%d: %s) - parent recovery may hold DRM master\n",
            errno, strerror(errno));
    return 0;
  }
  ag_drm.is_master = 1;

  struct drm_mode_card_res res;
  memset(&res, 0, sizeof(res));
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
    return 0;
  }
  if (res.count_connectors == 0 || res.count_crtcs == 0) {
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
      if (conn.encoder_id != 0) {
        struct drm_mode_get_encoder enc;
        memset(&enc, 0, sizeof(enc));
        enc.encoder_id = conn.encoder_id;
        if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id != 0) {
          chosen_crtc = enc.crtc_id;
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
                break;
              }
            }
          }
        }
      }

      if (chosen_crtc != 0 && modes[selected_mode].hdisplay > 0 && modes[selected_mode].vdisplay > 0) {
        ag_drm.conn_id = conn.connector_id;
        ag_drm.crtc_id = chosen_crtc;
        ag_drm.mode    = modes[selected_mode];
        ag_drm.width   = ag_drm.mode.hdisplay;
        ag_drm.height  = ag_drm.mode.vdisplay;
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
    return 0;
  }

  memset(&ag_drm.saved_crtc, 0, sizeof(ag_drm.saved_crtc));
  ag_drm.saved_crtc.crtc_id = ag_drm.crtc_id;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &ag_drm.saved_crtc) == 0) {
    ag_drm.has_saved_crtc = 1;
  }

  struct drm_mode_create_dumb creq;
  memset(&creq, 0, sizeof(creq));
  creq.width  = ag_drm.width;
  creq.height = ag_drm.height;
  creq.bpp    = 32;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
    return 0;
  }
  ag_drm.handle = creq.handle;
  ag_drm.pitch  = creq.pitch;
  ag_drm.size   = creq.size;

  struct drm_mode_fb_cmd fbreq;
  memset(&fbreq, 0, sizeof(fbreq));
  fbreq.width  = ag_drm.width;
  fbreq.height = ag_drm.height;
  fbreq.pitch  = ag_drm.pitch;
  fbreq.bpp    = 32;
  fbreq.depth  = 24;
  fbreq.handle = ag_drm.handle;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbreq) != 0) {
    return 0;
  }
  ag_drm.fb_id = fbreq.fb_id;

  struct drm_mode_map_dumb mreq;
  memset(&mreq, 0, sizeof(mreq));
  mreq.handle = ag_drm.handle;
  if (ag_drm_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
    return 0;
  }

  ag_drm.map = (unsigned char *) mmap(0, (size_t) ag_drm.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t) mreq.offset);
  if (ag_drm.map == MAP_FAILED) {
    ag_drm.map = NULL;
    return 0;
  }
  memset(ag_drm.map, 0, (size_t) ag_drm.size);

  struct drm_mode_crtc setcrtc;
  memset(&setcrtc, 0, sizeof(setcrtc));
  setcrtc.crtc_id            = ag_drm.crtc_id;
  setcrtc.fb_id              = ag_drm.fb_id;
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

unsigned char ag_drm_init(int * out_w, int * out_h, int * out_pitch, unsigned char ** out_map) {
  char path[32];
  int card;
  for (card = 0; card < 4; card++) {
    snprintf(path, sizeof(path), "/dev/dri/card%d", card);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }
    memset(&ag_drm, 0, sizeof(ag_drm));
    ag_drm.fd = fd;
    if (ag_drm_setup_card(fd)) {
      *out_w     = (int) ag_drm.width;
      *out_h     = (int) ag_drm.height;
      *out_pitch = (int) ag_drm.pitch;
      *out_map   = ag_drm.map;
      fprintf(stdout, "aroma/s: DRM: Initialized %s (%ux%u, pitch=%u)\n", path, ag_drm.width, ag_drm.height, ag_drm.pitch);
      return 1;
    }
    ag_drm_free_ctx();
  }
  return 0;
}

void ag_drm_flush(void) {
  if (ag_drm.fd < 0 || ag_drm.fb_id == 0) {
    return;
  }
  struct drm_mode_fb_dirty_cmd dirty;
  struct drm_clip_rect clip;
  memset(&dirty, 0, sizeof(dirty));
  clip.x1 = 0;
  clip.y1 = 0;
  clip.x2 = (unsigned short) ag_drm.width;
  clip.y2 = (unsigned short) ag_drm.height;
  dirty.fb_id      = ag_drm.fb_id;
  dirty.num_clips  = 1;
  dirty.clips_ptr  = (uint64_t)(uintptr_t) &clip;
  ag_drm_ioctl(ag_drm.fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
}

void ag_drm_close(void) {
  ag_drm_free_ctx();
}

#endif /* __AROMA_GRAPH_DRM_C__ */
