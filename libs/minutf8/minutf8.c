/*
 * Copyright (C) 2011 Ahmad Amarullah ( http://amarullz.com/ )
 * Copyright (C) 2026 Miustone (AROMA-Next-Installer)
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
 * Minimalist, RFC 3629-Compliant UTF-8 Decoder Source
 *
 * Supports full Unicode range U+0000..U+10FFFF (1- to 4-byte sequences),
 * rejects overlong encodings and UTF-16 surrogates (U+D800..U+DFFF), and
 * recovers cleanly with U+FFFD on malformed byte sequences without
 * truncating the remaining string.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <minutf8.h>

#define UTF8_REPLACEMENT_CHAR 0xFFFD

static inline int is_cont(unsigned char b) {
  return (b & 0xC0u) == 0x80u;
}

/* Decode one UTF-8 codepoint from src.
 * Returns 0 at NUL terminator, valid Unicode codepoint (U+0001..U+10FFFF),
 * or U+FFFD on malformed byte sequence (advancing past the bad byte). */
int utf8c(const char * src, const char ** ss, int * move) {
  if (src == NULL || *src == '\0') {
    if (ss != NULL)   *ss = src;
    if (move != NULL) *move = 0;
    return 0;
  }

  const unsigned char * s = (const unsigned char *) src;
  unsigned char b0 = s[0];
  uint32_t cp = UTF8_REPLACEMENT_CHAR;
  int n = 1;

  if (b0 < 0x80u) {
    /* 1-byte ASCII: 0xxxxxxx */
    cp = b0;
    n = 1;
  }
  else if (b0 >= 0xC2u && b0 <= 0xDFu) {
    /* 2-byte sequence: U+0080..U+07FF (rejects overlong 0xC0..0xC1) */
    if (is_cont(s[1])) {
      cp = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
      n = 2;
    }
  }
  else if (b0 >= 0xE0u && b0 <= 0xEFu) {
    /* 3-byte sequence: U+0800..U+FFFF (rejects overlong and surrogates U+D800..U+DFFF) */
    if (is_cont(s[1]) && is_cont(s[2])) {
      uint32_t v = ((uint32_t)(b0 & 0x0Fu) << 12) |
                   ((uint32_t)(s[1] & 0x3Fu) << 6) |
                   (uint32_t)(s[2] & 0x3Fu);
      if (v >= 0x0800u && (v < 0xD800u || v > 0xDFFFu)) {
        cp = v;
        n = 3;
      }
    }
  }
  else if (b0 >= 0xF0u && b0 <= 0xF4u) {
    /* 4-byte sequence: U+10000..U+10FFFF (supplementary planes / CJK Ext / Emoji) */
    if (is_cont(s[1]) && is_cont(s[2]) && is_cont(s[3])) {
      uint32_t v = ((uint32_t)(b0 & 0x07u) << 18) |
                   ((uint32_t)(s[1] & 0x3Fu) << 12) |
                   ((uint32_t)(s[2] & 0x3Fu) << 6) |
                   (uint32_t)(s[3] & 0x3Fu);
      if (v >= 0x10000u && v <= 0x10FFFFu) {
        cp = v;
        n = 4;
      }
    }
  }

  if (ss != NULL)   *ss = src + n;
  if (move != NULL) *move = n;
  return (int) cp;
}

int utf8_len(const char * s) {
  if (s == NULL) {
    return 0;
  }
  int count = 0;
  const char * p = s;
  while (utf8c(p, &p, NULL) != 0) {
    count++;
  }
  return count;
}

void utf8_dec_ex(int * d, int dl, const char * s) {
  if (d == NULL || dl <= 0) {
    return;
  }
  if (s == NULL) {
    d[0] = 0;
    return;
  }
  int idx = 0;
  int max_chars = dl - 1;
  const char * p = s;
  int cp;
  while (idx < max_chars && (cp = utf8c(p, &p, NULL)) != 0) {
    d[idx++] = cp;
  }
  d[idx] = 0;
}

int * utf8_dec(const char * s) {
  if (s == NULL) {
    return NULL;
  }

  int dl = utf8_len(s) + 1;
  int * r = (int *) calloc((size_t) dl, sizeof(int));
  if (r != NULL) {
    utf8_dec_ex(r, dl, s);
  }
  return r;
}
