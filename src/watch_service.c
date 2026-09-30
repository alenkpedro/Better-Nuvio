#include "watch_service.h"
#include "continuar_motor.h"
#include "progresso.h"
#include "addons.h"
#include "perfis.h"
#include "ajustes.h"
#include "trakt.h"
#include "simkl.h"
#include "cwordem.h"
#include "seriealias.h"
#include "vistoep.h"
#include "rede.h"
#include "js.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static pthread_mutex_t snapshotMu=PTHREAD_MUTEX_INITIALIZER;
static CatItem *snapshot;
static int snapshotCount,snapshotProfile,snapshotSource,running,readyProfile,readySource;
static unsigned requested,revision,applied,readyAddons,lastRepo,lastAddons;
static int lastProfile,lastSource;
static unsigned lastCatalog;
static int cw_cached_identity(int profile,const char *id,CatItem *out) {
  int found=0;pthread_mutex_lock(&snapshotMu);
  if(snapshotProfile==profile)for(int i=0;i<snapshotCount;i++)
    if(!strcmp(snapshot[i].imdb,id)){*out=snapshot[i];found=1;break;}
  pthread_mutex_unlock(&snapshotMu);return found;
}
static void cw_stage(const CatItem *cards,int n,int profile,int source) {
  if(profile!=perfis_ativo() || source!=ajustes_cw_fonte())return;
  CatItem *copy=n>0?malloc((size_t)n*sizeof *copy):NULL;
  if(n>0 && !copy)return;
  if(n>0)memcpy(copy,cards,(size_t)n*sizeof *copy);
  pthread_mutex_lock(&snapshotMu);
  free(snapshot);snapshot=copy;snapshotCount=n;snapshotProfile=profile;snapshotSource=source;revision++;
  pthread_mutex_unlock(&snapshotMu);
}
/* Metadata has no authority over progress, episode coordinates or identity. */
static int cw_metadata(const char *m,const char *type,CatItem *item) {
  const char *end=js_fim(m);memset(item,0,sizeof *item);
  if(!js_texto_raiz_em(m,end,"name",item->titulo,sizeof item->titulo))return 0;
  js_texto_raiz_em(m,end,"id",item->imdb,sizeof item->imdb);
  snprintf(item->tipo,sizeof item->tipo,"%s",type);
  js_texto_raiz_em(m,end,"poster",item->poster,sizeof item->poster);
  js_texto_raiz_em(m,end,"background",item->backdrop,sizeof item->backdrop);
  js_texto_raiz_em(m,end,"logo",item->logo,sizeof item->logo);
  js_texto_raiz_em(m,end,"description",item->sinopse,sizeof item->sinopse);
  js_texto_raiz_em(m,end,"releaseInfo",item->meta,sizeof item->meta);
  item->nota=(int)(js_num(m,end,"imdbRating",0)*10+.5);
  item->tmdb=(long)js_num(m,end,"moviedb_id",0);
  if(!strncmp(item->imdb,"tmdb:",5))item->tmdb=strtol(item->imdb+5,NULL,10);
  if(!strcmp(item->logo,item->poster))item->logo[0]=0;
  snprintf(item->backdropCatalogo,sizeof item->backdropCatalogo,"%s",item->backdrop);
  seriealias_aplicar(item);return 1;
}
#define CW_CANDIDATES 64

static int cw_capacity(int profile,int source) {
  int n=(source==AJ_CWF_CONTA || source==AJ_CWF_AMBAS)?prog_ler_perfil(profile,NULL,PROG_MAX):0;
  if(source==AJ_CWF_TRAKT || source==AJ_CWF_AMBAS)n+=CW_CANDIDATES;
  if(source==AJ_CWF_SIMKL || source==AJ_CWF_AMBAS)n+=CW_CANDIDATES;
  return n>0?n:1;
}

typedef struct { CwEstado state; CatItem item; int ready, profile, cachedFollowing; } CwCard;

static void cw_project(CwCard *card) {
  const ProgRegistro *r=&card->state.progresso;CatItem *it=&card->item;
  memset(it,0,sizeof *it);
  snprintf(it->imdb,sizeof it->imdb,"%s",r->contentId);snprintf(it->tipo,sizeof it->tipo,"%s",r->tipo);
  snprintf(it->titulo,sizeof it->titulo,"%s",r->titulo[0]?r->titulo:r->contentId);
  snprintf(it->nomeEpisodio,sizeof it->nomeEpisodio,"%s",r->nomeEpisodio);
  snprintf(it->poster,sizeof it->poster,"%s",r->poster);snprintf(it->backdrop,sizeof it->backdrop,"%s",r->backdrop);snprintf(it->logo,sizeof it->logo,"%s",r->logo);
  it->temporada=r->temporada;it->episodio=r->episodio;it->posicaoSeg=r->posSeg;it->duracaoSeg=r->durSeg;
  it->progresso=(int)(prog_fracao(r)*100);it->retomadoMs=r->lastWatchedMs;
  it->continuarSeguinte=card->state.seguinte;
  it->continuarGerenciado=1;
  it->restanteMin=r->durSeg>r->posSeg?(int)((r->durSeg-r->posSeg)/60+.5):0;
}
static void cw_apply_metadata(CatItem *item,const CatItem *meta,int preserve) {
  CatItem old=*item;*item=*meta;
  snprintf(item->imdb,sizeof item->imdb,"%s",old.imdb);snprintf(item->tipo,sizeof item->tipo,"%s",old.tipo);
  item->temporada=old.temporada;item->episodio=old.episodio;item->progresso=old.progresso;
  item->retomadoMs=old.retomadoMs;item->posicaoSeg=old.posicaoSeg;item->duracaoSeg=old.duracaoSeg;item->restanteMin=old.restanteMin;item->continuarSeguinte=old.continuarSeguinte;
  item->continuarGerenciado=1;
  if((preserve&&old.poster[0])||!item->poster[0])snprintf(item->poster,sizeof item->poster,"%s",old.poster);
  if((preserve&&old.backdrop[0])||!item->backdrop[0])snprintf(item->backdrop,sizeof item->backdrop,"%s",old.backdrop);
  if((preserve&&old.logo[0])||!item->logo[0])snprintf(item->logo,sizeof item->logo,"%s",old.logo);
  if(preserve&&old.titulo[0]&&strcmp(old.titulo,old.imdb))snprintf(item->titulo,sizeof item->titulo,"%s",old.titulo);
  snprintf(item->nomeEpisodio,sizeof item->nomeEpisodio,"%s",old.nomeEpisodio);
}
static void cw_cached(CwCard *card) {
  CatItem cached;
  if(cw_cached_identity(card->profile,card->state.progresso.contentId,&cached)) {
    cw_apply_metadata(&card->item,&cached,card->state.fonte==CW_CONTA);
    const ProgRegistro *r=&card->state.progresso;
    if(card->state.fonte==CW_CONTA && !card->state.seguinte && prog_concluido(r) &&
       cached.continuarSeguinte && cached.retomadoMs==r->lastWatchedMs) {
      card->item=cached;card->cachedFollowing=1;
    }
  }
}
static int cw_next_episode(CwCard *card,const char *meta) {
  CatItem *item=&card->item;int seed=prog_concluido(&card->state.progresso) && !card->state.seguinte;
  int bestS=9999,bestE=99999;char bestName[120]="",bestReleased[64]="";
  const char *p=js_array(meta,js_fim(meta),"videos");
  for(;p;p=js_prox(js_fim(p))) {
    const char *f=js_fim(p);int s=(int)js_num(p,f,"season",-1),e=(int)js_num(p,f,"episode",-1);
    if(s<0||e<1)continue;
    if(!seed) {
      if(s==item->temporada && e==item->episodio) {
        js_texto(p,f,"title",item->nomeEpisodio,sizeof item->nomeEpisodio);
        if(!item->nomeEpisodio[0])js_texto(p,f,"name",item->nomeEpisodio,sizeof item->nomeEpisodio);
        char released[64]="";js_texto(p,f,"released",released,sizeof released);if(released[0])cwo_marcar_estreia(item->imdb,js_ms_iso(released));
      }continue;
    }
    if(s<card->state.progresso.temporada || (s==card->state.progresso.temporada && e<=card->state.progresso.episodio) || s==0)continue;
    if(vistoep_estado(item->imdb,s,e)==1)continue;
    if(s<bestS || (s==bestS && e<bestE)) {
      bestS=s;bestE=e;js_texto(p,f,"title",bestName,sizeof bestName);if(!bestName[0])js_texto(p,f,"name",bestName,sizeof bestName);
      js_texto(p,f,"released",bestReleased,sizeof bestReleased);
    }
  }
  if(!seed)return 1;if(bestS==9999)return 0;
  item->temporada=bestS;item->episodio=bestE;item->posicaoSeg=0;item->duracaoSeg=0;item->restanteMin=0;item->progresso=0;item->continuarSeguinte=1;
  snprintf(item->nomeEpisodio,sizeof item->nomeEpisodio,"%s",bestName);
  if(bestReleased[0])cwo_marcar_estreia(item->imdb,js_ms_iso(bestReleased));
  return 1;
}
static void cw_enrich(CwCard *card) {
  cw_cached(card);
  char encoded[256],url[1024];size_t used=0;
  for(const unsigned char *p=(const unsigned char*)card->state.progresso.contentId;*p && used+4<sizeof encoded;p++) {
    if(isalnum(*p)||*p=='-'||*p=='_'||*p=='.')encoded[used++]=(char)*p;
    else {snprintf(encoded+used,4,"%%%02X",*p);used+=3;}
  }encoded[used]=0;
  int found=0;
  for(int i=0;i<addons_n() && !found;i++) {
    if(perfis_ativo()!=card->profile || addons_perfil_da_lista()!=card->profile)return;
    if(!addons_tem_meta(i))continue;
    snprintf(url,sizeof url,"%s/meta/%s/%s.json",addons_base(i),card->item.tipo,encoded);
    char *body=rede_baixar(url,4);if(!body)continue;
    const char *key=strstr(body,"\"meta\"");
    const char *m=key?strchr(key,'{'):NULL;
    if(m) {
      char identity[128]="";js_texto_raiz_em(m,js_fim(m),"id",identity,sizeof identity);
      if(!identity[0] || !strcmp(identity,card->state.progresso.contentId)) {
        CatItem *metadata=malloc(sizeof *metadata);
        if(metadata && cw_metadata(m,card->item.tipo,metadata)) {cw_apply_metadata(&card->item,metadata,card->state.fonte==CW_CONTA&&!identity[0]);found=1;card->ready=cw_next_episode(card,m);}
        free(metadata);
      }
    }
    free(body);
  }
  if(!found && prog_concluido(&card->state.progresso) && !card->state.seguinte && !card->cachedFollowing)card->ready=0;
  if(card->ready && card->state.fonte==CW_CONTA && card->profile==perfis_ativo()) {
    ProgRegistro metadata=card->state.progresso;
    snprintf(metadata.titulo,sizeof metadata.titulo,"%s",card->item.titulo);snprintf(metadata.poster,sizeof metadata.poster,"%s",card->item.poster);
    snprintf(metadata.backdrop,sizeof metadata.backdrop,"%s",card->item.backdrop);snprintf(metadata.logo,sizeof metadata.logo,"%s",card->item.logo);
    if(!card->item.continuarSeguinte)snprintf(metadata.nomeEpisodio,sizeof metadata.nomeEpisodio,"%s",card->item.nomeEpisodio);
    prog_enriquecer(&metadata);
  }
}
typedef struct { CwCard *cards; int n,next; pthread_mutex_t mu; } CwEnrichment;
static void *cw_enrichment_worker(void *arg) {
  CwEnrichment *job=arg;
  for(;;) {pthread_mutex_lock(&job->mu);int i=job->next++;pthread_mutex_unlock(&job->mu);if(i>=job->n)break;cw_enrich(&job->cards[i]);}
  return NULL;
}
int cw_service_build(CatItem *out,int max,unsigned char *localized) {
  if(!out || max<1)return 0;
  int profile=perfis_ativo(),source=ajustes_cw_fonte();unsigned revision=prog_revisao();
  ProgRegistro *records=calloc(PROG_MAX,sizeof *records);CwEstado *states=calloc(PROG_MAX+128,sizeof *states);
  CatItem *provider=calloc(CW_CANDIDATES,sizeof *provider);
  if(!records||!states||!provider){free(records);free(states);free(provider);return 0;}
  int n=0;
  if(source==AJ_CWF_AMBAS || source==AJ_CWF_CONTA) {
    int nr=prog_ler_perfil(profile,records,PROG_MAX);
    for(int i=0;i<nr;i++) {states[n].progresso=records[i];states[n++].fonte=CW_CONTA;}
  }
  for(int which=1;which<=2;which++) {
    if(which==1 && source!=AJ_CWF_AMBAS && source!=AJ_CWF_TRAKT)continue;
    if(which==2 && source!=AJ_CWF_AMBAS && source!=AJ_CWF_SIMKL)continue;
    int nr=which==1?trakt_continuar(provider,CW_CANDIDATES):simkl_continuar(provider,CW_CANDIDATES);
    for(int i=0;i<nr;i++) {
      CatItem *it=&provider[i];CwEstado *state=&states[n++];ProgRegistro *r=&state->progresso;r->perfil=profile;r->percentual=it->progresso;
      int s=it->temporada,e=it->episodio;prog_content_id(r->contentId,sizeof r->contentId,it->imdb,&s,&e);
      r->temporada=s;r->episodio=e;r->posSeg=it->posicaoSeg;r->durSeg=it->duracaoSeg;r->lastWatchedMs=it->retomadoMs;
      snprintf(r->tipo,sizeof r->tipo,"%s",it->tipo);snprintf(r->titulo,sizeof r->titulo,"%s",it->titulo);
      snprintf(r->poster,sizeof r->poster,"%s",it->poster);snprintf(r->backdrop,sizeof r->backdrop,"%s",it->backdrop);snprintf(r->logo,sizeof r->logo,"%s",it->logo);
      snprintf(r->nomeEpisodio,sizeof r->nomeEpisodio,"%s",it->nomeEpisodio);prog_chave(r->chave,sizeof r->chave,r->contentId,s,e);
      state->fonte=which;state->seguinte=which==1?trakt_e_a_seguir(it->imdb):simkl_e_a_seguir(it->imdb);
      if(state->seguinte)cwo_marcar_estreia(r->contentId,cwo_estreia(it->imdb));
    }
  }
  int *selected=calloc(n?n:1,sizeof *selected);
  if(!selected){free(records);free(states);free(provider);return 0;}
  int nc=cw_selecionar(states,n,selected,n),k=0;
  CwCard *cards=calloc(nc?nc:1,sizeof *cards);
  if(!cards){free(selected);free(records);free(states);free(provider);return 0;}
  for(int i=0;i<nc;i++) {
    cards[i].state=states[selected[i]];cards[i].profile=profile;cards[i].ready=1;cw_project(&cards[i]);cw_cached(&cards[i]);
    if(!localized && k<max && (cw_estado_visivel(&cards[i].state) || cards[i].cachedFollowing) && !prog_removido_vence(cards[i].item.imdb,cards[i].item.retomadoMs))out[k++]=cards[i].item;
  }
  if(!localized && profile==perfis_ativo() && revision==prog_revisao() && source==ajustes_cw_fonte())cw_stage(out,k,profile,source);
  free(selected);free(records);free(states);free(provider);
  CwEnrichment job={.cards=cards,.n=nc,.mu=PTHREAD_MUTEX_INITIALIZER};pthread_t threads[3];int nt=0;
  for(int i=0;i<3 && i<nc;i++)if(!pthread_create(&threads[nt],NULL,cw_enrichment_worker,&job))nt++;
  cw_enrichment_worker(&job);for(int i=0;i<nt;i++)pthread_join(threads[i],NULL);pthread_mutex_destroy(&job.mu);
  CwoItem *ordering=calloc(nc?nc:1,sizeof *ordering);
  int *permutation=calloc(nc?nc:1,sizeof *permutation);
  const char **future=calloc(nc?nc:1,sizeof *future);int nf=0,valid=0;
  if(!ordering||!permutation||!future){free(ordering);free(permutation);free(future);free(cards);return 0;}
  long long now=prog_agora_ms();
  for(int i=0;i<nc;i++) {
    if(!cards[i].ready || prog_removido_vence(cards[i].item.imdb,cards[i].item.retomadoMs))continue;
    CwoItem order={cards[i].item.continuarSeguinte,cwo_estreia(cards[i].item.imdb)};
    if(!ajustes_cw_mostrar_nao_exibidos() && cwo_futuro(&order,now))continue;
    if(valid!=i)cards[valid]=cards[i];ordering[valid++]=order;
  }
  int principal=cwo_ordenar(ordering,valid,ajustes_cw_ordem(),now,permutation),mp,mf;
  cwo_corte(principal,valid-principal,max,&mp,&mf);k=0;
  for(int i=0;i<mp+mf;i++) {
    int at=i<mp?permutation[i]:permutation[principal+i-mp];out[k]=cards[at].item;if(localized)localized[k]=1;
    if(cwo_futuro(&ordering[at],now))future[nf++]=out[k].imdb;k++;
  }
  cwo_publicar_futuros(future,nf);
  free(ordering);free(permutation);free(future);free(cards);
  if(profile!=perfis_ativo() || revision!=prog_revisao() || source!=ajustes_cw_fonte())return -1;
  printf("[watch service] profile %d: %d states, %d identities, %d cards\n",profile,n,nc,k);return k;
}

static void *refreshWorker(void *unused) {
  (void)unused;
  for(;;) {
    pthread_mutex_lock(&snapshotMu);unsigned captured=requested;pthread_mutex_unlock(&snapshotMu);
    int profile=perfis_ativo(),source=ajustes_cw_fonte();unsigned addonsVersion=addons_versao(),repoVersion=prog_revisao();
    int capacity=cw_capacity(profile,source);
    CatItem *cards=calloc((size_t)capacity,sizeof *cards);
    if(!cards){pthread_mutex_lock(&snapshotMu);running=0;pthread_mutex_unlock(&snapshotMu);return NULL;}
    int n=cw_service_build(cards,capacity,NULL);
    pthread_mutex_lock(&snapshotMu);
    int current=captured==requested && repoVersion==prog_revisao() && profile==perfis_ativo() && addonsVersion==addons_versao() && source==ajustes_cw_fonte();
    if(n>=0 && current) {
      free(snapshot);snapshot=cards;cards=NULL;snapshotCount=n;snapshotProfile=profile;snapshotSource=source;
      readyProfile=profile;readySource=source;readyAddons=addonsVersion;revision++;
    }
    free(cards);
    if(!current || n<0){pthread_mutex_unlock(&snapshotMu);continue;}
    running=0;pthread_mutex_unlock(&snapshotMu);break;
  }
  return NULL;
}
void cw_service_refresh(void) {
  pthread_mutex_lock(&snapshotMu);requested++;
  if(!running){pthread_t thread;running=1;if(pthread_create(&thread,NULL,refreshWorker,NULL))running=0;else pthread_detach(thread);}
  pthread_mutex_unlock(&snapshotMu);
}
int cw_service_ready(int profile,unsigned version) {
  pthread_mutex_lock(&snapshotMu);int ready=readyProfile==profile && readySource==ajustes_cw_fonte() && readyAddons==version;pthread_mutex_unlock(&snapshotMu);return ready;
}
int cw_service_snapshot(CatItem *out,int max) {
  pthread_mutex_lock(&snapshotMu);int n=snapshotProfile==perfis_ativo() && snapshotSource==ajustes_cw_fonte()?snapshotCount:0;
  if(out){if(n>max)n=max;if(n>0)memcpy(out,snapshot,(size_t)n*sizeof *out);}pthread_mutex_unlock(&snapshotMu);return n;
}
void cw_service_pump(int canPublish) {
  int profile=perfis_ativo(),source=ajustes_cw_fonte();unsigned repo=prog_revisao(),addonsVersion=addons_versao();
  if(profile!=lastProfile || source!=lastSource || repo!=lastRepo || addonsVersion!=lastAddons) {
    lastProfile=profile;lastSource=source;lastRepo=repo;lastAddons=addonsVersion;cw_service_refresh();
  }
  if(!canPublish || perfis_precisa_escolher())return;
  pthread_mutex_lock(&snapshotMu);
  unsigned ver=revision;int current=snapshotProfile==profile && snapshotSource==source;
  int needs=current && (applied!=ver || lastCatalog!=cat_revisao());
  CatItem *cards=needs?calloc(snapshotCount?snapshotCount:1,sizeof *cards):NULL;
  int n=snapshotCount;if(cards && n>0)memcpy(cards,snapshot,(size_t)n*sizeof *cards);
  pthread_mutex_unlock(&snapshotMu);
  if(cards) {cat_trocar_continuar(cards,n);applied=ver;lastCatalog=cat_revisao();free(cards);}
}
int cw_service_remove(const char *id,int season,int episode) {
  char key[96];prog_chave(key,sizeof key,id,season,episode);prog_remover(key);prog_marcar_removido(id);
  pthread_mutex_lock(&snapshotMu);
  for(int i=0;i<snapshotCount;) {
    if(!strcmp(snapshot[i].imdb,id)){memmove(snapshot+i,snapshot+i+1,(size_t)(--snapshotCount-i)*sizeof *snapshot);revision++;}
    else i++;
  }
  pthread_mutex_unlock(&snapshotMu);int removed=cat_tirar_continuar(id);lastCatalog=cat_revisao();cw_service_refresh();return removed;
}
void cw_service_reset(void) {
  pthread_mutex_lock(&snapshotMu);requested++;free(snapshot);snapshot=NULL;snapshotCount=0;snapshotProfile=0;readyProfile=0;revision++;pthread_mutex_unlock(&snapshotMu);
  lastProfile=lastRepo=lastAddons=lastCatalog=0;
}
