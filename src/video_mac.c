// FFmpeg decodifica no fio de trabalho; somente o fio de desenho toca OpenGL.
// Este backend e exclusivo da build interativa do Mac (NV_MAC_VIDEO). Os testes
// de UI e o pacote webOS continuam sem depender das bibliotecas do Homebrew.
#if defined(__APPLE__) && defined(NV_MAC_VIDEO)
#include "video_mac.h"
#include "gfx.h"
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { int x, y, w, h; } Rect;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t fio;
static int fioVivo;
static atomic_int pararFio;
static char endereco[4096], cabs[4096];
static int ativo, pronto, falhou, terminou, pausado, largura, altura;
static double duracao, baseSeg;
static Uint64 baseMs;
static int seekPendente;
static double seekSeg;
static unsigned char *quadro;
static int quadroW, quadroH;
static unsigned long quadroSeq, quadroEnviado;
static Rect destino = {0, 0, 1920, 1080}, recorte;
static GLuint textura;
static int texW, texH;
static unsigned char *upload;
static size_t uploadTam;
static SDL_AudioDeviceID audioDev;
static atomic_int volumePct = 100;
static VideoFaixa legFaixas[NV_FAIXA_MAX];
static int legStreams[NV_FAIXA_MAX], nLeg, legAtual = -1;
static atomic_int legStreamSelecionado = -1;
typedef struct { double inicio, fim; char texto[768]; } LegCueMac;
static LegCueMac legCues[32];
static int legNCues;

static void listarLegendas(const AVFormatContext *fmt) {
  VideoFaixa faixas[NV_FAIXA_MAX] = {{0}};
  int streams[NV_FAIXA_MAX], n = 0;
  for (unsigned i = 0; i < fmt->nb_streams && n < NV_FAIXA_MAX; i++) {
    const AVStream *s = fmt->streams[i];
    const AVCodecDescriptor *d;
    AVDictionaryEntry *lang;
    if (s->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
    d = avcodec_descriptor_get(s->codecpar->codec_id);
    if (!d || !(d->props & AV_CODEC_PROP_TEXT_SUB)) continue;
    lang = av_dict_get(s->metadata, "language", NULL, 0);
    snprintf(faixas[n].idioma, sizeof faixas[n].idioma, "%s",
             lang && lang->value ? lang->value : "und");
    snprintf(faixas[n].rotulo, sizeof faixas[n].rotulo,
             "Legenda %d · %.8s", n + 1, faixas[n].idioma);
    faixas[n].numero = (int)i;
    faixas[n].ordinalMkv = fmt->iformat && strstr(fmt->iformat->name,"matroska") ? n : -1;
    snprintf(faixas[n].codec,sizeof faixas[n].codec,"%s",
      s->codecpar->codec_id==AV_CODEC_ID_ASS?"S_TEXT/ASS":
      s->codecpar->codec_id==AV_CODEC_ID_SSA?"S_TEXT/SSA":
      s->codecpar->codec_id==AV_CODEC_ID_SUBRIP?"S_TEXT/UTF8":"native");
    streams[n++] = (int)i;
  }
  pthread_mutex_lock(&mu);
  memcpy(legFaixas, faixas, (size_t)n * sizeof faixas[0]);
  memcpy(legStreams, streams, (size_t)n * sizeof streams[0]);
  nLeg = n;
  pthread_mutex_unlock(&mu);
}

static const char *textoAss(const char *s) {
  int virgulas = 0;
  if (strncmp(s, "Dialogue:", 9)) return s;
  for (; *s; s++) if (*s == ',' && ++virgulas == 9) return s + 1;
  return s;
}

static void publicarLegenda(const AVSubtitle *sub, const AVPacket *pkt,
                            const AVStream *stream, int indice) {
  LegCueMac cue = {0};
  int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
  double inicio = sub->pts != AV_NOPTS_VALUE
                ? sub->pts / (double)AV_TIME_BASE
                : ts != AV_NOPTS_VALUE ? ts * av_q2d(stream->time_base) : -1.0;
  if (inicio < 0) return;
  if (stream->start_time != AV_NOPTS_VALUE)
    inicio -= stream->start_time * av_q2d(stream->time_base);
  cue.inicio = inicio + sub->start_display_time / 1000.0;
  cue.fim = inicio + sub->end_display_time / 1000.0;
  if (cue.fim <= cue.inicio) cue.fim = cue.inicio + 3.0;
  for (unsigned i = 0; i < sub->num_rects; i++) {
    const AVSubtitleRect *r = sub->rects[i];
    const char *parte = r->text ? r->text : r->ass ? textoAss(r->ass) : NULL;
    size_t usado = strlen(cue.texto);
    if (!parte || !*parte || usado + 2 >= sizeof cue.texto) continue;
    if (usado) cue.texto[usado++] = '\n';
    snprintf(cue.texto + usado, sizeof cue.texto - usado, "%s", parte);
  }
  if (!cue.texto[0] || atomic_load(&legStreamSelecionado) != indice) return;
  pthread_mutex_lock(&mu);
  if (legNCues == (int)(sizeof legCues / sizeof legCues[0])) {
    memmove(legCues, legCues + 1, (size_t)(legNCues - 1) * sizeof legCues[0]);
    legNCues--;
  }
  legCues[legNCues++] = cue;
  pthread_mutex_unlock(&mu);
}

static double relogio(void) {
  double t = baseSeg;
  if (largura > 0 && !pausado && !terminou)
    t += (double)(SDL_GetTicks64() - baseMs) / 1000.0;
  if (duracao > 0 && t > duracao) t = duracao;
  return t;
}

static int interromper(void *u) { (void)u; return atomic_load(&pararFio); }

static void erro(const char *etapa, int codigo) {
  char msg[128];
  av_strerror(codigo, msg, sizeof msg);
  fprintf(stderr, "[video Mac] %s: %s\n", etapa, msg);
  pthread_mutex_lock(&mu);
  if (!atomic_load(&pararFio)) falhou = 1;
  pthread_mutex_unlock(&mu);
}

static void cabecalhosFfmpeg(AVDictionary **opcoes) {
  char linhas[8192];
  size_t n = 0;
  const char *p = cabs;
  while (*p && n + 4 < sizeof linhas) {
    if (*p == '\r') { p++; continue; }
    if (*p == '\n') { linhas[n++] = '\r'; linhas[n++] = '\n'; p++; continue; }
    linhas[n++] = *p++;
  }
  if (n && (n < 2 || linhas[n-2] != '\r' || linhas[n-1] != '\n')) {
    linhas[n++] = '\r'; linhas[n++] = '\n';
  }
  linhas[n] = 0;
  if (n) av_dict_set(opcoes, "headers", linhas, 0);
}

static AVCodecContext *abrirCodec(const AVStream *s) {
  const AVCodec *codec = avcodec_find_decoder(s->codecpar->codec_id);
  AVCodecContext *ctx;
  if (!codec) return NULL;
  ctx = avcodec_alloc_context3(codec);
  if (!ctx) return NULL;
  if (avcodec_parameters_to_context(ctx, s->codecpar) < 0 ||
      avcodec_open2(ctx, codec, NULL) < 0) {
    avcodec_free_context(&ctx);
    return NULL;
  }
  return ctx;
}

static int controle(void) {
  int p, seek;
  pthread_mutex_lock(&mu);
  p = pausado;
  seek = seekPendente;
  pthread_mutex_unlock(&mu);
  if (audioDev) SDL_PauseAudioDevice(audioDev, p);
  return seek || atomic_load(&pararFio) ? 1 : p ? 2 : 0;
}

static double instanteQuadro(const AVFrame *f, const AVStream *s) {
  int64_t ts = f->best_effort_timestamp;
  if (ts == AV_NOPTS_VALUE) return -1.0;
  if (s->start_time != AV_NOPTS_VALUE) ts -= s->start_time;
  return ts * av_q2d(s->time_base);
}

static void publicarQuadro(const AVFrame *f, const AVStream *s,
                           struct SwsContext *sws, unsigned char *rgba,
                           int w, int h) {
  double pts = instanteQuadro(f, s);
  if (pts < 0) pts = 0;
  for (;;) {
    int estado = controle();
    double agora;
    if (estado == 1) return;
    if (estado == 2) { SDL_Delay(10); continue; }
    pthread_mutex_lock(&mu);
    agora = relogio();
    pthread_mutex_unlock(&mu);
    if (pts <= agora + 0.025) break;
    SDL_Delay((Uint32)fmin(10.0, (pts - agora) * 1000.0));
  }
  // Quando o decode nao acompanha o relogio, descarta o quadro atrasado em
  // vez de congelar a UI. O audio continua sendo entregue normalmente.
  pthread_mutex_lock(&mu);
  int atrasado = pts + 0.35 < relogio();
  pthread_mutex_unlock(&mu);
  if (atrasado) return;
  uint8_t *dst[4] = { rgba, NULL, NULL, NULL };
  int passo[4] = { w * 4, 0, 0, 0 };
  sws_scale(sws, (const uint8_t *const *)f->data, f->linesize,
            0, f->height, dst, passo);
  pthread_mutex_lock(&mu);
  memcpy(quadro, rgba, (size_t)w * h * 4);
  quadroSeq++;
  pronto = 1;
  pthread_mutex_unlock(&mu);
}

static void enviarAudio(const AVFrame *f, SwrContext *swr) {
  if (!swr || !audioDev) return;
  int n = (int)av_rescale_rnd(swr_get_delay(swr, f->sample_rate) + f->nb_samples,
                               48000, f->sample_rate, AV_ROUND_UP);
  if (n < 1 || n > 48000 * 2) return;
  uint8_t *pcm = av_malloc((size_t)n * 4);
  if (!pcm) return;
  uint8_t *saida[1] = { pcm };
  int feito = swr_convert(swr, saida, n, (const uint8_t **)f->extended_data,
                         f->nb_samples);
  if (feito > 0) {
    int volume = atomic_load(&volumePct);
    if (volume < 100) {
      int16_t *amostras = (int16_t *)pcm;
      for (int i = 0; i < feito * 2; i++)
        amostras[i] = (int16_t)((int)amostras[i] * volume / 100);
    }
    while (!atomic_load(&pararFio) && SDL_GetQueuedAudioSize(audioDev) > 48000 * 4 / 2) {
      int estado = controle();
      if (estado == 1) break;
      SDL_Delay(10);
    }
    if (controle() != 1 && !atomic_load(&pararFio))
      SDL_QueueAudio(audioDev, pcm, (Uint32)feito * 4);
  }
  av_free(pcm);
}

static void *decodificar(void *u) {
  (void)u;
  AVFormatContext *fmt = avformat_alloc_context();
  AVCodecContext *vc = NULL, *ac = NULL, *sc = NULL;
  AVFrame *frame = NULL;
  AVPacket *pkt = NULL;
  struct SwsContext *sws = NULL;
  SwrContext *swr = NULL;
  unsigned char *rgba = NULL;
  int vi = -1, ai = -1, si = -1, ow = 0, oh = 0, rc;
  double descartarAntes = 0.0;
  AVDictionary *opcoes = NULL;
  if (!fmt) { erro("alocar", AVERROR(ENOMEM)); goto fim; }
  fmt->interrupt_callback.callback = interromper;
  av_dict_set(&opcoes, "rw_timeout", "10000000", 0);
  cabecalhosFfmpeg(&opcoes);
  rc = avformat_open_input(&fmt, endereco, NULL, &opcoes);
  av_dict_free(&opcoes);
  if (rc < 0) { erro("abrir fonte", rc); goto fim; }
  rc = avformat_find_stream_info(fmt, NULL);
  if (rc < 0) { erro("ler faixas", rc); goto fim; }
  listarLegendas(fmt);
  vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (vi < 0 || !(vc = abrirCodec(fmt->streams[vi]))) {
    erro("video indisponivel", vi < 0 ? vi : AVERROR_DECODER_NOT_FOUND);
    goto fim;
  }
  ai = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if (ai >= 0) ac = abrirCodec(fmt->streams[ai]);
  ow = vc->width; oh = vc->height;
  if (ow < 1 || oh < 1) { erro("dimensoes", AVERROR_INVALIDDATA); goto fim; }
  double escala = fmin(1.0, fmin(1920.0 / ow, 1080.0 / oh));
  int rw = (int)(ow * escala), rh = (int)(oh * escala);
  if (rw < 1) rw = 1;
  if (rh < 1) rh = 1;
  sws = sws_getContext(ow, oh, vc->pix_fmt, rw, rh, AV_PIX_FMT_RGBA,
                       SWS_BILINEAR, NULL, NULL, NULL);
  rgba = av_malloc((size_t)rw * rh * 4);
  frame = av_frame_alloc(); pkt = av_packet_alloc();
  if (!sws || !rgba || !frame || !pkt) { erro("buffers", AVERROR(ENOMEM)); goto fim; }
  if (ac) {
    SDL_AudioSpec want = {0};
    want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2;
    want.samples = 4096;
    audioDev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audioDev) {
      AVChannelLayout stereo;
      av_channel_layout_default(&stereo, 2);
      if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, 48000,
                              &ac->ch_layout, ac->sample_fmt,
                              ac->sample_rate, 0, NULL) < 0 || swr_init(swr) < 0) {
        swr_free(&swr);
      }
      av_channel_layout_uninit(&stereo);
      SDL_PauseAudioDevice(audioDev, 0);
    }
  }
  pthread_mutex_lock(&mu);
  quadro = malloc((size_t)rw * rh * 4);
  quadroW = rw; quadroH = rh;
  largura = ow; altura = oh;
  duracao = fmt->duration > 0 ? (double)fmt->duration / AV_TIME_BASE : 0;
  baseMs = SDL_GetTicks64();
  pthread_mutex_unlock(&mu);
  if (!quadro) { erro("quadro", AVERROR(ENOMEM)); goto fim; }

  while (!atomic_load(&pararFio)) {
    int fazSeek = 0; double alvo = 0;
    pthread_mutex_lock(&mu);
    if (seekPendente) {
      fazSeek = 1; alvo = seekSeg; seekPendente = 0;
    }
    pthread_mutex_unlock(&mu);
    if (fazSeek) {
      int64_t ts = (int64_t)(alvo / av_q2d(fmt->streams[vi]->time_base));
      if (fmt->streams[vi]->start_time != AV_NOPTS_VALUE)
        ts += fmt->streams[vi]->start_time;
      if (av_seek_frame(fmt, vi, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
        avcodec_flush_buffers(vc);
        if (ac) avcodec_flush_buffers(ac);
        if (sc) avcodec_flush_buffers(sc);
        if (audioDev) SDL_ClearQueuedAudio(audioDev);
        descartarAntes = alvo - 0.03;
        pthread_mutex_lock(&mu);
        legNCues = 0;
        pthread_mutex_unlock(&mu);
      }
      continue;
    }
    if (controle() == 2) { SDL_Delay(10); continue; }
    rc = av_read_frame(fmt, pkt);
    if (rc < 0) {
      if (rc == AVERROR(EAGAIN)) { SDL_Delay(10); continue; }
      if (rc != AVERROR_EOF && !atomic_load(&pararFio)) erro("ler fluxo", rc);
      else if (!atomic_load(&pararFio)) {
        while (audioDev && SDL_GetQueuedAudioSize(audioDev) && !atomic_load(&pararFio))
          SDL_Delay(20);
        pthread_mutex_lock(&mu);
        terminou = 1;
        if (duracao > 0) baseSeg = duracao;
        pthread_mutex_unlock(&mu);
      }
      break;
    }
    { int escolhido = atomic_load(&legStreamSelecionado);
      if (escolhido != si) {
        avcodec_free_context(&sc);
        si = escolhido;
        if (si >= 0 && si < (int)fmt->nb_streams) sc = abrirCodec(fmt->streams[si]);
      }
    }
    if (pkt->stream_index == si && sc) {
      AVSubtitle sub = {0};
      int recebeu = 0;
      if (avcodec_decode_subtitle2(sc, &sub, &recebeu, pkt) >= 0 && recebeu)
        publicarLegenda(&sub, pkt, fmt->streams[si], si);
      avsubtitle_free(&sub);
      av_packet_unref(pkt);
      continue;
    }
    AVCodecContext *ctx = pkt->stream_index == vi ? vc :
                          pkt->stream_index == ai ? ac : NULL;
    if (ctx && avcodec_send_packet(ctx, pkt) >= 0) {
      while (!atomic_load(&pararFio) && avcodec_receive_frame(ctx, frame) == 0) {
        double pts = instanteQuadro(frame, fmt->streams[pkt->stream_index]);
        if (pts >= 0 && pts + (ctx == ac && frame->sample_rate > 0
                                  ? (double)frame->nb_samples / frame->sample_rate : 0)
                        < descartarAntes) {
          av_frame_unref(frame);
          continue;
        }
        if (ctx == vc)
          publicarQuadro(frame, fmt->streams[vi], sws, rgba, rw, rh);
        else enviarAudio(frame, swr);
        av_frame_unref(frame);
        if (controle() == 1) break;
      }
    }
    av_packet_unref(pkt);
  }
fim:
  if (audioDev) { SDL_CloseAudioDevice(audioDev); audioDev = 0; }
  swr_free(&swr);
  sws_freeContext(sws);
  av_free(rgba);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&vc);
  avcodec_free_context(&ac);
  avcodec_free_context(&sc);
  avformat_close_input(&fmt);
  return NULL;
}

int mac_video_iniciar(void) {
  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) &&
      SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    fprintf(stderr, "[video Mac] audio: %s\n", SDL_GetError());
  return 1;
}

void mac_video_parar(void) {
  atomic_store(&pararFio, 1);
  if (fioVivo) { pthread_join(fio, NULL); fioVivo = 0; }
  pthread_mutex_lock(&mu);
  ativo = pronto = falhou = terminou = pausado = 0;
  largura = altura = quadroW = quadroH = 0;
  duracao = baseSeg = 0;
  quadroSeq = quadroEnviado = 0;
  seekPendente = 0;
  nLeg = legNCues = 0; legAtual = -1;
  atomic_store(&legStreamSelecionado, -1);
  free(quadro); quadro = NULL;
  pthread_mutex_unlock(&mu);
  if (textura && SDL_GL_GetCurrentContext()) {
    gfx_tex_esquecer(textura);
    glDeleteTextures(1, &textura);
  }
  textura = 0; texW = texH = 0;
  free(upload); upload = NULL; uploadTam = 0;
}

int mac_video_tocar(const char *url, const char *cabecalhos) {
  mac_video_parar();
  if (!url || !*url) return 0;
  mac_video_iniciar();
  snprintf(endereco, sizeof endereco, "%s", url);
  snprintf(cabs, sizeof cabs, "%s", cabecalhos ? cabecalhos : "");
  atomic_store(&pararFio, 0);
  pthread_mutex_lock(&mu);
  ativo = 1;
  destino = (Rect){0, 0, 1920, 1080};
  recorte = (Rect){0, 0, 0, 0};
  pthread_mutex_unlock(&mu);
  if (pthread_create(&fio, NULL, decodificar, NULL) != 0) {
    pthread_mutex_lock(&mu); ativo = 0; falhou = 1; pthread_mutex_unlock(&mu);
    return 0;
  }
  fioVivo = 1;
  return 1;
}

void mac_video_bombear(void) {}
void mac_video_pausar(int p) {
  pthread_mutex_lock(&mu);
  if (!!p != pausado) {
    if (p) baseSeg = relogio();
    else baseMs = SDL_GetTicks64();
    pausado = !!p;
  }
  pthread_mutex_unlock(&mu);
}
void mac_video_volume(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  atomic_store(&volumePct, pct);
}

int mac_video_n_legenda(void) {
  pthread_mutex_lock(&mu);
  int n = nLeg;
  pthread_mutex_unlock(&mu);
  return n;
}

const VideoFaixa *mac_video_legenda(int i) {
  pthread_mutex_lock(&mu);
  const VideoFaixa *f = i >= 0 && i < nLeg ? &legFaixas[i] : NULL;
  pthread_mutex_unlock(&mu);
  return f;
}

int mac_video_legenda_atual(void) {
  pthread_mutex_lock(&mu);
  int i = legAtual;
  pthread_mutex_unlock(&mu);
  return i;
}

void mac_video_escolher_legenda(int i) {
  pthread_mutex_lock(&mu);
  if (i < 0 || i >= nLeg) i = -1;
  legAtual = i;
  legNCues = 0;
  atomic_store(&legStreamSelecionado, i < 0 ? -1 : legStreams[i]);
  pthread_mutex_unlock(&mu);
}

int mac_video_legenda_nativa(char *dst, int tam) {
  size_t n = 0;
  if (!dst || tam < 2) return 0;
  dst[0] = 0;
  pthread_mutex_lock(&mu);
  if (legAtual >= 0 && ativo) {
    double agora = relogio();
    for (int i = 0; i < legNCues; i++) {
      const LegCueMac *c = &legCues[i];
      if (agora < c->inicio || agora >= c->fim) continue;
      if (n && n + 1 < (size_t)tam) dst[n++] = '\n';
      if (n + 1 >= (size_t)tam) break;
      snprintf(dst + n, (size_t)tam - n, "%s", c->texto);
      n = strlen(dst);
    }
  }
  pthread_mutex_unlock(&mu);
  return n > 0;
}
void mac_video_buscar(double s) {
  pthread_mutex_lock(&mu);
  if (s < 0) s = 0;
  if (duracao > 0 && s > duracao) s = duracao;
  seekSeg = s; seekPendente = 1;
  baseSeg = s; baseMs = SDL_GetTicks64(); terminou = 0;
  pthread_mutex_unlock(&mu);
}
void mac_video_janela(int x, int y, int w, int h) {
  pthread_mutex_lock(&mu);
  destino = (Rect){x,y,w,h}; recorte = (Rect){0,0,0,0};
  quadroEnviado = 0;
  pthread_mutex_unlock(&mu);
}
void mac_video_janela_fonte(int sx, int sy, int sw, int sh,
                            int dx, int dy, int dw, int dh) {
  pthread_mutex_lock(&mu);
  destino = (Rect){dx,dy,dw,dh}; recorte = (Rect){sx,sy,sw,sh};
  quadroEnviado = 0;
  pthread_mutex_unlock(&mu);
}

// O OpenGL do SDL vive no fio principal. Recortar aqui permite reaproveitar
// GFX_SNAP e os mesmos retangulos de aspecto/zoom enviados pelo player da TV.
void mac_video_desenhar(void) {
  Rect d, c;
  int fw, fh, x, y, w, h, mudou;
  pthread_mutex_lock(&mu);
  if (!quadro || !pronto) { pthread_mutex_unlock(&mu); return; }
  d = destino; c = recorte; fw = quadroW; fh = quadroH;
  if (c.w > 0 && c.h > 0 && largura > 0 && altura > 0) {
    x = (int)((double)c.x * fw / largura);
    y = (int)((double)c.y * fh / altura);
    w = (int)((double)c.w * fw / largura);
    h = (int)((double)c.h * fh / altura);
  } else { x = y = 0; w = fw; h = fh; }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= fw) x = fw - 1;
  if (y >= fh) y = fh - 1;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (x + w > fw) w = fw - x;
  if (y + h > fh) h = fh - y;
  mudou = quadroEnviado != quadroSeq || texW != w || texH != h;
  if (mudou) {
    size_t tam = (size_t)w * h * 4;
    if (uploadTam < tam) {
      unsigned char *novo = realloc(upload, tam);
      if (!novo) { pthread_mutex_unlock(&mu); return; }
      upload = novo; uploadTam = tam;
    }
    for (int linha = 0; linha < h; linha++)
      memcpy(upload + (size_t)linha * w * 4,
             quadro + ((size_t)(y + linha) * fw + x) * 4, (size_t)w * 4);
    quadroEnviado = quadroSeq;
  }
  pthread_mutex_unlock(&mu);
  if (d.w <= 0 || d.h <= 0) return;
  if (!textura) {
    glGenTextures(1, &textura);
    glBindTexture(GL_TEXTURE_2D, textura);
    gfx_tex_esquecer(0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  if (mudou) {
    glBindTexture(GL_TEXTURE_2D, textura);
    gfx_tex_esquecer(0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texW != w || texH != h) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, upload);
      texW = w; texH = h;
    } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                           GL_RGBA, GL_UNSIGNED_BYTE, upload);
  }
  gfx_rect((GfxRect){d.x,d.y,d.w,d.h}, textura, GFX_SNAP,
           0, 0, 0, 0, 0, 0, 0, 1);
}

double mac_video_pos(void) { pthread_mutex_lock(&mu); double v=relogio(); pthread_mutex_unlock(&mu); return v; }
double mac_video_duracao(void) { pthread_mutex_lock(&mu); double v=duracao; pthread_mutex_unlock(&mu); return v; }
int mac_video_tocando(void) { pthread_mutex_lock(&mu); int v=pronto&&!pausado&&!falhou&&!terminou; pthread_mutex_unlock(&mu); return v; }
int mac_video_pronto(void) { pthread_mutex_lock(&mu); int v=pronto; pthread_mutex_unlock(&mu); return v; }
int mac_video_ativo(void) { pthread_mutex_lock(&mu); int v=ativo&&!falhou; pthread_mutex_unlock(&mu); return v; }
int mac_video_falhou(void) { pthread_mutex_lock(&mu); int v=falhou; pthread_mutex_unlock(&mu); return v; }
int mac_video_terminou(void) { pthread_mutex_lock(&mu); int v=terminou; pthread_mutex_unlock(&mu); return v; }
int mac_video_largura(void) { pthread_mutex_lock(&mu); int v=largura; pthread_mutex_unlock(&mu); return v; }
int mac_video_altura(void) { pthread_mutex_lock(&mu); int v=altura; pthread_mutex_unlock(&mu); return v; }

#endif
