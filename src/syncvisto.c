#include "syncprog.h"
#include "vistoep.h"
#include "progresso.h"
#include "sessao.h"
#include "perfis.h"
#include "jsw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int ok2xx(const char *r,int st){return r && st>=200 && st<300;}
int syncep_empurrar(const char *imdb, const char *tipo,
                    const VistoPar *pares, int qtd, int visto) {
  Jsw w;
  char *r, id[24];
  int i, st = 0, ok;
  long long agora;
  if (!imdb || !imdb[0] || !pares || qtd < 1) return 0;
  for (i = 0; imdb[i] && imdb[i] != ':' && i < (int)sizeof id - 1; i++) id[i] = imdb[i];
  id[i] = 0;
  if (!id[0]) return 0;
  agora = prog_agora_ms();

  jsw_iniciar(&w);
  jsw_obj_ini(&w);
  // O PERFIL VAI NO CORPO, como no web (watchedItemsSyncService.js manda
  // p_profile_id no push E no delete). Sem ele a linha caia no perfil padrao
  // do servidor, nao no da pessoa que marcou.
  jsw_ci(&w, "p_profile_id", perfis_ativo());
  jsw_chave(&w, visto ? "p_items" : "p_keys");
  jsw_arr_ini(&w);
  for (i = 0; i < qtd; i++) {
    jsw_obj_ini(&w);
    jsw_cs(&w, "content_id", id);
    if (visto) {
      jsw_cs(&w, "content_type", tipo && tipo[0] ? tipo : "series");
      jsw_ci(&w, "watched_at", agora);
    }
    jsw_ci(&w, "season", pares[i].temporada);
    jsw_ci(&w, "episode", pares[i].episodio);
    jsw_obj_fim(&w);
  }
  jsw_arr_fim(&w);
  jsw_obj_fim(&w);

  r = sessao_rpc(visto ? "sync_push_watched_items" : "sync_delete_watched_items",
                 jsw_texto_final(&w), &st);
  jsw_livre(&w);
  ok = ok2xx(r, st);
  free(r);
  printf("[sync] %s %d episodios de %s na conta -> %s (HTTP %d)\n",
         visto ? "marcar" : "desmarcar", qtd, id, ok ? "ok" : "falhou", st);
  fflush(stdout);
  return ok;
}

// O TITULO INTEIRO NA CONTA: a linha de watched_items com season/episode
// nulos, que e o que o web grava para filme (toRemoteItem) e o que
// contalib_aplicar_vistos le como "titulo visto" (temporada e episodio 0).
// Mesma tabela e mesmas duas RPCs dos episodios; nenhuma tabela nova. Delete e
// so {content_id}, a forma de toDeleteKey para linha sem temporada.
int syncvisto_titulo(const char *imdb, const char *tipo, int visto) {
  Jsw w;
  char *r, id[24];
  int i, st = 0, ok;
  if (!imdb || imdb[0] != 't') return 0;
  for (i = 0; imdb[i] && imdb[i] != ':' && i < (int)sizeof id - 1; i++) id[i] = imdb[i];
  id[i] = 0;
  jsw_iniciar(&w);
  jsw_obj_ini(&w);
  jsw_ci(&w, "p_profile_id", perfis_ativo());
  jsw_chave(&w, visto ? "p_items" : "p_keys");
  jsw_arr_ini(&w);
  jsw_obj_ini(&w);
  jsw_cs(&w, "content_id", id);
  if (visto) {
    jsw_cs(&w, "content_type", tipo && !strcmp(tipo, "series") ? "series" : "movie");
    jsw_cs(&w, "title", "");
    jsw_chave(&w, "season");  jsw_nulo(&w);
    jsw_chave(&w, "episode"); jsw_nulo(&w);
    jsw_ci(&w, "watched_at", prog_agora_ms());
  }
  jsw_obj_fim(&w);
  jsw_arr_fim(&w);
  jsw_obj_fim(&w);
  r = sessao_rpc(visto ? "sync_push_watched_items" : "sync_delete_watched_items",
                 jsw_texto_final(&w), &st);
  jsw_livre(&w);
  ok = ok2xx(r, st);
  free(r);
  printf("[sync] %s titulo %s na conta -> %s (HTTP %d)\n",
         visto ? "marcar" : "desmarcar", id, ok ? "ok" : "falhou", st);
  fflush(stdout);
  return ok;
}

