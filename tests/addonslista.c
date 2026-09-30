// O que addons_carregar tira de addons.txt fica USAVEL?
//
// Este teste existe por um defeito mudo: a leitura do arquivo preenchia nome,
// base e as tres capacidades e NAO preenchia `ativo`. O vetor de addons e
// estatico, nasce zerado, entao todo addon do arquivo ficava desligado — e
// desligado, em addons.c, quer dizer "nao consultado" por fonte, por legenda e
// por catalogo. Na tela ele aparecia; na busca de fontes ele nao existia, sem
// uma linha de log dizendo por que. E a forma extrema do sintoma da issue #83
// ("certain addons not showing").
//
// So o leitor: nada aqui vai a rede.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "addons.h"
#include "fontecache.h"

// ---------------------------------------------------------------- duble
//
// A BUSCA DE FONTES DE VERDADE (addons_buscar -> fio -> addons_estado), com a
// rede e o parser trocados por dubles. O que se confere e o RESUMO que a folha
// de fontes vazia usa para dizer a causa (B6/#107, D5): quem foi consultado,
// quem respondeu vazio, quem nao respondeu.
//
// resp[0] e o corpo de exemplo.test, resp[1] o de antigo.test; NULL = sem
// resposta (rede_baixar devolve NULL em falha e em 4xx).
static const char *resp[2];
static int nDebridNovaBusca;
static int pedidosExemplo, pedidosAntigo, fontesPublicadas;
static char nomesPublicados[2][96];

// canal.test imita o FrostView TV medido no #112: 200 {"streams":[]} para
// /stream/tv/ e a lista de verdade para /stream/channel/. `pedidosCanal`
// guarda o tipo de cada pedido, na ordem, para conferir quem foi primeiro.
static const char *respCanalTv, *respCanalChannel;
static char pedidosCanal[200];
char *rede_baixar(const char *url, int s) {
  const char *r;
  (void)s;
  if (strstr(url, "/subtitles/") && strstr(url, "addon-")) {
    char texto[4096];
    size_t usado = 0;
    int id = 0;
    const char *p = strstr(url, "addon-");
    if (p) sscanf(p, "addon-%d", &id);
    usado += (size_t)snprintf(texto+usado, sizeof texto-usado, "{\"subtitles\":[");
    for (int j = 0; j < 20; j++) {
      const char *campo = j == 0 ? "\"subtitleFileName\":\"Teste.S01E01.srt\",\"movieReleaseName\":\"Teste.S01E01.WEB-DL\",\"fpsMilli\":23976" :
                          j == 1 ? "\"fileName\":\"Teste.S01E02.srt\"" :
                          j == 2 ? "\"name\":\"Teste.S01E03.srt\"" :
                          j == 3 ? "\"title\":\"Teste.S01E04.srt\"" :
                          j == 4 ? "\"movieReleaseName\":\"Teste.S01E05.WEB-DL\"" :
                          j == 5 ? "" : "\"subtitleFileName\":\"Teste.srt\"";
      usado += (size_t)snprintf(texto+usado, sizeof texto-usado,
        "%s{\"lang\":\"pt-BR\",\"url\":\"https://legenda.test/%d/%d.srt\"%s%s}",
        j ? "," : "", id, j, *campo ? "," : "", campo);
    }
    snprintf(texto+usado, sizeof texto-usado, "]}");
    return strdup(texto);
  }
  if (strstr(url, "canal.test")) {
    int tv = strstr(url, "/stream/tv/") != NULL;
    strncat(pedidosCanal, tv ? "tv," : "channel,",
            sizeof pedidosCanal - strlen(pedidosCanal) - 1);
    r = tv ? respCanalTv : respCanalChannel;
    return r ? strdup(r) : NULL;
  }
  if (strstr(url, "exemplo.test")) pedidosExemplo++;
  if (strstr(url, "antigo.test")) pedidosAntigo++;
  r = strstr(url, "exemplo.test") ? resp[0]
    : strstr(url, "antigo.test")  ? resp[1] : NULL;
  return r ? strdup(r) : NULL;
}
// conta um "url" por fonte; o bastante para distinguir lista vazia de cheia
int stream_extrair(const char *json, const char *prov, Stream **saida) {
  int n = 0; const char *p = json;
  while ((p = strstr(p, "\"url\"")) != NULL) { n++; p += 5; }
  *saida = n ? calloc((size_t)n, sizeof(Stream)) : NULL;
  for (int i = 0; i < n; i++)
    snprintf((*saida)[i].addonNome, sizeof (*saida)[i].addonNome, "%s", prov);
  if (n && strstr(json, "OBSOLETE CONFIGURATION"))
    snprintf((*saida)[0].descricao, sizeof (*saida)[0].descricao,
             "OBSOLETE CONFIGURATION");
  return n;
}
void stream_definir_lista(const Stream *l, int n) {
  fontesPublicadas = n;
  memset(nomesPublicados, 0, sizeof nomesPublicados);
  for (int i = 0; l && i < n && i < 2; i++)
    snprintf(nomesPublicados[i], sizeof nomesPublicados[i], "%s", l[i].addonNome);
}
void debrid_definir_episodio(int t, int e) { (void)t; (void)e; }
void debrid_nova_busca(void) { nDebridNovaBusca++; }
const char *i18n(const char *s) { return s; }
const char *ling_legenda(void) { return "pt-BR"; }
const char *ling_legenda2(void) { return "en"; }
int ling_casa(const char *a, const char *b) { return !*b || !strcasecmp(a,b); }
int ling_legenda_visivel(const char *codigo) { (void)codigo; return 1; }
const char *ling_nome(const char *s) { return s; }
const char *rede_url_publica(const char *url, char *dst, unsigned tam) {
  snprintf(dst, tam, "%s", url ? url : ""); return dst; }
void marco(const char *s) { (void)s; }
int  fontecache_pegar(const char *id, const char *tipo, Stream **l, int *n) {
  (void)id; (void)tipo; *l = NULL; *n = 0; return FC_NADA; }
void fontecache_guardar(const char *id, const char *tipo, const Stream *l, int n) {
  (void)id; (void)tipo; (void)l; (void)n; }
void fontecache_ceder(void) {}
void fontecache_avancar(void) {}
int plugins_ativos(void) { return 0; }
int plugins_consultar(const char *id, const char *tipo, Stream **saida) {
  (void)id; (void)tipo; *saida = NULL; return 0;
}

// Uma busca inteira, esperando o fio acabar, e a frase da folha.
static const char *buscarMotivo(const char *id) {
  static char m[200];
  addons_buscar(id, "movie");
  while (addons_estado() == ADD_BUSCANDO) usleep(1000);
  if (!addons_motivo_vazio(m, sizeof m)) snprintf(m, sizeof m, "(sem causa)");
  return m;
}

static int falhas;

static void conferir(const char *o_que, int obtido, int esperado) {
  if (obtido == esperado) return;
  printf("FALHOU %s: obtido %d, esperado %d\n", o_que, obtido, esperado);
  falhas++;
}

static void conferirTexto(const char *o_que, const char *obtido, const char *esperado) {
  if (!strcmp(obtido, esperado)) return;
  printf("FALHOU %s: obtido \"%s\", esperado \"%s\"\n", o_que, obtido, esperado);
  falhas++;
}

int main(void) {
  char dir[] = "/tmp/nuvio-addonslista-XXXXXX";
  char caminho[600];
  FILE *f;
  int n;

  if (!mkdtemp(dir)) { printf("FALHOU: sem diretorio temporario\n"); return 1; }
  snprintf(caminho, sizeof caminho, "%s/addons.txt", dir);
  f = fopen(caminho, "w");
  if (!f) { printf("FALHOU: sem %s\n", caminho); return 1; }
  // Uma linha por forma que o arquivo aceita: com as tres colunas de
  // capacidade, e sem elas (formato antigo, em que tudo vale 1 menos legenda).
  fprintf(f, "# comentario ignorado\n");
  fprintf(f, "So legenda\thttps://opensubtitles-v3.strem.io\t0\t0\t1\n");
  fprintf(f, "Fonte e catalogo\thttps://exemplo.test/manifest.json\t1\t1\t0\n");
  fprintf(f, "Formato antigo\thttps://antigo.test\n");
  fclose(f);

  n = addons_carregar(dir);
  conferir("addons lidos", n, 3);

  // O QUE ESTE TESTE GUARDA: os tres nascem LIGADOS.
  conferir("ativo[0]", addons_ativo(0), 1);
  conferir("ativo[1]", addons_ativo(1), 1);
  conferir("ativo[2]", addons_ativo(2), 1);

  // E as capacidades continuam vindo das colunas, como antes.
  conferir("fonte[0]",   addons_fornece(0, ADD_STREAM),   0);
  conferir("legenda[0]", addons_fornece(0, ADD_LEGENDA), 1);
  conferir("fonte[1]",   addons_fornece(1, ADD_STREAM),   1);
  conferir("catalogo[1] (ativo e catalogo)", addons_tem_catalogo(1), 1);
  // Formato antigo: fonte e catalogo valem 1, legenda 0.
  conferir("fonte[2]",   addons_fornece(2, ADD_STREAM),   1);
  conferir("legenda[2]", addons_fornece(2, ADD_LEGENDA), 0);

  // A base sai sem "/manifest.json" — a mesma regra de baseNormalizada.
  conferirTexto("base[1]", addons_base(1), "https://exemplo.test");
  conferirTexto("nome[1]", addons_nome(1), "Fonte e catalogo");

  // ---- a causa da folha vazia
  // 1) id 1504 (#107): todos responderam {"streams":[]}
  resp[0] = "{\"streams\":[]}"; resp[1] = "{\"streams\":[]}";
  conferirTexto("todos vazios", buscarMotivo("tt0000001"),
                "2 add-ons responderam: nenhum tem este título");
  conferir("debrid_nova_busca por busca", nDebridNovaBusca, 1);
  // 2) um vazio, um mudo
  resp[1] = NULL;
  conferirTexto("vazio + mudo", buscarMotivo("tt0000002"),
                "1 sem este título · 1 sem resposta");
  // 3) algum trouxe fonte: a lista nao esta vazia por culpa dos addons
  resp[0] = "{\"streams\":[{\"url\":\"https://x/a.mp4\"}]}";
  conferir("com fonte nao explica vazio",
           strcmp(buscarMotivo("tt0000003"), "(sem causa)"), 0);
  // Uma resposta de configuracao obsoleta do addon nao e link reproduzivel.
  resp[0] = "{\"streams\":[{\"url\":\"https://x/aviso.mp4\",\"description\":\"OBSOLETE CONFIGURATION\"}]}";
  resp[1] = "{\"streams\":[]}";
  conferirTexto("aviso de configuracao", buscarMotivo("tt0000007"),
                "Reconfigure Fonte e catalogo no site do add-on");
  resp[1] = NULL;
  // 4) so um consultado, e ele nao responde
  addons_alternar(1);                       // "Fonte e catalogo" desligado
  conferirTexto("um mudo", buscarMotivo("tt0000004"), "Formato antigo não respondeu");
  // 5) so um, e responde vazio
  resp[1] = "{\"err\":\"Invalid debrid key\"}";
  conferirTexto("um vazio", buscarMotivo("tt0000005"),
                "Formato antigo respondeu: não tem este título");
  // 6) D5: os de fonte desligados
  addons_alternar(2);
  conferirTexto("desligados", buscarMotivo("tt0000006"),
                "Os add-ons de fontes estão desligados");

  // ---- canal ao vivo: o segundo nome de tipo (issue #112)
  // 7) FrostView sem manifesto lido: "tv" responde 200 vazio. Antes o segundo
  //    nome so saia quando o primeiro NAO respondia, e o canal ficava sem
  //    fonte com o addon tendo quatro. Agora a lista vazia tambem dispara.
  conferir("addon de canal entrou", addons_adicionar("Canal TV", "https://canal.test/manifest.json"), 1);
  respCanalTv = "{\"streams\":[]}";
  respCanalChannel = "{\"streams\":[{\"url\":\"https://x/axn.m3u8\"}]}";
  pedidosCanal[0] = 0;
  addons_definir_origem("https://canal.test");
  addons_buscar("cs:channel:axn", "tv");
  addons_definir_origem(NULL);
  while (addons_estado() == ADD_BUSCANDO) usleep(1000);
  conferir("tv vazio -> channel traz a fonte", addons_estado(), ADD_PRONTO);
  conferirTexto("ordem sem manifesto", pedidosCanal, "tv,channel,");
  // 8) Com o manifesto do FrostView (catalogo "channel"), "channel" vai
  //    primeiro e sozinho: uma viagem por canal, nao duas.
  addons_manifesto_lido(3,
    "{\"id\":\"com.frostview\",\"name\":\"Canal TV\","
    "\"resources\":[\"catalog\",\"meta\",\"stream\"],\"types\":[\"channel\"],"
    "\"catalogs\":[{\"id\":\"froststream-channels\",\"type\":\"channel\",\"name\":\"Canais\"}]}");
  pedidosCanal[0] = 0;
  addons_definir_origem("https://canal.test");
  addons_buscar("cs:channel:globo", "tv");
  addons_definir_origem(NULL);
  while (addons_estado() == ADD_BUSCANDO) usleep(1000);
  conferir("channel declarado traz a fonte", addons_estado(), ADD_PRONTO);
  conferirTexto("ordem com manifesto", pedidosCanal, "channel,");
  // 9) Os dois nomes vazios: a causa fala de CANAL, e nao de titulo.
  respCanalChannel = "{\"streams\":[]}";
  addons_definir_origem("https://canal.test");
  { static char m[200];
    addons_buscar("cs:channel:bleach", "tv");
    addons_definir_origem(NULL);
    while (addons_estado() == ADD_BUSCANDO) usleep(1000);
    if (!addons_motivo_vazio(m, sizeof m)) snprintf(m, sizeof m, "(sem causa)");
    conferirTexto("canal vazio", m, "Canal TV não tem fonte para este canal agora"); }
  // 10) Segundo nome sem resposta nao apaga o "respondeu vazio" do primeiro.
  respCanalChannel = NULL;
  respCanalTv = "{\"streams\":[]}";
  addons_definir_origem("https://canal.test");
  { static char m[200];
    addons_buscar("cs:channel:sbt", "tv");
    addons_definir_origem(NULL);
    while (addons_estado() == ADD_BUSCANDO) usleep(1000);
    if (!addons_motivo_vazio(m, sizeof m)) snprintf(m, sizeof m, "(sem causa)");
    conferirTexto("channel mudo, tv vazio", m, "Canal TV não tem fonte para este canal agora"); }

  // A fonte da colecao pode estar no 18o addon da conta. O limite antigo de
  // 16 descartava esse addon antes de a sonda aprender o id do manifesto.
  AddonRemoto muitos[18] = {0};
  for (int i = 0; i < 18; i++) {
    snprintf(muitos[i].nome, sizeof muitos[i].nome, "Addon %d", i + 1);
    snprintf(muitos[i].url, sizeof muitos[i].url,
             "https://addon-%d.test/manifest.json", i + 1);
    muitos[i].ativo = 1;
  }
  conferir("lista da conta com 18 addons aplicada",
           addons_definir_lista(muitos, 18), 1);
  conferir("18o addon preservado", addons_n(), 18);
  conferirTexto("base do 18o addon", addons_base(17), "https://addon-18.test");
  addons_manifesto_lido(0,
    "{\"id\":\"app.xperience.11111111-1111-1111-1111-111111111111\","
    "\"name\":\"Xperience 1\",\"resources\":[\"catalog\"]}");
  addons_manifesto_lido(17,
    "{\"id\":\"app.xperience.22222222-2222-2222-2222-222222222222\","
    "\"name\":\"Xperience\",\"resources\":[\"catalog\"]}");
  conferirTexto("colecao resolve o addon do fim da lista",
    addons_base_por_id("app.xperience.22222222-2222-2222-2222-222222222222"),
    "https://addon-18.test");
  conferirTexto("outra instancia continua separada",
    addons_base_por_id("app.xperience.11111111-1111-1111-1111-111111111111"),
    "https://addon-1.test");

  // 16 provedores retornam 20 legendas cada. A lista global comporta 256 e
  // distribui rodadas: o ultimo provedor tambem precisa aparecer.
  addons_buscar_legendas("tt1234567", "movie");
  for (int espera = 0; espera < 3000 && !addons_legendas_prontas(); espera++) usleep(1000);
  conferir("legendas de multiplos provedores", addons_n_legendas(), LEG_MAX);
  { int viuUltimo = 0, viuNome = 0, viuFps = 0, viuFallback = 0, viuSemNome = 0;
    for (int j = 0; j < addons_n_legendas(); j++) {
      const Legenda *l = addons_legenda(j);
      if (l && !strcmp(l->provedor, "Addon 17")) viuUltimo = 1;
      if (l && strstr(l->url, "/2/0.srt")) {
        viuNome = !strcmp(l->arquivo, "Teste.S01E01.srt") &&
                  !strcmp(l->lancamento, "Teste.S01E01.WEB-DL");
        viuFps = l->fps > 23.975 && l->fps < 23.977;
      }
      if (l && strstr(l->url, "/2/1.srt"))
        viuFallback = !strcmp(l->arquivo, "Teste.S01E02.srt");
      if (l && strstr(l->url, "/2/5.srt")) viuSemNome = !l->arquivo[0];
    }
    conferir("ultimo provedor de legenda presente", viuUltimo, 1);
    conferir("nome e release preservados", viuNome, 1);
    conferir("fpsMilli convertido", viuFps, 1);
    conferir("fileName alternativo", viuFallback, 1);
    conferir("sem nome permanece sem match", viuSemNome, 1);
  }

  // Duas configuracoes do AIOStreams com o mesmo nome devem ser consultadas
  // e aparecer como provedores separados na folha de fontes.
  { AddonRemoto duas[2] = {0}, exportadas[2] = {0};
    for (int i = 0; i < 2; i++) {
      snprintf(duas[i].nome, sizeof duas[i].nome, "AIOStreams");
      snprintf(duas[i].url, sizeof duas[i].url, "https://%s.test/manifest.json",
               i ? "antigo" : "exemplo");
      duas[i].ativo = 1;
    }
    conferir("duas instancias aplicadas", addons_definir_lista(duas, 2), 1);
    for (int i = 0; i < 2; i++)
      addons_manifesto_lido(i,
        "{\"id\":\"com.aiostreams\",\"name\":\"AIOStreams\","
        "\"resources\":[\"stream\"]}");
    conferirTexto("primeira instancia", addons_nome(0), "AIOStreams (1)");
    conferirTexto("segunda instancia", addons_nome(1), "AIOStreams (2)");
    resp[0] = "{\"streams\":[{\"url\":\"https://x/1.mp4\"}]}";
    resp[1] = "{\"streams\":[{\"url\":\"https://x/2.mp4\"}]}";
    pedidosExemplo = pedidosAntigo = 0;
    addons_buscar("tt9999999", "movie");
    while (addons_estado() == ADD_BUSCANDO) usleep(1000);
    conferir("primeira consultada", pedidosExemplo > 0, 1);
    conferir("segunda consultada", pedidosAntigo > 0, 1);
    conferir("duas fontes publicadas", fontesPublicadas, 2);
    conferirTexto("fonte da primeira", nomesPublicados[0], "AIOStreams (1)");
    conferirTexto("fonte da segunda", nomesPublicados[1], "AIOStreams (2)");
    addons_exportar(exportadas, 2);
    conferirTexto("nome da conta preservado", exportadas[1].nome, "AIOStreams");
    // Uma instalacao pode nao ter este titulo. Ela continua consultada e a
    // folha deve poder mostra-la como aba vazia pela lista de instalados.
    resp[1] = "{\"streams\":[]}";
    pedidosExemplo = pedidosAntigo = 0;
    addons_buscar("tt8888888", "movie");
    while (addons_estado() == ADD_BUSCANDO) usleep(1000);
    conferir("primeira ainda consultada", pedidosExemplo > 0, 1);
    conferir("segunda vazia ainda consultada", pedidosAntigo > 0, 1);
    conferir("uma fonte publicada", fontesPublicadas, 1);
    conferirTexto("segunda aba nomeada", addons_nome(1), "AIOStreams (2)");
  }

  // Configuracoes longas, como a do Comet, nao podem parar no byte 599.
  { AddonRemoto longo = {0}, exportado = {0};
    char pedido[ADD_REQUEST_URL_MAX];
    size_t inicio;
    snprintf(longo.nome, sizeof longo.nome, "Configurado");
    snprintf(longo.url, sizeof longo.url, "https://longo.test/");
    inicio = strlen(longo.url);
    memset(longo.url + inicio, 'a', 1100);
    snprintf(longo.url + inicio + 1100,
             sizeof longo.url - inicio - 1100, "/manifest.json?key=teste");
    longo.ativo = 1;
    conferir("URL longa aplicada", addons_definir_lista(&longo, 1), 1);
    conferir("URL longa preservada na fonte",
             addons_montar_url(0, "/stream/movie/tt1.json", pedido, sizeof pedido), 1);
    conferir("query preservada no recurso",
             strstr(pedido, "/stream/movie/tt1.json?key=teste") != NULL, 1);
    addons_exportar(&exportado, 1);
    conferirTexto("URL longa exportada", exportado.url, longo.url);
  }

  remove(caminho);
  rmdir(dir);
  if (falhas) { printf("%d falha(s)\n", falhas); return 1; }
  printf("addonslista: ok\n");
  return 0;
}
