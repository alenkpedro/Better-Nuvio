// AS FAIXAS DE EPISODIO SOBREVIVEM AO CATALOGO CRESCER.
//
// Os episodios nao vivem num vetor por titulo: ha um vetor unico e cada titulo
// guarda uma janela (epIni, epQtd) sobre ele — a mesma forma das fileiras, e a
// mesma armadilha. Quem acrescenta um titulo no FIM nao muda o indice de
// ninguem, entao nada ali pode invalidar a janela dos outros.
//
// Era o defeito: garantirFaixas() zerava o vetor inteiro e era chamada tambem
// por cat_acrescentar e pelo append em lote. Na TV isso apagava os episodios da
// serie ABERTA assim que a descoberta acrescentava qualquer titulo, e a secao
// de episodios desaparecia da pagina de detalhe — o D-pad passava de
// "Temporadas" direto para as abas, porque secao com zero colunas e
// intransponivel.
//
// O QUE ESTE TESTE PROVA:
//   1. acrescentar UM titulo nao mexe nos episodios de quem ja estava;
//   2. acrescentar um LOTE tambem nao;
//   3. o titulo NOVO nasce com zero episodios, e nao com lixo do realloc;
//   4. trocar o catalogo inteiro (cat_definir_tudo) INVALIDA tudo, porque ai os
//      indices mudaram de verdade — o contrario dos casos acima.
//
//   bash tests/cateps.sh
// Alem das faixas, o teste da NOTA DE EPISODIO do TMDB (issue #87) vive aqui:
// desc_tmdb_enriquecer_temporada e pura sobre JSON + CatEp, e o jeito de linka-la e
// o mesmo de tests/colfileiras.c — descoberta.c INTEIRO entra por #include e
// os simbolos que ele pede (addons, rede, trakt...) viram dubles abaixo. Os
// cat_* NAO viram duble: este arquivo existe exatamente para exercitar os de
// verdade do catalogo.c.
#include "../src/seriealias.h"
#include "../src/descoberta.c"
#include "../src/progresso.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

// --- DUBLES: nenhum participa da regra, so fazem descoberta.c linkar ---------
// (o conjunto e o de tests/colfileiras.c, menos os cat_* — que catalogo.c ja
// traz — e mais os que este teste ja tinha)
int         ajustes_idioma_ingles(void) { return 0; }
int   ajustes_cw_ordem(void)               { return 0; }   // Padrao (issue #127)
int   ajustes_cw_mostrar_nao_exibidos(void) { return 1; }
const char *i18n(const char *s)         { return s; }
const char *dados_dir(void)             { return ""; }
const char *sessao_usuario(void)        { return ""; }
int         perfis_ativo(void)          { return 1; }
int         perfis_precisa_escolher(void) { return 0; }
unsigned homeestado_geracao(void) { return 1; }
int homeestado_contexto_valido(void) { return 0; }
int homeestado_tem_fileira(const char *chave) { (void)chave; return 0; }
int homeestado_ordem_fileira(const char *chave) { (void)chave; return -1; }
int homeestado_salvar_se_geracao(const CatFileira *f, int n, unsigned g) {
  (void)f; (void)n; return g == 1;
}
int homeestado_identidade_geracao(unsigned g, char *d, unsigned z, int *p) {
  if (g != 1) return 0;
  if (d && z) d[0] = 0;
  if (p) *p = 1;
  return 1;
}
int arte_reserva_episodios(const char *imdb, const char *corpo) { (void)imdb; (void)corpo; return 0; }
// Contexto em partes (homeestado.h, 1.4.5): constante aqui, entao nada muda
// no meio da montagem e o fim dela segue o caminho de sempre.
void homeestado_contexto(HomeContexto *c) { *c = (HomeContexto){0}; c->perfil = 1; }
int homeestado_mudancas(const HomeContexto *a, const HomeContexto *b) { (void)a; (void)b; return 0; }
const char *homeestado_mudancas_texto(int m, char *b, unsigned t) { (void)m; if (b && t) b[0] = 0; return b; }
int prog_ler(ProgRegistro *saida, int max) { (void)saida; (void)max; return 0; }
int prog_gravar_local(const char *imdb, int t, int e, double p, double d) {
  (void)imdb; (void)t; (void)e; (void)p; (void)d; return 0;
}

void  SDL_Delay(Uint32 ms)                 { usleep(ms * 1000); }
int   ajustes_cw_fonte(void)               { return 0; }
int   ajustes_tmdb_ligado(void)            { return 1; }
int   ajustes_tmdb_basico(void)            { return 0; }
int   ajustes_tmdb_eps(void)               { return 0; }
int   ajustes_tmdb_arte(void)              { return 0; }
int   ajustes_tmdb_elenco(void)            { return 0; }
int   ajustes_tmdb_cw(void)                { return 0; }
const char *ajustes_tmdb_idioma(void)      { return "pt-BR"; }
const char *ajustes_tmdb_chave(void)       { return ""; }
void  fil_gravar_registro(void)            { }
int   fil_podar_catalogos(const char *const *ids, const char *const *bases, int n,
                          int perfilDaLista) {
  (void)ids; (void)bases; (void)n; (void)perfilDaLista; return 0; }
int   fil_addon_novo(const char *id, const char *base) { (void)id; (void)base; return 0; }
int   addons_perfil_da_lista(void)         { return 0; }
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
int   fil_tem_ordem(void)                  { return 0; }
int   fil_unir(const char *const *c, int n, int *s, int m) {
  int i; (void)c; for (i = 0; i < n && i < m; i++) s[i] = i; return i;
}
void  marco(const char *n)                 { (void)n; }
void  prog_chave(char *d, unsigned n, const char *c, int t, int e) {
  (void)c; (void)t; (void)e; if (n) d[0] = 0;
}
void  prog_content_id(char *d, unsigned n, const char *i, int *t, int *e) {
  if (!n) return;
  snprintf(d, n, "%s", i ? i : "");
  char *ultimo = strrchr(d, ':');
  if (!ultimo || !ultimo[1] || strspn(ultimo + 1, "0123456789") != strlen(ultimo + 1)) return;
  char *penultimo = ultimo;
  while (penultimo > d && *--penultimo != ':') {}
  if (*penultimo != ':' || !penultimo[1] ||
      strspn(penultimo + 1, "0123456789") != (size_t)(ultimo - penultimo - 1)) return;
  if (t) *t = atoi(penultimo + 1);
  if (e) *e = atoi(ultimo + 1);
  *penultimo = 0;
}
int   prog_por_chave(const char *c, ProgRegistro *s) { (void)c; (void)s; return 0; }
// "Tirar de Continuar assistindo" (desc_tirar_continuar, tests/cwremover.sh).
void  prog_remover(const char *c)          { (void)c; }
void  prog_marcar_removido(const char *i)  { (void)i; }
int   prog_removido_vence(const char *i, long long ms) { (void)i; (void)ms; return 0; }
int   trakt_continuar(CatItem *s, int m)   { (void)s; (void)m; return 0; }
// Simkl (issue #110): sem vinculo nos testes de fileira, como o Trakt acima.
int   simkl_ativo(void)                    { return 0; }
int   simkl_continuar(CatItem *s, int m)   { (void)s; (void)m; return 0; }
int   simkl_e_a_seguir(const char *id)     { (void)id; return 0; }
int   simkl_plantowatch(CatItem *s, int m) { (void)s; (void)m; return 0; }
int   ajustes_salvos_no_simkl(void)        { return 0; }
int   trakt_enfeitar_lote(CatItem *s, int n) { (void)s; (void)n; return 0; }
int   trakt_lista(const char *q, CatItem *s, int m) { (void)q; (void)s; (void)m; return 0; }
int   trakt_social(CatItem *s, int m)      { (void)s; (void)m; return 0; }
const char *nuvem_trakt_cliente(void)      { return ""; }
static int addonTesteAtivo;
int   addons_n(void)                       { return addonTesteAtivo; }
int   addons_tem_catalogo(int i)           { (void)i; return 0; }
int   addons_tem_meta(int i)               { return addonTesteAtivo && i == 0; }
const char *addons_base(int i)             { (void)i; return "https://metadata.example.test"; }
int addons_montar_url(int i, const char *r, char *d, size_t n) {
  (void)i; (void)r; if (n) d[0] = 0; return 0;
}
const char *addons_id_manifesto(int i)     { (void)i; return ""; }
const char *addons_nome(int i)            { (void)i; return "addon"; }
unsigned addons_versao(void)             { return 1; }   // estatico no teste
const char *addons_base_por_id(const char *id) { (void)id; return ""; }
void  addons_manifesto_lido(int i, const char *corpo) { (void)i; (void)corpo; }
static int requisicoesMonstro;
static int requisicoesLizzie;
char *rede_baixar(const char *u, int t) {
  (void)t;
  if (strstr(u, "/meta/series/tt13207736.json")) {
    requisicoesMonstro++;
    return strdup("{\"meta\":{\"id\":\"tt13207736\",\"name\":\"Monster\","
                  "\"videos\":["
                  "{\"season\":1,\"episode\":1,\"name\":\"Dahmer\",\"thumbnail\":\"dahmer.jpg\"},"
                  "{\"season\":2,\"episode\":1,\"name\":\"Menendez\",\"thumbnail\":\"menendez.jpg\"},"
                  "{\"season\":3,\"episode\":1,\"name\":\"Ed Gein\",\"thumbnail\":\"gein.jpg\"},"
                  "{\"season\":4,\"episode\":1,\"name\":\"Lizzie Borden\",\"thumbnail\":\"mother-lizzie.jpg\"}]}}");
  }
  if (strstr(u, "/meta/series/tmdb:299939.json")) {
    requisicoesLizzie++;
    return strdup("{\"meta\":{\"id\":\"tmdb:299939\",\"name\":\"Monstro: Lizzie Borden\","
                  "\"videos\":["
                  "{\"season\":1,\"episode\":1,\"name\":\"Banho de Sangue\",\"thumbnail\":\"lizzie-e1.jpg\"},"
                  "{\"season\":1,\"episode\":2,\"name\":\"Flor Forte\",\"thumbnail\":\"lizzie-e2.jpg\"}]}}");
  }
  if (strstr(u, "/meta/series/tmdb:777.json"))
    return strdup("{\"meta\":{\"id\":\"tmdb:777\",\"imdb_id\":\"tt0000777\",\"name\":\"Exemplo\","
                  "\"videos\":[{\"season\":1,\"episode\":1,\"name\":\"Piloto\"}]}}");
  return NULL;
}
char *rede_baixar_com(const char *u, int t, const char *const *c) {
  (void)c; return rede_baixar(u, t); }

// cat_acrescentar publica a reserva junto com o item. Este espião deixa o
// teste provar os dois campos sem depender de rede ou de uma implementação de
// TMDB: o registry próprio é exercitado em tests/artereserva.c.
static int reservaN;
static char reservaUrls[8][512];
static char reservaIds[8][24];
static int reservaTipos[8];
int arte_reserva_registrar(const char *url, const char *imdb, int poster) {
  if (reservaN < 8) {
    snprintf(reservaUrls[reservaN], sizeof reservaUrls[reservaN], "%s", url);
    snprintf(reservaIds[reservaN], sizeof reservaIds[reservaN], "%s", imdb);
    reservaTipos[reservaN] = poster;
  }
  reservaN++;
  return 1;
}

static CatItem itens[3];

static void montarCatalogo(void) {
  int i;
  memset(itens, 0, sizeof itens);
  for (i = 0; i < 3; i++) {
    snprintf(itens[i].titulo, sizeof itens[i].titulo, "T%d", i);
    snprintf(itens[i].imdb, sizeof itens[i].imdb, "tt%d", i);
    snprintf(itens[i].tipo, sizeof itens[i].tipo, "series");
  }
  cat_definir_tudo(itens, 3, NULL, 0);
}

// Tres episodios com nome reconhecivel, para a assercao ser sobre CONTEUDO e
// nao so sobre a contagem: um vetor com o tamanho certo e os dados de outra
// serie passaria por um teste que so contasse.
static void publicar(int alvo, const char *prefixo, int quantos) {
  CatEp eps[4];
  int i;
  memset(eps, 0, sizeof eps);
  for (i = 0; i < quantos; i++) {
    eps[i].temporada = 1;
    eps[i].episodio = i + 1;
    snprintf(eps[i].nome, sizeof eps[i].nome, "%s-E%d", prefixo, i + 1);
  }
  cat_definir_episodios(alvo, eps, quantos);
}

static int temEpisodios(int alvo, const char *prefixo, int quantos) {
  int i;
  if (cat_n_episodios(alvo) != quantos) return 0;
  for (i = 0; i < quantos; i++) {
    char esperado[64];
    const CatEp *e = cat_episodio(alvo, i);
    snprintf(esperado, sizeof esperado, "%s-E%d", prefixo, i + 1);
    if (!e || strcmp(e->nome, esperado)) return 0;
  }
  return 1;
}

int main(void) {
  CatItem novo;
  CatItem lote[2];

  // A) UM TITULO ACRESCENTADO no fim nao apaga os episodios de quem ja estava.
  //    Este e o caso que a TV mostrava: a serie aberta perdia a secao inteira
  //    quando a descoberta trazia mais um titulo.
  montarCatalogo();
  publicar(0, "A", 3);
  publicar(2, "C", 2);
  assert(temEpisodios(0, "A", 3));
  memset(&novo, 0, sizeof novo);
  snprintf(novo.titulo, sizeof novo.titulo, "novo");
  { int i = cat_acrescentar(&novo);
    assert(i == 3);
    assert(temEpisodios(0, "A", 3));
    assert(temEpisodios(2, "C", 2));
    // E o recem-chegado nasce VAZIO. realloc nao inicializa o que cresceu: um
    // epQtd de lixo aqui faria cat_episodio ler fora do vetor de episodios.
    assert(cat_n_episodios(i) == 0); }
  puts("ok  acrescentar um titulo preserva as faixas e o novo nasce vazio");

  // B) O MESMO PARA O LOTE, que e outro caminho de codigo.
  montarCatalogo();
  publicar(1, "B", 4);
  memset(lote, 0, sizeof lote);
  snprintf(lote[0].titulo, sizeof lote[0].titulo, "L0");
  snprintf(lote[1].titulo, sizeof lote[1].titulo, "L1");
  { int idx[2];
    assert(cat_acrescentar_lote(lote, 2, idx) == 2);
    assert(temEpisodios(1, "B", 4));
    assert(cat_n_episodios(idx[0]) == 0 && cat_n_episodios(idx[1]) == 0); }
  puts("ok  acrescentar em lote preserva as faixas");

  // C) DOIS APPENDS SEGUIDOS. A cauda zerada da primeira vez nao pode ser
  //    zerada de novo por cima de episodios publicados depois dela.
  montarCatalogo();
  { int i = cat_acrescentar(&novo);
    publicar(i, "D", 2);
    assert(cat_acrescentar(&novo) >= 0);
    assert(temEpisodios(i, "D", 2)); }
  puts("ok  publicar depois de crescer e crescer de novo mantem o que foi publicado");

  // D) TROCAR O CATALOGO INTEIRO INVALIDA, e tem de invalidar: os indices
  //    passam a apontar para outros titulos. E o oposto exato de (A), e por
  //    isso os dois estao no mesmo arquivo — quem mexer num vai ler o outro.
  montarCatalogo();
  publicar(0, "A", 3);
  assert(temEpisodios(0, "A", 3));
  cat_definir_tudo(itens, 3, NULL, 0);
  assert(cat_n_episodios(0) == 0);
  puts("ok  trocar o catalogo inteiro invalida as faixas");

  // E) APPEND DE ARTE: o caminho unitário precisa registrar poster e fundo
  // exatamente como o lote e a publicação completa. Repetir o mesmo item é
  // permitido pelo catálogo, mas deve reusar a mesma chave no registry (a
  // deduplicação da chave é coberta pelo teste de arte); aqui garantimos que
  // não existe um caminho unitário que simplesmente esqueça a arte.
  montarCatalogo();
  { CatItem arte;
    int antes;
    memset(&arte, 0, sizeof arte);
    snprintf(arte.imdb, sizeof arte.imdb, "tt9000001");
    snprintf(arte.poster, sizeof arte.poster, "https://addon/poster/one.jpg");
    snprintf(arte.backdrop, sizeof arte.backdrop, "https://addon/background/one.jpg");
    reservaN = 0;
    assert(cat_acrescentar(&arte) == 3);
    assert(reservaN == 2);
    assert(!strcmp(reservaUrls[0], arte.poster) && !strcmp(reservaIds[0], arte.imdb) && reservaTipos[0] == 1);
    assert(!strcmp(reservaUrls[1], arte.backdrop) && !strcmp(reservaIds[1], arte.imdb) && reservaTipos[1] == 0);
    antes = reservaN;
    assert(cat_acrescentar(&arte) == 4);
    assert(reservaN == antes + 2);
  }
  puts("ok  append unitario registra poster/fundo em novo e repetido item");

  // F) TMDB LOCALIZADO POR EPISODIO. Casa numero e temporada, substitui
  //    nome/sinopse traduzidos sem apagar campos ausentes, e respeita os
  //    ajustes independentes de texto e notas.
  { CatEp eps2[4];
    const char *json =
      "{\"episodes\":["
      "{\"episode_number\":1,\"crew\":[{\"name\":\"Pessoa\"}],\"name\":\"O início\",\"overview\":\"Sinopse em português\",\"still_path\":\"/lizzie-e1.jpg\",\"vote_average\":8.3},"
      "{\"episode_number\":2,\"name\":\"\",\"overview\":\"\"},"
      "{\"episode_number\":3,\"name\":\"O final\",\"vote_average\":7.0}]}";
    memset(eps2, 0, sizeof eps2);
    eps2[0].temporada = 1; eps2[0].episodio = 1;
    eps2[1].temporada = 1; eps2[1].episodio = 2;
    eps2[2].temporada = 1; eps2[2].episodio = 3;
    eps2[3].temporada = 2; eps2[3].episodio = 1;
    snprintf(eps2[1].nome, sizeof eps2[1].nome, "Original");
    snprintf(eps2[1].sinopse, sizeof eps2[1].sinopse, "Resumo original");
    assert(desc_tmdb_enriquecer_temporada(json, eps2, 4, 1, 1, 1) == 2);
    assert(!strcmp(eps2[0].nome, "O início"));
    assert(!strcmp(eps2[0].sinopse, "Sinopse em português"));
    assert(!strcmp(eps2[0].thumb, "https://image.tmdb.org/t/p/w780/lizzie-e1.jpg"));
    assert(eps2[0].nota == 83);          // 8.3 x10
    assert(!strcmp(eps2[1].nome, "Original"));
    assert(!strcmp(eps2[1].sinopse, "Resumo original"));
    assert(eps2[1].nota == 0);
    assert(!strcmp(eps2[2].nome, "O final"));
    assert(eps2[2].nota == 70);
    assert(eps2[3].nota == 0 && !eps2[3].nome[0]);
    // Pedindo a temporada errada nao toca os episodios.
    memset(eps2, 0, sizeof eps2);
    eps2[0].temporada = 1; eps2[0].episodio = 1;
    assert(desc_tmdb_enriquecer_temporada(json, eps2, 1, 2, 1, 1) == 0);
    assert(eps2[0].nota == 0 && !eps2[0].nome[0]);
    assert(desc_tmdb_enriquecer_temporada(json, eps2, 1, 1, 0, 1) == 1);
    assert(eps2[0].nota == 83 && !eps2[0].nome[0]); }
  puts("ok  TMDB complementa episodios localizados sem trocar outros campos");

  // G) O addon da conta pode oferecer os nomes traduzidos sem chave TMDB.
  //    A lista do Cinemeta continua mandando na cobertura de episodios; o
  //    complemento so troca os pares encontrados e preserva texto ausente.
  { CatEp eps2[3] = {0};
    const char *json = "{\"videos\":["
      "{\"season\":1,\"episode\":1,\"name\":\"Piloto\",\"overview\":\"Apresentação da série\"},"
      "{\"season\":1,\"episode\":2,\"name\":\"\"},"
      "{\"season\":2,\"episode\":1,\"name\":\"Outra temporada\"}]}";
    eps2[0].temporada = 1; eps2[0].episodio = 1;
    eps2[1].temporada = 1; eps2[1].episodio = 2;
    eps2[2].temporada = 3; eps2[2].episodio = 1;
    snprintf(eps2[0].nome, sizeof eps2[0].nome, "Pilot");
    snprintf(eps2[1].nome, sizeof eps2[1].nome, "Second episode");
    assert(desc_addon_enriquecer_episodios(json, eps2, 3) == 1);
    assert(!strcmp(eps2[0].nome, "Piloto"));
    assert(!strcmp(eps2[0].sinopse, "Apresentação da série"));
    assert(!strcmp(eps2[1].nome, "Second episode"));
    assert(!eps2[2].nome[0]); }
  puts("ok  addon completa nomes sem chave TMDB nem perder episodios");

  // H) Cada arco e uma serie propria com temporada 1 e thumbs proprias.
  // O ID da antologia permanece so para localizar as fontes de video.
  { CatItem arcos[4] = {0};
    const char *nomes[] = {
      "Monstro: A História de Jeffrey Dahmer",
      "Monstros: Irmãos Menendez: Assassinos dos Pais",
      "Monstro: A História de Ed Gein",
      "Monstro: A História de Lizzie Borden"
    };
    for (int i = 0; i < 4; i++) {
      snprintf(arcos[i].titulo, sizeof arcos[i].titulo, "%s", nomes[i]);
      snprintf(arcos[i].imdb, sizeof arcos[i].imdb, "tmdb:%d", i == 3 ? 299939 : 100 + i);
      snprintf(arcos[i].tipo, sizeof arcos[i].tipo, "series");
      assert(seriealias_aplicar(&arcos[i]));
      assert(arcos[i].temporadaFonte == i + 1);
      assert(!strcmp(cat_id_fonte(&arcos[i]), "tt13207736"));
      assert(strncmp(arcos[i].imdb, "tmdb:", 5) == 0);
    }
    cat_definir_tudo(arcos, 4, NULL, 0);
    for (int i = 0; i < 4; i++) {
      addonTesteAtivo = i == 3;
      assert(seriealias_monster_card_temporada(cat_item(i)) == i + 1);
      epItem = i; epTemp = i + 1; fioEpVivo = 1;
      buscarEps(NULL);
      if (i == 3) assert(requisicoesLizzie > 0);
      assert(cat_n_episodios(i) == (i == 3 ? 2 : 1));
      assert(cat_item(i)->nTemporadas == 1 && cat_item(i)->temporadas[0] == 1);
      assert(cat_item(i)->temporadaFonte == i + 1);
      assert(!strcmp(cat_item(i)->imdb, arcos[i].imdb));
      assert(cat_episodio(i, 0)->temporada == 1);
      assert(!strcmp(cat_episodio(i, 0)->thumb,
                     i == 0 ? "dahmer.jpg" : i == 1 ? "menendez.jpg" :
                     i == 2 ? "gein.jpg" : "lizzie-e1.jpg"));
    }
    addonTesteAtivo = 0;
    assert(requisicoesMonstro == 1);
    CatItem doc = {0};
    snprintf(doc.titulo, sizeof doc.titulo, "Ed Gein: Original Psycho");
    snprintf(doc.tipo, sizeof doc.tipo, "series");
    assert(!seriealias_aplicar(&doc));
  }
  puts("ok  arcos de Monster mostram uma temporada e as thumbs de cada serie");

  // I) Um ID proprio de add-on conserva o prefixo inteiro na URL /meta.
  // Cortar no primeiro ':' fazia a busca de episodios morrer antes da rede.
  { CatItem proprio = {0};
    addonTesteAtivo = 1;
    snprintf(proprio.titulo, sizeof proprio.titulo, "Exemplo");
    snprintf(proprio.imdb, sizeof proprio.imdb, "tmdb:777");
    snprintf(proprio.tipo, sizeof proprio.tipo, "series");
    cat_definir_tudo(&proprio, 1, NULL, 0);
    epItem = 0; epTemp = 1; fioEpVivo = 1;
    buscarEps(NULL);
    assert(cat_n_episodios(0) == 1);
    assert(!strcmp(cat_episodio(0, 0)->nome, "Piloto"));
    assert(!strcmp(cat_item(0)->imdb, "tmdb:777"));
    assert(!strcmp(cat_id_fonte(cat_item(0)), "tt0000777"));
    addonTesteAtivo = 0;
  }
  puts("ok  serie com ID proprio recebe episodios do meta do addon");

  puts("cateps: tudo ok");
  return 0;
}
