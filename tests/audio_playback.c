#include "video_mac.h"
#include "gfx.h"
#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
static atomic_int observedHz;
float gfx_tex_aspect_atual;
void gfx_rect(GfxRect r,GLuint t,GfxModo m,float f,float x,float y,float rad,float cr,float cg,float cb,float ca){(void)r;(void)t;(void)m;(void)f;(void)x;(void)y;(void)rad;(void)cr;(void)cg;(void)cb;(void)ca;}
void gfx_tex_esquecer(GLuint t){(void)t;}
/* Observe decoded PCM, not just the selected index reported by the player. */
int audioCapturar(SDL_AudioDeviceID dev,const void *bytes,Uint32 len) {
  (void)dev;const int16_t *pcm=bytes;int n=(int)len/4,crossings=0;
  for(int i=1;i<n;i++)if(pcm[2*(i-1)]<=0 && pcm[2*i]>0)crossings++;
  if(n>200)atomic_store(&observedHz,(int)lround(crossings*48000.0/n));return 0;
}
static void waitTrack(int selected,int frequency) {
  Uint64 until=SDL_GetTicks64()+3000;
  while(SDL_GetTicks64()<until){int hz=atomic_load(&observedHz);if(mac_video_audio_atual()==selected && abs(hz-frequency)<50)return;SDL_Delay(5);}
  fprintf(stderr,"expected audio %d/%dHz, observed %d/%dHz\n",selected,frequency,mac_video_audio_atual(),atomic_load(&observedHz));assert(0);
}
int main(int argc,char **argv) {
  assert(argc==2);assert(SDL_Init(SDL_INIT_TIMER)==0);mac_video_preferir_audio("pt-br");assert(mac_video_tocar(argv[1],""));
  waitTrack(1,880);assert(mac_video_n_audio()==2);
  assert(!strcmp(mac_video_audio(0)->idioma,"eng"));assert(!strcmp(mac_video_audio(1)->rotulo,"Português 880Hz"));
  mac_video_escolher_audio(0);waitTrack(0,440);
  mac_video_pausar(1);double pos=mac_video_pos();mac_video_escolher_audio(1);
  Uint64 until=SDL_GetTicks64()+3000;while(mac_video_audio_atual()!=1 && SDL_GetTicks64()<until)SDL_Delay(5);
  assert(mac_video_audio_atual()==1);assert(fabs(mac_video_pos()-pos)<.02);
  atomic_store(&observedHz,0);mac_video_pausar(0);waitTrack(1,880);
  mac_video_escolher_audio(0);waitTrack(0,440);
  mac_video_parar();assert(mac_video_n_audio()==0);SDL_Quit();
  puts("PASS: real AAC audio enumeration, language preference, decoded 440/880Hz switch both ways, paused position preserved, session reset");return 0;
}
