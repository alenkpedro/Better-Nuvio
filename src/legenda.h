#ifndef NV_LEGENDA_H
#define NV_LEGENDA_H
#include <stddef.h>
#define LEGENDA_SIMULTANEAS 8
enum { LEG_OFF, LEG_LOADING, LEG_READY, LEG_ERROR };
typedef struct {
  double inicio,fim;
  char texto[768];
  short an,negrito,italico;
  int cor;
  float posX,posY,resX,resY;
  int ordem;
} LegendaCue;
/* A positive delay displays a cue later: file time = media time - delay. */
void legenda_carregar(const char *url);
void legenda_desligar(void);
void legenda_definir_corpo(const char *corpo);
void legenda_atualizar_corpo(const char *corpo);
unsigned legenda_geracao(void);
/* Installs a complete official extraction window, including a silent one. */
int legenda_instalar_janela(const char *corpo,unsigned geracao);
int legenda_ligada_em(unsigned geracao);
unsigned legenda_definir_corpo_se(const char *corpo,unsigned geracao);
int legenda_atualizar_corpo_se(const char *corpo,unsigned geracao);
int legenda_estado(void);
void legenda_bombear(void);
char *legenda_decodificar(const void *bytes,size_t tamanho);
int legenda_texto(double posSeg,int atrasoMs,char *dst,size_t tam);
int legenda_cues(double posSeg,int atrasoMs,LegendaCue *dst,int max);
int legenda_falas(double posSeg,int deslocamento,LegendaCue *dst,int max,int *foco,int *inicio,int *total);
int legenda_extrair(const char *corpo,LegendaCue **saida);
int legenda_extrair_srt(const char *corpo,LegendaCue **saida);
int legenda_extrair_ass(const char *corpo,LegendaCue **saida);
int legenda_eh_ass(const char *corpo);
#endif
