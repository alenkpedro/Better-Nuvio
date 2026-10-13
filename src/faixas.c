#include "faixas.h"
#include "idioma.h"
#include "player.h"
#include "video.h"
#include "addons.h"
#include "gfx.h"
#include "text.h"
#include "anim.h"
#include "layout.h"
#include "legenda.h"
#include "subtitle_engine.h"
#include "assrender.h"
#include "ajustes.h"
#include "catalogo.h"
#include "streams.h"
#include "subtitle_match.h"
#include "subtitle_catalog.h"
#include "linguas.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

// O painel webOS do Enhanced mede 640px e fica a 64px da borda direita.
#define FX_LINHA   76.0f
#define FX_OPCAO   104.0f
#define FX_X       1216.0f
#define FX_W       640.0f
#define FX_MAX_GRUPOS (LEG_MAX + 2)

static int aberta, coluna, foco[3];
// Rolagem do audio e do estilo; idiomas e opcoes de legenda tem janelas proprias.
static int rolagem[3];
// Quantas linhas cabem no painel. Calculada no desenho (depende da altura
// escolhida ali) e lida pelo tratamento de tecla, que roda antes.
static int visiveis = 8;
static void ajustarRolagem(void);
static float anim;
static int paginaEstilo, paginaMix, focoCabecalho, focoIdioma, focoOpcao, focoOpcoes;
static int rolagemIdioma, rolagemOpcao;
static int estiloLado = -1;
static int syncFala, syncCapturado, syncN, syncFoco, syncAviso;
static int syncDeslocamento, syncInicio, syncTotal;
static double syncTempo;
static LegendaCue syncCues[7];

static int carregarFalas(int deslocamento) {
  return legenda_falas(player_leg_tempo_arquivo(syncTempo),
                       deslocamento, syncCues, 7, &syncFoco,
                       &syncInicio, &syncTotal);
}

static void corFocoFaixa(float *r, float *g, float *b) {
  float ar, ag, ab, lum, k = 0.74f;
  ajustes_acento(&ar, &ag, &ab);
  lum = 0.2126f * ar + 0.7152f * ag + 0.0722f * ab;
  if (lum > 0.88f) k = 0.88f;
  *r = 0.055f + (ar - 0.055f) * k;
  *g = 0.058f + (ag - 0.058f) * k;
  *b = 0.068f + (ab - 0.068f) * k;
}

// Qual legenda EXTERNA (OpenSubtitles) esta valendo, em indice da lista
// combinada — ou -1 quando a ativa e embutida ou nao ha nenhuma.
//
// Isto vive aqui e nao no video.c porque o pipeline nao devolve essa
// informacao: video_legenda_externa manda o setSubtitleSource com a URL e o
// legAtual do video.c fica intocado, apontando para a legenda EMBUTIDA de
// antes. Sem esta variavel, escolher uma legenda do OpenSubtitles fazia a
// marca de "ativa" ficar em outra linha (ou em "Desativada") e a folha
// reabria com o foco no lugar errado — a legenda certa tocava, so a folha
// mentia sobre qual era.
static int legExterna = -1;
static char legExternaUrl[4096],legExternaProvider[64];

static int legAuto;
static Uint32 legAutoDesde;
static int ehAss(const VideoFaixa *f) { return f && (!strcmp(f->codec,"S_TEXT/ASS") || !strcmp(f->codec,"S_TEXT/SSA")); }
int faixas_legenda_embutida_na_tv(void) {
#if (defined(__arm__) && !defined(__ANDROID__)) || defined(__EMSCRIPTEN__)
  return subtitle_engine_native();
#else
  return 0; /* Mac/Media3 deliver cues to the app, not to a native video plane. */
#endif
}
void faixas_reiniciar(void) {
  legExterna=-1;legExternaUrl[0]=0;aberta=syncFala=0;legAuto=1;legAutoDesde=0;
  subtitle_engine_reset();
}
static int legendaAtiva(void) {
  if(legExternaUrl[0]) {
    for(int i=0;i<addons_n_legendas();i++) {
      const Legenda *l=addons_legenda(i);
      if(l && !strcmp(l->url,legExternaUrl) && !strcmp(l->provedor,legExternaProvider))return video_n_legenda()+i;
    }
    return -1; // pending lookup must not mark an unrelated option as active
  }
  if(legExterna>=0)return legExterna;
  int selected=subtitle_engine_selected();
  return selected>=0?selected:video_legenda_atual();
}

// Paineis separados: 0 = Audio, 1 = Legendas. Cada um tem cabecalho e paginas.
static int modo;
// O painel de legenda tem idiomas e faixas empilhados; Estilo e outra pagina.
#define FX_COL_ESTILO 2
#define FX_N_ESTILO   10

typedef struct { char codigo[32]; char nome[72]; int total; } FxGrupo;
static FxGrupo grupos[FX_MAX_GRUPOS];
static int nGrupos, ordemLeg[LEG_MAX], ordemN;
static unsigned ordemVersao=(unsigned)-1;
static int compararOpcao(const void *a,const void *b) {
  int x=*(const int *)a,y=*(const int *)b;
  const Legenda *l=addons_legenda(x),*r=addons_legenda(y);
  int n=l && r?subtitle_option_compare(l,r):0;
  return n?n:x-y;
}
static int evidenciaLegenda(int indice) {
  const Stream *s = stream_item(stream_atual());
  const Legenda *l = addons_legenda(indice);
  const char *fonte;
  double fps;
  int arquivoCasa, lancamentoCasa;
  if (!s || !l) return 0;
  fonte = s->arquivo[0] ? s->arquivo :
          s->titulo[0] && (strstr(s->titulo, ".mkv") || strstr(s->titulo, ".mp4"))
            ? s->titulo :
          (strstr(s->url, ".mkv") || strstr(s->url, ".mp4")) ? s->url : NULL;
  if (!fonte) return 0;
  fps = video_fps_atual();
  arquivoCasa = subtitle_release_match(fonte, l->arquivo, fps, l->fps, 1);
  lancamentoCasa = subtitle_release_match(fonte, l->lancamento, fps, l->fps, 1);
  if (!arquivoCasa && !lancamentoCasa) return 0;
  return (arquivoCasa ? 2 : 1) + (fps > 0 && l->fps > 0 ? 2 : 0);
}

static int prioridadeGrupo(const FxGrupo *grupo) {
  const char *primeiro = ling_legenda(), *segundo = ling_legenda2();
  if (primeiro && *primeiro && strcasecmp(primeiro, "none")) {
    if (!strcasecmp(grupo->codigo, ling_grupo_codigo(primeiro))) return 0;
    if (ling_casa(grupo->codigo, primeiro)) return 1;
  }
  if (segundo && *segundo && strcasecmp(segundo, "none")) {
    if (!strcasecmp(grupo->codigo, ling_grupo_codigo(segundo))) return 2;
    if (ling_casa(grupo->codigo, segundo)) return 3;
  }
  return !strcmp(grupo->codigo, "und") ? 5 : 4;
}

static void montarGrupos(void) {
  unsigned versao=addons_legendas_versao();
  int total=addons_n_legendas();if(total>LEG_MAX)total=LEG_MAX;
  if(versao!=ordemVersao || total!=ordemN) {
    ordemVersao=versao;ordemN=total;
    for(int i=0;i<total;i++)ordemLeg[i]=i;
    qsort(ordemLeg,(size_t)total,sizeof ordemLeg[0],compararOpcao);
  }
  char focoAnterior[32] = "";
  if (focoIdioma >= 0 && focoIdioma < nGrupos)
    snprintf(focoAnterior, sizeof focoAnterior, "%s", grupos[focoIdioma].codigo);
  nGrupos = 1;
  memset(grupos, 0, sizeof grupos);
  snprintf(grupos[0].nome, sizeof grupos[0].nome, "%s", i18n("Nenhuma"));
  if (video_n_legenda() > 0) {
    snprintf(grupos[1].codigo, sizeof grupos[1].codigo, "embedded");
    snprintf(grupos[1].nome, sizeof grupos[1].nome, "%s", i18n("Embutidas"));
    grupos[1].total = video_n_legenda();
    nGrupos++;
  }
  for (int i = 0; i < addons_n_legendas(); i++) {
    const Legenda *l = addons_legenda(i);
    const char *cod = l ? l->idioma : "";
    if (!cod || !*cod) cod = "und";
    // O filtro de favoritos limita as buscas externas. A faixa selecionada
    // permanece visivel para que a folha nunca pareca ter perdido a selecao.
    if (!ling_legenda_visivel(cod) && video_n_legenda() + i != legendaAtiva()) continue;
    cod = ling_grupo_codigo(cod);
    int g;
    for (g = video_n_legenda() > 0 ? 2 : 1; g < nGrupos; g++)
      if (!strcasecmp(grupos[g].codigo, cod)) break;
    if (g == nGrupos) {
      if (nGrupos >= FX_MAX_GRUPOS) continue;
      snprintf(grupos[g].codigo, sizeof grupos[g].codigo, "%s", cod);
      snprintf(grupos[g].nome, sizeof grupos[g].nome, "%s",
               !strcmp(cod, "und") ? i18n("Idioma desconhecido") : i18n(ling_nome(cod)));
      nGrupos++;
    }
    grupos[g].total++;
  }
  // Desativada, Embutidas, idiomas preferidos e depois os demais em ordem
  // alfabetica. Desconhecido fica no fim.
  for (int i = video_n_legenda() > 0 ? 2 : 1; i < nGrupos; i++) {
    FxGrupo atual = grupos[i];
    int j = i;
    while (j > (video_n_legenda() > 0 ? 2 : 1)) {
      int pa = prioridadeGrupo(&atual), pb = prioridadeGrupo(&grupos[j-1]);
      if (pa > pb || (pa == pb && strcasecmp(atual.nome, grupos[j-1].nome) >= 0)) break;
      grupos[j] = grupos[j-1];
      j--;
    }
    grupos[j] = atual;
  }
  if (focoAnterior[0])
    for (int g = 0; g < nGrupos; g++)
      if (!strcmp(focoAnterior, grupos[g].codigo)) { focoIdioma = g; break; }
  if (focoIdioma >= nGrupos) focoIdioma = nGrupos - 1;
}

static int grupoDaFaixa(int i) {
  const Legenda *l;
  const char *cod;
  if (i < 0) return 0;
  if (i < video_n_legenda()) return video_n_legenda() > 0 ? 1 : 0;
  l = addons_legenda(i - video_n_legenda());
  cod = l ? l->idioma : "";
  if (!cod || !*cod) cod = "und";
  cod = ling_grupo_codigo(cod);
  for (int g = video_n_legenda() > 0 ? 2 : 1; g < nGrupos; g++)
    if (!strcasecmp(grupos[g].codigo, cod)) return g;
  return 0;
}

/* Uma unica legenda por idioma recebe o badge. Match pelo arquivo vale mais
 * que release alternativo; FPS declarado desempata. Empate preserva a ordem. */
static int melhorLegendaGrupo(int grupo) {
  int melhor = -1, evidencia = 0;
  for (int j = 0; j < addons_n_legendas(); j++) {
    int i = video_n_legenda() + j;
    int atual;
    if (grupoDaFaixa(i) != grupo) continue;
    atual = evidenciaLegenda(j);
    if (atual > evidencia) { melhor = i; evidencia = atual; }
  }
  return melhor;
}

static int faixaDaOpcao(int grupo, int opcao) {
  if (grupo <= 0 || grupo >= nGrupos) return -1;
  if (video_n_legenda() > 0 && grupo == 1)
    return opcao >= 0 && opcao < video_n_legenda() ? opcao : -1;
  for (int j = 0; j < ordemN; j++) {
    int i = video_n_legenda() + ordemLeg[j];
    if (grupoDaFaixa(i) != grupo) continue;
    if (opcao-- == 0) return i;
  }
  return -1;
}

static int nLinhas(int col);

void faixas_abrir(void) { faixas_abrir_em(0); }

// Abre o painel pedido pelo icone do player, na pagina de faixas.
void faixas_abrir_em(int col) {
  int n;
  aberta = 1;
  modo = (col == 1) ? 1 : 0;
  coluna = modo;                 // audio -> col 0; legenda -> col 1
  paginaEstilo = paginaMix = 0;
  syncFala = 0;
  estiloLado = -1;
  focoCabecalho = !modo && video_n_audio() == 0;
  rolagemIdioma = rolagemOpcao = 0;
  montarGrupos();
  focoIdioma = grupoDaFaixa(legendaAtiva());
  focoOpcoes = focoIdioma > 0;
  focoOpcao = 0;
  for (int i = 0; i < grupos[focoIdioma].total; i++)
    if (faixaDaOpcao(focoIdioma, i) == legendaAtiva()) { focoOpcao = i; break; }
  foco[0] = video_audio_atual();
  // A legenda pode estar desligada (-1); a primeira linha da coluna e sempre
  // "Desativada", entao o indice da lista e deslocado em um.
  foco[1] = legendaAtiva() + 1;
  // A folha pode abrir enquanto o video e varias buscas ainda disputam RAM.
  // A sonda do MKV continua no primeiro uso de uma faixa embutida (aplicar),
  // quando codec/ordinal sao de fato necessarios para reproduzi-la.
  if (modo) txt_podar_inativas(120);
  // Clamp nas duas colunas. A lista de legendas CRESCE durante a sessao (as do
  // OpenSubtitles chegam depois) e a de audio so existe apos o sourceInfo:
  // guardar um indice de antes e reabrir sem conferir poe o foco fora do vetor.
  { int c; for (c = 0; c < 3; c++) {
      n = nLinhas(c);
      if (foco[c] >= n) foco[c] = n > 0 ? n - 1 : 0;
      if (foco[c] < 0)  foco[c] = 0;
    rolagem[c] = 0;
    } }
}

int faixas_aberta(void) { return aberta; }

static int nLegendas(void) {
  int n = video_n_legenda() + addons_n_legendas();
  return n;
}

static int nLinhas(int col) {
  if (col == FX_COL_ESTILO) return FX_N_ESTILO;
  if (col == 0) { int n = video_n_audio(); return n; }
  return nLegendas() + 1;   // +1 pela linha "Desativada"
}

// --- COLUNA DE ESTILO --------------------------------------------------------
//
// Oito linhas "rotulo: valor". OK cicla o valor e aplica NA HORA. A ultima
// linha restaura o conjunto inteiro sem exigir dezenas de toques no controle.
static const char *const EST_ROT[FX_N_ESTILO] = {
  "Tamanho", "Negrito", "Cor", "Opacidade", "Fundo", "Posição", "Contorno", "Atraso",
  "Sincronizar por fala", "Restaurar padrão"
};
static const char *const EST_FUNDO[5] = { "Nenhum", "Escuro 25%", "Escuro 50%",
                                          "Escuro 75%", "Escuro 100%" };
static const char *const EST_OPAC[4]  = { "100%", "75%", "50%", "25%" };

static void valorEstilo(int linha, char *dst, size_t tam) {
  const VideoLegendaEstilo *e = player_leg_estilo();
  switch (linha) {
    case 0:
      snprintf(dst, tam, "%d%%", e->tamanho);
      break;
    case 1: snprintf(dst, tam, "%s", i18n(e->negrito ? "Ligado" : "Desligado")); break;
    case 2: snprintf(dst, tam, "%s", VIDEO_LEG_CORES_PT[e->cor % VIDEO_LEG_NCORES]); break;
    case 3: snprintf(dst, tam, "%s", EST_OPAC[e->opacidade > 3 ? 3 : e->opacidade]); break;
    case 4: snprintf(dst, tam, "%s", EST_FUNDO[e->fundo > 4 ? 4 : e->fundo]); break;
    case 5: snprintf(dst, tam, "%d", e->posicao); break;
    case 6: snprintf(dst, tam, "%s", i18n(e->borda ? "Ligado" : "Desligado")); break;
    case 7: {
      int a = e->atrasoMs;
      if (!a) snprintf(dst, tam, "0 s");
      else    snprintf(dst, tam, "%+.2f s", a / 1000.0f);
      break; }
    case 8: snprintf(dst, tam, "%s", i18n("Abrir")); break;
    default: snprintf(dst, tam, "Aplicar"); break;
  }
}

static int voltaIndice(int valor, int delta, int n) {
  return (valor + delta + n) % n;
}

static void ajustarEstilo(int linha, int dir) {
  VideoLegendaEstilo *e = player_leg_estilo();
  switch (linha) {
    case 0:
      e->tamanho += dir * 10;
      if (e->tamanho > 200) e->tamanho = 50;
      if (e->tamanho < 50) e->tamanho = 200;
      break;
    case 1: e->negrito = !e->negrito; break;
    // A cor escolhida vale para legendas simples e para as falas ASS.
    case 2: e->cor     = voltaIndice(e->cor,dir,VIDEO_LEG_NCORES); player_leg_estilo_tocou(PLR_LEG_COR); break;
    case 3: e->opacidade = voltaIndice(e->opacidade,dir,4); break;
    case 4: e->fundo   = voltaIndice(e->fundo,dir,5); break;
    case 5:
      e->posicao += dir * 5;
      if (e->posicao < -20) e->posicao = -20;
      if (e->posicao > 50) e->posicao = 50;
      break;
    case 6: e->borda   = e->borda ? 0 : 2; break;
    // O alcance e o passo seguem o Enhanced. Na borda mantemos o valor, sem
    // saltar de +180 s para -180 s ao pressionar mais uma vez.
    case 7:
      e->atrasoMs += dir * 100;
      if (e->atrasoMs > 180000) e->atrasoMs = 180000;
      if (e->atrasoMs < -180000) e->atrasoMs = -180000;
      break;
    case 8: return;
    default:
      *e = (VideoLegendaEstilo){ 100, 0, 0, 5, 2, 0, 0, 0 };
      // Restaurar o estilo padrao do app.
      player_leg_estilo_tocou(PLR_LEG_NADA);
      break;
  }
  player_leg_estilo_mudou();
}

// Rotulo da linha `i` da coluna de legenda. Ate video_n_legenda() sao as
// embutidas; depois vem as do OpenSubtitles.
static const char *rotuloLegenda(int i,const char **marca) {
  int embedded=video_n_legenda();
  if(i<embedded) {
    const VideoFaixa *f=video_legenda(i);
    *marca=subtitle_engine_selected()==i && subtitle_engine_loading()?i18n("Incorporada · carregando"):
      ehAss(f)?i18n("Incorporada · ASS"):i18n("Incorporada");
    return f?f->rotulo:"";
  }
  const Legenda *l=addons_legenda(i-embedded);*marca=l&&l->provedor[0]?l->provedor:i18n("Legenda");return l?l->rotulo:"";
}
static void escolherLegenda(int i) {
  player_leg_sincronizacao_limpar();legExterna=-1;legExternaUrl[0]=0;
  if(i>=video_n_legenda()) {
    const Legenda *l=addons_legenda(i-video_n_legenda());
    if(l&&l->url[0]){legExterna=i;snprintf(legExternaUrl,sizeof legExternaUrl,"%s",l->url);snprintf(legExternaProvider,sizeof legExternaProvider,"%s",l->provedor);subtitle_engine_select_headers(-1,l->url,l->cabecalhos);}
  } else subtitle_engine_select(i,NULL);
}

static void aplicar(void) {
  if (coluna == 0) {
    if (video_n_audio() > 0) video_escolher_audio(foco[0]);
  } else {
    // A pessoa escolheu: a automatica nao mexe mais nesta sessao, nem se a
    // escolha foi "Desativada".
    legAuto = 0;
    escolherLegenda(foco[1] - 1);
  }
}

// LEGENDA AUTOMATICA (#129). Roda a cada quadro enquanto `legAuto` esta de pe,
// e so decide quando da para decidir bem (ling_legenda_auto). Os prazos existem
// porque as duas listas podem nunca "fechar": a sonda do MKV so dispara com
// buffer saudavel, e numa fonte lenta isso demora; o fio de legendas consulta
// cada addon com 25 s de teto. Vencido o prazo, decide-se com o que ha.
#define FX_AUTO_EMB_MS  30000u   // espera pelos idiomas das embutidas
#define FX_AUTO_FIM_MS  60000u   // desiste de vez: legenda ligada no minuto 5 assusta
static void legendaAutomatica(Uint32 agora) {
  const char *emb[NV_FAIXA_MAX], *add[LEG_MAX];
  const CatItem *ci;
  int nEmb, nAdd = 0, embFechado, addFechado = 1, i, r;
  Uint32 passou;
  if (!legAuto || aberta || !player_aberto() || !player_com_video()) return;
  // Canal ao vivo nao tem legenda de addon nem idioma no arquivo que valha.
  if (player_id_canal()[0]) { legAuto = 0; return; }
  // A lista de faixas so existe depois do sourceInfo (LG) / lerFaixas (Tizen).
  // Antes dele a sonda "ja voltou" por falta de pendencia (ver
  // video_mkv_sondado) e as embutidas pareceriam fechadas e vazias.
  if (!legAutoDesde) legAutoDesde = agora | 1u;
  passou = agora - legAutoDesde;
  if (!video_n_audio() && !video_n_legenda() && passou < 8000u) return;
  nEmb = video_n_legenda();
  if (nEmb > NV_FAIXA_MAX) nEmb = NV_FAIXA_MAX;
  for (i = 0; i < nEmb; i++) { const VideoFaixa *f = video_legenda(i); emb[i] = f ? f->idioma : ""; }
  embFechado = video_mkv_sondado() != 0 || passou >= FX_AUTO_EMB_MS;
  // So confia na lista dos addons quando ela e DESTE titulo: sem imdb o app
  // nao pede legenda nenhuma (app.c), e o que estiver em memoria e do anterior.
  ci = cat_item(player_indice());
  if (ci && ci->imdb[0]) {
    nAdd = addons_n_legendas();
    if (nAdd > LEG_MAX) nAdd = LEG_MAX;
    for (i = 0; i < nAdd; i++) { const Legenda *l = addons_legenda(i); add[i] = l ? l->idioma : ""; }
    addFechado = addons_legendas_prontas();
  }
  if (passou >= FX_AUTO_FIM_MS) embFechado = addFechado = 1;
  r = ling_legenda_auto(ling_legenda(), emb, nEmb, embFechado, add, nAdd, addFechado);
  if (r == LING_AUTO_ESPERA) return;
  legAuto = 0;
  if (r == LING_AUTO_NADA) {
    if (ling_legenda()[0] && strcasecmp(ling_legenda(), "none"))
      printf("[legenda] automatica: nada em '%s' (%d embutida(s), %d de addon)\n",
             ling_legenda(), nEmb, nAdd);
    fflush(stdout);
    return;
  }
  if (r == subtitle_engine_selected() || (r>=nEmb && r==legExterna)) return;
  printf("[legenda] automatica: '%s' -> %s %d (%s) aos %u ms\n", ling_legenda(),
         r < nEmb ? "embutida" : "addon", r < nEmb ? r : r - nEmb,
         r < nEmb ? emb[r] : add[r - nEmb], (unsigned)passou);
  fflush(stdout);
  escolherLegenda(r);
}

void faixas_evento(const SDL_Event *e) {
  SDL_Keycode k;
  if (!aberta || e->type != SDL_KEYDOWN) return;
  k = e->key.keysym.sym;
  if (syncFala) {
    if (k == SDLK_AC_BACK || k == SDLK_ESCAPE || k == SDLK_BACKSPACE) {
      syncFala = 0;
    } else if (!syncCapturado && (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE)) {
      // Captura no mesmo relogio que desenha a legenda. posSeg sozinho e a
      // ultima amostra do pipeline e pode atrasar ate uma atualizacao inteira.
      syncTempo = player_posicao_legenda_seg();
      syncDeslocamento = 0;
      syncN = carregarFalas(syncDeslocamento);
      syncAviso = syncN == 0;
      if (syncN) syncCapturado = 1;
    } else if (syncCapturado) {
      int passo = k == SDLK_UP ? -1 : k == SDLK_DOWN ? 1
                : k == SDLK_LEFT || k == SDLK_PAGEUP ? -7
                : k == SDLK_RIGHT || k == SDLK_PAGEDOWN ? 7 : 0;
      if (passo) {
        int anterior = syncInicio + syncFoco;
        syncN = carregarFalas(syncDeslocamento + passo);
        if (syncN) {
          syncDeslocamento += syncInicio + syncFoco - anterior;
          syncAviso = 0;
        } else {
          syncCapturado = 0;
          syncAviso = 1;
        }
      } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
        // Delay is a single offset on the media clock; compensate for remote reaction time.
        int ajuste;
        ajuste = player_leg_sincronizar_fala(syncTempo, syncCues[syncFoco].inicio);
        if (!ajuste) { syncAviso = 3; return; }
        player_toast(i18n("Atraso da legenda ajustado"),4500);
        syncFala = 0;
      }
    }
    return;
  }
  montarGrupos();
  if (focoIdioma <= 0 || !grupos[focoIdioma].total) focoOpcoes = 0;
  if (k == SDLK_AC_BACK || k == SDLK_ESCAPE || k == SDLK_BACKSPACE) {
    if (paginaEstilo || paginaMix) {
      paginaEstilo = paginaMix = 0;
      focoCabecalho = !modo && video_n_audio() == 0;
      coluna = modo;
    } else aberta = 0;
    return;
  }
  if (focoCabecalho) {
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
      if (modo) { paginaEstilo = !paginaEstilo; coluna = paginaEstilo ? FX_COL_ESTILO : 1; estiloLado = -1; }
      else paginaMix = !paginaMix;
      focoCabecalho = 0;
      return;
    }
    if (k == SDLK_DOWN && (modo || (video_n_audio() > 0 && !paginaMix))) focoCabecalho = 0;
    return;
  }
  if (paginaMix) {
    if (k == SDLK_UP) focoCabecalho = 1;
    return;
  }
  if (paginaEstilo) {
    if (k == SDLK_UP) {
      if (foco[FX_COL_ESTILO] > 0) foco[FX_COL_ESTILO]--;
      else focoCabecalho = 1;
    } else if (k == SDLK_DOWN && foco[FX_COL_ESTILO] < FX_N_ESTILO - 1)
      foco[FX_COL_ESTILO]++;
    else if (k == SDLK_LEFT) {
      estiloLado = -1;
      if (foco[FX_COL_ESTILO] == 7) ajustarEstilo(7, -1);
    }
    else if (k == SDLK_RIGHT) {
      estiloLado = 1;
      if (foco[FX_COL_ESTILO] == 7) ajustarEstilo(7, 1);
    }
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
      if (foco[FX_COL_ESTILO] == 8) {
        syncFala = 1; syncCapturado = syncN = syncAviso = 0;
        syncDeslocamento = syncInicio = syncTotal = 0;
      } else ajustarEstilo(foco[FX_COL_ESTILO],estiloLado);
    }
    ajustarRolagem();
    return;
  }
  if (!modo) {
    coluna = 0;
    if (k == SDLK_UP) {
      if (foco[0] > 0) foco[0]--;
      else focoCabecalho = 1;
    } else if (k == SDLK_DOWN && foco[0] < nLinhas(0) - 1) foco[0]++;
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) aplicar();
    ajustarRolagem();
    return;
  }
  coluna = 1;
  if (k == SDLK_UP) {
    if (focoOpcoes) {
      if (focoOpcao > 0) focoOpcao--;
      else focoOpcoes = 0;
    } else if (focoIdioma > 0) { focoIdioma--; focoOpcao = rolagemOpcao = 0; }
    else focoCabecalho = 1;
  } else if (k == SDLK_DOWN) {
    if (focoOpcoes) {
      if (focoIdioma > 0 && focoOpcao < grupos[focoIdioma].total - 1) focoOpcao++;
    } else if (focoIdioma < nGrupos - 1) {
      focoIdioma++; focoOpcao = rolagemOpcao = 0;
    } else if (focoIdioma > 0) focoOpcoes = 1;
  } else if (k == SDLK_RIGHT) {
    if (focoIdioma > 0) focoOpcoes = 1;
  } else if (k == SDLK_LEFT) focoOpcoes = 0;
  else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
    int faixa;
    if (focoOpcoes) faixa = faixaDaOpcao(focoIdioma, focoOpcao);
    else faixa = focoIdioma ? faixaDaOpcao(focoIdioma, 0) : -1;
    if (faixa >= -1 && (faixa >= 0 || !focoIdioma)) {
      foco[1] = faixa + 1;
      aplicar();
      if (!focoOpcoes && faixa >= 0) { focoOpcoes = 1; focoOpcao = 0; }
    }
  }
}

void faixas_atualizar(float dt,Uint32 agora) {
  anim=anim_mola(anim,aberta?1.0f:0.0f,dt,NV_MOLA_TELA);
  legendaAutomatica(agora);
  subtitle_engine_tick(player_posicao_legenda_seg(), player_leg_estilo()->atrasoMs);
  if(subtitle_engine_take_error())player_toast(i18n("Não foi possível carregar esta legenda"),5000);
}

static void ajustarRolagem(void) {
  int n = nLinhas(coluna), f = foco[coluna], *r = &rolagem[coluna];
  if (visiveis < 1) return;
  if (f < *r) *r = f;
  else if (f >= *r + visiveis) *r = f - visiveis + 1;
  if (*r > n - visiveis) *r = n - visiveis;
  if (*r < 0) *r = 0;
}

static void linhaPainel(float x, float y, float w, const char *rot,
                         const char *sub, int focado, int ativo, int apagado,
                         int recomendado, float a) {
  GfxRect r = { x, y, w, 70 };
  float fr, fg, fb;
  corFocoFaixa(&fr, &fg, &fb);
  if (focado) {
    gfx_cor(r, .27f, fr, fg, fb, a);
    gfx_anel(r, .27f, 2, fr, fg, fb, a);
  } else if (ativo) gfx_cor(r, .27f, fr, fg, fb, .13f * a);
  int c = focado ? ajustes_tinta_foco() : apagado ? 128 : 245;
  int s = focado ? ajustes_tinta_foco2() : apagado ? 112 : 170;
  float textoW = w - (recomendado > 0 ? (ativo ? 222 : 182) : (ativo ? 104 : 52));
  txt_desenhar_alpha(txt_linha_corta(TXT_BODY, rot, c,c,c,255,textoW),
                    x+22, y+(sub && *sub ? 6 : 19), a);
  if (sub && *sub)
    txt_desenhar_alpha(txt_linha_corta(TXT_PG_FIM, sub, s,s,s,255,textoW),
                      x+22, y+37, a);
  if (recomendado > 0) {
    float bx = x+w-(ativo ? 202.f : 164.f);
    GfxRect selo = {bx,y+17,142,36};
    gfx_cor(selo,.24f,.10f,.75f,.24f,.24f*a);
    gfx_anel(selo,.24f,1,.14f,.94f,.35f,.82f*a);
    TxtLinha texto = txt_linha(TXT_PG_ROTULO,i18n("Recomendada"),
                               130,244,160,255);
    txt_desenhar_alpha(texto,bx+(142-texto.w)*.5f,y+21,a);
  }
  if (ativo)
    txt_desenhar_alpha(txt_linha(TXT_BODY,"✓",c,c,c,255),x+w-43,y+17,a);
}

static void linhaEstilo(float x, float y, float w, int i, int focado, float a) {
  char valor[64];
  int apagado = 0;
  valorEstilo(i,valor,sizeof valor);
  if (i == 8 || i == FX_N_ESTILO-1) {
    linhaPainel(x,y,w,i18n(EST_ROT[i]),valor,focado,0,apagado,-1,a);
    return;
  }
  GfxRect menos = {x+8,y+9,52,52}, mais = {x+w-60,y+9,52,52};
  GfxRect passo[2] = {menos,mais};
  float fr,fg,fb; corFocoFaixa(&fr,&fg,&fb);
  for (int j=0;j<2;j++) {
    int sel = focado && !apagado && (j ? estiloLado>0 : estiloLado<0);
    gfx_cor(passo[j],.3f,sel?fr:.15f,sel?fg:.15f,sel?fb:.15f,
            (sel?1.f:.75f)*a);
    gfx_anel(passo[j],.3f,2,sel?fr:.8f,sel?fg:.8f,sel?fb:.8f,
              (sel?1.f:.18f)*a);
    const char *simbolo = j ? "+" : "−";
    TxtLinha t = txt_linha(TXT_BODY,simbolo,
                           sel?ajustes_tinta_foco():apagado?110:235,
                           sel?ajustes_tinta_foco():apagado?110:235,
                           sel?ajustes_tinta_foco():apagado?110:235,255);
    txt_desenhar_alpha(t,passo[j].x+(passo[j].w-t.w)*.5f,
                        passo[j].y+(passo[j].h-t.h)*.5f,a);
  }
  int c=apagado?128:245, sub=apagado?112:170;
  TxtLinha l=txt_linha_corta(TXT_BODY,i18n(EST_ROT[i]),c,c,c,255,w-160);
  txt_desenhar_alpha(l,x+(w-l.w)*.5f,y+5,a);
  l=txt_linha_corta(TXT_PG_FIM,valor,sub,sub,sub,255,w-160);
  txt_desenhar_alpha(l,x+(w-l.w)*.5f,y+38,a);
}

static int rolarPara(int focoLinha, int total, int vis, int atual) {
  if (vis < 1) return 0;
  if (focoLinha < atual) atual = focoLinha;
  if (focoLinha >= atual + vis) atual = focoLinha - vis + 1;
  if (atual > total - vis) atual = total - vis;
  return atual < 0 ? 0 : atual;
}

void faixas_desenhar(Uint32 agora) {
  (void)agora;
  if (anim < .01f) return;
  float a = anim, h, y, contentX = FX_X + 32, contentW = FX_W - 64;
  int ativoGrupo, nAudio = video_n_audio();
  montarGrupos();
  ativoGrupo = grupoDaFaixa(legendaAtiva());
  if (focoIdioma >= nGrupos) focoIdioma = nGrupos - 1;
  if (modo) h = paginaEstilo || focoIdioma > 0 ? 850 : 152 + nGrupos*FX_LINHA;
  else h = paginaMix ? 340 : fmaxf(260,152+nAudio*FX_LINHA);
  if (h > (modo ? 850 : 720)) h = modo ? 850 : 720;
  y = 1024 - h;
  gfx_cor((GfxRect){0,0,NV_TELA_W,NV_TELA_H},0,0,0,0,.58f*a);
  gfx_cor((GfxRect){FX_X,y,FX_W,h},.06f,.02f,.02f,.02f,.96f*a);
  const char *titulo = modo ? paginaEstilo ? i18n("ESTILO DA LEGENDA") : i18n("LEGENDAS")
                           : paginaMix ? i18n("MIXAGEM") : i18n("ÁUDIO");
  const char *acao = modo ? paginaEstilo ? i18n("Legendas") : i18n("Ajustes")
                         : paginaMix ? i18n("Áudio") : i18n("Ajustes");
  txt_desenhar_alpha(txt_linha(TXT_PG_ROTULO,titulo,172,172,172,255),
                    contentX+20,y+42,a);
  GfxRect bot = { FX_X+FX_W-210, y+27, 174, 52 };
  gfx_cor(bot,.3f, focoCabecalho ? .30f : .12f,
          focoCabecalho ? .30f : .12f, focoCabecalho ? .30f : .12f,
          focoCabecalho ? a : .70f*a);
  if (focoCabecalho) {
    float fr,fg,fb; corFocoFaixa(&fr,&fg,&fb);
    gfx_cor(bot,.3f,fr,fg,fb,a);
    gfx_anel(bot,.3f,2,fr,fg,fb,a);
  } else gfx_anel(bot,.3f,2,.7f,.7f,.7f,.28f*a);
  TxtLinha bt = txt_linha(TXT_PG_ROTULO,acao,
                          focoCabecalho?ajustes_tinta_foco():242,
                          focoCabecalho?ajustes_tinta_foco():242,
                          focoCabecalho?ajustes_tinta_foco():242,255);
  txt_desenhar_alpha(bt,bot.x+(bot.w-bt.w)*.5f,bot.y+(bot.h-bt.h)*.5f,a);
  gfx_cor((GfxRect){contentX+8,y+96,contentW-16,1},0,1,1,1,.18f*a);
  float inicio = y+116, fim = y+h-28;
  if (paginaMix) {
    linhaPainel(contentX,inicio,contentW,i18n("Amplificação de áudio"),
                i18n("Indisponível neste dispositivo"),0,0,1,-1,a);
    linhaPainel(contentX,inicio+FX_LINHA,contentW,
                i18n("Salvar amplificação: Desligado"),
                i18n("A mixagem não está disponível no player nativo"),0,0,1,-1,a);
    return;
  }
  if (paginaEstilo) {
    int vis = (int)((fim-inicio)/FX_LINHA);
    if (vis < 1) vis = 1;
    visiveis = vis; coluna = FX_COL_ESTILO; ajustarRolagem();
    gfx_recorte(contentX,inicio,contentW,fim-inicio);
    for (int i = rolagem[FX_COL_ESTILO]; i < FX_N_ESTILO && i < rolagem[FX_COL_ESTILO]+vis; i++) {
      linhaEstilo(contentX,inicio+(i-rolagem[FX_COL_ESTILO])*FX_LINHA,
                  contentW,i,!focoCabecalho && foco[FX_COL_ESTILO]==i,a);
    }
    gfx_sem_recorte();
    if (syncFala) {
      GfxRect caixa = { 340, 165, 1240, 750 };
      gfx_cor((GfxRect){0,0,NV_TELA_W,NV_TELA_H},0,0,0,0,.82f*a);
      gfx_cor(caixa,.04f,.055f,.055f,.065f,a);
      txt_desenhar_alpha(txt_linha(TXT_TITULO3,i18n("Sincronizar por fala"),255,255,255,255),
                        caixa.x+56,caixa.y+42,a);
      if (!syncCapturado) {
        const char *aviso = syncAviso
          ? i18n("Sem falas disponíveis. Escolha uma legenda renderizada pelo app.")
          : i18n("Aperte OK no início da fala e escolha a fala correspondente.");
        txt_bloco(TXT_BODY,aviso,205,205,210,caixa.x+56,caixa.y+180,
                  caixa.w-112,42,a,3);
      } else {
        txt_desenhar_alpha(txt_linha(TXT_BODY,i18n("Selecione a fala que você ouviu:"),205,205,210,255),
                          caixa.x+56,caixa.y+122,a);
        char contador[48];
        snprintf(contador,sizeof contador,"%d / %d",syncInicio+syncFoco+1,syncTotal);
        TxtLinha cont = txt_linha(TXT_PG_ROTULO,contador,180,180,185,255);
        txt_desenhar_alpha(cont,caixa.x+caixa.w-56-cont.w,caixa.y+122,a);
        for (int i = 0; i < syncN; i++) {
          GfxRect linha = {caixa.x+48,caixa.y+192+i*69,caixa.w-96,62};
          if (i == syncFoco) {
            float fr,fg,fb; corFocoFaixa(&fr,&fg,&fb);
            gfx_cor(linha,.08f,fr,fg,fb,a);
          }
          char rot[890];
          int min = (int)syncCues[i].inicio / 60;
          double seg = syncCues[i].inicio - min*60;
          snprintf(rot,sizeof rot,"%02d:%05.2f  %s",min,seg,syncCues[i].texto);
          for (char *p = rot; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
          txt_desenhar_alpha(txt_linha_corta(TXT_BODY,rot,235,235,240,255,linha.w-32),
                            linha.x+16,linha.y+10,a);
        }
      }
      txt_desenhar_alpha(txt_linha(TXT_PG_FIM,
                        syncAviso == 3
                          ? i18n("Falas incompatíveis ou ajuste inicial acima de 3 min. Tente outra fala.")
                          : i18n("↑↓ falas · ←→ pular 7 · OK confirmar · Voltar cancelar"),
                        syncAviso == 3 ? 230 : 165,
                        syncAviso == 3 ? 170 : 165,
                        syncAviso == 3 ? 120 : 170,255),
                        caixa.x+56,caixa.y+caixa.h-52,a);
    }
    return;
  }
  if (!modo) {
    int vis = (int)((fim-inicio)/FX_LINHA);
    if (vis < 1) vis = 1;
    visiveis = vis; coluna = 0; ajustarRolagem();
    gfx_recorte(contentX,inicio,contentW,fim-inicio);
    for (int i = rolagem[0]; i < nAudio && i < rolagem[0]+vis; i++) {
      const VideoFaixa *f = video_audio(i);
      linhaPainel(contentX,inicio+(i-rolagem[0])*FX_LINHA,contentW,
                   f ? f->rotulo : "",f ? i18n(ling_nome(f->idioma)) : "",
                   !focoCabecalho && foco[0]==i,i==video_audio_atual(),0,-1,a);
    }
    gfx_sem_recorte();
    if (!nAudio)
      txt_bloco(TXT_PG_FIM,i18n("Nenhuma faixa de áudio disponível nesta fonte."),
                180,180,180,contentX+20,inicio+22,contentW-40,28,a,2);
    return;
  }
  int linguaVis = (int)((focoIdioma > 0 ? 300.f : fim-inicio)/FX_LINHA);
  if (linguaVis < 1) linguaVis = 1;
  if (linguaVis > nGrupos) linguaVis = nGrupos;
  rolagemIdioma = rolarPara(focoIdioma,nGrupos,linguaVis,rolagemIdioma);
  float linguaH = linguaVis*FX_LINHA;
  gfx_recorte(contentX,inicio,contentW,linguaH);
  for (int g = rolagemIdioma; g < nGrupos && g < rolagemIdioma+linguaVis; g++) {
    float rowY=inicio+(g-rolagemIdioma)*FX_LINHA;
    linhaPainel(contentX,rowY,contentW,grupos[g].nome,"",
                !focoCabecalho && !focoOpcoes && focoIdioma==g,g==ativoGrupo,0,-1,a);
    if(g) {
      char count[16];snprintf(count,sizeof count,"%d",grupos[g].total);
      TxtLinha name=txt_linha(TXT_BODY,grupos[g].nome,245,245,245,255);
      TxtLinha badge=txt_linha(TXT_PG_FIM,count,200,200,200,255);
      float bx=fminf(contentX+22+name.w+12,contentX+contentW-92);
      gfx_cor((GfxRect){bx,rowY+23,badge.w+14,24},.2f,1,1,1,.09f*a);
      txt_desenhar_alpha(badge,bx+7,rowY+22,a);
    }
  }
  gfx_sem_recorte();
  if (focoIdioma <= 0) return;
  float optsY = inicio+linguaH+28;
  gfx_cor((GfxRect){contentX+8,optsY-14,contentW-16,1},0,1,1,1,.14f*a);
  int optsVis = (int)((fim-optsY)/FX_OPCAO);
  if (optsVis < 1) optsVis = 1;
  if (focoOpcao >= grupos[focoIdioma].total) focoOpcao = grupos[focoIdioma].total-1;
  if (focoOpcao < 0) focoOpcao = 0;
  rolagemOpcao = rolarPara(focoOpcao,grupos[focoIdioma].total,optsVis,rolagemOpcao);
  int melhor = melhorLegendaGrupo(focoIdioma);
  gfx_recorte(contentX,optsY,contentW,fim-optsY);
  for (int o=rolagemOpcao; o<grupos[focoIdioma].total && o<rolagemOpcao+optsVis; o++) {
    int i = faixaDaOpcao(focoIdioma,o);
    const char *marca = NULL, *rot = rotuloLegenda(i,&marca);
    if (!marca) marca = i < video_n_legenda() ? i18n("Incorporada") : "OpenSubtitles";
    int recomendado = i == melhor;
    float optionY=optsY+(o-rolagemOpcao)*FX_OPCAO;
    if(i<video_n_legenda()) {
      linhaPainel(contentX,optionY,contentW,rot,marca,
                  !focoCabecalho && focoOpcoes && focoOpcao==o,i==legendaAtiva(),0,recomendado,a);
    } else {
      const Legenda *l=addons_legenda(i-video_n_legenda());
      int focused=!focoCabecalho && focoOpcoes && focoOpcao==o,active=i==legendaAtiva();
      float fr,fg,fb;corFocoFaixa(&fr,&fg,&fb);
      GfxRect box={contentX,optionY,contentW,FX_OPCAO-6};
      if(focused){gfx_cor(box,.2f,fr,fg,fb,a);gfx_anel(box,.2f,2,fr,fg,fb,a);}
      else if(active)gfx_cor(box,.2f,fr,fg,fb,.13f*a);
      int c=focused?ajustes_tinta_foco():245,secondary=focused?ajustes_tinta_foco2():170;
      float width=contentW-(active?76:44);
      TxtLinha provider=txt_linha_corta(TXT_PG_FIM,marca,secondary,secondary,secondary,255,width-16);
      gfx_cor((GfxRect){contentX+22,optionY+7,provider.w+16,26},.18f,1,1,1,.09f*a);
      txt_desenhar_alpha(provider,contentX+30,optionY+8,a);
      txt_desenhar_alpha(txt_linha_corta(TXT_BODY,rot,c,c,c,255,width),contentX+22,optionY+34,a);
      txt_desenhar_alpha(txt_linha_corta(TXT_PG_FIM,l?subtitle_metadata(l):"",secondary,secondary,secondary,255,
                                       width-(recomendado?152:0)),contentX+22,optionY+68,a);
      if(recomendado) {
        GfxRect badge={contentX+contentW-(active?202:164),optionY+60,142,32};
        gfx_cor(badge,.24f,.10f,.75f,.24f,.24f*a);gfx_anel(badge,.24f,1,.14f,.94f,.35f,.82f*a);
        TxtLinha label=txt_linha(TXT_PG_ROTULO,i18n("Recomendada"),130,244,160,255);
        txt_desenhar_alpha(label,badge.x+(badge.w-label.w)*.5f,badge.y+2,a);
      }
      if(active)txt_desenhar_alpha(txt_linha(TXT_BODY,"✓",c,c,c,255),contentX+contentW-43,optionY+37,a);
    }
  }
  gfx_sem_recorte();
}
