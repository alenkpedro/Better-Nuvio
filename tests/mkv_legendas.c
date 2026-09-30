// #92: casamento legenda da TV <-> TrackEntry num MKV de varias legendas, e
// sonda de codecs e ordinais pela faixa certa. O MKV vem de tests/mkv_legendas.sh:
//   TrackNumber 1 video, 2 audio, 3 ASS "Faixa A" (eng), 4 SRT "Faixa B" (por),
//   5 ASS "Faixa C" (spa).
// A LG lista essas legendas com trackNum 0, 1, 2 (MEDIDO na C9 num Erai-raws
// de 8 legendas: 0..7 contra TrackNumber 3..10).
#include "../src/mkv.h"
#include "../src/video.h"
#include "../src/rede.h"
#include "../src/dados.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int falhas;
static void ok(int c, const char *o) {
  printf("  %-60s %s\n", o, c ? "ok" : "FALHOU");
  if (!c) falhas++;
}
int main(int argc, char **argv) {
  MkvFaixa fx[MKV_MAX_FAIXAS];
  int n, idx[8], modo;
  if (argc < 2) { fprintf(stderr, "uso: %s url\n", argv[0]); return 2; }
  dados_iniciar(NULL);

  printf("[1] cabecalho do MKV de varias legendas\n");
  n = mkv_faixas(argv[1], fx, MKV_MAX_FAIXAS);
  ok(n == 5, "5 TrackEntry (video, audio, 3 legendas)");
  ok(n == 5 && fx[2].numero == 3 && fx[3].numero == 4 && fx[4].numero == 5,
     "legendas com TrackNumber 3, 4, 5");
  ok(n == 5 && !strcmp(fx[2].codec, "S_TEXT/ASS") && !strcmp(fx[3].codec, "S_TEXT/UTF8") &&
     !strcmp(fx[4].codec, "S_TEXT/ASS"), "codecs ASS, SRT, ASS");

  printf("\n[2] trackNum da LG (ordinal) -> TrackEntry\n");
  { int tv[3] = { 0, 1, 2 };
    modo = mkv_casar_legendas(fx, n, tv, 3, idx);
    ok(modo == MKV_CASA_ORDINAL, "trackNum 0,1,2 casa por ORDINAL");
    ok(idx[0] == 2 && idx[1] == 3 && idx[2] == 4, "0->TrackNumber 3, 1->4, 2->5");
    ok(!strcmp(fx[idx[0]].codec, "S_TEXT/ASS") && !strcmp(fx[idx[1]].codec, "S_TEXT/UTF8"),
       "a PRIMEIRA legenda leva o selo ASS (era a que ficava sem, o English do #92)"); }
  { int tv[3] = { 2, 0, 1 };     // a TV lista fora de ordem: o ordinal vale, a posicao nao
    modo = mkv_casar_legendas(fx, n, tv, 3, idx);
    ok(modo == MKV_CASA_ORDINAL && idx[0] == 4 && idx[1] == 2 && idx[2] == 3,
       "lista fora de ordem casa pelo valor, nao pela posicao"); }
  { int tv[3] = { 3, 4, 5 };     // leitura antiga, se alguma TV entregar TrackNumber
    modo = mkv_casar_legendas(fx, n, tv, 3, idx);
    ok(modo == MKV_CASA_NUMERO && idx[0] == 2 && idx[2] == 4, "TrackNumber 3,4,5 ainda casa");
  }
  { int tv[2] = { 0, 1 };        // a TV escondeu uma faixa: ordinal nao e confiavel
    modo = mkv_casar_legendas(fx, n, tv, 2, idx);
    ok(modo == MKV_CASA_NADA && idx[0] == -1 && idx[1] == -1, "contagem diferente: nao casa nada");
  }
  { int tv[3] = { 0, 0, 1 };
    ok(mkv_casar_legendas(fx, n, tv, 3, idx) == MKV_CASA_NADA, "ordinal repetido: nao casa nada"); }
  { int tv[3] = { 1, 2, 3 };     // nem ordinal (3 fora) nem TrackNumber (1, 2 nao sao legenda)
    ok(mkv_casar_legendas(fx, n, tv, 3, idx) == MKV_CASA_NADA,
       "trackNum que cairia no video/audio nao casa"); }

  printf("\n[3] release com 43 legendas: transicao para o overlay do app\n");
  {
    MkvFaixa muitas[45] = {{0}};
    int tv[43], pares[43], completo=1;
    muitas[0].tipo=1;muitas[0].numero=1;
    muitas[1].tipo=2;muitas[1].numero=2;
    for(int i=0;i<43;i++) {
      muitas[i+2].tipo=17;muitas[i+2].numero=i+3;
      snprintf(muitas[i+2].codec,sizeof muitas[i+2].codec,"S_TEXT/UTF8");
      tv[i]=i;
    }
    ok(NV_FAIXA_MAX>=43,"painel comporta todas as 43 legendas");
    ok(MKV_MAX_FAIXAS>NV_FAIXA_MAX,"cabecalho reserva espaco para video e audio");
    ok(mkv_casar_legendas(muitas,45,tv,32,pares)==MKV_CASA_NADA,
       "antigo corte em 32 bloqueava todos os ordinais");
    modo=mkv_casar_legendas(muitas,45,tv,43,pares);
    for(int i=0;i<43;i++)if(pares[i]!=i+2)completo=0;
    ok(modo==MKV_CASA_ORDINAL&&completo,"43 legendas recebem o codec e o ordinal corretos");
  }

  printf("\n%s (%d falha%s)\n", falhas ? "FALHOU" : "tudo ok", falhas, falhas == 1 ? "" : "s");
  return falhas ? 1 : 0;
}
