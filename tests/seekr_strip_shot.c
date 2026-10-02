#include "dados.h"
#include "seekr_view.h"
#include "text.h"
#include "tex_cache.h"
#include "player.h"
#include "catalogo.h"
#include <assert.h>
// Exercise the real sprite crop/cache with an offline transport. Production
// has no fixture hooks, and this cannot spend the user's Seekr API quota.
#define rede_baixar_bin_medido_controle seekr_fixture_baixar
#include "../src/seekr.c"
#undef rede_baixar_bin_medido_controle
static const char *sheetFile;
static SDL_atomic_t downloads;
static int playerShot;
char *seekr_fixture_baixar(const char *url,int seconds,const char *const *headers,
                          const RedeControle *ctl,long *size,RedeMedida *med) {
  (void)url;(void)seconds;(void)headers;(void)ctl;
  SDL_AtomicAdd(&downloads,1);SDL_Delay(60);
  FILE *f=fopen(sheetFile,"rb");assert(f);fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
  char *bytes=malloc(*size);assert(bytes);assert(fread(bytes,1,*size,f)==(size_t)*size);fclose(f);
  med->status=200;return bytes;
}
static void metadata(void) {
  seekr_preparar(NULL,0,0,0);
  cues=calloc(10,sizeof *cues);assert(cues);nCues=10;escala=2;
  for(int i=0;i<10;i++) {
    cues[i]=(SeekrCue){.inicio=i*30,.fim=(i+1)*30,.x=i*320,.w=320,.h=180};
    snprintf(cues[i].url,sizeof cues[i].url,"https://sprites.seekr.tv/fixture/sheet.jpg");
  }
}
static int esperar(double pos,SeekrPrevia q[SEEKR_PREVIAS]) {
  Uint32 deadline=SDL_GetTicks()+4000;int pronto=0;
  do {
    seekr_bombear();assert(seekr_faixa(pos,q));pronto=1;
    for(int i=0;i<SEEKR_PREVIAS;i++)if(q[i].valido && !q[i].tex)pronto=0;
    SDL_Delay(5);
  }while(!pronto && (Sint32)(deadline-SDL_GetTicks())>0);
  assert(pronto);return pronto;
}
static void captura(const char *prefix,const char *suffix,const SeekrPrevia q[SEEKR_PREVIAS],double pos) {
  char file[1024];snprintf(file,sizeof file,"%s-%s.bmp",prefix,suffix);
  for(int frame=0;frame<(playerShot?80:8);frame++) {
    txt_novo_quadro();gfx_novo_quadro();glClearColor(.015f,.023f,.038f,1);glClear(GL_COLOR_BUFFER_BIT);
    if(playerShot){player_atualizar(1.f/60.f,SDL_GetTicks());player_desenhar(SDL_GetTicks());}
    else seekr_view_desenhar(q,pos,9077,903,1,.86f,.065f,.12f);
  }
  SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,1920,1080,32,SDL_PIXELFORMAT_RGBA32);
  unsigned char *raw=malloc(1920*1080*4);assert(out && raw);
  glReadPixels(0,0,1920,1080,GL_RGBA,GL_UNSIGNED_BYTE,raw);
  for(int y=0;y<1080;y++)memcpy((char *)out->pixels+y*out->pitch,raw+(1079-y)*1920*4,1920*4);
  assert(SDL_SaveBMP(out,file)==0);SDL_FreeSurface(out);free(raw);assert(glGetError()==GL_NO_ERROR);
  printf("capture: %s\n",file);
}
int main(int argc,char **argv) {
  assert(argc==3);sheetFile=argv[1];
  SDL_SetHint("SDL_MAC_BACKGROUND_APP","1");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);
  IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
  SDL_Window *w=SDL_CreateWindow("Seekr",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(w);
  SDL_GLContext gl=SDL_GL_CreateContext(w);assert(gl);
  GLuint target,fbo;glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1920,1080,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
  glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
  assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  glViewport(0,0,1920,1080);gfx_tamanho_alvo(1920,1080);assert(gfx_iniciar());
  dados_iniciar("deploy/app/art");assert(txt_iniciar("deploy/app",1));tex_iniciar(32);
  SeekrPrevia q[SEEKR_PREVIAS];metadata();
  seekr_faixa(220,q);SDL_Delay(90);seekr_bombear();seekr_faixa(220,q);
  assert(q[2].tex && !q[0].tex && !q[4].tex); // center gets the first download
  esperar(220,q);assert(q[2].segundos==240);GLuint central=q[2].tex;
  assert(SDL_AtomicGet(&downloads)==1);captura(argv[2],"middle",q,220);
  esperar(420,q);esperar(220,q);assert(q[2].tex==central);assert(SDL_AtomicGet(&downloads)==1);
  esperar(0,q);assert(!q[0].valido && !q[1].valido && q[2].segundos==0);captura(argv[2],"start",q,0);
  esperar(540,q);assert(!q[3].valido && !q[4].valido && q[2].segundos==540);captura(argv[2],"end",q,540);
  int count=0;for(int i=0;i<SEEKR_CACHE;i++)if(quadrosCache[i].tex)count++;
  assert(count<=SEEKR_CACHE);seekr_fechar();assert(!glIsTexture(central));
  // A slow response from a closed title must not populate the next title.
  metadata();seekr_faixa(220,q);seekr_fechar();SDL_Delay(100);seekr_bombear();
  for(int i=0;i<SEEKR_CACHE;i++)assert(!quadrosCache[i].tex);
  puts("PASS: center priority, five distinct neighboring cues, scale, bounded cache, sprite reuse, endpoints and stale response");
  CatItem item={0};snprintf(item.tipo,sizeof item.tipo,"movie");
  snprintf(item.titulo,sizeof item.titulo,"Fixture Seekr");snprintf(item.meta,sizeof item.meta,"151 min");
  cat_definir(&item,1);player_abrir(0,NULL);player_erro_fonte();player_limpar_erro_fonte();
  player_atualizar(210,SDL_GetTicks());
  SDL_Event event={0};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_DOWN;player_evento(&event);
  event.key.keysym.sym=SDLK_RIGHT;player_evento(&event);
  assert(player_so_barra() && player_foco_na_barra());metadata();esperar(player_posicao_seg(),q);
  playerShot=1;captura(argv[2],"player",q,player_posicao_seg());
  event.key.keysym.sym=SDLK_DOWN;player_evento(&event);assert(!player_foco_na_barra() && !player_so_barra());
  player_encerrar();puts("PASS: integrated player seek layout and return to transport controls");
  tex_encerrar();txt_encerrar();gfx_encerrar();glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&target);
  SDL_GL_DeleteContext(gl);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
