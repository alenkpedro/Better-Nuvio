#ifndef NV_PROGRESSO_H
#define NV_PROGRESSO_H
#include <stdint.h>

/* Portable Nuvio watch state. Display identity is never a stream lookup ID.
 * Times at this boundary are seconds; cloud/storage use integer milliseconds. */
#define PROG_MAX 2048

typedef struct {
  int perfil;
  char chave[96], contentId[64], tipo[16], videoId[128];
  int temporada, episodio;
  double posSeg, durSeg;
  long long lastWatchedMs;
  int pendente, removido;
  uint64_t revisao;
  long long remotoMs;
  double percentual; /* -1 = absent, 0..100 = provider percentage */
  char titulo[160], nomeEpisodio[120];
  char poster[512], backdrop[512], logo[512];
} ProgRegistro;

void prog_content_id(char *dst, unsigned n, const char *id, int *t, int *e);
void prog_chave(char *dst, unsigned n, const char *id, int t, int e);
double prog_fracao(const ProgRegistro *r);
int prog_andamento(const ProgRegistro *r);
int prog_concluido(const ProgRegistro *r);
int prog_ler(ProgRegistro *saida, int max);
int prog_ler_perfil(int perfil, ProgRegistro *saida, int max);
int prog_por_chave(const char *chave, ProgRegistro *saida);
int prog_gravar_registro_local(const ProgRegistro *r);
int prog_gravar_local(const char *id, int t, int e, double pos, double dur);
void prog_enriquecer(const ProgRegistro *r);
int prog_aplicar_remoto(const ProgRegistro *r);
int prog_aplicar_snapshot(int perfil, const ProgRegistro *r, int n);
int prog_pendentes(ProgRegistro *saida, int max);
int prog_pendentes_perfil(int perfil, ProgRegistro *saida, int max, int apagados);
void prog_confirmar(const ProgRegistro *enviados, int n);
void prog_remover(const char *chave);
void prog_marcar_removido(const char *id);
int prog_removido_vence(const char *id, long long instanteMs);
void prog_esquecer_tudo(void);
void prog_invalidar(void);
long long prog_agora_ms(void);
void prog_definir_relogio(long long (*relogio)(void));
unsigned prog_revisao(void);
#endif
