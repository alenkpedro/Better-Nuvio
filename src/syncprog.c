#include "syncprog.h"
#include "progresso.h"
#include "sessao.h"
#include "perfis.h"
#include "dados.h"
#include "catalogo.h"
#include "js.h"
#include "jsw.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Cloud adapter rebuilt from Nuvio's portable RPC contract.
 * Every request captures its profile; acknowledgements capture row revisions. */
static pthread_mutex_t mailboxMu=PTHREAD_MUTEX_INITIALIZER;
static ProgRegistro *mailbox;
static int mailboxCount, mailboxProfile, mailboxChanged;
static unsigned epoch;
static int success(const char *body,int status){return body && status>=200 && status<300;}
static long long freshness(const char *p,const char *f) {
  const char *keys[]={"updated_at","last_watched","lastWatched","updatedAt"};
  for(unsigned i=0;i<sizeof keys/sizeof *keys;i++) {
    char text[80];double v;
    if(js_texto(p,f,keys[i],text,sizeof text)) {
      if(strchr(text,'T') || strchr(text,'-')) {long long t=js_ms_iso(text);if(t>0)return t;continue;}
      v=strtod(text,NULL);
    } else v=js_num(p,f,keys[i],-1);
    if(isfinite(v) && v>0)return (long long)(v>1000000000000.0?v:v*1000);
  }return 0;
}
static void text2(const char *p,const char *f,const char *a,const char *b,char *dst,unsigned cap) {
  if(!js_texto(p,f,a,dst,cap) || !*dst)js_texto(p,f,b,dst,cap);
}
static int parseRow(const char *p,int profile,ProgRegistro *r) {
  const char *f=js_fim(p);memset(r,0,sizeof *r);r->perfil=profile;r->percentual=-1;
  int rowProfile=(int)js_num(p,f,"profile_id",profile);if(rowProfile!=profile)return 0;
  char id[128];text2(p,f,"content_id","contentId",id,sizeof id);if(!*id)return 0;
  int st=0,ep=0;prog_content_id(r->contentId,sizeof r->contentId,id,&st,&ep);
  r->temporada=(int)js_num(p,f,"season",js_num(p,f,"season_number",st));
  r->episodio=(int)js_num(p,f,"episode",js_num(p,f,"episode_number",ep));
  text2(p,f,"video_id","videoId",r->videoId,sizeof r->videoId);
  if(r->episodio<=0) {
    int s=0,e=0;char parent[64];
    if(sscanf(r->videoId,"__nuvio_episode__:%d:%d",&s,&e)==2 && s>=0 && e>0){r->temporada=s;r->episodio=e;}
    else {prog_content_id(parent,sizeof parent,r->videoId,&s,&e);if(e>0 && !strcmp(parent,r->contentId)){r->temporada=s;r->episodio=e;}}
  }
  if(r->episodio<=0)r->temporada=r->episodio=0;
  text2(p,f,"content_type","contentType",r->tipo,sizeof r->tipo);
  if(!r->tipo[0])snprintf(r->tipo,sizeof r->tipo,"%s",r->episodio>0?"series":"movie");
  double pm=js_num(p,f,"position_ms",js_num(p,f,"positionMs",-1));
  double dm=js_num(p,f,"duration_ms",js_num(p,f,"durationMs",-1));
  double lp=js_num(p,f,"position",0),ld=js_num(p,f,"duration",0);
  int legacyMillis=pm<0 && dm<0 && ld>28800;
  if(pm<0)pm=lp*(legacyMillis||lp>28800?1:1000);
  if(dm<0)dm=ld*(legacyMillis||ld>28800?1:1000);
  if(dm>86400000 && dm/1000<=86400000){pm/=1000;dm/=1000;}
  if(!isfinite(pm)||!isfinite(dm)||pm<0||dm<0)return 0;
  r->posSeg=pm/1000;r->durSeg=dm/1000;
  r->percentual=js_num(p,f,"progress_percent",js_num(p,f,"progressPercent",-1));
  char source[48];js_texto(p,f,"source",source,sizeof source);if(!strcmp(source,"trakt_history"))r->percentual=100;
  if(r->posSeg<=0 && r->percentual<=0)return 0;
  r->lastWatchedMs=freshness(p,f);
  text2(p,f,"title","name",r->titulo,sizeof r->titulo);
  text2(p,f,"episode_title","episodeTitle",r->nomeEpisodio,sizeof r->nomeEpisodio);
  js_texto(p,f,"poster",r->poster,sizeof r->poster);text2(p,f,"backdrop","background",r->backdrop,sizeof r->backdrop);js_texto(p,f,"logo",r->logo,sizeof r->logo);
  prog_chave(r->chave,sizeof r->chave,r->contentId,r->temporada,r->episodio);
  return 1;
}
int syncprog_puxar(void) {
  int profile=perfis_ativo(),status=0,n=0;unsigned started;
  pthread_mutex_lock(&mailboxMu);started=epoch;pthread_mutex_unlock(&mailboxMu);
  Jsw w;jsw_iniciar(&w);jsw_obj_ini(&w);jsw_ci(&w,"p_profile_id",profile);jsw_obj_fim(&w);
  char *body=sessao_rpc("sync_pull_watch_progress",jsw_texto_final(&w),&status);jsw_livre(&w);
  if(!success(body,status)){free(body);return -1;}
  const char *first=js_raiz_array(body);
  const char *check=body;while(*check==' '||*check=='\r'||*check=='\n'||*check=='\t')check++;
  if(*check!='['){free(body);return -1;}
  ProgRegistro *snapshot=calloc(PROG_MAX,sizeof *snapshot);if(!snapshot){free(body);return -1;}
  for(const char *p=first;p && n<PROG_MAX;p=js_prox(js_fim(p))) {
    ProgRegistro r;if(!parseRow(p,profile,&r))continue;
    int dup=-1;for(int i=0;i<n;i++)if(!strcmp(snapshot[i].chave,r.chave)){dup=i;break;}
    if(dup>=0){if(r.lastWatchedMs>snapshot[dup].lastWatchedMs)snapshot[dup]=r;}
    else snapshot[n++]=r;
  }
  free(body);pthread_mutex_lock(&mailboxMu);
  if(started!=epoch){pthread_mutex_unlock(&mailboxMu);free(snapshot);return -1;}
  /* Reconcile before any push in this cycle. Staging until the UI frame let
   * stale pending rows overwrite newer progress from another device. */
  int changed=prog_aplicar_snapshot(profile,snapshot,n);
  free(mailbox);mailbox=snapshot;mailboxCount=n;mailboxProfile=profile;mailboxChanged+=changed;
  pthread_mutex_unlock(&mailboxMu);return n;
}
static int flushDeletes(int profile) {
  ProgRegistro *sent=calloc(PROG_MAX,sizeof *sent);if(!sent)return -1;
  int n=prog_pendentes_perfil(profile,sent,PROG_MAX,1);if(!n){free(sent);return 0;}
  Jsw w;jsw_iniciar(&w);jsw_obj_ini(&w);jsw_ci(&w,"p_profile_id",profile);jsw_chave(&w,"p_keys");jsw_arr_ini(&w);
  for(int i=0;i<n;i++)jsw_str(&w,sent[i].chave);jsw_arr_fim(&w);jsw_obj_fim(&w);
  int status=0;char *body=sessao_rpc("sync_delete_watch_progress",jsw_texto_final(&w),&status);jsw_livre(&w);
  int ok=success(body,status);free(body);if(ok)prog_confirmar(sent,n);free(sent);return ok?n:-1;
}
int syncprog_empurrar(void) {
  int profile=perfis_ativo();if(flushDeletes(profile)<0)return -1;
  ProgRegistro *sent=calloc(PROG_MAX,sizeof *sent);if(!sent)return -1;
  int n=prog_pendentes_perfil(profile,sent,PROG_MAX,0),k=0,status=0;
  if(!n){free(sent);return 0;}
  Jsw w;jsw_iniciar(&w);jsw_obj_ini(&w);jsw_ci(&w,"p_profile_id",profile);jsw_cs(&w,"p_origin_client_id",dados_cliente_id());jsw_chave(&w,"p_entries");jsw_arr_ini(&w);
  for(int i=0;i<n;i++) {
    ProgRegistro *r=&sent[i];if(r->durSeg>0 && r->durSeg<60)continue;
    char video[128];if(r->videoId[0])snprintf(video,sizeof video,"%s",r->videoId);
    else if(r->episodio>0)snprintf(video,sizeof video,"__nuvio_episode__:%d:%d",r->temporada,r->episodio);
    else snprintf(video,sizeof video,"%s",r->contentId);
    jsw_obj_ini(&w);jsw_cs(&w,"content_id",r->contentId);jsw_cs(&w,"content_type",r->tipo);jsw_cs(&w,"video_id",video);
    if(r->episodio>0){jsw_ci(&w,"season",r->temporada);jsw_ci(&w,"episode",r->episodio);}
    else {jsw_chave(&w,"season");jsw_nulo(&w);jsw_chave(&w,"episode");jsw_nulo(&w);}
    jsw_ci(&w,"position",(long long)llround(r->posSeg*1000));jsw_ci(&w,"duration",(long long)llround(r->durSeg*1000));
    jsw_ci(&w,"last_watched",r->lastWatchedMs);jsw_cs(&w,"progress_key",r->chave);
    if(r->titulo[0])jsw_cs(&w,"title",r->titulo);if(r->poster[0])jsw_cs(&w,"poster",r->poster);if(r->backdrop[0])jsw_cs(&w,"backdrop",r->backdrop);if(r->logo[0])jsw_cs(&w,"logo",r->logo);
    jsw_obj_fim(&w);sent[k++]=*r;
  }
  jsw_arr_fim(&w);jsw_obj_fim(&w);
  if(!k || w.erro){jsw_livre(&w);free(sent);return k? -1:0;}
  char *body=sessao_rpc("sync_push_watch_progress",jsw_texto_final(&w),&status);jsw_livre(&w);
  int ok=success(body,status);free(body);if(ok)prog_confirmar(sent,k);free(sent);return ok?k:-1;
}
int syncprog_remover(const char *key) {
  if(!key||!*key)return 0;prog_remover(key);return flushDeletes(perfis_ativo())>=0;
}
int syncprog_aplicar(int *matched) {
  pthread_mutex_lock(&mailboxMu);ProgRegistro *snapshot=mailbox;int n=mailboxCount,profile=mailboxProfile;
  int changed=mailboxChanged;mailboxChanged=0;mailbox=NULL;mailboxCount=0;pthread_mutex_unlock(&mailboxMu);
  if(matched)*matched=n;
  free(snapshot);
  /* Catalog projection is rebuilt from the repository, never arrival order. */
  if(changed && profile==perfis_ativo())cat_reaplicar_progresso();
  return changed;
}
int syncprog_puxadas(void){pthread_mutex_lock(&mailboxMu);int n=mailboxCount;pthread_mutex_unlock(&mailboxMu);return n;}
void syncprog_esquecer(void){pthread_mutex_lock(&mailboxMu);epoch++;free(mailbox);mailbox=NULL;mailboxCount=mailboxChanged=0;pthread_mutex_unlock(&mailboxMu);}
