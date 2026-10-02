#include "assrender.h"
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

int assrender_bytes_sao_fonte(const void *dados, size_t n) {
  const unsigned char *p = (const unsigned char *)dados;
  if (!p || n < 4) return 0;
  return (p[0] == 0 && p[1] == 1 && p[2] == 0 && p[3] == 0) ||
         !memcmp(p, "OTTO", 4) || !memcmp(p, "true", 4) ||
         !memcmp(p, "typ1", 4) || !memcmp(p, "ttcf", 4);
}

int assrender_ler_pasta_fontes(const char *dir,
                               void (*cb)(const char *nome, const void *dados,
                                          size_t tam, void *u),
                               void *u, int *ignorados) {
  DIR *d;
  struct dirent *e;
  int lidas = 0, fora = 0;
  if (ignorados) *ignorados = 0;
  if (!dir || !*dir || !(d = opendir(dir))) return 0;
  while ((e = readdir(d)) != NULL) {
    char caminho[1024];
    struct stat st;
    unsigned char cab[4];
    FILE *f;
    char *buf;
    if (e->d_name[0] == '.') continue;
    snprintf(caminho, sizeof caminho, "%s/%s", dir, e->d_name);
    if (stat(caminho, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 4 ||
        (unsigned long long)st.st_size > (unsigned long long)INT_MAX) continue;
    f = fopen(caminho, "rb");
    if (!f) continue;
    if (fread(cab, 1, 4, f) != 4 || !assrender_bytes_sao_fonte(cab, 4)) {
      fclose(f); fora++; continue;
    }
    buf = malloc((size_t)st.st_size);
    if (buf && fseek(f, 0, SEEK_SET) == 0 &&
        fread(buf, 1, (size_t)st.st_size, f) == (size_t)st.st_size) {
      if (cb) cb(e->d_name, buf, (size_t)st.st_size, u);
      lidas++;
    }
    free(buf);
    fclose(f);
  }
  closedir(d);
  if (ignorados) *ignorados = fora;
  return lidas;
}

/* The player's presentation time is the render time. There is no queue of
 * old subtitle frames, independent clock, drift correction or FPS remapping. */
#ifdef NV_ASS_LIBASS
#include <SDL2/SDL.h>
#include <ass/ass.h>
#include "gfx.h"
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static ASS_Library *library;
static ASS_Renderer *renderer;
static ASS_Track *track;
static unsigned generation,trackGeneration;
static int invalidated=1,prewarming;
static float originX,originY;
static int width=1920,height=1080,storageW=1920,storageH=1080;
static double scale=1;
static int bold,shadow=1,color=0xffffff,background,position=5;
static int tint,tintColor;
static GLuint *textures;
static int capacity,visible;
static int forceUpload=1,fonts;
static size_t fontBytes;
static long long renderedTime;
static double renderMs,maxRenderMs;
static char fallback[768],fontDirectory[640];
static void message(int level,const char *format,va_list args,void *user) {
  (void)user;if(level>2)return;fputs("[libass] ",stderr);vfprintf(stderr,format,args);fputc('\n',stderr);
}
static void addFont(const char *name,const void *bytes,size_t n,void *user) {
  (void)user;if(n>8*1024*1024 || fontBytes+n>32*1024*1024)return;
  ass_add_font(library,name,bytes,(int)n);fontBytes+=n;fonts++;
}
static void initialize(void) {
  if(library)return;
  library=ass_library_init();if(!library)return;
  ass_set_message_cb(library,message,NULL);ass_set_extract_fonts(library,1);
  renderer=ass_renderer_init(library);if(!renderer)return;
  char directory[640]="deploy/app/fonts";
#ifdef __EMSCRIPTEN__
  snprintf(directory,sizeof directory,"/app/fonts");
#else
  char *base=SDL_GetBasePath();
  if(base){char path[640];snprintf(path,sizeof path,"%sfonts",base);if(access(path,R_OK)==0)snprintf(directory,sizeof directory,"%s",path);SDL_free(base);}
#endif
  snprintf(fontDirectory,sizeof fontDirectory,"%s",directory);
  snprintf(fallback,sizeof fallback,"%s/NetflixSans-Regular.otf",directory);
  assrender_ler_pasta_fontes(directory,addFont,NULL,NULL);
  ass_set_fonts(renderer,access(fallback,R_OK)==0?fallback:NULL,"Netflix Sans",ASS_FONTPROVIDER_AUTODETECT,NULL,1);
  ass_set_cache_limits(renderer,0,32);
  ass_set_shaper(renderer,ASS_SHAPING_COMPLEX);
  ass_set_frame_size(renderer,width,height);ass_set_storage_size(renderer,storageW,storageH);
}
static void applyStyle(void) {
  if(!renderer)return;
  int rx=track&&track->PlayResX>0?track->PlayResX:1920,ry=track&&track->PlayResY>0?track->PlayResY:1080;
  ASS_Style s={0};s.FontName=bold?"Netflix Sans Med":"Netflix Sans";
  s.FontSize=48.0*ry/1080.0;s.ScaleX=s.ScaleY=1;
  s.PrimaryColour=s.SecondaryColour=((unsigned)(color>>16&255)<<24)|((unsigned)(color>>8&255)<<16)|((unsigned)(color&255)<<8);
  s.OutlineColour=0;s.BackColour=background==4?0:(unsigned)(255-background*64);
  s.BorderStyle=background?3:1;s.Shadow=shadow?2:0;s.Alignment=2;
  s.MarginL=s.MarginR=(int)lround(60.0*rx/1920.0);s.MarginV=(int)lround((.08+(position-5)*.004)*ry);
  ass_set_selective_style_override(renderer,&s);
  ass_set_selective_style_override_enabled(renderer,ASS_OVERRIDE_BIT_FONT_NAME|ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS|ASS_OVERRIDE_BIT_BORDER|ASS_OVERRIDE_BIT_ALIGNMENT|ASS_OVERRIDE_BIT_MARGINS);
  forceUpload=1;
}
static void *warm(void *unused) {
  (void)unused;pthread_mutex_lock(&lock);initialize();prewarming=0;pthread_mutex_unlock(&lock);return NULL;
}
void assrender_preaquecer(void) {
  pthread_mutex_lock(&lock);
  if(!library&&!prewarming){pthread_t t;prewarming=1;if(pthread_create(&t,NULL,warm,NULL))prewarming=0;else pthread_detach(t);}
  pthread_mutex_unlock(&lock);
}
void assrender_geracao(unsigned g){pthread_mutex_lock(&lock);generation=g;invalidated=forceUpload=1;pthread_mutex_unlock(&lock);}
int assrender_carregar(const char *body,size_t n,unsigned g) {
  if(!body||n>4*1024*1024)return 0;
  pthread_mutex_lock(&lock);if(g!=generation){pthread_mutex_unlock(&lock);return 0;}
  initialize();char *copy=malloc(n+1);ASS_Track *next=NULL;
  if(copy&&library){memcpy(copy,body,n);copy[n]=0;next=ass_read_memory(library,copy,n,"UTF-8");}free(copy);
  if(next){if(track)ass_free_track(track);track=next;trackGeneration=g;applyStyle();invalidated=forceUpload=1;}
  pthread_mutex_unlock(&lock);return next!=NULL;
}
int assrender_atualizar(const char *body,size_t n,unsigned g){return assrender_carregar(body,n,g);}
void assrender_limpar(void){pthread_mutex_lock(&lock);if(track)ass_free_track(track);track=NULL;invalidated=forceUpload=1;pthread_mutex_unlock(&lock);}
void assrender_limpar_fontes(void){
  pthread_mutex_lock(&lock);
  if(library){
    /* Discard the renderer's font cache before the library shrinks. */
    if(renderer)ass_renderer_done(renderer);
    renderer=NULL;ass_clear_fonts(library);fonts=0;fontBytes=0;
    assrender_ler_pasta_fontes(fontDirectory,addFont,NULL,NULL);
    renderer=ass_renderer_init(library);
    if(renderer){
      ass_set_fonts(renderer,access(fallback,R_OK)==0?fallback:NULL,"Netflix Sans",ASS_FONTPROVIDER_AUTODETECT,NULL,1);
      ass_set_cache_limits(renderer,0,32);ass_set_shaper(renderer,ASS_SHAPING_COMPLEX);
      ass_set_frame_size(renderer,width,height);ass_set_storage_size(renderer,storageW,storageH);
      ass_set_font_scale(renderer,scale);applyStyle();
    }
    invalidated=forceUpload=1;
  }
  pthread_mutex_unlock(&lock);
}
int assrender_adicionar_fonte(const char *name,const void *bytes,size_t n){
  if(!name||!assrender_bytes_sao_fonte(bytes,n)||n>8*1024*1024)return 0;
  pthread_mutex_lock(&lock);initialize();int ok=library&&fontBytes+n<=32*1024*1024;if(ok)addFont(name,bytes,n,NULL);pthread_mutex_unlock(&lock);return ok;
}
void assrender_definir_layout(float x,float y,float w,float h,int vw,int vh,double fontScale) {
  if(w<16||h<16)return;pthread_mutex_lock(&lock);originX=x;originY=y;
  int iw=(int)lroundf(w),ih=(int)lroundf(h);if(vw<2||vh<2){vw=iw;vh=ih;}if(fontScale<.1||fontScale>4)fontScale=1;
  if(width!=iw||height!=ih||storageW!=vw||storageH!=vh||scale!=fontScale){
    width=iw;height=ih;storageW=vw;storageH=vh;scale=fontScale;forceUpload=1;
    if(renderer){ass_set_frame_size(renderer,width,height);ass_set_storage_size(renderer,vw,vh);ass_set_font_scale(renderer,scale);}
  }pthread_mutex_unlock(&lock);
}
void assrender_definir_cor(int enabled,int r,int g,int b){pthread_mutex_lock(&lock);int c=(r&255)<<16|(g&255)<<8|(b&255);if(tint!=enabled||tintColor!=c)forceUpload=1;tint=enabled;tintColor=c;pthread_mutex_unlock(&lock);}
void assrender_definir_estilo(int b,int sh,int c,int bg,int pos){
  pthread_mutex_lock(&lock);if(bold!=b||shadow!=sh||color!=c||background!=bg||position!=pos){bold=b;shadow=sh;color=c;background=bg<0?0:bg>4?4:bg;position=pos;applyStyle();}pthread_mutex_unlock(&lock);
}
static void clearTextures(void){
  for(int i=0;i<capacity;i++)if(textures[i]){glDeleteTextures(1,&textures[i]);textures[i]=0;}
  gfx_tex_esquecer(0);visible=0;invalidated=0;
}
void assrender_aplicar_invalidacao(void){pthread_mutex_lock(&lock);if(invalidated)clearTextures();pthread_mutex_unlock(&lock);}
static int reserve(int n){if(n<=capacity)return 1;int cap=capacity?capacity*2:32;while(cap<n)cap*=2;GLuint *p=realloc(textures,(size_t)cap*sizeof *p);if(!p)return 0;memset(p+capacity,0,(size_t)(cap-capacity)*sizeof *p);textures=p;capacity=cap;return 1;}
static int upload(int index,const ASS_Image *im){
  if(im->w<=0||im->h<=0||im->w>8192||im->h>8192)return 0;
  size_t n=(size_t)im->w*im->h*4;if(n>16*1024*1024)return 0;
  unsigned char *rgba=malloc(n);if(!rgba)return 0;
  unsigned col=im->color;int r=col>>24&255,g=col>>16&255,b=col>>8&255,opacity=255-(col&255);
  if(tint&&im->type==IMAGE_TYPE_CHARACTER){r=tintColor>>16&255;g=tintColor>>8&255;b=tintColor&255;}
  for(int y=0;y<im->h;y++)for(int x=0;x<im->w;x++) {
    unsigned char *p=rgba+((size_t)y*im->w+x)*4;p[0]=r;p[1]=g;p[2]=b;p[3]=(im->bitmap[(size_t)y*im->stride+x]*opacity+127)/255;
  }
  if(!textures[index])glGenTextures(1,&textures[index]);glBindTexture(GL_TEXTURE_2D,textures[index]);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,im->w,im->h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  free(rgba);gfx_tex_esquecer(0);return 1;
}
static double mono(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int assrender_desenhar(double seconds,int delay,float alpha,float x,float y,float w,float h){
  (void)w;(void)h;if(alpha<=0)return 0;
  pthread_mutex_lock(&lock);
  if(!renderer||!track||trackGeneration!=generation){pthread_mutex_unlock(&lock);return 0;}
  if(invalidated)clearTextures();
  int changed=0,n=0;renderedTime=(long long)llround((seconds-delay/1000.0)*1000);
  double start=mono();ASS_Image *images=ass_render_frame(renderer,track,renderedTime,&changed);
  renderMs=(mono()-start)*1000;if(renderMs>maxRenderMs)maxRenderMs=renderMs;
  for(ASS_Image *im=images;im;im=im->next)if(im->w>0&&im->h>0)n++;
  if(!reserve(n)){pthread_mutex_unlock(&lock);return 0;}
  int slot=0;for(ASS_Image *im=images;im;im=im->next){
    if(im->w<=0||im->h<=0)continue;
    if((changed||forceUpload||slot>=visible)&&!upload(slot,im)){slot++;continue;}
    gfx_tex_aspect_atual=0;
    gfx_rect((GfxRect){x+originX+im->dst_x,y+originY+im->dst_y,im->w,im->h},textures[slot],GFX_TEXTO,0,0,0,0,1,1,1,alpha);
    slot++;
  }
  visible=n;forceUpload=0;gfx_tex_aspect_atual=0;pthread_mutex_unlock(&lock);return n;
}
int assrender_ativo(void){pthread_mutex_lock(&lock);int yes=renderer&&track&&trackGeneration==generation;pthread_mutex_unlock(&lock);return yes;}
int assrender_quadro_cpu(double seconds){pthread_mutex_lock(&lock);int n=-1,changed=0;if(renderer&&track){n=0;for(ASS_Image *im=ass_render_frame(renderer,track,(long long)llround(seconds*1000),&changed);im;im=im->next)n++;forceUpload=1;}pthread_mutex_unlock(&lock);return n;}
const char *assrender_diagnostico(void){static _Thread_local char out[256];pthread_mutex_lock(&lock);snprintf(out,sizeof out,"presentation-time libass; eventos=%d fontes=%d time=%lldms render=%.2fms max=%.2fms",track?track->n_events:0,fonts,renderedTime,renderMs,maxRenderMs);pthread_mutex_unlock(&lock);return out;}
#else
void assrender_preaquecer(void){}
void assrender_geracao(unsigned g){(void)g;}
void assrender_aplicar_invalidacao(void){}
int assrender_carregar(const char *b,size_t n,unsigned g){(void)b;(void)n;(void)g;return 0;}
int assrender_atualizar(const char *b,size_t n,unsigned g){return assrender_carregar(b,n,g);}
void assrender_limpar(void){}
void assrender_limpar_fontes(void){}
int assrender_adicionar_fonte(const char *n,const void *b,size_t s){(void)n;(void)b;(void)s;return 0;}
void assrender_definir_cor(int e,int r,int g,int b){(void)e;(void)r;(void)g;(void)b;}
void assrender_definir_estilo(int b,int s,int c,int f,int p){(void)b;(void)s;(void)c;(void)f;(void)p;}
void assrender_definir_layout(float x,float y,float w,float h,int vw,int vh,double s){(void)x;(void)y;(void)w;(void)h;(void)vw;(void)vh;(void)s;}
int assrender_desenhar(double p,int d,float a,float x,float y,float w,float h){(void)p;(void)d;(void)a;(void)x;(void)y;(void)w;(void)h;return 0;}
int assrender_ativo(void){return 0;}
int assrender_quadro_cpu(double p){(void)p;return -1;}
const char *assrender_diagnostico(void){return "platform text cues";}
#endif
