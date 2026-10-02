#ifndef NV_SEEKR_H
#define NV_SEEKR_H
#include "gl_compat.h"

// A chave e o contador pertencem a conta logada nesta TV.
int seekr_chave_configurada(void);
int seekr_chave_pessoal_configurada(void);
int seekr_chave_integrada(void);
int seekr_definir_chave(const char *chave); // string vazia remove; nunca expor em UI/log
int seekr_usadas_hoje(void);

void seekr_preparar(const char *imdb, long tmdb, int temporada, int episodio);
void seekr_duracao(double segundos); // chamar so com duracao real do pipeline
void seekr_prefetch(double segundos); // uma miniatura proxima da posicao inicial
void seekr_bombear(void);            // upload GL, somente no fio de desenho
int seekr_previa(double segundos, GLuint *tex, double *cueSegundos);
#define SEEKR_PREVIAS 5
typedef struct {
  GLuint tex;
  double segundos;
  float aspecto;
  int valido;
} SeekrPrevia;
// O centro e o cue escolhido; os vizinhos seguem a ordem do VTT. Extremos
// ficam invalidos, sem repetir o primeiro/ultimo quadro para preencher a fila.
int seekr_faixa(double segundos, SeekrPrevia quadros[SEEKR_PREVIAS]);
void seekr_fechar(void);

// Leitor WebVTT puro, usado pelo teste e pelo worker. Devolve o numero de cues.
typedef struct {
  double inicio, fim;
  char url[768];
  int x, y, w, h;
} SeekrCue;
int seekr_ler_vtt(const char *vtt, SeekrCue *saida, int max);
int seekr_escolher_cue(const SeekrCue *cues, int n, double segundoFonte);
#endif
