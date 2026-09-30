// Navegacao superior: perfil a esquerda, destinos ao centro, ajustes e hora
// a direita. A capsula central cresce a partir do centro e revela os rotulos,
// como a navegacao do Nuvio Desktop, por hover ou foco do controle remoto.
#include "menu.h"
#include "perfis.h"
#include "tex_cache.h"
#include "gfx.h"
#include "text.h"
#include "anim.h"
#include "layout.h"
#include "ajustes.h"
#include "botoes.h"
#include "ponteiro.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NAV_Y             28.0f
#define NAV_H             72.0f
#define NAV_CELULA_FECHADA 76.0f
#define NAV_CELULA_ABERTA 200.0f
#define NAV_MARGEM          8.0f
#define NAV_ICONE         34.0f
#define NAV_AVATAR        50.0f
#define NAV_PERFIL_X      76.0f
#define NAV_AJUSTES_X   1734.0f
#define NAV_HORA_DIREITA 1882.0f
#define NAV_POP_X         40.0f
#define NAV_POP_Y        112.0f
#define NAV_POP_W        372.0f
#define NAV_POP_LINHA_H   70.0f
#define NAV_ABRIR_MS     280.0f
#define NAV_FECHAR_MS    220.0f

// IDs legados continuam no enum. Ajustes fica fora do grupo central.
static const MenuDestino VISIVEIS[] = {
  MENU_INICIO, MENU_BUSCAR, MENU_DESCUBRIR, MENU_BIBLIOTECA, MENU_AGENDA
};
static const char *ROTULOS[] = {
  "Início", "Busca", "Explorar", "Biblioteca", "Agenda"
};
static const char *ICONES[] = {
  "menu_home", "menu_search", "menu_discover", "menu_library", "menu_agenda"
};
#define NAV_MAX_DESTINOS ((int)(sizeof VISIVEIS / sizeof VISIVEIS[0]))
enum { FOCO_PERFIL, FOCO_INICIO, NAV_ITENS = NAV_MAX_DESTINOS + 2 };
static int nDestinos(void) { return ajustes_menu_descobrir() ? 5 : 4; }
static int focoAjustes(void) { return nDestinos() + FOCO_INICIO; }
static int indiceReal(int i) { return !ajustes_menu_descobrir() && i >= 2 ? i + 1 : i; }

static int aberto;
static int abertoPonteiro;
static int popupAberto, popupLinha;
static int destino = MENU_INICIO;
static int linha;
static int mudou;
static int pediuTrocarPerfil, pediuGerenciar;
static float expansao;
static float animFoco[NAV_ITENS];
static float realce, realceVel, realceAlpha;

static int indiceVisivel(int d) {
  for (int i = 0; i < nDestinos(); i++) if (VISIVEIS[indiceReal(i)] == d) return i;
  return -1;
}

static float largura(void) {
  float celula = anim_mistura(NAV_CELULA_FECHADA, NAV_CELULA_ABERTA,
                              anim_suave(expansao));
  return celula * nDestinos() + 2.0f * NAV_MARGEM;
}

static int focoDestino(int d) {
  int i = indiceVisivel(d);
  return d == MENU_AJUSTES ? focoAjustes() : i < 0 ? FOCO_INICIO : i + 1;
}

static int popupPerfis(void) { return perfis_n() > 0 ? perfis_n() : 1; }
static int popupLinhas(void) { return popupPerfis() + 1; }
static float popupAltura(void) { return 20.0f + NAV_POP_LINHA_H * popupLinhas(); }

int menu_iniciar(void) {
  aberto = abertoPonteiro = popupAberto = mudou = 0;
  pediuTrocarPerfil = pediuGerenciar = 0;
  destino = MENU_INICIO;
  linha = FOCO_INICIO;
  expansao = 0;
  realce = realceVel = 0;
  realceAlpha = 1;
  for (int i = 0; i < NAV_ITENS; i++) animFoco[i] = 0;
  return 1;
}

void menu_abrir(void) {
  if (aberto) return;
  linha = focoDestino(destino);
  aberto = 1;
  abertoPonteiro = 0;
}

void menu_fechar(void) {
  aberto = abertoPonteiro = popupAberto = 0;
  linha = focoDestino(destino);
}

int menu_aberto(void) { return aberto; }
int menu_visivel(void) { return 1; }
int menu_destino(void) { return destino; }
void menu_definir_destino(int d) {
  if (d < 0 || d >= MENU_N) return;
  if (indiceVisivel(d) < 0 && d != MENU_AJUSTES) d = MENU_INICIO;
  destino = d;
  if (!aberto) linha = focoDestino(d);
}
int menu_mudou_destino(void) { int m = mudou; mudou = 0; return m; }
int menu_pediu_trocar_perfil(void) {
  int p = pediuTrocarPerfil;
  pediuTrocarPerfil = 0;
  return p;
}
int menu_pediu_gerenciar_perfis(void) {
  int p = pediuGerenciar;
  pediuGerenciar = 0;
  return p;
}
const char *menu_rotulo(int d) {
  int i = indiceVisivel(d);
  return d == MENU_AJUSTES ? "Ajustes" : i >= 0 ? ROTULOS[indiceReal(i)] : "";
}

static void escolher(void) {
  int novo = linha == focoAjustes() ? MENU_AJUSTES : VISIVEIS[indiceReal(linha - FOCO_INICIO)];
  if (novo != destino) { destino = novo; mudou = 1; }
  // O clique troca a tela, mas o cursor continua sobre a capsula. Recolher
  // aqui fazia os destinos fugirem da mao antes do proximo movimento.
  if (!abertoPonteiro) menu_fechar();
}

void menu_evento(const SDL_Event *e) {
  if (!aberto || e->type != SDL_KEYDOWN) return;
  SDL_Keycode k = e->key.keysym.sym;
  if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP)
    abertoPonteiro = 0;
  if (popupAberto) {
    if (k == SDLK_ESCAPE || k == SDLK_AC_BACK || k == SDLK_BACKSPACE ||
        k == SDLK_DELETE || k == SDLK_LEFT) { popupAberto = 0; return; }
    if (k == SDLK_UP && popupLinha > 0) popupLinha--;
    if (k == SDLK_DOWN && popupLinha < popupLinhas() - 1) popupLinha++;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
      if (popupLinha == popupPerfis()) pediuGerenciar = 1;
      else {
        const ContaPerfil *p = perfis_item(popupLinha);
        if (p && p->indice != perfis_ativo())
          pediuTrocarPerfil = popupLinha + 1; // zero fica reservado a nenhum pedido
      }
      menu_fechar();
    }
    return;
  }
  if (k == SDLK_ESCAPE || k == SDLK_AC_BACK || k == SDLK_BACKSPACE ||
      k == SDLK_DELETE || k == SDLK_DOWN) { menu_fechar(); return; }
  if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
    if (linha == FOCO_PERFIL) {
      popupAberto = 1;
      popupLinha = perfis_indice_sugerido();
      if (popupLinha < 0 || popupLinha >= popupPerfis()) popupLinha = 0;
    } else escolher();
    return;
  }
  if (k == SDLK_LEFT && linha > FOCO_PERFIL) linha--;
  if (k == SDLK_RIGHT && linha < focoAjustes()) linha++;
}

void menu_atualizar(float dt, Uint32 agora) {
  (void)agora;
  if (aberto && abertoPonteiro && !popupAberto) {
    if (!ponteiro_ativo()) menu_fechar();
    else {
      float w = largura();
      float x = (NV_TELA_W - w) * 0.5f;
      float mx = ponteiro_x(), my = ponteiro_y();
      int centro = mx >= x - 12 && mx <= x + w + 12;
      int perfil = mx >= NAV_PERFIL_X - 54 && mx <= NAV_PERFIL_X + 54;
      int ajustes = mx >= NAV_AJUSTES_X - 54 && mx <= NAV_AJUSTES_X + 54;
      if (my < NAV_Y - 12 || my > NAV_Y + NAV_H + 12 ||
          (!centro && !perfil && !ajustes)) menu_fechar();
    }
  }
  if (linha > focoAjustes()) linha = focoAjustes();
  if (destino == MENU_DESCUBRIR && !ajustes_menu_descobrir()) {
    destino = MENU_INICIO;
    if (!aberto) linha = FOCO_INICIO;
  }
  int expandir = aberto && !popupAberto &&
                 linha >= FOCO_INICIO && linha < focoAjustes();
  expansao = anim_rampa(expansao, expandir ? 1.0f : 0.0f, dt,
                        expandir ? NAV_ABRIR_MS : NAV_FECHAR_MS);
  for (int i = 0; i < NAV_ITENS; i++)
    animFoco[i] = anim_mola(animFoco[i], aberto && i == linha ? 1.0f : 0.0f,
                            dt, aberto && i == linha ? NV_MOLA_FOCO : 60.0f);
  // Com mouse, o destaque marca a tela ativa; com D-pad, acompanha o foco.
  int alvo = aberto && !abertoPonteiro && expandir ?
             linha - FOCO_INICIO : indiceVisivel(destino);
  realceAlpha = anim_mola(realceAlpha, alvo >= 0 ? 1.0f : 0.0f, dt, 24.0f);
  if (alvo >= 0)
    realce = anim_mola2(&realceVel, realce, (float)alvo, dt, 24.0f);
  realce = anim_clamp(realce, 0.0f, (float)(nDestinos() - 1));
}

static void ponteiroLinha(int i, int b) {
  (void)b;
  if (i < 0 || i >= NAV_ITENS || popupAberto) return;
  if (!aberto) menu_abrir();
  abertoPonteiro = 1;
  linha = i;
}
static void ponteiroCentro(int a, int b) {
  (void)a; (void)b;
  if (popupAberto) return;
  if (!aberto) menu_abrir();
  abertoPonteiro = 1;
  if (linha < FOCO_INICIO || linha >= focoAjustes())
    linha = focoDestino(destino == MENU_AJUSTES ? MENU_INICIO : destino);
}
static void ponteiroAbsorver(int a, int b) { (void)a; (void)b; }
static void ponteiroFora(int a, int b) { (void)a; (void)b; menu_fechar(); }
static void ponteiroPopupLinha(int i, int b) {
  (void)b;
  if (popupAberto && i >= 0 && i < popupLinhas()) popupLinha = i;
}
static void ponteiroPopupClicar(int i, int b) {
  (void)b;
  if (!popupAberto || i < 0 || i >= popupLinhas()) return;
  popupLinha = i;
  SDL_Event e = {0};
  e.type = SDL_KEYDOWN;
  e.key.keysym.sym = SDLK_RETURN;
  menu_evento(&e);
}

static void icone(int i, float cx, float cy, float s, float lum) {
  if (i < 0 || i >= nDestinos()) return;
  gfx_icone((GfxRect){cx - s * 0.5f, cy - s * 0.5f, s, s},
            ICONES[indiceReal(i)], lum, lum, lum, 1);
}

static void corAvatar(const char *hex, float *r, float *g, float *b) {
  unsigned v;
  *r = 0.12f; *g = 0.53f; *b = 0.90f;
  if (!hex || hex[0] != '#' || strlen(hex) < 7 ||
      sscanf(hex + 1, "%6x", &v) != 1) return;
  *r = ((v >> 16) & 255) / 255.0f;
  *g = ((v >> 8) & 255) / 255.0f;
  *b = (v & 255) / 255.0f;
}

static void desenhaAvatar(const ContaPerfil *p, float cx, float cy, float lado) {
  GfxRect r = {cx - lado * .5f, cy - lado * .5f, lado, lado};
  GLuint tex = p && p->avatarUrl[0] ? tex_obter(p->avatarUrl) : 0;
  if (tex) {
    gfx_tex_aspect_atual = 1.0f;
    gfx_rect(r, tex, GFX_CARD, 0, 0, 0, .5f, 0, 0, 0, 1);
    gfx_tex_aspect_atual = 0.0f;
  } else {
    float cr, cg, cb;
    char ini[4] = "?";
    corAvatar(p ? p->corHex : NULL, &cr, &cg, &cb);
    gfx_cor(r, .5f, cr, cg, cb, 1);
    if (p && p->nome[0]) {
      ini[0] = p->nome[0]; ini[1] = 0;
      if ((unsigned char)ini[0] >= 0xC0 && p->nome[1]) {
        ini[1] = p->nome[1]; ini[2] = 0;
      }
    }
    TxtLinha t = txt_linha(TXT_CAPTION, ini, 255, 255, 255, 255);
    txt_desenhar(t, cx - t.w * .5f, cy - t.h * .5f);
  }
}

static void desenharPopup(void) {
  int n = popupPerfis();
  float ar, ag, ab;
  int tinta = ajustes_tinta_foco();
  ajustes_acento(&ar, &ag, &ab);
  gfx_cor((GfxRect){NAV_POP_X, NAV_POP_Y, NAV_POP_W, popupAltura()},
          .085f, .115f, .120f, .130f, .98f);
  for (int i = 0; i < n + 1; i++) {
    float ry = NAV_POP_Y + 10.0f + NAV_POP_LINHA_H * i;
    int focado = popupLinha == i;
    if (focado)
      gfx_cor((GfxRect){NAV_POP_X + 10, ry + 2, NAV_POP_W - 20, 62},
              .18f, ar, ag, ab, 1);
    if (i == n) {
      gfx_cor((GfxRect){NAV_POP_X + 18, ry - 4, NAV_POP_W - 36, 1},
              0, 1, 1, 1, .13f);
      gfx_icone((GfxRect){NAV_POP_X + 28, ry + 17, 32, 32},
                "menu_settings", focado ? tinta / 255.0f : .84f,
                focado ? tinta / 255.0f : .84f,
                focado ? tinta / 255.0f : .84f, 1);
      TxtLinha t = txt_linha(TXT_BODY, "Gerenciar perfis",
                             focado ? tinta : 230, focado ? tinta : 231,
                             focado ? tinta : 233, 255);
      txt_desenhar(t, NAV_POP_X + 78, ry + 18);
      continue;
    }
    const ContaPerfil *p = perfis_item(i);
    int atual = p && p->indice == perfis_ativo();
    desenhaAvatar(p, NAV_POP_X + 48, ry + 34, 42);
    const char *nome = p && p->nome[0] ? p->nome : "Perfil principal";
    TxtLinha t = txt_linha_corta(TXT_BODY, nome,
                                focado ? tinta : 242, focado ? tinta : 242,
                                focado ? tinta : 244, 255, NAV_POP_W - 130);
    txt_desenhar(t, NAV_POP_X + 86, ry + (atual ? 4 : 17));
    if (atual) {
      TxtLinha s = txt_linha(TXT_CAPTION, "Perfil atual",
                             focado ? tinta : 171, focado ? tinta : 174,
                             focado ? tinta : 180, 255);
      txt_desenhar(s, NAV_POP_X + 86, ry + 36);
    } else if (p && p->temPin) {
      TxtLinha lock = txt_linha(TXT_CAPTION, "PIN",
                                focado ? tinta : 172, focado ? tinta : 175,
                                focado ? tinta : 180, 255);
      txt_desenhar(lock, NAV_POP_X + NAV_POP_W - lock.w - 26, ry + 19);
    }
  }
}

void menu_desenhar(Uint32 agora) {
  (void)agora;
  float w = largura();
  float x = (NV_TELA_W - w) * .5f;
  float cell = (w - 2.0f * NAV_MARGEM) / nDestinos();
  float cy = NAV_Y + NAV_H * .5f;
  float ar, ag, ab;
  ajustes_acento(&ar, &ag, &ab);
  float corTinta = ajustes_acento_tinta(NULL, NULL, NULL);
  gfx_vidro_topo_capturar();

  // O menu de perfis toma o ponteiro inteiro. Um clique fora o fecha sem
  // atravessar para o conteudo nem selecionar outro destino por acidente.
  if (ponteiro_ativo()) {
    if (aberto) ponteiro_alvo(0, 0, NV_TELA_W, NV_TELA_H,
                            popupAberto ? NULL : ponteiroFora,
                            popupAberto ? ponteiroFora : NULL, 0, 0);
    if (!popupAberto) {
      ponteiro_alvo(NAV_PERFIL_X - 42, NAV_Y - 4, 84, NAV_H + 8,
                    ponteiroLinha, NULL, FOCO_PERFIL, 0);
      ponteiro_alvo(x, NAV_Y - 4, w, NAV_H + 8,
                    ponteiroCentro, ponteiroAbsorver, 0, 0);
      for (int i = 0; i < nDestinos(); i++)
        ponteiro_alvo(x + NAV_MARGEM + i * cell, NAV_Y - 4, cell, NAV_H + 8,
                      ponteiroLinha, NULL, i + FOCO_INICIO, 0);
      ponteiro_alvo(NAV_AJUSTES_X - 42, NAV_Y - 4, 84, NAV_H + 8,
                    ponteiroLinha, NULL, focoAjustes(), 0);
    } else {
      // A margem do popup absorve o clique. No vao entre avatar e popup, o
      // ponteiro pode passar pelo fundo sem fechar a lista antes da escolha.
      ponteiro_alvo(NAV_POP_X, NAV_POP_Y, NAV_POP_W, popupAltura(),
                    NULL, NULL, 0, 0);
      for (int i = 0; i < popupLinhas(); i++)
        ponteiro_alvo(NAV_POP_X + 10, NAV_POP_Y + 10 + i * NAV_POP_LINHA_H,
                      NAV_POP_W - 20, NAV_POP_LINHA_H,
                      ponteiroPopupLinha, ponteiroPopupClicar, i, 0);
    }
  }

  // Perfil isolado a esquerda; o foco desenha um fundo leve e o menu fica
  // ancorado sob ele, como na interface de referencia.
  if (aberto && linha == FOCO_PERFIL && !popupAberto)
    gfx_anel((GfxRect){NAV_PERFIL_X - 34, cy - 34, 68, 68},
             .5f, 3.0f, ar, ag, ab, animFoco[FOCO_PERFIL]);
  desenhaAvatar(perfis_item_ativo(), NAV_PERFIL_X, cy, NAV_AVATAR);
  if (popupAberto)
    gfx_anel((GfxRect){NAV_PERFIL_X - 31, cy - 31, 62, 62},
             .5f, 2.0f, ar, ag, ab, 1);

  GfxRect capsula = {x, NAV_Y, w, NAV_H};
  gfx_cor((GfxRect){x - 2, NAV_Y + 4, w + 4, NAV_H + 4},
          .5f, 0, 0, 0, .18f);
  gfx_vidro_topo_desenhar(capsula, .5f);
  gfx_anel(capsula, .5f, 1.0f, 1, 1, 1, .10f);
  if (realceAlpha > .01f)
    gfx_cor((GfxRect){x + NAV_MARGEM + realce * cell + 2, NAV_Y + 6,
                      cell - 4, NAV_H - 12},
            .5f, ar, ag, ab, .52f * realceAlpha);

  float revelar = anim_suave(expansao);
  for (int i = 0; i < nDestinos(); i++) {
    float ix = x + NAV_MARGEM + i * cell;
    int atual = VISIVEIS[indiceReal(i)] == destino;
    float f = animFoco[i + FOCO_INICIO];
    if (abertoPonteiro && !atual && f > .01f)
      gfx_cor((GfxRect){ix + 2, NAV_Y + 6, cell - 4, NAV_H - 12},
              .5f, 1, 1, 1, .08f * f);
    int focoTecla = aberto && !abertoPonteiro &&
                   linha >= FOCO_INICIO && linha < focoAjustes();
    float lum = ((focoTecla ? linha == i + FOCO_INICIO : atual) || f > .55f)
                ? corTinta : .74f;
    int c = (int)(lum * 255.0f + .5f);
    TxtLinha t = txt_linha(TXT_CALLOUT, ROTULOS[indiceReal(i)], c, c, c, 255);
    float textoW = (t.w + 14.0f) * revelar;
    float iconX = ix + (cell - NAV_ICONE - textoW) * .5f + NAV_ICONE * .5f;
    icone(i, iconX, cy, NAV_ICONE, lum);
    if (textoW > 1.0f) {
      float tx = iconX + NAV_ICONE * .5f + 14.0f * revelar;
      // O texto ocupa so a largura revelada neste quadro: os nomes aparecem
      // junto com a expansao, sem atravessar o icone ou a celula vizinha.
      gfx_recorte(tx, NAV_Y, t.w * revelar, NAV_H);
      txt_desenhar_alpha(t, tx, cy - t.h * .5f, revelar);
      gfx_sem_recorte();
    }
  }

  float sf = animFoco[focoAjustes()];
  if (destino == MENU_AJUSTES || sf > .01f) {
    GfxRect p = {NAV_AJUSTES_X - 34, cy - 34, 68, 68};
    if (sf > .01f) botao_luz(p, sf, 1.0f);
    gfx_cor(p, .5f, ar, ag, ab, sf > .01f ?
            (destino == MENU_AJUSTES ? .84f + .14f * sf : .96f * sf) : .84f);
  }
  float sl = destino == MENU_AJUSTES || sf > .55f ? corTinta : .78f;
  gfx_icone((GfxRect){NAV_AJUSTES_X - 18, cy - 18, 36, 36},
            "menu_settings", sl, sl, sl, 1);

  time_t t = time(NULL);
  struct tm local;
  char hora[16] = "--:--";
  if (localtime_r(&t, &local)) strftime(hora, sizeof hora, "%H:%M", &local);
  TxtLinha relogio = txt_linha(TXT_BODY, hora, 180, 184, 190, 255);
  txt_desenhar(relogio, NAV_HORA_DIREITA - relogio.w,
               cy - relogio.h * .5f);

  if (popupAberto) desenharPopup();
}
