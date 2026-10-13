#include "../src/subtitle_match.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  const char *video = "The.Example.S01E03.1080p.WEB-DL.x265.mkv";
  const char *same = "The.Example.S01E03.1080p.WEB-DL.x265.pt-BR.srt";
  assert(subtitle_release_match(video, same, 23.976, 23.976, 1));
  assert(subtitle_release_match(video, same, 0, 0, 1));
  assert(!subtitle_release_match(video, same, 23.976, 25, 1));
  assert(!subtitle_release_match(video,
      "The.Example.S01E04.1080p.WEB-DL.x265.pt-BR.srt", 0, 0, 1));
  assert(!subtitle_release_match(video,
      "The.Example.S01E03.720p.HDTV.x265.srt", 0, 0, 1));
  assert(!subtitle_release_match("The Example", same, 0, 0, 0));
  assert(!subtitle_release_match(video, "", 0, 0, 1));
  assert(subtitle_release_match(
      "Game.of.Thrones.S01E01.720p.HDTV.DD5.1.x264-EbP.mkv",
      "Game.of.Thrones.S01E01.720p.HDTV.DD5.1.x264-EbP.HI.srt",
      23.976, 23.976, 1));
  puts("subtitle_match: ok");
  return 0;
}
