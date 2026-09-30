#include "subtitle_engine.h"
#include "legenda.h"
#include "assrender.h"
#include "video.h"
#include "rede.h"
#include "js.h"
#include "jsw.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>

#define WINDOW_SECONDS 120.0
#define PREFETCH_SECONDS 30.0
#define BODY_LIMIT (4 * 1024 * 1024)
typedef struct {
  unsigned selection, request;
  int ordinal;
  double start;
  char url[4096];
} WindowRequest;
typedef struct {
  unsigned selection, request;
  char *body;
  double start, end;
  int failed, permanent;
} WindowResult;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned selection, request;
static WindowResult *result;
/* The following state belongs exclusively to the app thread. */
static int selected = -1, nativeOwner, loading, extracting, started, errorNotice;
static unsigned documentOwner;
static double windowStart, windowEnd, loadingStart;
static Uint32 failedAt;
static char sourceUrl[4096];

static void freeResult(WindowResult *r) { if (r) { free(r->body); free(r); } }
static void ensureService(void) {
#ifdef __arm__
  char *health = rede_baixar("http://127.0.0.1:2732/health", 1);
  if (!health) (void)system("/usr/bin/luna-send-pub -n 1 -f luna://com.betternuvio.app.plugin/ping '{}' >/dev/null 2>&1");
  free(health);
#endif
}
static void *loadWindow(void *arg) {
  WindowRequest *q = arg;
  WindowResult *r = calloc(1, sizeof *r);
  if (!r) { free(q); return NULL; }
  r->selection=q->selection; r->request=q->request; r->failed=1;
  Jsw w; jsw_iniciar(&w); jsw_obj_ini(&w);
  jsw_cs(&w,"url",q->url); jsw_ci(&w,"trackOrdinal",q->ordinal);
  jsw_ci(&w,"startSeconds",(long long)q->start);
  jsw_ci(&w,"endSeconds",(long long)(q->start+WINDOW_SECONDS));
  jsw_chave(&w,"includeAssBody"); jsw_bool(&w,1); jsw_obj_fim(&w);
  ensureService();
  int status=0;
  const char *endpoint=getenv("NUVIO_SUBTITLE_SERVICE_URL");
  if(!endpoint||!*endpoint)endpoint="http://127.0.0.1:2732/subtitles/window";
  char *response=rede_postar_st(endpoint,60,NULL,jsw_texto_final(&w),&status);
  jsw_livre(&w);
  if(response && status==200) {
    const char *end=js_fim(response);
    r->body=calloc(1,BODY_LIMIT+1);
    if(r->body) {
      js_texto_linhas(response,end,"assBody",r->body,BODY_LIMIT+1);
      if(!r->body[0]) js_texto_linhas(response,end,"body",r->body,BODY_LIMIT+1);
      r->start=js_num(response,end,"windowStartSeconds",q->start);
      r->end=js_num(response,end,"windowEndSeconds",q->start+WINDOW_SECONDS);
      r->failed=r->end<=r->start || !r->body[0];
    }
  } else if(response) {
    char code[64]="";js_texto(response,js_fim(response),"errorCode",code,sizeof code);
    r->permanent=!strcmp(code,"RANGE_UNAVAILABLE") || !strcmp(code,"TRACK_NOT_FOUND") || !strcmp(code,"INVALID_TRACK");
    fprintf(stderr,"[subtitles] official extractor: %s\n",code);
  }
  free(response);
  pthread_mutex_lock(&mu);
  if(q->selection==selection && q->request==request) {
    freeResult(result);result=r;r=NULL;
  }
  pthread_mutex_unlock(&mu);
  freeResult(r);free(q);return NULL;
}
void subtitle_engine_reset(void) {
  pthread_mutex_lock(&mu);selection++;request++;freeResult(result);result=NULL;pthread_mutex_unlock(&mu);
  selected=-1;nativeOwner=loading=extracting=started=errorNotice=0;
  windowStart=windowEnd=0;failedAt=0;sourceUrl[0]=0;
  legenda_desligar();documentOwner=legenda_geracao();
}
void subtitle_engine_select(int embedded,const char *externalUrl) {
  subtitle_engine_reset();selected=embedded;assrender_preaquecer();
  video_escolher_legenda(embedded);
  if(externalUrl && *externalUrl) { selected=-2;video_escolher_legenda(-1);legenda_carregar(externalUrl);return; }
  nativeOwner=embedded>=0;
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
  if(embedded>=0) { snprintf(sourceUrl,sizeof sourceUrl,"%s",video_url_atual()); extracting=1; }
#endif
}
static void requestWindow(double time,int ordinal) {
  WindowRequest *q=calloc(1,sizeof *q);if(!q)return;
  pthread_mutex_lock(&mu);q->selection=selection;q->request=++request;pthread_mutex_unlock(&mu);
  q->ordinal=ordinal;q->start=floor(fmax(0,time)/90.0)*90.0;loadingStart=q->start;
  snprintf(q->url,sizeof q->url,"%s",sourceUrl);
  pthread_t thread;
  if(pthread_create(&thread,NULL,loadWindow,q)){free(q);errorNotice=1;failedAt=SDL_GetTicks()|1u;return;}
  pthread_detach(thread);loading=1;started=1;
}
void subtitle_engine_tick(double media,int delay) {
  if(selected==-2) {
    if(legenda_estado()==LEG_ERROR && !failedAt){failedAt=SDL_GetTicks()|1u;errorNotice=1;}
    legenda_bombear();return;
  }
  if(!extracting)return;
  double time=fmax(0,media-delay/1000.0);
  pthread_mutex_lock(&mu);WindowResult *r=result;result=NULL;pthread_mutex_unlock(&mu);
  if(r) {
    loading=0;
    if(!r->failed) {
      /* Installation precedes hiding the platform track. Empty windows remain
       * successful windows: silence never causes renderer toggling. */
      if(legenda_instalar_janela(r->body,documentOwner)) {
        windowStart=r->start;windowEnd=r->end;failedAt=0;
        legenda_bombear();
        if(nativeOwner){video_escolher_legenda(-1);nativeOwner=0;}
        printf("[subtitles] official window %.0f..%.0f, media %.3f, selection %u\n",windowStart,windowEnd,media,selection);
      }
    } else {
      failedAt=SDL_GetTicks()|1u;errorNotice=1;
      if(r->permanent){extracting=0;legenda_desligar();video_escolher_legenda(selected);nativeOwner=1;}
    }
    freeResult(r);
  }
  int ordinal=video_legenda_ordinal_mkv(selected);
  if(ordinal<0) { if(!started && video_mkv_sondado()==0)video_sondar_mkv_agora();return; }
  const VideoFaixa *track=video_legenda(selected);
  if(!track || (strcmp(track->codec,"S_TEXT/ASS") && strcmp(track->codec,"S_TEXT/SSA") && strcmp(track->codec,"S_TEXT/UTF8"))) { extracting=0;return; }
  if(strncmp(sourceUrl,"http://",7) && strncmp(sourceUrl,"https://",8)){extracting=0;return;}
  if(loading && (time<loadingStart || time>=loadingStart+WINDOW_SECONDS)) {
    pthread_mutex_lock(&mu);request++;freeResult(result);result=NULL;pthread_mutex_unlock(&mu);
    loading=0;failedAt=0;
  }
  int outside=time<windowStart || time>=windowEnd;
  int approaching=windowEnd>0 && time>=windowEnd-PREFETCH_SECONDS;
  if(!loading && (!started||outside||approaching) && (!failedAt||SDL_GetTicks()-failedAt>=5000u))requestWindow(time,ordinal);
  legenda_bombear();
}
int subtitle_engine_selected(void){return selected;}
int subtitle_engine_native(void){return nativeOwner;}
int subtitle_engine_loading(void){return loading;}
int subtitle_engine_take_error(void){int e=errorNotice;errorNotice=0;return e;}
