#include "progresso.h"
#include "syncprog.h"
#include "continuar_motor.h"
#include "legenda.h"
#include "media_clock.h"
#include "rede.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
static char *store,*legacy,*rpcReply;static int profile=1,status=200,writeDuringPush;static long long now=1800000000000LL;
int perfis_ativo(void){return profile;}
long long testClock(void){return now;}
char *dados_ler(const char *name){char *s=!strcmp(name,"progresso.txt")?legacy:store;return s?strdup(s):NULL;}
int dados_gravar(const char *name,const char *body){assert(!strcmp(name,"watch-progress-v3.json"));free(store);store=strdup(body);return 1;}
int dados_apagar(const char *name){if(!strcmp(name,"progresso.txt")){free(legacy);legacy=NULL;}else{free(store);store=NULL;}return 1;}
const char *dados_cliente_id(void){return "new-engine-test";}
void cat_reaplicar_progresso(void){}
static char *lastPush;
char *sessao_rpc(const char *method,const char *body,int *code){
  *code=status;free(lastPush);lastPush=strdup(body);
  if(!strcmp(method,"sync_push_watch_progress")&&writeDuringPush){writeDuringPush=0;now++;prog_gravar_local("tmdb:299939",1,1,1500,0);}
  return strdup(rpcReply?rpcReply:"{}");
}
static void reset(void){prog_esquecer_tudo();syncprog_esquecer();profile=1;status=200;free(rpcReply);rpcReply=NULL;}
static ProgRegistro row(const char *id,int season,int episode,double pos,double duration,long long time){ProgRegistro r={0};r.perfil=profile;r.percentual=-1;snprintf(r.contentId,sizeof r.contentId,"%s",id);snprintf(r.tipo,sizeof r.tipo,"%s",episode?"series":"movie");r.temporada=season;r.episodio=episode;r.posSeg=pos;r.durSeg=duration;r.lastWatchedMs=time;prog_chave(r.chave,sizeof r.chave,id,season,episode);return r;}
static void watchTests(void){
  reset();legacy=strdup("#nvprog2\n1\ttmdb:299939_s1e1\ttmdb:299939\tseries\t1\t1\t1482\t0\t1800000000000\t0\tlizzie.jpg\tlizzie-bg.jpg\n");
  prog_invalidar();ProgRegistro imported;assert(prog_por_chave("tmdb:299939_s1e1",&imported)&&imported.posSeg==1482&&!strcmp(imported.poster,"lizzie.jpg"));
  reset();ProgRegistro r=row("tmdb:299939",1,1,1482,0,now);r.percentual=0;
  snprintf(r.titulo,sizeof r.titulo,"Monstro: A História de Lizzie Borden");snprintf(r.poster,sizeof r.poster,"lizzie.jpg");
  assert(prog_gravar_registro_local(&r));assert(prog_por_chave(r.chave,&r));assert(r.posSeg==1482&&r.durSeg==0&&prog_andamento(&r));
  prog_invalidar();assert(prog_por_chave("tmdb:299939_s1e1",&r));assert(!strcmp(r.poster,"lizzie.jpg"));
  ProgRegistro sent=r;now++;assert(prog_gravar_local(r.contentId,1,1,1500,0));prog_confirmar(&sent,1);assert(prog_pendentes(&r,1)==1&&r.posSeg==1500);
  ProgRegistro stale=row(r.contentId,1,1,100,3400,now-1000);assert(!prog_aplicar_remoto(&stale));assert(prog_por_chave(stale.chave,&r)&&r.posSeg==1500);
  ProgRegistro fresh=row(r.contentId,1,1,2000,3400,now+1000);assert(prog_aplicar_remoto(&fresh));assert(prog_por_chave(fresh.chave,&r)&&r.posSeg==2000&&!r.pendente);
  profile=2;assert(!prog_por_chave(r.chave,&r));profile=1;
  now+=2000;prog_marcar_removido("tmdb:299939");prog_invalidar();assert(!prog_por_chave("tmdb:299939_s1e1",&r));assert(prog_removido_vence("tmdb:299939",fresh.lastWatchedMs));assert(!prog_aplicar_remoto(&fresh));
  status=503;assert(syncprog_empurrar()==-1);ProgRegistro deletes[4];assert(prog_pendentes_perfil(1,deletes,4,1)==1);
  status=200;assert(syncprog_empurrar()==0);assert(prog_pendentes_perfil(1,deletes,4,1)==0);
  now++;assert(prog_gravar_local("tmdb:299939",1,1,30,3400));assert(!prog_removido_vence("tmdb:299939",fresh.lastWatchedMs));
  prog_marcar_removido("tt9999999");prog_invalidar();assert(prog_removido_vence("tt9999999",1));
  puts("ok progress: unknown duration, artwork, persistence, revisions, profiles, stale remote, durable deletion");
  reset();r=row("tt1",1,1,100,2000,now);assert(prog_aplicar_snapshot(1,&r,1)==1);assert(prog_aplicar_snapshot(1,NULL,0)==0);assert(prog_por_chave(r.chave,&r));
  fresh=row("tt2",1,1,100,2000,now);prog_aplicar_snapshot(1,&fresh,1);assert(!prog_por_chave("tt1_s1e1",&r));assert(prog_por_chave("tt2_s1e1",&r));
  puts("ok snapshots: empty recovery and removal of absent synchronized entries");
}
static void syncTests(void){
  reset();rpcReply=strdup("[{\"content_id\":\"tmdb:299939\",\"content_type\":\"series\",\"video_id\":\"tt13207736:4:1\",\"season\":1,\"episode\":1,\"position_ms\":1482000,\"duration_ms\":0,\"last_watched\":1800000000000,\"title\":\"Lizzie\",\"poster\":\"lizzie.jpg\"},{\"content_id\":\"ttDexter\",\"position\":19480,\"duration\":3154000,\"last_watched\":1800000000000}]");
  assert(syncprog_puxar()==2);profile=2;syncprog_aplicar(NULL);ProgRegistro r;assert(!prog_por_chave("tmdb:299939_s1e1",&r));profile=1;assert(prog_por_chave("tmdb:299939_s1e1",&r)&&r.posSeg==1482&&r.temporada==1);assert(!strcmp(r.poster,"lizzie.jpg"));assert(prog_por_chave("ttDexter",&r)&&fabs(r.posSeg-19.48)<.001);
  now+=2000;prog_gravar_local("tmdb:299939",1,1,1490,0);writeDuringPush=1;assert(syncprog_empurrar()==1);assert(prog_pendentes(&r,1)==1&&r.posSeg==1500);assert(strstr(lastPush,"\"p_profile_id\":1"));assert(strstr(lastPush,"\"duration\":0"));
  syncprog_puxar();syncprog_esquecer();assert(syncprog_aplicar(NULL)==0);
  /* Startup reconciles the remote winner before uploading old pending data,
   * even when the UI has not yet consumed the pull notification. */
  reset();now=1800000000000LL;assert(prog_gravar_local("ttRace",1,1,50,3000));
  rpcReply=strdup("[{\"content_id\":\"ttRace\",\"content_type\":\"series\",\"season\":1,\"episode\":1,\"position_ms\":2000000,\"duration_ms\":3000000,\"last_watched\":1800000005000}]");
  assert(syncprog_puxar()==1);assert(syncprog_empurrar()==0);
  assert(prog_por_chave("ttRace_s1e1",&r)&&r.posSeg==2000&&!r.pendente);
  puts("ok RPC: canonical content identity, legacy pair units, captured profile, save during acknowledgement, logout invalidation");
}
static void continueTests(void){
  CwEstado v[6]={0};v[0].progresso=row("tmdb:299939",1,1,1482,0,10);v[1].progresso=row("tmdb:299939",1,2,3000,3100,20);v[2].progresso=row("tmdb:225634",1,1,500,3200,15);
  v[3].progresso=row("ttMovie",0,0,100,1000,30);v[3].fonte=CW_TRAKT;v[4].progresso=row("ttMovie",0,0,200,1000,30);v[5].progresso=row("ttEnded",0,0,999,1000,40);
  int selected[6],n=cw_selecionar(v,6,selected,6);assert(n==3&&selected[0]==4&&selected[1]==1&&selected[2]==2);assert(!cw_estado_visivel(&v[1]));assert(cw_estado_visivel(&v[0]));
  v[0].fonte=CW_SIMKL;v[0].progresso.durSeg=1800;assert(!cw_estado_visivel(&v[0]));
  puts("ok continuing: newest episode suppresses stale paused rows; independent anthology IDs; account wins ties; Simkl completion");
}
static pthread_mutex_t downloadMu=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t downloadCond=PTHREAD_COND_INITIALIZER;static int entered,released,finished;
char *rede_baixar_bin_medido_controle(const char *url,int timeout,const char *const *headers,const RedeControle *control,long *n,RedeMedida *measure){
  (void)timeout;(void)headers;(void)measure;assert(control->max_bytes>0);assert(!strcmp(url,"old"));
  pthread_mutex_lock(&downloadMu);entered=1;pthread_cond_broadcast(&downloadCond);while(!released)pthread_cond_wait(&downloadCond,&downloadMu);pthread_mutex_unlock(&downloadMu);
  char *b=strdup("1\n00:00:01,000 --> 00:00:04,000\nOLD\n");*n=(long)strlen(b);
  pthread_mutex_lock(&downloadMu);finished=1;pthread_cond_broadcast(&downloadCond);pthread_mutex_unlock(&downloadMu);return b;
}
static void subtitleTests(void){
  LegendaCue *v=NULL;int n=legenda_extrair("WEBVTT\r\n\r\nNOTE ignore\r\nx\r\n\r\nsecond\r\n00:00:03.000 --> 00:00:04.000 align:center\r\n<b>Olá &amp; mundo</b>\r\n\r\nfirst\r\n00:01.5 --> 00:02.25\r\nPrimeira\r\n",&v);
  assert(n==2&&v[0].inicio==1.5&&v[0].fim==2.25&&!strcmp(v[1].texto,"Olá & mundo"));free(v);
  legenda_definir_corpo("1\n00:00:03,000 --> 00:00:04,000\nsecond\n\n2\n00:00:01,000 --> 00:00:03,000\nfirst\n");char text[768];
  assert(!legenda_texto(.999,0,text,sizeof text));assert(legenda_texto(1,0,text,sizeof text)&&!strcmp(text,"first"));assert(legenda_texto(3,0,text,sizeof text)&&!strcmp(text,"second"));assert(!legenda_texto(4,0,text,sizeof text));assert(legenda_texto(1.5,500,text,sizeof text)&&!strcmp(text,"first"));assert(!legenda_texto(1,500,text,sizeof text));
  assert(legenda_texto(3.5,0,text,sizeof text));assert(legenda_texto(1.5,0,text,sizeof text)&&!strcmp(text,"first"));
  unsigned stale=legenda_geracao();legenda_desligar();assert(!legenda_atualizar_corpo_se("",stale));
  const unsigned char utf16[]={0xff,0xfe,'O',0,0xe1,0,'!',0};char *decoded=legenda_decodificar(utf16,sizeof utf16);assert(decoded&&!strcmp(decoded,"Oá!"));free(decoded);
  const unsigned char cp1252[]={'N',0xe3,'o'};decoded=legenda_decodificar(cp1252,sizeof cp1252);assert(decoded&&!strcmp(decoded,"Não"));free(decoded);
  legenda_carregar("old");pthread_mutex_lock(&downloadMu);while(!entered)pthread_cond_wait(&downloadCond,&downloadMu);pthread_mutex_unlock(&downloadMu);
  legenda_definir_corpo("1\n00:00:01,000 --> 00:00:04,000\nNEW\n");pthread_mutex_lock(&downloadMu);released=1;pthread_cond_broadcast(&downloadCond);while(!finished)pthread_cond_wait(&downloadCond,&downloadMu);pthread_mutex_unlock(&downloadMu);
  for(int i=0;i<50;i++){assert(legenda_texto(2,0,text,sizeof text)&&!strcmp(text,"NEW"));usleep(1000);}
  puts("ok subtitles: SRT/VTT ordering, boundaries, positive delay, backwards seek, encoding, obsolete download discarded");
}
static void clockTests(void){media_clock_sample(10,100,1);assert(fabs(media_clock_read(100.1)-10.1)<.0001);media_clock_running(100.1,0);assert(fabs(media_clock_read(110)-10.1)<.0001);media_clock_sample(500,200,0);assert(media_clock_read(200.2)==500);media_clock_sample(500,201,1);assert(media_clock_read(202)==500.25);media_clock_sample(1,203,1);assert(media_clock_read(203)==1);puts("ok media clock: pause, seek discontinuity, bounded stall, backwards jump without drift");}
int main(void){prog_definir_relogio(testClock);watchTests();syncTests();continueTests();subtitleTests();clockTests();free(store);free(legacy);free(rpcReply);free(lastPush);puts("ALL REBUILT ENGINE TESTS PASSED");return 0;}
