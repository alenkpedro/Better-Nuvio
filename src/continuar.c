#include "continuar.h"
#include "ajustes.h"
#include "idioma.h"
#include "layout.h"
#include "recomenda.h"
#include "badges.h"
#include "text.h"
#include "anim.h"
#include "proximo.h"
#include "trakt.h"
#include "simkl.h"
#include "cwordem.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

void continuar_desenhar(const CatItem *ci, GfxRect r, float raio) {
  if (!ci) return;

  {
  float esc = r.w / NV_DESTAQUE_W;
  float pad = NV_CW_PAD * esc, largura = r.w - pad * 2;
  // A mascara do veu precisa coincidir com a da arte e do anel de foco.
  // Um raio fixo deixava o preto vazar nos quatro cantos arredondados.
  gfx_rect(r, 0, GFX_VEU, 0, 0, 0, raio, 0, 0, 0, .85f);

  // Um retangulo compacto, nao uma pilula. Nunca inventar status de estreia.
  if (ci->restanteMin > 0 || ci->posicaoSeg > 0 ||
      (ci->progresso == 0 && ci->continuarSeguinte)) {
    char selo[48];
    int h = ci->restanteMin / 60, m = ci->restanteMin % 60;
    // "A SEGUIR" e nao "53min Restantes": o item de progresso 0 e o proximo
    // episodio de uma serie cujo ultimo terminou (issue #66) — ninguem
    // comecou a ve-lo, entao "restantes" seria mentira.
    // O "a seguir" que AINDA NAO FOI AO AR (issue #127) diz quando estreia:
    // "A seguir" nele prometia um episodio que nao ha como tocar. Quem e
    // futuro e a decisao unica da montagem (cwo_e_futuro), a mesma que o poe
    // em "Proximos episodios".
    char quando[32];
    if (ci->progresso == 0 && cwo_e_futuro(ci->imdb) &&
        cwo_data_curta(cwo_estreia(ci->imdb), (long long)time(NULL) * 1000LL,
                       ajustes_idioma_ingles(), 0, quando, sizeof quando))
      snprintf(selo, sizeof selo, i18n("Estreia %s"), quando);
    else if (ci->progresso == 0 && ci->continuarSeguinte)
      snprintf(selo, sizeof selo, "%s", i18n("A seguir"));
    else if (ci->posicaoSeg > 0 && ci->restanteMin <= 0)
      snprintf(selo, sizeof selo, "%d:%02d %s", (int)ci->posicaoSeg / 60,
               (int)ci->posicaoSeg % 60,
               !ajustes_idioma_ingles() ? "assistidos" : "watched");
    else if (h && m) snprintf(selo, sizeof selo, i18n("%dh %dmin Restantes"), h, m);
    else if (h) snprintf(selo, sizeof selo, i18n("%dh Restantes"), h);
    else snprintf(selo, sizeof selo, i18n("%dmin Restantes"), m);
    // O selo usa Roboto Condensed 500/20, com fundo escuro compacto. O badge
    // generico usa Caption2 400/21 e espacamento diferente do card original.
    TxtLinha rotulo = txt_linha(TXT_COND_CW_BADGE, selo, 240, 240, 240, 255);
    float bw = rotulo.w + NV_CW_BADGE_PAD_X * 2.0f * esc;
    float bh = rotulo.h + NV_CW_BADGE_PAD_Y * 2.0f * esc;
    if (bw <= largura) {
      GfxRect caixa = {r.x + r.w - pad - bw, r.y + pad, bw, bh};
      float canto = NV_CW_BADGE_RADIUS * esc / (bw < bh ? bw : bh);
      gfx_cor(caixa, canto, 0.075f, 0.075f, 0.075f, 0.88f);
      txt_desenhar_alpha(rotulo, caixa.x + (bw - rotulo.w) * 0.5f,
                        caixa.y + (bh - rotulo.h) * 0.5f, 1.0f);
    }
  }

  // O selo do IMDb, no CANTO INFERIOR DIREITO (issue #87): o card ja tinha a
  // nota no CatItem — quem nao a lia era o enfeite do Trakt — e a fileira e o
  // unico lugar da home onde ela nao aparecia. O desenho e o mesmo selo da
  // aba Salvos (rec_selo_imdb), pela base da linha do titulo e com o mesmo
  // `pad` lateral do badge de "restam". A largura dele e medida ANTES do
  // texto e sai da pista das linhas, que encurtam em vez de passar por baixo.
  float seloLarg = 0.0f;
  if (ci->nota > 0 && ajustes_notas_home())
    seloLarg = badge_imdb_largura(ci->nota);
  // Quando o selo existe as linhas de texto perdem seloLarg + uma folga de
  // 16*esc — e o preco ja sai na largura, porque rec_selo_imdb ancora pela
  // esquerda e so devolve a medida depois de desenhar.
  float livre = largura - (seloLarg > 0 ? seloLarg + 16.0f * esc : 0.0f);
  if (livre < 40.0f) livre = 40.0f;   // texto sempre ganha um minimo de pista

  float base = r.y + r.h - 30*esc;
  int serie = !strcmp(ci->tipo, "series") && ci->temporada > 0 && ci->episodio > 0;
  if (serie && ci->nomeEpisodio[0]) {
    TxtLinha ep = txt_linha_corta(TXT_COND_CW_META, ci->nomeEpisodio, 230, 232, 238, 255, livre);
    base -= ep.h;
    txt_desenhar_alpha(ep, r.x + pad, base, 1);
    base -= 4*esc;
  }
  if (seloLarg > 0)
    badge_imdb(r.x + r.w - pad - seloLarg, base - BADGE_H, ci->nota, 0, 1.0f);
  TxtLinha titulo = txt_linha_corta(TXT_COND_CW_TITULO, ci->titulo, 247, 248, 250, 255, livre);
  base -= titulo.h;
  txt_desenhar_alpha(titulo, r.x + pad, base, 1);
  if (serie) {
    // Sem sigla "T1:E2": quem ve o card nao sabe o que T e E querem dizer.
    // Sao duas chaves prontas da tabela ("Temporada %d", "Episodio %d")
    // compostas aqui — em ingles sai "Season 1 · Episode 2".
    char tmpT[24], tmpE[24], te[64];
    snprintf(tmpT, sizeof tmpT, i18n("Temporada %d"), ci->temporada);
    snprintf(tmpE, sizeof tmpE, i18n("Episódio %d"), ci->episodio);
    snprintf(te, sizeof te, "%s · %s", tmpT, tmpE);
    TxtLinha ep = txt_linha(TXT_COND_CW_META, te, 230, 232, 238, 255);
    txt_desenhar_alpha(ep, r.x + pad, base - ep.h - 4*esc, 1);
  }

  // Linha fina, recuada da moldura. A parte vazia nao vira uma faixa cinza.
  if (ci->progresso > 0) {
    float h = NV_CW_BAR_H * esc;
    float preenchido = largura * anim_clamp(ci->progresso / 100.f, 0, 1);
    if (preenchido < h) preenchido = h;
    GfxRect barra = {r.x + pad, r.y + r.h - NV_CW_BAR_BOTTOM*esc - h, preenchido, h};
    gfx_cor(barra, .5f, .96f, .965f, .98f, .98f);
  }
  }
}
