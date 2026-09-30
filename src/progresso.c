#include "progresso.h"
#include "dados.h"
#include "perfis.h"
#include "js.h"
#include "jsw.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

/* New repository: durable revisions and deletion outbox, profile-scoped.
 * Reference: NuvioTVSmart/watchProgressSyncService + NuvioTV/WatchProgress.
 * The former TSV is read only by the one-time data importer below. */
#define STORE "watch-progress-v3.json"
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static ProgRegistro rows[PROG_MAX];
static int count, loaded;
static unsigned version;
static uint64_t sequence;
static long long (*testClock)(void);

long long prog_agora_ms(void) {
  struct timeval t;
  if (testClock) return testClock();
  gettimeofday(&t, NULL);
  return (long long)t.tv_sec * 1000 + t.tv_usec / 1000;
}
void prog_definir_relogio(long long (*fn)(void)) { testClock = fn; }

void prog_content_id(char *dst, unsigned cap, const char *id, int *season, int *episode) {
  const char *end, *previous = NULL;
  size_t length;
  if (!dst || !cap) return;
  dst[0] = 0;
  if (!id || !*id) return;
  length = strlen(id);
  end = strrchr(id, ':');
  if (end) for (const char *p = end; p > id;) if (*--p == ':') { previous = p; break; }
  if (previous) {
    char *a, *b;
    long s = strtol(previous + 1, &a, 10), e = strtol(end + 1, &b, 10);
    if (a == end && a > previous + 1 && b > end + 1 && !*b && s >= 0 && s <= 9999 && e > 0 && e <= 99999) {
      length = (size_t)(previous - id);
      if (season) *season = (int)s;
      if (episode) *episode = (int)e;
    }
  }
  if (length >= cap) length = cap - 1;
  memcpy(dst, id, length); dst[length] = 0;
}
void prog_chave(char *dst, unsigned cap, const char *id, int season, int episode) {
  char parent[64];
  prog_content_id(parent, sizeof parent, id, NULL, NULL);
  if (episode > 0 && season >= 0) snprintf(dst, cap, "%s_s%de%d", parent, season, episode);
  else snprintf(dst, cap, "%s", parent);
}
static int normalize(ProgRegistro *r) {
  char parent[64]; int s = 0, e = 0;
  if (!r || !r->contentId[0] || r->perfil < 1 || !isfinite(r->posSeg) || !isfinite(r->durSeg)) return 0;
  prog_content_id(parent, sizeof parent, r->contentId, &s, &e);
  snprintf(r->contentId, sizeof r->contentId, "%s", parent);
  if (r->episodio <= 0 && e > 0) { r->temporada = s; r->episodio = e; }
  if (r->episodio <= 0) r->temporada = r->episodio = 0;
  if (r->temporada < 0) r->temporada = 0;
  if (r->posSeg < 0) r->posSeg = 0;
  if (r->durSeg < 0) r->durSeg = 0;
  if (!isfinite(r->percentual) || r->percentual < 0 || r->percentual > 100) r->percentual = -1;
  if (!r->tipo[0]) snprintf(r->tipo, sizeof r->tipo, "%s", r->episodio > 0 ? "series" : "movie");
  prog_chave(r->chave, sizeof r->chave, r->contentId, r->temporada, r->episodio);
  return r->contentId[0] != 0;
}
double prog_fracao(const ProgRegistro *r) {
  if (!r) return 0;
  double f = r->durSeg > 0 ? r->posSeg / r->durSeg : r->percentual > 0 ? r->percentual / 100.0 : r->posSeg >= 1 ? .05 : 0;
  return f < 0 ? 0 : f > 1 ? 1 : f;
}
int prog_concluido(const ProgRegistro *r) { return r && !r->removido && prog_fracao(r) >= .90; }
int prog_andamento(const ProgRegistro *r) {
  return r && !r->removido && (r->durSeg <= 0 || r->durSeg >= 60) && prog_fracao(r) >= .02 && !prog_concluido(r);
}
static int find(int profile, const char *key) {
  for (int i = 0; i < count; i++) if (rows[i].perfil == profile && !strcmp(rows[i].chave, key)) return i;
  return -1;
}
static int slot(void) {
  if (count < PROG_MAX) return count++;
  int oldest = -1;
  for (int i = 0; i < count; i++) if (!rows[i].pendente && (oldest < 0 || rows[i].lastWatchedMs < rows[oldest].lastWatchedMs)) oldest = i;
  return oldest;
}
#define STR(dst, src, key) js_texto(src, js_fim(src), key, dst, sizeof dst)
static void readRow(const char *p, ProgRegistro *r) {
  const char *f = js_fim(p);
  memset(r, 0, sizeof *r); r->percentual = -1;
  r->perfil = (int)js_num(p,f,"profile",1);
  STR(r->contentId,p,"contentId"); STR(r->tipo,p,"type"); STR(r->videoId,p,"videoId");
  STR(r->titulo,p,"title"); STR(r->nomeEpisodio,p,"episodeTitle");
  STR(r->poster,p,"poster"); STR(r->backdrop,p,"backdrop"); STR(r->logo,p,"logo");
  r->temporada=(int)js_num(p,f,"season",0); r->episodio=(int)js_num(p,f,"episode",0);
  r->posSeg=js_num(p,f,"positionMs",0)/1000; r->durSeg=js_num(p,f,"durationMs",0)/1000;
  r->lastWatchedMs=(long long)js_num(p,f,"updatedAt",0);
  r->percentual=js_num(p,f,"progressPercent",-1);
  r->pendente=(int)js_num(p,f,"pending",0); r->removido=(int)js_num(p,f,"deleted",0);
  r->revisao=(uint64_t)js_num(p,f,"revision",0); r->remotoMs=(long long)js_num(p,f,"remoteAt",0);
}
static int persist(void) {
  Jsw w; jsw_iniciar(&w); jsw_arr_ini(&w);
  for (int i=0;i<count;i++) {
    const ProgRegistro *r=&rows[i]; jsw_obj_ini(&w);
    jsw_ci(&w,"profile",r->perfil); jsw_cs(&w,"contentId",r->contentId); jsw_cs(&w,"type",r->tipo);
    jsw_cs(&w,"videoId",r->videoId); jsw_ci(&w,"season",r->temporada); jsw_ci(&w,"episode",r->episodio);
    jsw_ci(&w,"positionMs",(long long)llround(r->posSeg*1000)); jsw_ci(&w,"durationMs",(long long)llround(r->durSeg*1000));
    jsw_ci(&w,"updatedAt",r->lastWatchedMs); jsw_ci(&w,"progressPercent",(long long)r->percentual);
    jsw_ci(&w,"pending",r->pendente); jsw_ci(&w,"deleted",r->removido); jsw_ci(&w,"revision",r->revisao);
    jsw_ci(&w,"remoteAt",r->remotoMs); jsw_cs(&w,"title",r->titulo); jsw_cs(&w,"episodeTitle",r->nomeEpisodio);
    jsw_cs(&w,"poster",r->poster); jsw_cs(&w,"backdrop",r->backdrop); jsw_cs(&w,"logo",r->logo); jsw_obj_fim(&w);
  }
  jsw_arr_fim(&w); int ok=!w.erro && dados_gravar(STORE,jsw_texto_final(&w)); jsw_livre(&w); return ok;
}
static void importLegacy(void) {
  char *body=dados_ler("progresso.txt"), *save=NULL;
  if (!body) return;
  int v2=!strncmp(body,"#nvprog2",8);
  for (char *line=strtok_r(body,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
    if (*line=='#') continue;
    ProgRegistro r; memset(&r,0,sizeof r); r.percentual=-1;
    int got;
    if (v2) {
      char *fields[12], *p=line; int n=0;
      fields[n++]=p; while (*p && n<12) { if (*p=='\t') { *p=0; fields[n++]=p+1; } p++; }
      if (n<10) continue;
      r.perfil=atoi(fields[0]); snprintf(r.contentId,sizeof r.contentId,"%s",fields[2]);
      snprintf(r.tipo,sizeof r.tipo,"%s",fields[3]); r.temporada=atoi(fields[4]); r.episodio=atoi(fields[5]);
      r.posSeg=strtod(fields[6],NULL);r.durSeg=strtod(fields[7],NULL);r.lastWatchedMs=strtoll(fields[8],NULL,10);r.pendente=atoi(fields[9]);
      if(n>10 && strcmp(fields[10],"-")) snprintf(r.poster,sizeof r.poster,"%s",fields[10]);
      if(n>11 && strcmp(fields[11],"-")) snprintf(r.backdrop,sizeof r.backdrop,"%s",fields[11]);
    } else {
      r.perfil=perfis_ativo(); r.pendente=1;
      got=sscanf(line,"%63s %lf %lf %d %d",r.contentId,&r.posSeg,&r.durSeg,&r.temporada,&r.episodio);
      if(got<3) continue;
    }
    if(!normalize(&r)) continue;
    int i=find(r.perfil,r.chave); if(i>=0 && rows[i].lastWatchedMs>r.lastWatchedMs) continue;
    if(i<0)i=slot(); if(i<0)break;
    r.revisao=++sequence; r.remotoMs=r.pendente?0:r.lastWatchedMs;rows[i]=r;
  }
  free(body);
}
static void load(void) {
  if(loaded)return; loaded=1;count=0;
  char *body=dados_ler(STORE);
  if(body) {
    const char *p=js_raiz_array(body);
    for(;p && count<PROG_MAX;p=js_prox(js_fim(p))) {
      ProgRegistro r;readRow(p,&r);if(!normalize(&r))continue;
      int i=find(r.perfil,r.chave);if(i<0)i=slot();if(i<0)break;
      if(r.revisao>sequence)sequence=r.revisao;rows[i]=r;
    }
    free(body);
  } else { importLegacy();persist(); }
}
static int newest(const void *a,const void *b) {
  const ProgRegistro *x=a,*y=b;
  if(x->lastWatchedMs!=y->lastWatchedMs)return x->lastWatchedMs<y->lastWatchedMs?1:-1;
  return strcmp(x->chave,y->chave);
}
int prog_ler_perfil(int profile,ProgRegistro *out,int max) {
  int n=0;pthread_mutex_lock(&mu);load();
  for(int i=0;i<count && n<max;i++)if(rows[i].perfil==profile && !rows[i].removido)out[n++]=rows[i];
  pthread_mutex_unlock(&mu);qsort(out,n,sizeof *out,newest);return n;
}
int prog_ler(ProgRegistro *out,int max) { return prog_ler_perfil(perfis_ativo(),out,max); }
int prog_por_chave(const char *key,ProgRegistro *out) {
  if(!key||!*key)return 0;pthread_mutex_lock(&mu);load();int i=find(perfis_ativo(),key);
  int ok=i>=0 && !rows[i].removido;if(ok && out)*out=rows[i];pthread_mutex_unlock(&mu);return ok;
}
static void preserveMetadata(ProgRegistro *dst,const ProgRegistro *old) {
#define KEEP(field) if(!dst->field[0])snprintf(dst->field,sizeof dst->field,"%s",old->field)
  KEEP(titulo);KEEP(nomeEpisodio);KEEP(poster);KEEP(backdrop);KEEP(logo);KEEP(videoId);
#undef KEEP
}
int prog_gravar_registro_local(const ProgRegistro *input) {
  if(!input)return 0;ProgRegistro r=*input;if(r.perfil<1)r.perfil=perfis_ativo();r.removido=0;
  if(!normalize(&r)||r.posSeg<1 || (r.durSeg>0 && r.durSeg<60))return 0;
  pthread_mutex_lock(&mu);load();int i=find(r.perfil,r.chave);if(i<0)i=slot();
  if(i<0){pthread_mutex_unlock(&mu);return 0;}
  if(rows[i].perfil==r.perfil && !strcmp(rows[i].chave,r.chave)) {
    preserveMetadata(&r,&rows[i]);r.remotoMs=rows[i].remotoMs;
    if(r.durSeg<=0)r.durSeg=rows[i].durSeg;
  }
  r.lastWatchedMs=prog_agora_ms();r.revisao=++sequence;r.pendente=1;rows[i]=r;version++;
  int ok=persist();pthread_mutex_unlock(&mu);return ok;
}
int prog_gravar_local(const char *id,int s,int e,double pos,double dur) {
  ProgRegistro r={0};r.percentual=-1;snprintf(r.contentId,sizeof r.contentId,"%s",id?id:"");
  r.temporada=s;r.episodio=e;r.posSeg=pos;r.durSeg=dur;return prog_gravar_registro_local(&r);
}
void prog_enriquecer(const ProgRegistro *metadata) {
  if(!metadata)return;pthread_mutex_lock(&mu);load();int i=find(metadata->perfil>0?metadata->perfil:perfis_ativo(),metadata->chave);
  if(i>=0 && !rows[i].removido) {
    ProgRegistro r=*metadata;preserveMetadata(&r,&rows[i]);
    snprintf(rows[i].titulo,sizeof rows[i].titulo,"%s",r.titulo);snprintf(rows[i].nomeEpisodio,sizeof rows[i].nomeEpisodio,"%s",r.nomeEpisodio);
    snprintf(rows[i].poster,sizeof rows[i].poster,"%s",r.poster);snprintf(rows[i].backdrop,sizeof rows[i].backdrop,"%s",r.backdrop);snprintf(rows[i].logo,sizeof rows[i].logo,"%s",r.logo);persist();
  }pthread_mutex_unlock(&mu);
}
static int mergeRemote(ProgRegistro r,int profile) {
  r.perfil=profile;r.pendente=0;r.removido=0;if(!normalize(&r))return 0;
  int i=find(profile,r.chave);
  if(i>=0) {
    if(rows[i].removido && rows[i].lastWatchedMs>=r.lastWatchedMs)return 0;
    if(rows[i].pendente && rows[i].lastWatchedMs>=r.lastWatchedMs)return 0;
    if(!rows[i].removido && rows[i].lastWatchedMs>r.lastWatchedMs)return 0;
    preserveMetadata(&r,&rows[i]);
    if(rows[i].lastWatchedMs==r.lastWatchedMs && rows[i].posSeg==r.posSeg && rows[i].durSeg==r.durSeg && !rows[i].removido) { rows[i].remotoMs=r.lastWatchedMs;return 0; }
  } else { i=slot();if(i<0)return 0; }
  r.remotoMs=r.lastWatchedMs;r.revisao=++sequence;rows[i]=r;version++;return 1;
}
int prog_aplicar_remoto(const ProgRegistro *input) {
  if(!input)return 0;pthread_mutex_lock(&mu);load();int n=mergeRemote(*input,perfis_ativo());if(n)persist();pthread_mutex_unlock(&mu);return n;
}
int prog_aplicar_snapshot(int profile,const ProgRegistro *remote,int n) {
  if(n<=0)return 0; /* Official recovery policy: empty remote is not a deletion. */
  pthread_mutex_lock(&mu);load();int changed=0;
  for(int i=count-1;i>=0;i--)if(rows[i].perfil==profile && !rows[i].pendente && !rows[i].removido && rows[i].remotoMs>0) {
    int present=0;for(int j=0;j<n;j++)if(!strcmp(rows[i].chave,remote[j].chave)){present=1;break;}
    if(!present){rows[i]=rows[--count];changed++;version++;}
  }
  for(int j=0;j<n;j++)changed+=mergeRemote(remote[j],profile);
  persist();pthread_mutex_unlock(&mu);return changed;
}
int prog_pendentes_perfil(int profile,ProgRegistro *out,int max,int deleted) {
  int n=0;pthread_mutex_lock(&mu);load();for(int i=0;i<count && n<max;i++)
    if(rows[i].perfil==profile && rows[i].pendente && rows[i].removido==deleted)out[n++]=rows[i];
  pthread_mutex_unlock(&mu);return n;
}
int prog_pendentes(ProgRegistro *out,int max) {return prog_pendentes_perfil(perfis_ativo(),out,max,0);}
void prog_confirmar(const ProgRegistro *sent,int n) {
  pthread_mutex_lock(&mu);load();int changed=0;
  for(int j=0;j<n;j++) {int i=find(sent[j].perfil,sent[j].chave);
    if(i>=0 && rows[i].revisao==sent[j].revisao && rows[i].removido==sent[j].removido) {
      rows[i].pendente=0;rows[i].remotoMs=rows[i].lastWatchedMs;changed++;
    }
  }if(changed)persist();pthread_mutex_unlock(&mu);
}
void prog_remover(const char *key) {
  if(!key||!*key)return;pthread_mutex_lock(&mu);load();int i=find(perfis_ativo(),key);
  if(i>=0){rows[i].removido=1;rows[i].pendente=1;rows[i].lastWatchedMs=prog_agora_ms();rows[i].revisao=++sequence;version++;persist();}
  pthread_mutex_unlock(&mu);
}
void prog_marcar_removido(const char *id) {
  char parent[64];prog_content_id(parent,sizeof parent,id,NULL,NULL);int profile=perfis_ativo();
  pthread_mutex_lock(&mu);load();int changed=0;
  for(int i=0;i<count;i++)if(rows[i].perfil==profile && !strcmp(rows[i].contentId,parent)) {
    rows[i].removido=1;rows[i].pendente=1;rows[i].lastWatchedMs=prog_agora_ms();rows[i].revisao=++sequence;changed++;
  }
  if(!changed && parent[0]) {
    int i=slot();if(i>=0){ProgRegistro r={0};r.perfil=profile;r.percentual=-1;
      snprintf(r.contentId,sizeof r.contentId,"%s",parent);normalize(&r);r.removido=1;r.pendente=1;
      r.lastWatchedMs=prog_agora_ms();r.revisao=++sequence;rows[i]=r;changed=1;}
  }
  if(changed){version++;persist();}pthread_mutex_unlock(&mu);
}
int prog_removido_vence(const char *id,long long time) {
  char parent[64];prog_content_id(parent,sizeof parent,id,NULL,NULL);int profile=perfis_ativo(),yes=0;
  pthread_mutex_lock(&mu);load();
  long long deleted=0,newestLocal=0;
  for(int i=0;i<count;i++)if(rows[i].perfil==profile && !strcmp(rows[i].contentId,parent)) {
    if(rows[i].removido && rows[i].lastWatchedMs>deleted)deleted=rows[i].lastWatchedMs;
    if(!rows[i].removido && rows[i].lastWatchedMs>newestLocal)newestLocal=rows[i].lastWatchedMs;
  }
  yes=deleted>0 && deleted>=time && deleted>newestLocal;pthread_mutex_unlock(&mu);return yes;
}
void prog_invalidar(void){pthread_mutex_lock(&mu);loaded=0;count=0;version++;pthread_mutex_unlock(&mu);}
void prog_esquecer_tudo(void){pthread_mutex_lock(&mu);count=0;loaded=1;sequence=0;version++;dados_apagar(STORE);dados_apagar("progresso.txt");pthread_mutex_unlock(&mu);}
unsigned prog_revisao(void){pthread_mutex_lock(&mu);unsigned v=version;pthread_mutex_unlock(&mu);return v;}
