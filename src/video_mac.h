#ifndef NV_VIDEO_MAC_H
#define NV_VIDEO_MAC_H

#include "video.h"

// Backend de desenvolvimento. So a build Mac com NV_MAC_VIDEO o vincula.
int mac_video_iniciar(void);
int mac_video_tocar(const char *url, const char *cabecalhos);
void mac_video_bombear(void);
void mac_video_parar(void);
void mac_video_pausar(int pausado);
void mac_video_volume(int pct);
void mac_video_buscar(double segundos);
void mac_video_janela(int x, int y, int w, int h);
void mac_video_janela_fonte(int sx, int sy, int sw, int sh,
                            int dx, int dy, int dw, int dh);
void mac_video_desenhar(void);
double mac_video_pos(void);
double mac_video_duracao(void);
int mac_video_tocando(void);
int mac_video_pronto(void);
int mac_video_ativo(void);
int mac_video_falhou(void);
int mac_video_terminou(void);
int mac_video_largura(void);
int mac_video_altura(void);
void mac_video_preferir_audio(const char *language);
int mac_video_n_audio(void);
const VideoFaixa *mac_video_audio(int i);
int mac_video_audio_atual(void);
void mac_video_escolher_audio(int i);
int mac_video_n_legenda(void);
const VideoFaixa *mac_video_legenda(int i);
int mac_video_legenda_atual(void);
void mac_video_escolher_legenda(int i);
int mac_video_legenda_nativa(char *dst, int tam);

#endif
