#ifndef NUVIO_SERIEALIAS_H
#define NUVIO_SERIEALIAS_H

#include <ctype.h>
#include <string.h>
#include "catalogo.h"

// O catalogo pode listar cada arco de Monster como uma serie independente.
// Cinemeta e os addons de streams indexam os quatro arcos pela serie mae.
// Exigir o nome da franquia evita confundir documentarios sobre as mesmas
// pessoas com esses arcos. A identidade e a arte do card permanecem intactas.
static inline int seriealias_contem(const char *texto, const char *parte) {
  size_t n = strlen(parte);
  if (!texto || !n) return 0;
  for (; *texto; texto++) {
    size_t i = 0;
    while (i < n && texto[i] &&
           tolower((unsigned char)texto[i]) == tolower((unsigned char)parte[i])) i++;
    if (i == n) return 1;
  }
  return 0;
}

static inline int seriealias_monster_temporada(const char *titulo) {
  if (!titulo || (!seriealias_contem(titulo, "monstro") &&
                  !seriealias_contem(titulo, "monster"))) return 0;
  if (seriealias_contem(titulo, "dahmer")) return 1;
  if (seriealias_contem(titulo, "menendez")) return 2;
  if (seriealias_contem(titulo, "gein")) return 3;
  if (seriealias_contem(titulo, "borden")) return 4;
  return 0;
}

static inline int seriealias_aplicar(CatItem *item) {
  int temporada;
  if (!item || strcmp(item->tipo, "series") || item->imdbFonte[0]) return 0;
  temporada = seriealias_monster_temporada(item->titulo);
  if (!temporada) return 0;
  strcpy(item->imdbFonte, "tt13207736");
  item->temporadaFonte = temporada;
  return 1;
}

// O card publicado pela conta e uma serie independente, mesmo quando os
// addons de streams so conhecem a temporada equivalente da antologia.
static inline int seriealias_monster_card_temporada(const CatItem *item) {
  static const char mae[] = "tt13207736";
  if (!item || strcmp(item->tipo, "series") ||
      strcmp(item->imdbFonte, mae) ||
      (!strncmp(item->imdb, mae, sizeof mae - 1) &&
       (!item->imdb[sizeof mae - 1] || item->imdb[sizeof mae - 1] == ':')))
    return 0;
  return seriealias_monster_temporada(item->titulo);
}

#endif
