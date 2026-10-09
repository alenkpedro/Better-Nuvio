#include "text.h"
#include "idioma.h"
#include "gfx.h"
#include "layout.h"
#include "marco.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Quantas linhas de texto ficam guardadas ao mesmo tempo.
//
// 256 chegou ao limite quando a pagina de titulo passou a ter a secao de
// comentarios: cada cartao sao ~7 linhas (nome, cinco de texto e o rodape) e
// eles convivem com episodios, elenco, abas, sinopse e as duas linhas de meta.
// Passando do teto, o LRU despeja linhas que a PROPRIA TELA ainda vai desenhar
// no mesmo quadro; elas voltam pela fila de TXT_POR_QUADRO, duas por vez, e
// sao despejadas de novo. E esse laco que aparece como o texto "piscando".
//
// 512 nao muda o custo de busca (a sondagem parte do hash e para no primeiro
// buraco) nem o de rasterizacao. Custa memoria de textura para linhas que nao
// estao na tela — o preco de nao rerasterizar as que estao.
#define MAX_LINHAS 512

typedef struct {
  char chave[288];
  unsigned long hash;   // FNV-1a da chave, para pular o strcmp
  TxtLinha linha;
  unsigned long uso;
  unsigned long quadroUso;
  int ocupado;
} Entrada;

// Fator entre o pixel do BUFFER e o pixel de layout. As fontes sao abertas em
// `corpo * escala` e a linha cacheada guarda a medida DIVIDIDA por ele, entao
// todo o resto do app continua medindo em 1920x1080 enquanto o glifo tem a
// resolucao real da tela.
//
// Sem isto o texto era rasterizado a 1080p e ampliado ao dobro na TV 4K — que
// e exatamente o borrao que o dono viu comparando com o app web, onde o
// navegador rasteriza no devicePixelRatio.
static float escalaTxt = 1.0f;
static TTF_Font *fontes[TXT_NFONTES];

// Os arquivos TTF dos quatro pesos, LIDOS UMA VEZ e mantidos vivos enquanto o app
// vive: as faces do FreeType leem deles sob demanda, entao liberar aqui e
// leitura de memoria liberada no primeiro glifo novo. Sao ~1,6 MB no total.
// `donoPeso` marca quais ponteiros sao proprios: pesos que apontam para o mesmo
// arquivo compartilham o buffer e so um deles libera.
static unsigned char *bytesPeso[4];
static size_t         tamPeso[4];
static int            donoPeso[4];
// A familia editorial nao segue a escolha de fonte da interface. Os quatro
// pesos ficam vivos ate txt_encerrar porque as faces SDL_ttf leem o buffer.
static unsigned char *bytesCond[4];
static size_t         tamCond[4];
// O RWops de cada estilo. Guardado porque abrimos com freesrc=0 (o buffer e
// compartilhado, a fonte nao pode fecha-lo) e alguem tem de fechar em
// txt_encerrar.
static SDL_RWops     *rwFonte[TXT_NFONTES];
static char baseFontes[512];
static int fonteAppEscolhida;
static int fonteAppPronta;

// Le o arquivo inteiro para um buffer novo. NULL se nao abrir.
static unsigned char *lerTudo(const char *caminho, size_t *tam) {
  FILE *f = fopen(caminho, "rb");
  unsigned char *b;
  long n;
  *tam = 0;
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  n = ftell(f);
  if (n <= 0) { fclose(f); return NULL; }
  rewind(f);
  b = malloc((size_t)n);
  if (!b) { fclose(f); return NULL; }
  if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
  fclose(f);
  *tam = (size_t)n;
  return b;
}
// As alternativas so sao abertas para os 16 estilos de legenda e sob demanda.
// Abrir a matriz inteira (todas as familias x todos os estilos do app) gastaria
// memoria numa TV fraca por uma preferencia que afeta no maximo quatro linhas.
#define TXT_LEG_N (TXT_LEG_200 - TXT_LEG_50 + 1)
static TTF_Font *fontesLegAlt[TXT_FAMILIA_N][TXT_LEG_N];
static TTF_Font *fontesLegInter[TXT_LEG_N];
static SDL_RWops *rwLegInter[TXT_LEG_N];
static unsigned char *bytesLegInter;
static size_t tamLegInter;
static unsigned char fonteLegTentada[TXT_FAMILIA_N][TXT_LEG_N];
static int avisoFallback[TXT_FAMILIA_N];
static TTF_Font *fontesNetflix[2][TXT_LEG_N];
static SDL_RWops *rwNetflix[2][TXT_LEG_N];
static unsigned char *bytesNetflix[2];
static size_t tamNetflix[2];
static char caminhoNetflix[2][512];
static unsigned char netflixTentada[2][TXT_LEG_N];
const char *const TXT_FAMILIAS_PT[TXT_FAMILIA_N] = {
  "Inter", "LG Display", "Droid Sans", "Netflix Sans"
};

static Entrada cache[MAX_LINHAS];

// Quantas linhas NOVAS podem ser rasterizadas por quadro.
//
// Medido no aparelho: entrar na pagina de detalhe rasteriza 12 linhas de uma
// vez e custa 12 ms — um quadro inteiro, e o tranco aparece exatamente na
// transicao que se quer suave. Rasterizar em conta-gotas faz o texto assentar
// um ou dois quadros depois, o que ninguem ve; o tranco, todo mundo ve.
// 2 e nao 4: com 4 o pior quadro media 6 ms so de texto, e o objetivo aqui e
// que NENHUMA parte sozinha coma mais que um terco do quadro.
// QUATRO, e o numero e MEDIDO e nao escolhido.
//
// Eram dois, e o teto existe por um motivo real: rasterizar custa, e uma tela
// nova pede dezenas de linhas ineditas de uma vez. Mas com dois, ABRIR UM MODAL
// deixava a maior parte do texto em branco por varios quadros — as linhas vao
// entrando de duas em duas e, num aparelho mais lento, isso se le como "o texto
// some". Foi o relato do dono no Tizen, e a mesma coisa que a foto do modal de
// opcoes mostrou com uma linha vazia.
//
// MEDIDO na LG OLED65C9 antes de mexer: "texto 2.5ms em 2 linhas", pior quadro
// 23,6 ms, 0 janks. Quatro linhas custam ~5 ms no quadro em que a tela abre, e
// SO nesse — em regime a telemetria diz "texto 0.0ms em 0 linhas". O contador
// de janks e quem decide se este numero pode subir mais; se ele sair de zero
// abrindo tela, o certo e voltar a tres, nao ignorar.
#define TXT_POR_QUADRO 4
static int rastNesteQuadro;
static unsigned long quadroTxt = 1;

void txt_novo_quadro(void) { rastNesteQuadro = 0; quadroTxt++; }

void txt_podar_inativas(unsigned quadros) {
  for (int i = 0; i < MAX_LINHAS; i++) {
    if (!cache[i].ocupado || quadroTxt - cache[i].quadroUso <= quadros) continue;
    if (cache[i].linha.tex) {
      gfx_tex_esquecer(cache[i].linha.tex);
      glDeleteTextures(1, &cache[i].linha.tex);
    }
    memset(&cache[i], 0, sizeof cache[i]);
  }
}
static unsigned long relogio = 1;
int    txt_rasterizadas = 0;
// Quantas linhas foram DESPEJADAS para dar lugar a outras. Zero e o estado
// saudavel. Se voltar a subir com a tela parada, a tabela encheu de novo e o
// texto vai piscar — e melhor ler isso num contador do que descobrir pela
// reclamacao de quem esta olhando a tela.
int    txt_despejos = 0;
double txt_ms = 0.0;

// Peso por estilo. As familias embarcadas usam arquivos de peso real quando
// disponiveis (Inter Display nao inclui SemiBold: usa Medium nesse caso).
// O negrito sintetico (TTF_SetFontStyle) so entra
// nas familias de RESERVA (LG, Droid), que nao tem arquivo Bold proprio. Isso importa
// porque negrito sintetico engorda os tracos sem redesenhar nada, e ao lado de
// um Bold de verdade a diferenca aparece logo nos titulos grandes.
//
// A Inter comum embarcada alarga em torno de 10% as linhas da interface em
// relacao à Inter Display. A ultima corresponde melhor à mancha compacta das
// referencias da Home e dos cards. A Inter comum continua nas legendas.
enum { PESO_REGULAR, PESO_MEDIUM, PESO_SEMIBOLD, PESO_BOLD, PESO_N };
static const struct { int corpo, peso; } ESTILOS[TXT_NFONTES] = {
  { NV_FT_TITULO1,  PESO_BOLD   },   // titulo do filme na tela de detalhe
  // ERA PESO_REGULAR, pelo cabecalho espacado da pagina de titulo do app da
  // Apple. Esse cabecalho NAO EXISTE MAIS: a tela de detalhe do web e um
  // documento rolavel sem cabecalho fixo, e o do app da Apple saiu do port.
  // Hoje TXT_TITULO2 e usado so por "Biblioteca" (.library-page-title 56/600) e
  // pelos titulos de estado vazio da busca e da biblioteca — os TRES em peso
  // 600 no web. Regular errava 200 para baixo em todos.
  { NV_FT_TITULO2,  PESO_SEMIBOLD }, // .library-page-title e estados vazios
  { NV_FT_TITULO3,  PESO_BOLD   },   // nome dentro do card destaque
  { NV_FT_HEADLINE, PESO_SEMIBOLD }, // cabecalho de fileira
  { NV_FT_BODY,     PESO_MEDIUM },   // rotulo de botao, titulo de episodio
  { NV_FT_CALLOUT,  PESO_MEDIUM },   // linha de genero
  { NV_FT_CAPTION,  PESO_REGULAR },  // sinopse, texto corrido
  { NV_FT_CAPTION2, PESO_REGULAR },  // creditos, datas, rotulos
  // Abaixo do minimo de 23px que o tvOS estabelece para TEXTO — mas isto nao e
  // texto para ler, e um selo de classificacao indicativa, que no aparelho tem
  // mesmo o tamanho de um icone.
  { NV_FT_MINI,     PESO_BOLD    },  // badge de classificacao
  // Player, do app web: titulo em 700 e o corpo em 400 (.player-title tem
  // font-weight 700; .player-subtitle e .player-time-label nao declaram peso e
  // herdam o normal).
  { NV_FT_PLR_TITULO, PESO_BOLD    },
  { NV_FT_PLR_CORPO,  PESO_REGULAR },
  { NV_FT_ROW_TITULO, PESO_SEMIBOLD },// .home-row-title (600)
  { NV_FT_HERO_SEC,   PESO_SEMIBOLD },// .home-modern-hero-secondary (600)
  { NV_FT_DET_BOTAO, PESO_SEMIBOLD },// .series-primary-btn (600)
  { NV_FT_DET_META,  PESO_REGULAR },
  { NV_FT_DET_SIN,   PESO_REGULAR },
  { NV_FT_DET_META2, PESO_REGULAR },
  { NV_FT_HERO_META, PESO_MEDIUM  },   // .home-modern-hero-meta-line (21/500)
  { NV_FT_HERO_SIN,  PESO_REGULAR },   // .home-hero-description (22/400)
  { NV_FT_PG_RELOGIO, PESO_SEMIBOLD},  // .player-clock (26/600)
  { NV_FT_PG_FIM,     PESO_REGULAR },  // .player-ends-at (20/400)
  { NV_FT_PG_ROTULO,  PESO_SEMIBOLD},  // .player-parental-label (22/600)
  { NV_FT_PG_GRAV,    PESO_REGULAR },  // .player-parental-severity (22/400)
  { 36, PESO_REGULAR },             // cabecalhos dos paineis do player oficial
  { 24, PESO_BOLD },                // episodio/fonte dentro da lista
  { 28, PESO_MEDIUM },              // titulo no card Continuar assistindo
  { 23, PESO_REGULAR },             // temporada e nome do episodio
  { 20, PESO_MEDIUM },              // tempo restante no badge do card
  { 110, PESO_BOLD },               // posição real no ranking
  // Mesmo 4,4vh do overlay HTML do Enhanced em 1920x1080: 48 px a 100%.
  // Antes 100% abria 40 px, entao a porcentagem nao batia com o app web.
  { 24, PESO_REGULAR }, { 29, PESO_REGULAR }, { 34, PESO_REGULAR },
  { 38, PESO_REGULAR }, { 43, PESO_REGULAR }, { 48, PESO_REGULAR },
  { 53, PESO_REGULAR }, { 58, PESO_REGULAR }, { 62, PESO_REGULAR },
  { 67, PESO_REGULAR }, { 72, PESO_REGULAR }, { 77, PESO_REGULAR },
  { 82, PESO_REGULAR }, { 86, PESO_REGULAR }, { 91, PESO_REGULAR },
  { 96, PESO_REGULAR },
  { NV_FT_TITULO1, PESO_BOLD }, { NV_FT_TITULO2, PESO_SEMIBOLD },
  { NV_FT_TITULO3, PESO_BOLD }, { NV_FT_HEADLINE, PESO_SEMIBOLD },
  { NV_FT_CALLOUT, PESO_MEDIUM }, { NV_FT_CAPTION, PESO_REGULAR },
  { NV_FT_CAPTION2, PESO_REGULAR }, { NV_FT_MINI, PESO_BOLD },
  { NV_FT_PLR_CORPO, PESO_REGULAR },
  { NV_FT_ROW_TITULO, PESO_SEMIBOLD },
  { NV_FT_HERO_SEC, PESO_SEMIBOLD },
  { NV_FT_HERO_META, PESO_MEDIUM },
  { NV_FT_HERO_SIN, PESO_REGULAR },
  { NV_FT_DET_BOTAO, PESO_SEMIBOLD },
  { NV_FT_DET_META, PESO_REGULAR },
  { NV_FT_DET_SIN, PESO_REGULAR },
  { NV_FT_DET_META2, PESO_REGULAR },
  { 36, PESO_REGULAR }, { 24, PESO_BOLD },
  { 28, PESO_MEDIUM }, { 23, PESO_REGULAR },
  { 20, PESO_MEDIUM }, { NV_FT_PG_FIM, PESO_REGULAR },
  { NV_FT_BODY, PESO_MEDIUM },
};

typedef struct {
  TTF_Font *face[TXT_NFONTES];
  SDL_RWops *rw[TXT_NFONTES];
  unsigned char *bytes[PESO_N];
  size_t tam[PESO_N];
  int dono[PESO_N];
} FontesApp;

static void fecharFontesApp(FontesApp *f) {
  for (int i = 0; i < TXT_NFONTES; i++) {
    if (f->face[i]) TTF_CloseFont(f->face[i]);
    if (f->rw[i]) SDL_FreeRW(f->rw[i]);
  }
  for (int p = 0; p < PESO_N; p++) if (f->dono[p]) free(f->bytes[p]);
  memset(f, 0, sizeof *f);
}

// Abre todas as faces ANTES de mexer nas atuais: se faltar um arquivo no
// pacote, a escolha anterior continua inteiramente funcional.
static int abrirFontesApp(int escolha, FontesApp *f) {
  static const char *const nomes[3][PESO_N] = {
    { "InterDisplay-Regular.ttf", "InterDisplay-Medium.ttf",
      "InterDisplay-Medium.ttf", "InterDisplay-Bold.ttf" },
    { "DMSans-Regular.ttf", "DMSans-Medium.ttf", "DMSans-Bold.ttf", "DMSans-Bold.ttf" },
    { "OpenSans-Regular.ttf", "OpenSans-SemiBold.ttf", "OpenSans-SemiBold.ttf", "OpenSans-Bold.ttf" },
  };
  static const char *const lg[PESO_N] = {
    "/usr/share/fonts/LG_Display-Light.ttf", "/usr/share/fonts/LG_Display-Regular.ttf",
    "/usr/share/fonts/LG_Display-Regular.ttf",
    "/usr/share/fonts/LG_Display-Regular.ttf" };
  static const char *const droid[PESO_N] = {
    "/usr/share/fonts/DroidSans.ttf", "/usr/share/fonts/DroidSans.ttf",
    "/usr/share/fonts/DroidSans.ttf", "/usr/share/fonts/DroidSans.ttf" };
  char caminho[PESO_N][512];
  const char *arquivo[PESO_N];
  memset(f, 0, sizeof *f);
  if (escolha < 0 || escolha > 4) return 0;
  for (int p = 0; p < PESO_N; p++) {
    if (escolha < 3) {
      snprintf(caminho[p], sizeof caminho[p], "%sfonts/%s", baseFontes, nomes[escolha][p]);
      arquivo[p] = caminho[p];
    } else arquivo[p] = escolha == 3 ? lg[p] : droid[p];
    int anterior;
    for (anterior = 0; anterior < p; anterior++)
      if (!strcmp(arquivo[p], arquivo[anterior])) break;
    if (anterior < p) {
      f->bytes[p] = f->bytes[anterior];
      f->tam[p] = f->tam[anterior];
      continue;
    }
    f->bytes[p] = lerTudo(arquivo[p], &f->tam[p]);
    if (!f->bytes[p]) { fecharFontesApp(f); return 0; }
    f->dono[p] = 1;
  }
  for (int i = 0; i < TXT_NFONTES; i++) {
    int peso = ESTILOS[i].peso;
    int editorial = i >= TXT_COND_TITULO1 && bytesCond[peso];
    const unsigned char *bytes = editorial ? bytesCond[peso] : f->bytes[peso];
    size_t tam = editorial ? tamCond[peso] : f->tam[peso];
    f->rw[i] = SDL_RWFromConstMem(bytes, (int)tam);
    if (!f->rw[i]) { fecharFontesApp(f); return 0; }
    f->face[i] = TTF_OpenFontRW(f->rw[i], 0,
                   (int)(ESTILOS[i].corpo * escalaTxt + 0.5f));
    if (!f->face[i]) { fecharFontesApp(f); return 0; }
    if (!editorial && escolha >= 3 && peso >= PESO_SEMIBOLD)
      TTF_SetFontStyle(f->face[i], TTF_STYLE_BOLD);
  }
  return 1;
}

static void limparLinhas(void) {
  for (int i = 0; i < MAX_LINHAS; i++) {
    if (cache[i].ocupado && cache[i].linha.tex) {
      gfx_tex_esquecer(cache[i].linha.tex);
      glDeleteTextures(1, &cache[i].linha.tex);
    }
  }
  memset(cache, 0, sizeof cache);
  rastNesteQuadro = 0;
}

static void ativarFontesApp(FontesApp *novo) {
  FontesApp antigo = {0};
  for (int i = 0; i < TXT_NFONTES; i++) {
    antigo.face[i] = fontes[i]; antigo.rw[i] = rwFonte[i];
    fontes[i] = novo->face[i]; rwFonte[i] = novo->rw[i];
    novo->face[i] = NULL; novo->rw[i] = NULL;
  }
  for (int p = 0; p < PESO_N; p++) {
    antigo.bytes[p] = bytesPeso[p]; antigo.tam[p] = tamPeso[p];
    antigo.dono[p] = donoPeso[p];
    bytesPeso[p] = novo->bytes[p]; tamPeso[p] = novo->tam[p];
    donoPeso[p] = novo->dono[p];
    novo->bytes[p] = NULL; novo->tam[p] = 0; novo->dono[p] = 0;
  }
  fecharFontesApp(&antigo);
}

// RESERVA PARA O QUE A INTER NAO TEM.
//
// A Inter cobre latim, e so. Um titulo japones da filmografia de um ator (a
// tela nova de pessoa mostra varios) saia como fileira de quadradinhos — a
// fonte nao tem o glifo e o SDL_ttf desenha .notdef sem reclamar. A TV traz
// /usr/share/fonts/DroidSansFallback.ttf, que cobre CJK; abrimos ela SOB
// DEMANDA, no mesmo corpo do estilo, e so para as linhas que precisam.
//
// Nao e fallback por glifo (isso exigiria compor a linha caractere a caractere
// e perder o kerning): a linha INTEIRA vai para a reserva quando o primeiro
// caractere fora do ASCII nao existir na fonte principal. Titulo misto
// "Deadpool & ウルヴァリン" sairia todo na reserva, o que e feio mas legivel —
// e o caso raro; o comum e a linha ser toda de uma escrita so.
// Uma reserva POR ESCRITA. A primeira versao tinha um arquivo so, escolhido
// como "o CJK", e o nome de uma atriz iraniana continuava em quadradinhos: a
// DroidSansFallback nao tem arabe. A TV traz arquivo separado para cada
// familia de escrita, e e por isso que a escolha e por faixa de codepoint.
typedef enum { ESC_CJK, ESC_ARABE, ESC_CIRILICO_ETC, ESC_N } Escrita;
static TTF_Font *reservas[ESC_N][TXT_NFONTES];
static char caminhoReserva[ESC_N][512];

// Primeiro codepoint FORA do ASCII, ou 0. Decodifica UTF-8 na mao porque e o
// unico ponto do app que precisa disso e puxar uma biblioteca por causa de tres
// linhas nao se paga.
static Uint32 primeiroNaoAscii(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  for (; *p; p++) {
    if (*p < 0x80) continue;
    if ((*p & 0xE0) == 0xC0 && p[1])
      return (Uint32)((*p & 0x1F) << 6 | (p[1] & 0x3F));
    if ((*p & 0xF0) == 0xE0 && p[1] && p[2])
      return (Uint32)((*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F));
    if ((*p & 0xF8) == 0xF0) return 0x10000;   // fora do BMP: nao tratamos
    return 0;
  }
  return 0;
}

// Um codepoint DECORATIVO: simbolo, seta, pictograma, emoji, seletor de
// variacao. Nao pertence a escrita nenhuma e por isso nao pode decidir com que
// fonte a LINHA INTEIRA e desenhada.
//
// POR QUE ISTO EXISTE, com a cobertura da Inter MEDIDA e nao suposta.
//
// fonteDe manda a linha toda para a fonte de reserva quando o primeiro
// caractere fora do ASCII nao existe na Inter. A regra e certa para escrita
// ("Deadpool & ウルヴァリン" sai legivel na DroidSansFallback) e ERRADA para
// simbolo. Os nomes de fonte que os addons devolvem sao cheios de decoracao, e
// um simbolo que a Inter nao tem bastava para a lista de fontes INTEIRA trocar
// para a DroidSansFallback, que e uma fonte CJK: o latim dela e mais pesado e
// com outro hinting, o que na tela le exatamente como o issue #7 — "todo o
// texto aparece em negrito, a informacao fica pixelada". Era tambem trabalho a
// mais (abrir um segundo arquivo de fonte por estilo e rasterizar com uma fonte
// muito maior) na tela que o mesmo relato descreve como lenta.
//
// CONFERIDO na antiga Inter Display embarcada, por TTF_GlyphIsProvided:
//   TEM      ← ↑ → ↓  •  …  –  —  " '  ★  ▶  ✓  ·
//   NAO TEM  ⚡  ⚙  ⭐
// Ou seja: quem derrubava a linha eram os que a Inter nao tem — e "⚡" e o mais
// comum nos nomes do Torrentio e do AIOStreams. As setas e o "▶" que a PROPRIA
// interface usa nos seus rotulos sempre passaram, e continuam passando: esta
// funcao decide por glifo disponivel, nao por faixa de codepoint.
//
// Emoji tinham o OUTRO sintoma, nao este: fonteDe devolve a fonte principal
// para codepoint fora do BMP, entao "💾" e "🇧🇷" nunca trocaram a fonte da linha
// — saiam como o retangulo do .notdef. Os dois casos morrem aqui.
static int decorativo(Uint32 cp) {
  if (cp >= 0x2000  && cp <= 0x2BFF)  return 1;  // pontuacao, setas, simbolos, dingbats
  if (cp >= 0x2E00  && cp <= 0x2E7F)  return 1;  // pontuacao suplementar
  if (cp >= 0xFE00  && cp <= 0xFE0F)  return 1;  // seletores de variacao
  if (cp >= 0x1F000 && cp <= 0x1FAFF) return 1;  // emoji e pictogramas
  return 0;
}

// Decodifica um codepoint UTF-8 e diz quantos bytes ele ocupou. Sequencia
// invalida devolve o byte cru com tamanho 1, para nunca travar o laco.
static Uint32 decodifica(const unsigned char *p, int *n) {
  if (*p < 0x80) { *n = 1; return *p; }
  if ((*p & 0xE0) == 0xC0 && p[1]) { *n = 2; return (Uint32)((*p & 0x1F) << 6 | (p[1] & 0x3F)); }
  if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { *n = 3;
    return (Uint32)((*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F)); }
  if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { *n = 4;
    return (Uint32)((*p & 0x07) << 18 | (p[1] & 0x3F) << 12 |
                    (p[2] & 0x3F) << 6 | (p[3] & 0x3F)); }
  *n = 1; return *p;
}

// Tira da linha os decorativos que a fonte principal NAO TEM, e so eles.
//
// Sem isto sobrariam duas saidas ruins: manter o caractere e desenhar o
// retangulo do .notdef (que e o que acontecia com os emoji, ja hoje), ou trocar
// a fonte da linha inteira (que e o que acontecia com os simbolos do BMP).
// Tirar e a terceira: "⚡ 1080p · 4.2 GB" continua sendo "1080p · 4.2 GB", na
// Inter, sem quadradinho.
//
// O que a fonte TEM fica: en-dash, reticencias, aspas curvas e o proprio "·"
// estao na Inter e passam intactos — a decisao e por glifo disponivel, nao por
// faixa. Emoji fora do BMP saem sempre: TTF_GlyphIsProvided recebe Uint16 e nao
// alcanca esses codepoints, e nenhuma das fontes deste app os tem.
//
// CUSTO: um passe por linha, e ele so acontece quando a linha tem byte fora do
// ASCII — o caminho comum sai na primeira comparacao. A linha rasterizada e
// cacheada por text.c, mas a CHAVE do cache e montada com o texto ja limpo,
// entao este passe roda por quadro; e por isso que a saida rapida importa.
static const char *semDecorativoSemGlifo(TxtEstilo estilo, const char *s,
                                         char *dst, size_t tam) {
  const unsigned char *p = (const unsigned char *)s;
  size_t k = 0;
  int algum = 0;
  for (; *p; p++) if (*p >= 0x80) { algum = 1; break; }
  if (!algum) return s;
  // Linha maior que o buffer: fica como esta. Cortar texto visivel para caber
  // num buffer meu seria trocar um defeito visual por perda de informacao.
  if (strlen(s) + 1 > tam) return s;
  p = (const unsigned char *)s;
  while (*p) {
    int n = 1;
    Uint32 cp = decodifica(p, &n);
    int tirar = 0;
    if (decorativo(cp))
      tirar = cp >= 0x10000 ||
              !TTF_GlyphIsProvided(fontes[estilo], (Uint16)cp);
    if (!tirar) { int i; for (i = 0; i < n; i++) dst[k++] = (char)p[i]; }
    p += n;
  }
  dst[k] = 0;
  // Espaco duplo e espaco na borda sao restos do que saiu, nao do texto.
  { size_t r = 0, w = 0;
    while (dst[r] == ' ') r++;
    for (; dst[r]; r++) {
      if (dst[r] == ' ' && w && dst[w - 1] == ' ') continue;
      dst[w++] = dst[r];
    }
    while (w && dst[w - 1] == ' ') w--;
    dst[w] = 0; }
  return dst;
}

// Qual reserva cobre este codepoint. As faixas sao as usuais do Unicode; o que
// nao for arabe/hebraico nem CJK cai na terceira, que e a DroidSansFallback (ela
// cobre cirilico, grego, tailandes e mais).
static Escrita escritaDe(Uint32 cp) {
  if (cp >= 0x0590 && cp <= 0x07FF) return ESC_ARABE;       // hebraico + arabe
  if (cp >= 0xFB50 && cp <= 0xFEFF) return ESC_ARABE;       // formas de apresentacao
  if (cp >= 0x2E80 && cp <= 0x9FFF) return ESC_CJK;
  if (cp >= 0xAC00 && cp <= 0xD7AF) return ESC_CJK;         // hangul
  if (cp >= 0xF900 && cp <= 0xFAFF) return ESC_CJK;
  return ESC_CIRILICO_ETC;
}

// Fonte com que a linha `s` deve ser desenhada. Devolve a principal quando ela
// da conta — que e o caso da esmagadora maioria das linhas.
static TTF_Font *fonteDe(TxtEstilo estilo, const char *s) {
  Uint32 cp = primeiroNaoAscii(s);
  Escrita e;
  if (!cp || cp >= 0x10000) return fontes[estilo];
  // Acentos do portugues e do espanhol estao na Inter; so cai na reserva o que
  // ela realmente nao tem.
  if (TTF_GlyphIsProvided(fontes[estilo], (Uint16)cp)) return fontes[estilo];
  e = escritaDe(cp);
  if (!caminhoReserva[e][0]) return fontes[estilo];
  if (!reservas[e][estilo])
    reservas[e][estilo] = TTF_OpenFont(caminhoReserva[e],
                                       (int)(ESTILOS[estilo].corpo * escalaTxt + 0.5f));
  return reservas[e][estilo] ? reservas[e][estilo] : fontes[estilo];
}

static TTF_Font *fonteLegendaDe(TxtEstilo estilo, const char *s,
                                TxtFamilia familia, int enfase) {
  int i;
  const char *caminho;
  // "Inter" nas legendas continua sendo Inter mesmo quando os menus usam DM
  // Sans ou Open Sans. Abre apenas os tamanhos de legenda efetivamente usados.
  if (familia == TXT_FAMILIA_INTER && fonteAppEscolhida != 0 &&
      estilo >= TXT_LEG_50 && estilo <= TXT_LEG_200) {
    i = estilo - TXT_LEG_50;
    if (!fontesLegInter[i]) {
      if (!bytesLegInter) {
        char inter[512];
        snprintf(inter, sizeof inter, "%sfonts/Inter-Regular.ttf", baseFontes);
        bytesLegInter = lerTudo(inter, &tamLegInter);
      }
      if (bytesLegInter) {
        rwLegInter[i] = SDL_RWFromConstMem(bytesLegInter, (int)tamLegInter);
        fontesLegInter[i] = rwLegInter[i] ? TTF_OpenFontRW(rwLegInter[i], 0,
            (int)(ESTILOS[estilo].corpo * escalaTxt + 0.5f)) : NULL;
        if (!fontesLegInter[i] && rwLegInter[i]) {
          SDL_FreeRW(rwLegInter[i]); rwLegInter[i] = NULL;
        }
      }
    }
    if (fontesLegInter[i]) {
      Uint32 cp = primeiroNaoAscii(s);
      if (!cp || cp >= 0x10000 || TTF_GlyphIsProvided(fontesLegInter[i], (Uint16)cp))
        return fontesLegInter[i];
    }
    return fonteDe(estilo, s);
  }
  if (familia == TXT_FAMILIA_NETFLIX &&
      estilo >= TXT_LEG_50 && estilo <= TXT_LEG_200) {
    int peso = (enfase & TXT_ENF_NEGRITO) ? 1 : 0;
    i = estilo - TXT_LEG_50;
    if (fontesNetflix[peso][i]) return fontesNetflix[peso][i];
    if (!netflixTentada[peso][i]) {
      netflixTentada[peso][i] = 1;
      if (!bytesNetflix[peso])
        bytesNetflix[peso] = lerTudo(caminhoNetflix[peso], &tamNetflix[peso]);
      if (bytesNetflix[peso]) {
        SDL_RWops *rw = SDL_RWFromConstMem(bytesNetflix[peso], (int)tamNetflix[peso]);
        TTF_Font *f = rw ? TTF_OpenFontRW(rw, 0,
            (int)(ESTILOS[estilo].corpo * escalaTxt + 0.5f)) : NULL;
        if (f) { fontesNetflix[peso][i] = f; rwNetflix[peso][i] = rw; }
        else if (rw) SDL_FreeRW(rw);
      }
      if (!fontesNetflix[peso][i] && !avisoFallback[TXT_FAMILIA_NETFLIX]) {
        printf("Netflix Sans indisponivel; legenda usando fonte do app\n");
        avisoFallback[TXT_FAMILIA_NETFLIX] = 1;
      }
    }
    if (fontesNetflix[peso][i]) {
      Uint32 cp = primeiroNaoAscii(s);
      if (!cp || cp >= 0x10000 ||
          TTF_GlyphIsProvided(fontesNetflix[peso][i], (Uint16)cp))
        return fontesNetflix[peso][i];
      // Escritas que a Netflix Sans nao cobre continuam legiveis no fallback.
      return fonteDe(estilo, s);
    }
    return fonteDe(estilo, s);
  }
  if (familia <= TXT_FAMILIA_INTER || familia >= TXT_FAMILIA_N ||
      estilo < TXT_LEG_50 || estilo > TXT_LEG_200)
    return fonteDe(estilo, s);
  i = estilo - TXT_LEG_50;
  if (fontesLegAlt[familia][i]) return fontesLegAlt[familia][i];
  if (fonteLegTentada[familia][i]) return fonteDe(estilo, s);
  fonteLegTentada[familia][i] = 1;
  caminho = familia == TXT_FAMILIA_LG
          ? "/usr/share/fonts/LG_Display-Regular.ttf"
          : "/usr/share/fonts/DroidSans.ttf";
  fontesLegAlt[familia][i] = TTF_OpenFont(
      caminho, (int)(ESTILOS[estilo].corpo * escalaTxt + 0.5f));
  if (!fontesLegAlt[familia][i]) {
    if (!avisoFallback[familia]) {
      printf("fonte de legenda %s indisponivel; usando fonte do app\n",
             TXT_FAMILIAS_PT[familia]);
      avisoFallback[familia] = 1;
    }
    return fonteDe(estilo, s);
  }
  return fontesLegAlt[familia][i];
}

int txt_iniciar(const char *dirRecursos, float escala) {
  if (escala < 0.5f) escala = 1.0f;
  escalaTxt = escala;
  if (TTF_Init() != 0) { printf("TTF_Init: %s\n", TTF_GetError()); return 0; }
  // As tres familias da interface vao embarcadas; LG e Droid ficam como
  // reserva se o pacote for instalado sem fonts/.
  baseFontes[0] = 0;
  if (dirRecursos && *dirRecursos) {
    snprintf(baseFontes, sizeof baseFontes, "%s/", dirRecursos);
  } else {
    char *bp = SDL_GetBasePath();
    if (bp) { snprintf(baseFontes, sizeof baseFontes, "%s", bp); SDL_free(bp); }
  }
  snprintf(caminhoNetflix[0], sizeof caminhoNetflix[0],
           "%sfonts/NetflixSans-Regular.otf", baseFontes);
  snprintf(caminhoNetflix[1], sizeof caminhoNetflix[1],
           "%sfonts/NetflixSans-Medium.otf", baseFontes);
  { static const char *const nomes[PESO_N] = {
      "RobotoCondensed-Regular.ttf", "RobotoCondensed-Medium.ttf",
      "RobotoCondensed-SemiBold.ttf", "RobotoCondensed-Bold.ttf" };
    for (int p = 0; p < PESO_N; p++) {
      char caminho[512];
      snprintf(caminho, sizeof caminho, "%sfonts/%s", baseFontes, nomes[p]);
      bytesCond[p] = lerTudo(caminho, &tamCond[p]);
      if (!bytesCond[p])
        printf("fonte editorial indisponivel: %s; usando fonte do app\n", caminho);
    }
  }

  // Caminho da reserva CJK. Na TV e a DroidSansFallback; no Mac, a fonte do
  // sistema que cobre CJK — ali isto e so para a previa nao mentir.
  // Largura 5, nao 4: a linha do arabe tem quatro candidatos mais o NULL, e o
  // laco abaixo para no NULL. Com [4] o terminador era descartado em silencio e
  // a busca do arabe seguia lendo a linha do cirilico.
  { const char *cand[ESC_N][5] = {
      /* ESC_CJK          */ { "/usr/share/fonts/LG_Display_JP.ttf",
                               "/usr/share/fonts/DroidSansFallback.ttf",
                               "/System/Library/Fonts/Hiragino Sans GB.ttc", NULL },
      /* ESC_ARABE        */ { "/usr/share/fonts/DroidNaskh-Regular.ttf",
                               "/usr/share/fonts/LG_Display_Urdu.ttf",
                               "/System/Library/Fonts/Supplemental/GeezaPro.ttc",
                               "/System/Library/Fonts/Supplemental/Arial Unicode.ttf", NULL },
      /* ESC_CIRILICO_ETC */ { "/usr/share/fonts/DroidSansFallback.ttf",
                               "/usr/share/fonts/DroidSans.ttf",
                               "/System/Library/Fonts/Supplemental/Arial Unicode.ttf", NULL },
    };
    const char *nomeEsc[ESC_N] = { "CJK", "arabe", "resto" };
    for (int e = 0; e < ESC_N; e++) {
      for (int i = 0; cand[e][i]; i++) {
        FILE *fr = fopen(cand[e][i], "rb");
        if (fr) { fclose(fr);
                  snprintf(caminhoReserva[e], sizeof caminhoReserva[e], "%s", cand[e][i]);
                  break; }
      }
      printf("reserva %s: %s\n", nomeEsc[e],
             caminhoReserva[e][0] ? caminhoReserva[e] : "nenhuma");
    } }

  // Reserva de arabe embutida: funciona em qualquer plataforma mesmo sem
  // fonte de sistema (LG sem DroidNaskh, Samsung WASM, Android). So e usada
  // quando nenhum candidato de sistema foi encontrado acima.
  if (!caminhoReserva[ESC_ARABE][0] && baseFontes[0]) {
    char emb[512];
    snprintf(emb, sizeof emb, "%sfonts/NotoNaskhArabic-Regular.ttf", baseFontes);
    FILE *fe = fopen(emb, "rb");
    if (fe) {
      fclose(fe);
      snprintf(caminhoReserva[ESC_ARABE], sizeof caminhoReserva[ESC_ARABE],
               "%s", emb);
    }
  }

  marco("fontes: inicio");
  int candidatos[] = { fonteAppEscolhida, 0, 3, 4 };
  const char *const nomes[] = { "Inter Display", "DM Sans", "Open Sans", "LG Display", "DroidSans" };
  for (int c = 0; c < 4; c++) {
    if (c > 0 && candidatos[c] == candidatos[0]) continue;
    FontesApp novo;
    if (!abrirFontesApp(candidatos[c], &novo)) continue;
    ativarFontesApp(&novo);
    fonteAppEscolhida = candidatos[c] < 3 ? candidatos[c] : 0;
    fonteAppPronta = 1;
    printf("fonte: %s (%d estilos, ate 4 leituras)\n", nomes[candidatos[c]], TXT_NFONTES);
    marco("fontes: prontas");
    return 1;
  }
  printf("txt: nenhuma fonte carregou\n");
  marco("fontes: nenhuma carregou");
  return 0;
}

int txt_definir_fonte_app(int escolha) {
  if (escolha < 0 || escolha > 2) return 0;
  if (!fonteAppPronta) { fonteAppEscolhida = escolha; return 1; }
  if (fonteAppEscolhida == escolha) return 1;
  FontesApp novo;
  if (!abrirFontesApp(escolha, &novo)) return 0;
  limparLinhas();
  ativarFontesApp(&novo);
  fonteAppEscolhida = escolha;
  printf("fonte do app: %s\n", escolha == 0 ? "Inter Display" : escolha == 1 ? "DM Sans" : "Open Sans");
  return 1;
}

void txt_encerrar(void) {
  limparLinhas();
  // ORDEM: a fonte primeiro, o RWops depois, o buffer por ultimo. A face do
  // FreeType ainda referencia o stream, e o stream, os bytes.
  for (int i = 0; i < TXT_NFONTES; i++) {
    if (fontes[i]) TTF_CloseFont(fontes[i]);
    fontes[i] = NULL;
    if (rwFonte[i]) SDL_FreeRW(rwFonte[i]);
    rwFonte[i] = NULL;
    for (int e = 0; e < ESC_N; e++) {
      if (reservas[e][i]) TTF_CloseFont(reservas[e][i]);
      reservas[e][i] = NULL;
    }
  }
  for (int p = 0; p < PESO_N; p++) {
    if (donoPeso[p]) free(bytesPeso[p]);
    bytesPeso[p] = NULL; tamPeso[p] = 0; donoPeso[p] = 0;
    free(bytesCond[p]); bytesCond[p] = NULL; tamCond[p] = 0;
  }
  for (int f = 1; f < TXT_FAMILIA_N; f++)
    for (int i = 0; i < TXT_LEG_N; i++)
      if (fontesLegAlt[f][i]) TTF_CloseFont(fontesLegAlt[f][i]);
  for (int i = 0; i < TXT_LEG_N; i++) {
    if (fontesLegInter[i]) TTF_CloseFont(fontesLegInter[i]);
    if (rwLegInter[i]) SDL_FreeRW(rwLegInter[i]);
    fontesLegInter[i] = NULL; rwLegInter[i] = NULL;
  }
  free(bytesLegInter); bytesLegInter = NULL; tamLegInter = 0;
  for (int p = 0; p < 2; p++) {
    for (int i = 0; i < TXT_LEG_N; i++) {
      if (fontesNetflix[p][i]) TTF_CloseFont(fontesNetflix[p][i]);
      if (rwNetflix[p][i]) SDL_FreeRW(rwNetflix[p][i]);
      fontesNetflix[p][i] = NULL; rwNetflix[p][i] = NULL;
      netflixTentada[p][i] = 0;
    }
    free(bytesNetflix[p]); bytesNetflix[p] = NULL; tamNetflix[p] = 0;
  }
  TTF_Quit();
  fonteAppPronta = 0;
}

// `enfase` e a combinacao TXT_ENF_* pedida pela LEGENDA ASS, e so por ela.
//
// NEGRITO E ITALICO AQUI SAO SINTETICOS, e isso e uma escolha e nao um
// descuido: o pacote nao embarca Inter Italic. O SDL_ttf inclina e engorda o
// glifo por conta propria, que e pior do que uma face desenhada e melhor do
// que perder a distincao: num ASS de anime o italico e o que separa o
// pensamento da fala, e o negrito e o que separa o letreiro do dialogo.
//
// A ENFASE ENTRA NA CHAVE DO CACHE. Sem isso a mesma frase em italico e em
// redondo dividiriam a mesma textura, e qual das duas a tela mostra dependeria
// de quem rasterizou primeiro.
// O Enhanced webOS usa tres text-shadows com blur 2/6/12 px. Desfocar uma
// textura uma vez por fala (e guarda-la no cache) custa menos que dezenas de
// quads deslocados em TODO quadro de uma TV antiga.
static void desfocarAlfa(const Uint8 *src, Uint8 *meio, Uint8 *dst,
                         int w, int h, int raio) {
  int janela = raio * 2 + 1;
  for (int y = 0; y < h; y++) {
    int soma = 0;
    for (int k = 0; k <= raio && k < w; k++) soma += src[y * w + k];
    for (int x = 0; x < w; x++) {
      meio[y * w + x] = (Uint8)((soma + janela / 2) / janela);
      if (x - raio >= 0) soma -= src[y * w + x - raio];
      if (x + raio + 1 < w) soma += src[y * w + x + raio + 1];
    }
  }
  for (int x = 0; x < w; x++) {
    int soma = 0;
    for (int k = 0; k <= raio && k < h; k++) soma += meio[k * w + x];
    for (int y = 0; y < h; y++) {
      dst[y * w + x] = (Uint8)((soma + janela / 2) / janela);
      if (y - raio >= 0) soma -= meio[(y - raio) * w + x];
      if (y + raio + 1 < h) soma += meio[(y + raio + 1) * w + x];
    }
  }
}

static SDL_Surface *sombraLegenda(SDL_Surface *glifos) {
  // A sombra ja e desfocada. Guarda-la em unidades de layout evita quatro
  // vezes mais pixels e CPU numa TV 4K, enquanto o texto claro segue nitido.
  const int pad = 20;
  const int textoW = (int)(glifos->w / escalaTxt + .5f);
  const int textoH = (int)(glifos->h / escalaTxt + .5f);
  const int w = textoW + 2 * pad, h = textoH + 2 * pad;
  const size_t n = (size_t)w * (size_t)h;
  static const int opacidades[3] = { 242, 199, 115 }; // .95, .78, .45
  static const int raios[3] = { 1, 3, 6 };            // blur CSS 2, 6, 12
  static const int deslocamentos[3] = { 1, 3, 6 };
  Uint8 *origem = calloc(n, 1), *meio = malloc(n), *blur = malloc(n);
  Uint8 *alfa = calloc(n, 1);
  SDL_Surface *resultado = NULL;
  if (!origem || !meio || !blur || !alfa) goto fim;
  for (int y = 0; y < textoH; y++) for (int x = 0; x < textoW; x++) {
    int x0 = (int)(x * escalaTxt), x1 = (int)((x + 1) * escalaTxt);
    int y0 = (int)(y * escalaTxt), y1 = (int)((y + 1) * escalaTxt);
    int soma = 0, pixels = 0;
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x1 > glifos->w) x1 = glifos->w;
    if (y1 > glifos->h) y1 = glifos->h;
    for (int sy = y0; sy < y1; sy++) {
      const Uint32 *linha = (const Uint32 *)((const Uint8 *)glifos->pixels + sy * glifos->pitch);
      for (int sx = x0; sx < x1; sx++) {
        soma += (int)((linha[sx] >> glifos->format->Ashift) & 255);
        pixels++;
      }
    }
    origem[(y + pad) * w + x + pad] = pixels ? (Uint8)((soma + pixels/2) / pixels) : 0;
  }
  for (int camada = 0; camada < 3; camada++) {
    int raio = raios[camada];
    int dy = deslocamentos[camada];
    desfocarAlfa(origem, meio, blur, w, h, raio);
    for (int y = dy; y < h; y++) for (int x = 0; x < w; x++) {
      size_t pos = (size_t)y * w + x;
      int camadaAlfa = (blur[(size_t)(y - dy) * w + x] * opacidades[camada] + 127) / 255;
      alfa[pos] += (Uint8)(((255 - alfa[pos]) * camadaAlfa + 127) / 255);
    }
  }
  resultado = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
  if (resultado) for (int y = 0; y < h; y++) {
    Uint32 *linha = (Uint32 *)((Uint8 *)resultado->pixels + y * resultado->pitch);
    for (int x = 0; x < w; x++) linha[x] = (Uint32)alfa[(size_t)y * w + x] << resultado->format->Ashift;
  }
fim:
  free(origem); free(meio); free(blur); free(alfa);
  return resultado;
}

static TxtLinha linhaFamilia(TxtEstilo estilo, const char *s, int r, int g,
                             int b, int a, TxtFamilia familia, int enfase,
                             int sombra) {
  TxtLinha vazia = {0, 0, 0};
  char limpo[1024];
  if (!s || !*s || estilo < 0 || estilo >= TXT_NFONTES || !fontes[estilo]) return vazia;

  if (familia < TXT_FAMILIA_INTER || familia >= TXT_FAMILIA_N)
    familia = TXT_FAMILIA_INTER;

  // ANTES DA CHAVE do cache, para que a linha limpa seja a linha guardada: duas
  // entradas que diferem so por um emoji que ninguem desenha passam a ser a
  // mesma, o que tambem alivia a tabela na tela de fontes.
  // SO NA FAMILIA PRINCIPAL. A limpeza pergunta a fontes[estilo] se o glifo
  // existe, e as familias de legenda usam outro vetor de fontes (fontesLegAlt):
  // aplicar o mesmo teste ali tiraria de uma legenda um simbolo que a fonte
  // DELA tem. A tela que motivou isto — a lista de fontes — usa a fonte do app.
  if (familia == TXT_FAMILIA_INTER && (estilo < TXT_LEG_50 || estilo > TXT_LEG_200)) {
    s = semDecorativoSemGlifo(estilo, s, limpo, sizeof limpo);
    if (!*s) return vazia;
  }

  char chave[288];
  snprintf(chave, sizeof chave, "%d:%d:%d:%d|%02x%02x%02x|%.234s", (int)familia,
           (int)estilo, enfase & 3, !!sombra, r & 255, g & 255, b & 255, s);

  // Hash da chave para evitar o strcmp em quase todas as entradas: a busca
  // roda para CADA linha de CADA quadro, e comparar 288 bytes centenas de
  // vezes por quadro custa mais que o desenho.
  unsigned long h = 2166136261UL;
  { const char *p = chave;
    for (; *p; p++) { h ^= (unsigned char)*p; h *= 16777619UL; } }

  // Sondagem a partir de h % MAX_LINHAS, e nao varredura das 256 entradas.
  // Esta busca roda para CADA linha de CADA quadro; a varredura completa
  // custava em media 128 comparacoes por acerto. Sondando do ponto do hash o
  // acerto sai nas primeiras casas, e a busca PARA no primeiro slot vazio:
  // quem foi inserido por esta mesma regra nunca esta depois de um buraco.
  //
  // O despejo LRU pode abrir um buraco no meio de uma corrente antiga; o
  // efeito e no maximo uma rerasterizacao daquela linha (que entra de novo
  // mais perto do hash), nunca resultado errado — a chave e conferida por
  // strcmp de qualquer forma.
  int livre = -1;
  for (int k = 0; k < MAX_LINHAS; k++) {
    int i = (int)((h + (unsigned long)k) % MAX_LINHAS);
    if (!cache[i].ocupado) { livre = i; break; }
    if (cache[i].hash == h && strcmp(cache[i].chave, chave) == 0) {
      cache[i].uso = ++relogio;
      cache[i].quadroUso = quadroTxt;
      return cache[i].linha;
    }
  }

  // Orcamento estourado: devolve vazio e tenta de novo no proximo quadro. A
  // linha aparece com um quadro de atraso em vez de travar o atual.
  if (rastNesteQuadro >= TXT_POR_QUADRO) return vazia;
  rastNesteQuadro++;
  int slot = livre;
  if (slot < 0) {
    // Tabela cheia: so agora vale a varredura completa atras do LRU. Isso
    // acontece no maximo TXT_POR_QUADRO vezes por quadro, nao por linha.
    unsigned long menor = ~0UL;
    for (int i = 0; i < MAX_LINHAS; i++)
      if (cache[i].ocupado && cache[i].quadroUso != quadroTxt &&
          cache[i].uso < menor) {
        menor = cache[i].uso;
        slot = i;
      }
  }
  if (slot < 0) return vazia;
  if (cache[slot].ocupado) txt_despejos++;
  if (cache[slot].ocupado && cache[slot].linha.tex) {
    // avisa o gfx: o nome pode ser reutilizado pelo glGenTextures logo abaixo
    gfx_tex_esquecer(cache[slot].linha.tex);
    glDeleteTextures(1, &cache[slot].linha.tex);
  }

  Uint64 t0 = SDL_GetPerformanceCounter();
  SDL_Color cor = { (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a };
  TTF_Font *fonte = fonteLegendaDe(estilo, s, familia, enfase);
  // SOMA ao estilo que a fonte ja tem, e RESTAURA depois. As familias de
  // reserva nascem com TTF_STYLE_BOLD ligado (ver txt_iniciar); zerar aqui
  // tiraria delas o peso que o app inteiro conta com.
  int estiloAnt = TTF_GetFontStyle(fonte);
  if (enfase) {
    int novo = estiloAnt;
    if ((enfase & TXT_ENF_NEGRITO) &&
        (familia != TXT_FAMILIA_NETFLIX ||
         estilo < TXT_LEG_50 || estilo > TXT_LEG_200 ||
         fonte != fontesNetflix[1][estilo - TXT_LEG_50]))
      novo |= TTF_STYLE_BOLD;
    if (enfase & TXT_ENF_ITALICO) novo |= TTF_STYLE_ITALIC;
    if (novo != estiloAnt) TTF_SetFontStyle(fonte, novo);
  }
  SDL_Surface *sf = TTF_RenderUTF8_Blended(fonte, s, cor);
  if (enfase) TTF_SetFontStyle(fonte, estiloAnt);
  if (!sf) return vazia;
  SDL_Surface *cv = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ABGR8888, 0);
  SDL_FreeSurface(sf);
  if (!cv) return vazia;
  if (sombra) {
    SDL_Surface *suave = sombraLegenda(cv);
    SDL_FreeSurface(cv);
    if (!suave) return vazia;
    cv = suave;
  }

  GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cv->w, cv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cv->pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gfx_tex_esquecer(0);  // o bind do upload passou por fora do gfx_rect

  cache[slot].ocupado = 1;
  cache[slot].hash = h;
  strncpy(cache[slot].chave, chave, sizeof cache[slot].chave - 1);
  // Medida em unidades de LAYOUT, nao em pixeis do buffer.
  cache[slot].linha.tex = t;
  cache[slot].linha.w = (int)(cv->w / (sombra ? 1.f : escalaTxt) + 0.5f);
  cache[slot].linha.h = (int)(cv->h / (sombra ? 1.f : escalaTxt) + 0.5f);
  txt_rasterizadas++;
  txt_ms += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
  cache[slot].uso = ++relogio;
  cache[slot].quadroUso = quadroTxt;
  SDL_FreeSurface(cv);
  return cache[slot].linha;
}

TxtLinha txt_linha(TxtEstilo estilo, const char *s, int r, int g, int b, int a) {
  return linhaFamilia(estilo, i18n(s), r, g, b, a, TXT_FAMILIA_INTER, 0, 0);
}

TxtLinha txt_linha_familia(TxtEstilo estilo, const char *s, int r, int g,
                           int b, int a, TxtFamilia familia) {
  return linhaFamilia(estilo, i18n(s), r, g, b, a, familia, 0, 0);
}

void txt_desenhar(TxtLinha l, float x, float y) { txt_desenhar_alpha(l, x, y, 1.0f); }

// ENCAIXE NO PIXEL DA TELA.
//
// A textura do glifo tem exatamente a resolucao em que vai ser desenhada, mas
// o CANTO caia em coordenada fracionaria o tempo todo: centralizacao
// (`(r.h - l.h) * 0.5f`), pilhas ancoradas na base, molas de rolagem. Com o
// canto em 478.4 o GL_LINEAR amostra ENTRE dois texels e cada letra sai
// espalhada por duas colunas de pixel — o texto inteiro fica meio pixel fora
// de foco, em toda a tela, o tempo todo.
//
// Era isso que restava do "borrao" depois de o 4K se provar impossivel: nao
// falta resolucao, falta o texto cair em cima do pixel. O web nao tem esse
// problema porque o navegador ja posiciona glifo na grade do dispositivo.
//
// O arredondamento e feito na grade do DRAWABLE e nao na de layout: no Mac
// retina meio pixel de layout e um pixel de tela inteiro, e arredondar na
// grade errada jogaria o texto fora do lugar em vez de assenta-lo.
//
// So o TEXTO encaixa. Encaixar cartao e arte transformaria as molas em degraus
// visiveis; o glifo nao sofre disso porque a letra em si nao se deforma, ela
// so anda de um pixel para o outro.
static float encaixa(float v) {
  float e = escalaTxt;
  return (float)((int)(v * e + (v < 0.0f ? -0.5f : 0.5f))) / e;
}

void txt_desenhar_alpha(TxtLinha l, float x, float y, float alpha) {
  if (!l.tex) return;
  GfxRect r = { encaixa(x), encaixa(y), (float)l.w, (float)l.h };
  gfx_rect(r, l.tex, GFX_TEXTO, 0, 0, 0, 0.0f, 1, 1, 1, alpha);
}

float txt_tracking(TxtEstilo estilo, const char *s, int r, int g, int b,
                   float x, float y, float alpha, float tracking) {
  if (!s || !*s) return 0.0f;
  float larg = 0.0f;
  // Percorre por CARACTERE UTF-8, nao por byte: cortar no meio de um acento
  // produz um glifo invalido, e a fonte da LG devolve um retangulo vazio.
  for (const unsigned char *p = (const unsigned char *)s; *p; ) {
    int n = 1;
    if      ((*p & 0xF8) == 0xF0) n = 4;
    else if ((*p & 0xF0) == 0xE0) n = 3;
    else if ((*p & 0xE0) == 0xC0) n = 2;
    char c[5]; int k = 0;
    while (k < n && p[k]) { c[k] = (char)p[k]; k++; }
    c[k] = 0; p += k ? k : 1;

    TxtLinha l = txt_linha(estilo, c, r, g, b, 255);
    if (x >= 0.0f && l.w) txt_desenhar_alpha(l, x + larg, y, alpha);
    larg += l.w + tracking;
  }
  return larg > 0.0f ? larg - tracking : 0.0f;
}

// Declarada em text.h desde o inicio e NUNCA implementada. Ninguem chamava,
// entao o link passava; a primeira chamada derrubou o build ARM com
// "undefined reference". No Mac isso NAO aparece: `cc -fsyntax-only` num
// arquivo solto nao linka nada.
TxtLinha txt_linha_corta(TxtEstilo estilo, const char *s, int r, int g, int b,
                         int a, float maxW) {
  return txt_linha_corta_familia(estilo, s, r, g, b, a, maxW,
                                 TXT_FAMILIA_INTER);
}

static TxtLinha cortaFamilia(TxtEstilo estilo, const char *s, int r, int g,
                             int b, int a, float maxW, TxtFamilia familia,
                             int enfase, int sombra) {
  // Traduzir ANTES de cortar: o corte mede a largura e insere as reticencias,
  // e medir o portugues para desenhar o ingles poe as reticencias no lugar
  // errado — ou corta um texto que caberia inteiro.
  TxtLinha l;
  s = i18n(s);
  l = linhaFamilia(estilo, s, r, g, b, a, familia, enfase, sombra);
  if (!s || !*s || (float)l.w <= maxW) return l;
  char buf[512];
  size_t n = strlen(s);
  if (n >= sizeof buf - 4) n = sizeof buf - 4;
  memcpy(buf, s, n); buf[n] = 0;
  // Corta por PALAVRA enquanto houver espaco; so quando sobra uma palavra so e
  // que se corta no meio dela. Cortar sempre por caractere deixa meia palavra
  // antes das reticencias, e isso se le como texto corrompido, nao como corte.
  while (n > 0) {
    size_t corte = n;
    while (corte > 0 && buf[corte - 1] != ' ') corte--;
    if (corte > 1) n = corte - 1; else n--;
    // nunca parar no meio de um caractere UTF-8: meio caractere vira tofu
    while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--;
    buf[n] = 0;
    if (!n) break;
    char t[520];
    snprintf(t, sizeof t, "%s\xe2\x80\xa6", buf);
    l = linhaFamilia(estilo, t, r, g, b, a, familia, enfase, sombra);
    if ((float)l.w <= maxW) return l;
  }
  return linhaFamilia(estilo, "\xe2\x80\xa6", r, g, b, a, familia, enfase, sombra);
}

TxtLinha txt_linha_corta_familia(TxtEstilo estilo, const char *s, int r, int g,
                                 int b, int a, float maxW,
                                 TxtFamilia familia) {
  return cortaFamilia(estilo, s, r, g, b, a, maxW, familia, 0, 0);
}

TxtLinha txt_linha_corta_enfase(TxtEstilo estilo, const char *s, int r, int g,
                                int b, int a, float maxW, TxtFamilia familia,
                                int enfase) {
  return cortaFamilia(estilo, s, r, g, b, a, maxW, familia, enfase, 0);
}

TxtLinha txt_linha_sombra_enfase(TxtEstilo estilo, const char *s, float maxW,
                                 TxtFamilia familia, int enfase) {
  // A textura inclui 20 px de margem em cada lado para o blur. O limite de
  // largura continua sendo o do TEXTO, nao o da textura ampliada.
  return cortaFamilia(estilo, s, 0, 0, 0, 255, maxW + 40.f,
                      familia, enfase, 1);
}

float txt_medir_enfase(TxtEstilo estilo, const char *s, TxtFamilia familia,
                        int enfase) {
  if (!s || !*s || estilo < 0 || estilo >= TXT_NFONTES) return 0.f;
  TTF_Font *fonte = fonteLegendaDe(estilo, s, familia, enfase);
  if (!fonte) return 0.f;
  int antes = TTF_GetFontStyle(fonte), novo = antes, w = 0;
  if ((enfase & TXT_ENF_NEGRITO) &&
      (familia != TXT_FAMILIA_NETFLIX ||
       estilo < TXT_LEG_50 || estilo > TXT_LEG_200 ||
       fonte != fontesNetflix[1][estilo - TXT_LEG_50]))
    novo |= TTF_STYLE_BOLD;
  if (enfase & TXT_ENF_ITALICO) novo |= TTF_STYLE_ITALIC;
  if (novo != antes) TTF_SetFontStyle(fonte, novo);
  TTF_SizeUTF8(fonte, s, &w, NULL);
  if (novo != antes) TTF_SetFontStyle(fonte, antes);
  return w / escalaTxt;
}

float txt_bloco(TxtEstilo estilo, const char *s, int r, int g, int b,
                float x, float y, float larg, float leading, float alpha, int maxLinhas) {
  // Mesma razao do corte: a quebra de linha e feita no texto final.
  s = i18n(s);
  if (!s || !*s) return 0.0f;
  char linha[512]; linha[0] = 0;
  float usado = 0.0f;
  int nLinhas = 0;
  const char *p = s;
  while (*p && (maxLinhas <= 0 || nLinhas < maxLinhas)) {
    // pega a proxima palavra
    const char *ini = p;
    int quebra;
    while (*p && *p != ' ' && *p != '\n') p++;
    size_t np = (size_t)(p - ini);
    // QUEBRA DURA NO \n, e isto e conserto de defeito visto em foto.
    //
    // Este laco separava palavras SO por espaco. Um \n no meio do texto virava
    // parte da "palavra", chegava inteiro ao TTF_RenderUTF8_Blended e saia como
    // .notdef — o quadradinho. Quem escreveu o rodape de ajuda dos Ajustes
    // ("Navegar\nOK Abrir\nVoltar Ir para as categorias") via tres linhas no
    // codigo e uma linha corrida com dois quadrados na TV. O relator do issue
    // #12 fotografou exatamente isso e eu li a foto como texto sem traducao,
    // que era outra coisa: sao dois defeitos na mesma tela.
    quebra = (*p == '\n');
    while (*p == ' ' || *p == '\n') { if (*p == '\n') quebra = 1; p++; }

    char tentativa[512];
    size_t nl = strlen(linha);
    if (nl + np + 2 >= sizeof tentativa) break;
    memcpy(tentativa, linha, nl);
    if (nl) tentativa[nl++] = ' ';
    memcpy(tentativa + nl, ini, np);
    tentativa[nl + np] = 0;

    TxtLinha m = txt_linha(estilo, tentativa, r, g, b, 255);
    if (m.w > larg && linha[0]) {
      // nao coube: fecha a linha atual e recomeca com a palavra
      TxtLinha l = txt_linha(estilo, linha, r, g, b, 255);
      txt_desenhar_alpha(l, x, y + usado, alpha);
      usado += leading; nLinhas++;
      if (maxLinhas > 0 && nLinhas >= maxLinhas) return usado;
      memcpy(linha, ini, np); linha[np] = 0;
    } else {
      memcpy(linha, tentativa, nl + np + 1);
    }
    // Fecha a linha AQUI quando o TEXTO pediu, em vez de esperar a largura
    // acabar. `linha` pode estar vazia (dois \n seguidos): ai a linha em branco
    // e desenhada de proposito — e o vao que quem escreveu o texto pediu.
    if (quebra && (maxLinhas <= 0 || nLinhas < maxLinhas)) {
      if (linha[0]) {
        TxtLinha l = txt_linha(estilo, linha, r, g, b, 255);
        txt_desenhar_alpha(l, x, y + usado, alpha);
      }
      usado += leading; nLinhas++;
      linha[0] = 0;
    }
  }
  if (linha[0] && (maxLinhas <= 0 || nLinhas < maxLinhas)) {
    TxtLinha l = txt_linha(estilo, linha, r, g, b, 255);
    txt_desenhar_alpha(l, x, y + usado, alpha);
    usado += leading;
  }
  return usado;
}

// Quebra igual a txt_bloco, mas posiciona cada linha pela BORDA DIREITA. A
// duplicacao com txt_bloco e pequena e proposital: unificar as duas exigiria um
// parametro de alinhamento em todas as chamadas, e so este caso precisa.
float txt_bloco_dir(TxtEstilo estilo, const char *s, int r, int g, int b,
                    float xDir, float y, float larg, float leading,
                    float alpha, int maxLinhas) {
  // Mesma razao do corte: a quebra de linha e feita no texto final.
  s = i18n(s);
  if (!s || !*s) return 0.0f;
  char linha[512]; linha[0] = 0;
  float usado = 0.0f;
  int nLinhas = 0;
  const char *p = s;
  while (*p && (maxLinhas <= 0 || nLinhas < maxLinhas)) {
    const char *ini = p;
    int quebra;
    while (*p && *p != ' ' && *p != '\n') p++;
    size_t np = (size_t)(p - ini);
    // QUEBRA DURA NO \n, e isto e conserto de defeito visto em foto.
    //
    // Este laco separava palavras SO por espaco. Um \n no meio do texto virava
    // parte da "palavra", chegava inteiro ao TTF_RenderUTF8_Blended e saia como
    // .notdef — o quadradinho. Quem escreveu o rodape de ajuda dos Ajustes
    // ("Navegar\nOK Abrir\nVoltar Ir para as categorias") via tres linhas no
    // codigo e uma linha corrida com dois quadrados na TV. O relator do issue
    // #12 fotografou exatamente isso e eu li a foto como texto sem traducao,
    // que era outra coisa: sao dois defeitos na mesma tela.
    quebra = (*p == '\n');
    while (*p == ' ' || *p == '\n') { if (*p == '\n') quebra = 1; p++; }

    char tentativa[512];
    size_t nl = strlen(linha);
    if (nl + np + 2 >= sizeof tentativa) break;
    memcpy(tentativa, linha, nl);
    if (nl) tentativa[nl++] = ' ';
    memcpy(tentativa + nl, ini, np);
    tentativa[nl + np] = 0;

    TxtLinha m = txt_linha(estilo, tentativa, r, g, b, 255);
    if (m.w > larg && linha[0]) {
      TxtLinha l = txt_linha(estilo, linha, r, g, b, 255);
      if (xDir >= 0.0f) txt_desenhar_alpha(l, xDir - l.w, y + usado, alpha);
      usado += leading; nLinhas++;
      if (maxLinhas > 0 && nLinhas >= maxLinhas) return usado;
      memcpy(linha, ini, np); linha[np] = 0;
    } else {
      memcpy(linha, tentativa, nl + np + 1);
    }
    // Fecha a linha AQUI quando o TEXTO pediu, em vez de esperar a largura
    // acabar. `linha` pode estar vazia (dois \n seguidos): ai a linha em branco
    // e desenhada de proposito — e o vao que quem escreveu o texto pediu.
    if (quebra && (maxLinhas <= 0 || nLinhas < maxLinhas)) {
      if (linha[0]) {
        TxtLinha l = txt_linha(estilo, linha, r, g, b, 255);
        if (xDir >= 0.0f) txt_desenhar_alpha(l, xDir - l.w, y + usado, alpha);
      }
      usado += leading; nLinhas++;
      linha[0] = 0;
    }
  }
  if (linha[0] && (maxLinhas <= 0 || nLinhas < maxLinhas)) {
    TxtLinha l = txt_linha(estilo, linha, r, g, b, 255);
    if (xDir >= 0.0f) txt_desenhar_alpha(l, xDir - l.w, y + usado, alpha);
    usado += leading;
  }
  return usado;
}
