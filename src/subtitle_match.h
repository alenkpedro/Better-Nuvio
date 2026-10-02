#ifndef NV_SUBTITLE_MATCH_H
#define NV_SUBTITLE_MATCH_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Remove somente extensao e sufixos proprios de legenda. Os demais termos,
 * inclusive episodio, resolucao e grupo de release, precisam ser iguais. */
static int submatch_suffix(const char *word) {
  static const char *const suffixes[] = {
    "pt", "br", "ptbr", "pob", "por", "portuguese", "eng", "en", "english",
    "es", "spa", "esp", "spanish", "forced", "sdh", "hi", "cc", "sub", "subs"
  };
  for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++)
    if (!strcmp(word, suffixes[i])) return 1;
  return 0;
}

static int submatch_extension(const char *word) {
  return !strcmp(word, "mkv") || !strcmp(word, "mp4") || !strcmp(word, "avi") ||
         !strcmp(word, "srt") || !strcmp(word, "ass") || !strcmp(word, "ssa") ||
         !strcmp(word, "vtt") || !strcmp(word, "sub");
}

static int submatch_release(const char *name, char *out, size_t cap) {
  char words[48][48];
  int n = 0;
  size_t used = 0;
  const char *p;
  if (!name || !*name || cap == 0) return 0;
  p = strrchr(name, '/');
  p = p ? p + 1 : name;
  while (*p && *p != '?' && *p != '#' && n < 48) {
    int len = 0;
    while (*p && *p != '?' && *p != '#' && !isalnum((unsigned char)*p)) p++;
    while (*p && isalnum((unsigned char)*p)) {
      if (len < 47) words[n][len++] = (char)tolower((unsigned char)*p);
      p++;
    }
    if (len) { words[n][len] = 0; n++; }
  }
  if (n && submatch_extension(words[n-1])) n--;
  while (n && submatch_suffix(words[n-1])) n--;
  if (n < 2) return 0;
  out[0] = 0;
  for (int i = 0; i < n; i++) {
    size_t len = strlen(words[i]);
    if (used + len + 1 > cap) return 0;
    memcpy(out + used, words[i], len);
    used += len;
  }
  out[used] = 0;
  return used >= 8;
}

static double submatch_fps_name(const char *name) {
  if (!name) return 0;
  for (const char *p = name; *p; p++) {
    char *end;
    double fps;
    if (!isdigit((unsigned char)*p) || (p > name && isdigit((unsigned char)p[-1]))) continue;
    fps = strtod(p, &end);
    if (end == p || strncasecmp(end, "fps", 3)) continue;
    if (fps >= 15 && fps <= 120) return fps;
  }
  return 0;
}

static int subtitle_release_match(const char *video_name, const char *subtitle_name,
                                  double video_fps, double subtitle_fps,
                                  int video_is_file) {
  char video[512], subtitle[512];
  double delta;
  if (!video_is_file || !submatch_release(video_name, video, sizeof video) ||
      !submatch_release(subtitle_name, subtitle, sizeof subtitle) ||
      strcmp(video, subtitle)) return 0;
  if (video_fps <= 0) video_fps = submatch_fps_name(video_name);
  if (subtitle_fps <= 0) subtitle_fps = submatch_fps_name(subtitle_name);
  if (video_fps > 0 && subtitle_fps > 0) {
    delta = video_fps - subtitle_fps;
    if (delta < 0) delta = -delta;
    if (delta > 0.08) return 0;
  }
  return 1;
}

#endif
