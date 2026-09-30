#include "video_mac.h"
#include "subtitle_engine.h"
#include "legenda.h"
#include "assrender.h"
#include "gfx.h"
#include <SDL2/SDL.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
static const char *source;
float gfx_tex_aspect_atual;
void gfx_rect(GfxRect r,GLuint t,GfxModo m,float f,float x,float y,float rad,float cr,float cg,float cb,float ca){(void)r;(void)t;(void)m;(void)f;(void)x;(void)y;(void)rad;(void)cr;(void)cg;(void)cb;(void)ca;}
void gfx_tex_esquecer(GLuint t){(void)t;}
const char *video_url_atual(void){return source;}
void video_escolher_legenda(int i){mac_video_escolher_legenda(i);}
const VideoFaixa *video_legenda(int i){return mac_video_legenda(i);}
int video_legenda_ordinal_mkv(int i){const VideoFaixa *f=mac_video_legenda(i);return f?f->ordinalMkv:-1;}
int video_mkv_sondado(void){return 2;}
void video_sondar_mkv_agora(void){}
static void tick(void){subtitle_engine_tick(mac_video_pos(),0);SDL_Delay(5);}
int main(int argc,char **argv){
  assert(argc==3);source=argv[1];assert(SDL_Init(SDL_INIT_TIMER)==0);assert(mac_video_tocar(source,""));
  Uint64 deadline=SDL_GetTicks64()+5000;
  while(!mac_video_pronto()&&SDL_GetTicks64()<deadline)SDL_Delay(5);
  assert(mac_video_pronto());subtitle_engine_select(0,NULL);
  deadline=SDL_GetTicks64()+8000;
  while(subtitle_engine_native()&&SDL_GetTicks64()<deadline)tick();
  assert(!subtitle_engine_native()&&legenda_estado()==LEG_READY);
  char text[768];double first=-1;
  while(mac_video_pos()<3.0){
    tick();double pos=mac_video_pos();
    if(legenda_texto(pos,0,text,sizeof text)&&strstr(text,"FIRST")){first=pos;break;}
  }
  assert(first>=2.1&&first<2.15);
  printf("PASS %s: first subtitle at media %.3fs; expected 2.100s; error %.1fms\n",argv[2],first,(first-2.1)*1000);
  mac_video_pausar(1);double paused=mac_video_pos();for(int i=0;i<20;i++)tick();
  assert(fabs(mac_video_pos()-paused)<.02);assert(legenda_texto(mac_video_pos(),0,text,sizeof text));
  assert(assrender_quadro_cpu(paused)>0);assert(assrender_quadro_cpu(1)==0);
  mac_video_buscar(1);for(int i=0;i<20;i++)tick();assert(!legenda_texto(mac_video_pos(),0,text,sizeof text));
  mac_video_buscar(6.5);for(int i=0;i<20;i++)tick();assert(legenda_texto(mac_video_pos(),0,text,sizeof text)&&strstr(text,"SECOND"));
  assert(!legenda_texto(6.2,500,text,sizeof text));assert(legenda_texto(6.6,500,text,sizeof text));
  subtitle_engine_select(-1,NULL);assert(legenda_estado()==LEG_OFF);assert(!legenda_texto(6.5,0,text,sizeof text));
  subtitle_engine_select(0,NULL);subtitle_engine_reset();for(int i=0;i<20;i++)tick();assert(legenda_estado()==LEG_OFF);
  mac_video_parar();SDL_Quit();puts("PASS: real media clock, pause, backwards/forwards seek, libass, delay and selection ownership");return 0;
}
