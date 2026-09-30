// A MONTAGEM DE "CONTINUAR ASSISTINDO" OBEDECE A ORDENACAO (issue #127).
// Parte de tests/cwordem.sh: inclui src/descoberta.c, como tests/cwremover.c,
// com um "Trakt" falso que devolve pausados e "a seguir" com estreia passada e
// futura. Cobra, por modo, a ordem que montarContinuar entrega, quem ele
// publica como futuro (cwo_e_futuro, que a home usa para partir a fileira) e o
// `showUnairedNextUp` desligado tirando os futuros de vez.
//
//   bash tests/cwordem.sh
// Duples do estado da home (merge do Codex): sem snapshot valido aqui, que e
// o caso de um primeiro arranque — a remocao e o que este teste cobra.
#include "../src/homeestado.h"
unsigned homeestado_geracao(void) { return 1; }
int homeestado_contexto_valido(void) { return 0; }
int homeestado_tem_fileira(const char *chave) { (void)chave; return 0; }
int homeestado_ordem_fileira(const char *chave) { (void)chave; return -1; }
int homeestado_salvar_se_geracao(const CatFileira *fils, int n, unsigned g) { (void)fils; (void)n; (void)g; return 1; }
int homeestado_identidade_geracao(unsigned g, char *dono, unsigned tamDono, int *perfil) {
  (void)g; if (dono && tamDono) dono[0] = 0; if (perfil) *perfil = 0; return 0; }
int arte_reserva_episodios(const char *imdb, const char *corpo) { (void)imdb; (void)corpo; return 0; }

#include "../src/descoberta.c"
#include "../src/watch_service.c"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#define CONT_MAX 50 /* Small fixtures below; large history is allocated separately. */

// --- DUBLES: so fazem descoberta.c linkar (o conjunto de tests/cateps.c) -----
int         ajustes_idioma_ingles(void) { return 0; }
const char *i18n(const char *s)         { return s; }
const char *dados_dir(void)             { return ""; }
const char *sessao_usuario(void)        { return ""; }
int         perfis_ativo(void)          { return 1; }
int         perfis_precisa_escolher(void) { return 0; }
char *dados_ler(const char *nome)                 { (void)nome; return NULL; }
int   dados_gravar(const char *nome, const char *c) { (void)nome; (void)c; return 1; }
int   dados_apagar(const char *nome)              { (void)nome; return 1; }
void  SDL_Delay(Uint32 ms)                 { usleep(ms * 1000); }
static int fonteTeste = AJ_CWF_AMBAS;
int   ajustes_cw_fonte(void)               { return fonteTeste; }
int   ajustes_tmdb_ligado(void)            { return 0; }
int   ajustes_tmdb_basico(void)            { return 0; }
int   ajustes_tmdb_arte(void)              { return 0; }
int   ajustes_tmdb_elenco(void)            { return 0; }
int   ajustes_tmdb_cw(void)                { return 0; }
int   ajustes_tmdb_eps(void)               { return 0; }
const char *ajustes_tmdb_idioma(void)      { return "pt-BR"; }
const char *ajustes_tmdb_chave(void)       { return ""; }
void  fil_gravar_registro(void)            { }
int   fil_podar_catalogos(const char *const *ids, const char *const *bases, int n,
                          int perfilDaLista) {
  (void)ids; (void)bases; (void)n; (void)perfilDaLista; return 0; }
int   fil_addon_novo(const char *id, const char *base) { (void)id; (void)base; return 0; }
int   addons_perfil_da_lista(void)         { return 1; }
int   addons_ativo(int i)                  { (void)i; return 1; }
int   fil_limite(void)                     { return 16; }
int   fil_oculta(const char *c)            { (void)c; return 0; }
int   fil_ligada_local(const char *c)       { (void)c; return 0; }
// Dubles da escolha da cota (#126): nada escolhido na TV, e o registro dos
// catalogos fora da cota nao interessa a este teste.
int fil_escolhida(const char *c) { (void)c; return -1; }
void fil_registrar_se_couber(const char *c, const char *t, const char *a,
                             const char *tp) { (void)c; (void)t; (void)a; (void)tp; }
void  fil_registrar(const char *c, const char *t, const char *a,
                    const char *tp, int itens) {
  (void)c; (void)t; (void)a; (void)tp; (void)itens;
}
// Contexto em partes (homeestado.h, 1.4.5): constante aqui, entao nada muda
// no meio da montagem e o fim dela segue o caminho de sempre.
void homeestado_contexto(HomeContexto *c) { *c = (HomeContexto){0}; c->perfil = 1; }
int homeestado_mudancas(const HomeContexto *a, const HomeContexto *b) { (void)a; (void)b; return 0; }
const char *homeestado_mudancas_texto(int m, char *b, unsigned t) { (void)m; if (b && t) b[0] = 0; return b; }
int   fil_tem_ordem(void)                  { return 0; }
int   fil_unir(const char *const *c, int n, int *s, int m) {
  int i; (void)c; for (i = 0; i < n && i < m; i++) s[i] = i; return i;
}
void  marco(const char *n)                 { (void)n; }
int   simkl_ativo(void)                    { return 0; }
int   simkl_continuar(CatItem *s, int m)   { (void)s; (void)m; return 0; }
int   simkl_plantowatch(CatItem *s, int m) { (void)s; (void)m; return 0; }
int   ajustes_salvos_no_simkl(void)        { return 0; }
int   trakt_enfeitar_lote(CatItem *s, int n) { (void)s; return n; }
int   trakt_lista(const char *q, CatItem *s, int m) { (void)q; (void)s; (void)m; return 0; }
int   trakt_social(CatItem *s, int m)      { (void)s; (void)m; return 0; }
const char *nuvem_trakt_cliente(void)      { return ""; }
static int addonDisponivel = 1;
int   addons_n(void)                       { return addonDisponivel; }
int   addons_tem_catalogo(int i)           { (void)i; return 1; }
int   addons_tem_meta(int i)               { (void)i; return 1; }
const char *addons_base(int i)             { (void)i; return "https://metadata.test"; }
const char *addons_id_manifesto(int i)     { (void)i; return ""; }
const char *addons_nome(int i)             { (void)i; return "addon"; }
unsigned addons_versao(void)               { return 1; }
const char *addons_base_por_id(const char *id) { (void)id; return ""; }
int addons_montar_url(int i, const char *recurso, char *dst, size_t tam) {
  (void)i; (void)recurso; if (tam) dst[0] = 0; return 0; }
void  addons_manifesto_lido(int i, const char *corpo) { (void)i; (void)corpo; }
static int exactLizzieMetadata;
static int tmdbLizzieTeste;
static int cinemetaMonster;
char *rede_baixar(const char *u, int t)    {
  (void)t;
  if(exactLizzieMetadata&&strstr(u,"tmdb%3A299939"))return strdup("{\"meta\":{\"id\":\"tmdb:299939\",\"name\":\"Monstro: A História de Lizzie Borden\",\"poster\":\"lizzie-fresh.jpg\"}}");
  if (tmdbLizzieTeste && strstr(u, "/tv/299939?"))
    return strdup("{\"name\":\"Monstro: A História de Lizzie Borden\",\"poster_path\":\"/lizzie-red.jpg\",\"backdrop_path\":\"/lizzie-bg.jpg\"}");
  if (cinemetaMonster && strstr(u, "v3-cinemeta.strem.io/meta/series/tt13207736.json"))
    return strdup("{\"meta\":{\"id\":\"tt13207736\",\"name\":\"Monster\",\"poster\":\"monster.jpg\"}}");
  if (strstr(u, "metadata.test/meta/series/tt13207736.json"))
    return strdup("{\"meta\":{\"id\":\"tt13207736\",\"name\":\"Monster\",\"poster\":\"monster.jpg\",\"runtime\":\"52 min\"}}");
  if (strstr(u, "metadata.test/meta/"))
    return strdup("{\"meta\":{\"name\":\"Título do addon\",\"description\":\"Sinopse do addon\",\"poster\":\"poster.png\"}}");
  return NULL;
}
char *rede_baixar_com(const char *u, int t, const char *const *c) {
  (void)c; return rede_baixar(u, t); }
int   arte_reserva_registrar(const char *url, const char *imdb, int poster) {
  (void)url; (void)imdb; (void)poster; return 1; }

static int modoTeste = CWO_PADRAO, naoExibidosTeste = 1;
int ajustes_cw_ordem(void)                { return modoTeste; }
int ajustes_cw_mostrar_nao_exibidos(void) { return naoExibidosTeste; }

// --- O "TRAKT" FALSO ---------------------------------------------------------
// Por instante (o mais recente primeiro depois da ordenacao da montagem):
//   ttA:1:3   a seguir, estreia daqui a 10 dias  (futuro)
//   tt1       pausado
//   ttB:2:1   a seguir, estreia amanha           (futuro)
//   ttC:1:8   a seguir, foi ao ar ontem
//   tt2       pausado
#define DIA (24LL * 60 * 60 * 1000)
static long long agoraMs;
typedef struct { const char *id; int seguir, prog; long long quando, estreiaDias; } Falso;
static const Falso FALSO[] = {
  { "ttA:1:3", 1,  0, 900500,  10 },
  { "tt1",     0, 40, 900400,   0 },
  { "ttB:2:1", 1,  0, 900300,   1 },
  { "ttC:1:8", 1,  0, 900200,  -1 },
  { "tt2",     0, 60, 900100,   0 },
};
// BROTHERS (C9 do dono, 24/09): o Trakt manda 10 itens, TODOS "a seguir", e o
// mais recente e Brothers S1E6 — que estreia em 21/10. A conta tem 12 em
// andamento, um deles Brothers S1E4 (mais velho que o S1E6 do Trakt). Sao 21
// candidatos para 12 lugares. Com o slice simples o futuro era sempre o
// cortado: "Separar futuros" dava "0 futuro(s)" e Brothers sumia da home.
static const Falso BROTHERS[] = {
  { "tt6773088:1:6",  1, 0, 900900,  27 },
  { "tt32260680:1:2", 1, 0, 900800,  -2 },
  { "tt10541088:1:2", 1, 0, 900700,  -3 },
  { "tt11691774:1:2", 1, 0, 900600,  -4 },
  { "tt31091039:5:4", 1, 0, 900500,  -5 },
  { "tt31937954:1:3", 1, 0, 900400, -183 },
  { "tt2304589:1:2",  1, 0, 900300,  -6 },
  { "tt13111078:1:2", 1, 0, 900200,  -7 },
  { "tt10986410:1:2", 1, 0, 900100,  -8 },
  { "tt8599532:1:2",  1, 0, 900000,  -9 },
};
static const Falso MONSTROS[] = {
  { "tt13207736:3:2", 0, 42, 990000, 0 },
};
static const Falso *tabela = FALSO;
static int semDataPrimeiro;   // 1 = o primeiro da tabela vem sem `released`
static int nTabela = (int)(sizeof FALSO / sizeof *FALSO);
#define NFALSO nTabela
int trakt_e_a_seguir(const char *id) {
  int i;
  for (i = 0; i < NFALSO; i++) if (!strcmp(tabela[i].id, id)) return tabela[i].seguir;
  return 0;
}
int simkl_e_a_seguir(const char *id) { (void)id; return 0; }
int trakt_continuar(CatItem *s, int m) {
  int i;
  for (i = 0; i < NFALSO && i < m; i++) {
    const Falso *f = &tabela[i];
    memset(&s[i], 0, sizeof s[i]);
    snprintf(s[i].imdb, sizeof s[i].imdb, "%s", f->id);
    snprintf(s[i].titulo, sizeof s[i].titulo, "English title");
    snprintf(s[i].sinopse, sizeof s[i].sinopse, "English description");
    snprintf(s[i].tipo, sizeof s[i].tipo, "%s", strchr(f->id, ':') ? "series" : "movie");
    if (strchr(f->id, ':')) sscanf(strchr(f->id, ':') + 1, "%d:%d", &s[i].temporada, &s[i].episodio);
    s[i].progresso = f->prog;
    s[i].retomadoMs = f->quando;
    // O que trakt.c faz no enfeite, com o `released` do Cinemeta.
    if (f->seguir)
      cwo_marcar_estreia(f->id, semDataPrimeiro && i == 0 ? CWO_SEM_DATA
                                                         : agoraMs + f->estreiaDias * DIA);
  }
  return i;
}

static long long relogioProg(void) { return agoraMs; }

static void conferir(const char *rotulo, const char *const *esperado, int n) {
  CatItem lote[CONT_MAX];
  int k, nc = cw_service_build(lote, CONT_MAX, NULL);
  if (nc != n) { fprintf(stderr, "%s: %d itens, esperado %d\n", rotulo, nc, n); exit(1); }
  for (k = 0; k < n; k++)
    if (strcmp(lote[k].imdb, esperado[k])) {
      fprintf(stderr, "%s: posicao %d = %s, esperado %s\n", rotulo, k, lote[k].imdb, esperado[k]);
      exit(1);
    }
  for (k = 0; k < n; k++) {
    assert(!strcmp(lote[k].titulo, "Título do addon"));
    assert(!strcmp(lote[k].sinopse, "Sinopse do addon"));
  }
}


int vistoep_estado(const char *id,int season,int episode){(void)id;(void)season;(void)episode;return 0;}
int main(void) {
  CatItem canonical;
  assert(deMeta("{\"id\":\"tmdb:299939\",\"imdb_id\":\"tt13207736\",\"name\":\"Monstro: Lizzie Borden\",\"poster\":\"lizzie.jpg\"}",NULL,"series",&canonical));
  assert(!strcmp(canonical.imdb,"tmdb:299939"));
  agoraMs=(long long)time(NULL)*1000;prog_definir_relogio(relogioProg);
  CatItem cards[CONT_MAX];fonteTeste=AJ_CWF_TRAKT;modoTeste=CWO_STREAMING;
  int n=cw_service_build(cards,CONT_MAX,NULL);assert(n==5);
  const char *expected[]={"tt1","ttC","tt2","ttB","ttA"};
  for(int i=0;i<n;i++){printf("%d: %s expected %s\n",i,cards[i].imdb,expected[i]);assert(!strcmp(cards[i].imdb,expected[i]));assert(!strcmp(cards[i].titulo,"Título do addon"));}
  assert(cards[1].continuarSeguinte&&cards[1].temporada==1&&cards[1].episodio==8);
  naoExibidosTeste=0;n=cw_service_build(cards,CONT_MAX,NULL);assert(n==3);
  /* The display publisher cannot erase provider progress or put a completed
   * account episode back over an explicitly selected next episode. */
  cat_trocar_continuar(cards,n);
  assert(cat_item(0)->progresso==cards[0].progresso);
  assert(cat_item(1)->temporada==cards[1].temporada);
  prog_esquecer_tudo();fonteTeste=AJ_CWF_CONTA;modoTeste=CWO_PADRAO;naoExibidosTeste=1;
  // Full account history must survive selection, sorting, worker and publisher.
  {
    enum { TOTAL = 137 };
    ProgRegistro *history=calloc(TOTAL,sizeof *history);
    CatItem *full=calloc(TOTAL,sizeof *full);
    assert(history && full);
    addonDisponivel=0;
    for(int i=0;i<TOTAL;i++) {
      ProgRegistro *r=&history[i];r->perfil=1;r->posSeg=600;r->durSeg=3600;
      r->percentual=-1;r->lastWatchedMs=agoraMs-i*1000;
      snprintf(r->contentId,sizeof r->contentId,"tt-history-%03d",i);
      snprintf(r->tipo,sizeof r->tipo,"movie");
      snprintf(r->titulo,sizeof r->titulo,"History %d",i+1);
      snprintf(r->poster,sizeof r->poster,"poster-%03d.jpg",i);
      prog_chave(r->chave,sizeof r->chave,r->contentId,0,0);
    }
    assert(prog_aplicar_snapshot(1,history,TOTAL));
    assert(prog_ler_perfil(1,NULL,PROG_MAX)==TOTAL && cw_capacity(1,fonteTeste)==TOTAL);
    for(int mode=CWO_PADRAO;mode<=CWO_SEPARAR;mode++) {
      modoTeste=mode;
      assert(cw_service_build(full,TOTAL,NULL)==TOTAL);
      for(int i=0;i<TOTAL;i++)assert(!strcmp(full[i].imdb,history[i].contentId));
      assert(cw_service_snapshot(NULL,0)==TOTAL);
    }
    cw_service_refresh();
    for(int tries=0;tries<500;tries++) {
      pthread_mutex_lock(&snapshotMu);int active=running;pthread_mutex_unlock(&snapshotMu);
      if(!active)break;
      usleep(1000);
    }
    pthread_mutex_lock(&snapshotMu);assert(!running && snapshotCount==TOTAL);pthread_mutex_unlock(&snapshotMu);
    lastProfile=1;lastSource=fonteTeste;lastRepo=prog_revisao();lastAddons=addons_versao();
    cw_service_pump(1);
    const CatFileira *published=cat_fileira(0);
    assert(published && !strcmp(published->chave,"continue_watching") && published->n==TOTAL);
    assert(!strcmp(cat_item(published->ini+TOTAL-1)->imdb,history[TOTAL-1].contentId));
    free(history);free(full);cw_service_reset();prog_esquecer_tudo();
    addonDisponivel=1;modoTeste=CWO_PADRAO;
    puts("ok full account history: 137 titles through all ordering modes, async snapshot and catalog");
  }
  ProgRegistro row={0};row.percentual=0;row.perfil=1;row.temporada=1;row.episodio=1;row.posSeg=1482;
  snprintf(row.contentId,sizeof row.contentId,"tmdb:299939");snprintf(row.tipo,sizeof row.tipo,"series");
  snprintf(row.titulo,sizeof row.titulo,"Monstro: A História de Lizzie Borden");snprintf(row.poster,sizeof row.poster,"lizzie.jpg");
  assert(prog_gravar_registro_local(&row));n=cw_service_build(cards,CONT_MAX,NULL);assert(n==1);
  assert(!strcmp(cards[0].imdb,"tmdb:299939")&&!strcmp(cards[0].poster,"lizzie.jpg")&&cards[0].posicaoSeg==1482&&cards[0].temporada==1);
  exactLizzieMetadata=1;n=cw_service_build(cards,CONT_MAX,NULL);assert(n==1&&!strcmp(cards[0].poster,"lizzie-fresh.jpg"));
  addonDisponivel=0;n=cw_service_build(cards,CONT_MAX,NULL);assert(n==1&&!strcmp(cards[0].poster,"lizzie-fresh.jpg"));
  ProgRegistro completed=row;completed.posSeg=3200;completed.durSeg=3260;
  assert(prog_gravar_registro_local(&completed));
  CatItem next=cards[0];next.continuarSeguinte=1;next.temporada=1;next.episodio=2;
  next.progresso=0;next.posicaoSeg=next.duracaoSeg=0;
  cat_trocar_continuar(&next,1);cat_reaplicar_progresso();
  assert(cat_item(0)->episodio==2 && cat_item(0)->progresso==0);
  ProgRegistro seed;assert(prog_por_chave("tmdb:299939_s1e1",&seed));next.retomadoMs=seed.lastWatchedMs;
  cw_stage(&next,1,1,ajustes_cw_fonte());
  n=cw_service_build(cards,CONT_MAX,NULL);assert(n==1&&cards[0].episodio==2&&cards[0].continuarSeguinte);
  CatItem provisional;assert(cw_service_snapshot(&provisional,1)==1&&provisional.episodio==2);
  puts("ok stable next-up: keeps last verified next episode during refresh and metadata outage");
  desc_tirar_continuar("tmdb:299939",1,1);n=cw_service_build(cards,CONT_MAX,NULL);assert(n==0);
  puts("continuar_pipeline: canonical identity, future ordering, stored artwork, missing metadata and removal OK");return 0;
}
