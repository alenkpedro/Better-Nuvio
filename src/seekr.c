#include "seekr.h"
#include "dados.h"
#include "sessao.h"
#include "rede.h"
#include "js.h"
#include "gfx.h"
#include <SDL.h>
#include <SDL_image.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#define SEEKR_MAX_CUES 2200
#define SEEKR_LIMITE 40
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static unsigned geracao;
static char imdbAtual[64], chaveAtual[80], contaArquivo[48];
static long tmdbAtual;
static int temporadaAtual, episodioAtual, consultaFeita, prefetchFeito, limitarContaAtual;
static SeekrCue *cues;
static int nCues, cuePronto = -1, cueEmCurso = -1, janela[SEEKR_PREVIAS];
static double escala = 1.0;
static SDL_Surface *superficiePronta;
#define SEEKR_CACHE 9
typedef struct { int cue, falhou; GLuint tex; unsigned uso; Uint32 falhouEm; } QuadroCache;
static QuadroCache quadrosCache[SEEKR_CACHE];
static unsigned usoCacheQuadros;
static char *sheetCache;
static long sheetBytes;
static char sheetUrl[768];
static char chaveContaCache[48], usoContaCache[48];
static int chaveConfiguradaCache=-1, chavePessoalCache, usoCache;
static long usoDiaCache=-1;

static void nomeConta(void) {
  const unsigned char *p = (const unsigned char *)sessao_usuario();
  unsigned long long h = 1469598103934665603ULL;
  if (!p || !*p) { contaArquivo[0] = 0; return; }
  for (; *p; p++) { h ^= *p; h *= 1099511628211ULL; }
  snprintf(contaArquivo, sizeof contaArquivo, "seekr-%016llx", h);
}
static void nomeDado(char *dst, size_t tam, const char *sufixo) {
  nomeConta();
  if (contaArquivo[0]) snprintf(dst, tam, "%s-%s.txt", contaArquivo, sufixo);
  else dst[0] = 0;
}
static int chaveValida(const char *k) {
  if (!k || strlen(k) != 72 || strncmp(k, "sk_live_", 8)) return 0;
  for (int i=8; i<72; i++) if (!isxdigit((unsigned char)k[i])) return 0;
  return 1;
}
static int imdbValido(const char *id) {
  if (!id || id[0]!='t' || id[1]!='t' || !isdigit((unsigned char)id[2])) return 0;
  for (id+=2; *id && *id!=':'; id++) if (!isdigit((unsigned char)*id)) return 0;
  return 1;
}
static void lerChavePessoal(char *dst, size_t tam) {
  char nome[64], *v;
  dst[0] = 0; nomeDado(nome, sizeof nome, "key");
  if (!nome[0]) return;
  v = dados_ler(nome);
  if (v && chaveValida(v)) snprintf(dst, tam, "%s", v);
  free(v);
}
int seekr_chave_integrada(void) { return 0; }
static void atualizarChaveCache(void) {
  nomeConta();
  if (chaveConfiguradaCache<0 || strcmp(chaveContaCache,contaArquivo)) {
    char k[80]; lerChavePessoal(k,sizeof k);
    chavePessoalCache=!!k[0];
    chaveConfiguradaCache=chavePessoalCache;
    snprintf(chaveContaCache,sizeof chaveContaCache,"%s",contaArquivo);
    memset(k,0,sizeof k);
  }
}
int seekr_chave_configurada(void) {
  atualizarChaveCache();
  return chaveConfiguradaCache;
}
int seekr_chave_pessoal_configurada(void) {
  atualizarChaveCache();
  return chavePessoalCache;
}
int seekr_definir_chave(const char *chave) {
  char nome[64]; nomeDado(nome, sizeof nome, "key");
  if (!nome[0] || !chave || (*chave && !chaveValida(chave))) return 0;
  int ok;
  if (!*chave) {
    char *anterior=dados_ler(nome);
    ok=anterior ? dados_apagar(nome) : 1;
    free(anterior);
  } else ok=dados_gravar(nome,chave);
  if (ok) {
    chavePessoalCache=!!*chave;
    chaveConfiguradaCache=chavePessoalCache;
    snprintf(chaveContaCache,sizeof chaveContaCache,"%s",contaArquivo);
  }
  return ok;
}
static long diaUtc(void) { return (long)(time(NULL) / 86400); }
static int usoAtual(int adicionar) {
  char nome[64], texto[80], *lido;
  long dia = 0, hoje=diaUtc(); int n = 0;
  nomeDado(nome, sizeof nome, "usage");
  if (!nome[0]) return SEEKR_LIMITE;
  if (!strcmp(usoContaCache,contaArquivo) && usoDiaCache==hoje) n=usoCache;
  else {
    lido = dados_ler(nome);
    if (lido) { sscanf(lido, "%ld %d", &dia, &n); free(lido); }
    if (dia != hoje || n < 0 || n > SEEKR_LIMITE) n = 0;
    snprintf(usoContaCache,sizeof usoContaCache,"%s",contaArquivo);
    usoDiaCache=hoje; usoCache=n;
  }
  if (adicionar) {
    if (n >= SEEKR_LIMITE) return n;
    snprintf(texto, sizeof texto, "%ld %d\n", hoje, n+1);
    if (!dados_gravar(nome, texto)) return SEEKR_LIMITE;
    n++; usoCache=n;
  }
  return n;
}
int seekr_usadas_hoje(void) { return usoAtual(0); }

typedef struct { unsigned gen; char key[80], url[320]; } Consulta;
static void *consultar(void *arg) {
  Consulta *q=arg; RedeControle ctl={.max_bytes=32768}; RedeMedida med={0};
  char hdr[100]; snprintf(hdr,sizeof hdr,"X-API-Key: %s",q->key);
  const char *cab[]={hdr,NULL};
  char *json=rede_baixar_medido_controle(q->url,9,cab,&ctl,&med);
  char vttUrl[768]=""; double sc=1.0;
  if (json && med.status==200) {
    js_texto(json,NULL,"vtt_url",vttUrl,sizeof vttUrl);
    sc=js_num(json,NULL,"scale",1.0);
  }
  free(json); memset(hdr,0,sizeof hdr); memset(q->key,0,sizeof q->key);
  SeekrCue *novos=NULL; int n=0;
  if (!strncmp(vttUrl,"https://sprites.seekr.tv/",25) && isfinite(sc) && sc>0.01 && sc<100) {
    RedeControle cap={.max_bytes=400000}; RedeMedida vm={0};
    char *vtt=rede_baixar_medido_controle(vttUrl,9,NULL,&cap,&vm);
    if (vtt && vm.status==200) {
      novos=calloc(SEEKR_MAX_CUES,sizeof *novos);
      if (novos) n=seekr_ler_vtt(vtt,novos,SEEKR_MAX_CUES);
    }
    free(vtt);
  }
  pthread_mutex_lock(&trava);
  if (q->gen==geracao) {
    free(cues); cues=novos; novos=NULL; nCues=n; escala=sc;
  }
  pthread_mutex_unlock(&trava);
  free(novos); free(q); return NULL;
}
void seekr_preparar(const char *imdb, long tmdb, int t, int e) {
  pthread_mutex_lock(&trava);
  geracao++; free(cues); cues=NULL; nCues=0; consultaFeita=prefetchFeito=0;
  cuePronto=cueEmCurso=-1; escala=1.0;
  for (int i=0;i<SEEKR_PREVIAS;i++)janela[i]=-1;
  if (superficiePronta) { SDL_FreeSurface(superficiePronta); superficiePronta=NULL; }
  free(sheetCache); sheetCache=NULL; sheetBytes=0; sheetUrl[0]=0;
  snprintf(imdbAtual,sizeof imdbAtual,"%s",imdb?imdb:"");
  tmdbAtual=tmdb; temporadaAtual=t; episodioAtual=e;
  memset(chaveAtual,0,sizeof chaveAtual);
  lerChavePessoal(chaveAtual,sizeof chaveAtual);
  limitarContaAtual=0;
  pthread_mutex_unlock(&trava);
  for(int i=0;i<SEEKR_CACHE;i++) {
    if(quadrosCache[i].tex) {gfx_tex_esquecer(quadrosCache[i].tex);glDeleteTextures(1,&quadrosCache[i].tex);}
    quadrosCache[i]=(QuadroCache){.cue=-1};
  }
  usoCacheQuadros=0;
}
void seekr_fechar(void) { seekr_preparar(NULL,0,0,0); }
void seekr_duracao(double segundos) {
  if (!isfinite(segundos) || segundos<60 || segundos>86400) return;
  pthread_mutex_lock(&trava);
  if (consultaFeita || !chaveAtual[0] ||
      (!imdbValido(imdbAtual) && tmdbAtual<=0) ||
      (temporadaAtual>0 && episodioAtual<=0)) {
    pthread_mutex_unlock(&trava); return;
  }
  consultaFeita=1;
  int limitar=limitarContaAtual;
  Consulta *q=calloc(1,sizeof *q);
  if (q) { q->gen=geracao; snprintf(q->key,sizeof q->key,"%s",chaveAtual); }
  pthread_mutex_unlock(&trava);
  if (!q) return;
  if (limitar && usoAtual(0)>=SEEKR_LIMITE) { free(q); return; }
  int ms=(int)round(segundos*1000.0);
  char ids[120];
  if (imdbValido(imdbAtual)) {
    snprintf(ids,sizeof ids,"%s=%.*s",temporadaAtual>0?"show_imdb_id":"imdb_id",
             (int)strcspn(imdbAtual,":"),imdbAtual);
  } else {
    snprintf(ids,sizeof ids,"%s=%ld",temporadaAtual>0?"show_tmdb_id":"tmdb_id",tmdbAtual);
  }
  snprintf(q->url,sizeof q->url,"https://api.seekr.tv/sprites?%s&duration_ms=%d",ids,ms);
  if (temporadaAtual>0 && episodioAtual>0) {
    size_t len=strlen(q->url);
    snprintf(q->url+len,sizeof q->url-len,"&season=%d&episode=%d",temporadaAtual,episodioAtual);
  }
  if (limitar && usoAtual(1)>SEEKR_LIMITE) { free(q); return; }
  pthread_t fio;
  if (pthread_create(&fio,NULL,consultar,q)==0) pthread_detach(fio);
  else free(q);
}

typedef struct { unsigned gen; int indice; SeekrCue cue; } Tile;
static void *baixarTile(void *arg) {
  Tile *q=arg; RedeControle cap={.max_bytes=6500000}; RedeMedida med={0}; long n=0;
  char *jpeg=NULL;
  pthread_mutex_lock(&trava);
  if (q->gen==geracao && sheetCache && !strcmp(sheetUrl,q->cue.url)) {
    n=sheetBytes; jpeg=malloc((size_t)n);
    if (jpeg) memcpy(jpeg,sheetCache,(size_t)n);
    med.status=jpeg?200:0;
  }
  pthread_mutex_unlock(&trava);
  if (!jpeg) jpeg=rede_baixar_bin_medido_controle(q->cue.url,10,NULL,&cap,&n,&med);
  SDL_Surface *corte=NULL;
  if (jpeg && n>0 && n<=6500000 && med.status==200) {
    pthread_mutex_lock(&trava);
    if (q->gen==geracao && strcmp(sheetUrl,q->cue.url)) {
      char *copia=malloc((size_t)n);
      if (copia) {
        memcpy(copia,jpeg,(size_t)n);
        free(sheetCache); sheetCache=copia; sheetBytes=n;
        snprintf(sheetUrl,sizeof sheetUrl,"%s",q->cue.url);
      }
    }
    pthread_mutex_unlock(&trava);
    SDL_RWops *rw=SDL_RWFromConstMem(jpeg,(int)n);
    SDL_Surface *sheet=rw?IMG_Load_RW(rw,1):NULL;
    if (sheet && q->cue.x+q->cue.w<=sheet->w && q->cue.y+q->cue.h<=sheet->h) {
      corte=SDL_CreateRGBSurfaceWithFormat(0,q->cue.w,q->cue.h,32,SDL_PIXELFORMAT_ABGR8888);
      if (corte) {
        SDL_Rect src={q->cue.x,q->cue.y,q->cue.w,q->cue.h};
        if (SDL_BlitSurface(sheet,&src,corte,NULL)<0) { SDL_FreeSurface(corte); corte=NULL; }
      }
    }
    if (sheet) SDL_FreeSurface(sheet);
  }
  free(jpeg);
  pthread_mutex_lock(&trava);
  if (q->gen==geracao) {
    if (superficiePronta) SDL_FreeSurface(superficiePronta);
    superficiePronta=corte; corte=NULL; cuePronto=q->indice;
  }
  if (q->gen==geracao && cueEmCurso==q->indice) cueEmCurso=-1;
  pthread_mutex_unlock(&trava);
  if (corte) SDL_FreeSurface(corte);
  free(q); return NULL;
}
void seekr_bombear(void) {
  SDL_Surface *s=NULL; int i=-1;
  pthread_mutex_lock(&trava);
  if (cuePronto>=0) { s=superficiePronta; superficiePronta=NULL; i=cuePronto; cuePronto=-1; }
  pthread_mutex_unlock(&trava);
  if(i<0)return;
  // Somente este fio usa as texturas. Um worker entrega no maximo um corte,
  // e o cache conserva a janela atual antes de substituir os quadros antigos.
  int slot=-1;
  for(int j=0;j<SEEKR_CACHE;j++)if(quadrosCache[j].cue==i || quadrosCache[j].cue<0){slot=j;break;}
  if(slot<0)for(int j=0;j<SEEKR_CACHE;j++) {
    int protegido=0;for(int k=0;k<SEEKR_PREVIAS;k++)if(quadrosCache[j].cue==janela[k])protegido=1;
    if(!protegido && (slot<0 || quadrosCache[j].uso<quadrosCache[slot].uso))slot=j;
  }
  if(slot<0){if(s)SDL_FreeSurface(s);return;}
  QuadroCache *c=&quadrosCache[slot];
  if(c->tex){gfx_tex_esquecer(c->tex);glDeleteTextures(1,&c->tex);}
  *c=(QuadroCache){.cue=i,.falhou=!s,.falhouEm=SDL_GetTicks(),.uso=++usoCacheQuadros};
  if(s) {
    glGenTextures(1,&c->tex);glBindTexture(GL_TEXTURE_2D,c->tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,s->w,s->h,0,GL_RGBA,GL_UNSIGNED_BYTE,s->pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    gfx_tex_esquecer(0);SDL_FreeSurface(s);
  }
}
int seekr_faixa(double segundos,SeekrPrevia saida[SEEKR_PREVIAS]) {
  Tile *q=NULL;int i;
  if(saida)memset(saida,0,sizeof(SeekrPrevia)*SEEKR_PREVIAS);
  pthread_mutex_lock(&trava);
  i=seekr_escolher_cue(cues,nCues,segundos/escala);
  for(int k=0;k<SEEKR_PREVIAS;k++) {
    int indice=i>=0?i+k-SEEKR_PREVIAS/2:-1;
    janela[k]=indice>=0 && indice<nCues?indice:-1;
    if(janela[k]<0 || !saida)continue;
    saida[k]=(SeekrPrevia){.valido=1,.segundos=cues[indice].inicio*escala,
                           .aspecto=(float)cues[indice].w/cues[indice].h};
    for(int j=0;j<SEEKR_CACHE;j++)if(quadrosCache[j].cue==indice) {
      saida[k].tex=quadrosCache[j].tex;quadrosCache[j].uso=++usoCacheQuadros;break;
    }
  }
  if(i>=0 && cueEmCurso<0 && cuePronto<0) {
    static const int prioridade[SEEKR_PREVIAS]={2,1,3,0,4};
    for(int k=0;k<SEEKR_PREVIAS;k++) {
      int indice=janela[prioridade[k]],pronto=0;
      if(indice<0)continue;
      for(int j=0;j<SEEKR_CACHE;j++)if(quadrosCache[j].cue==indice) {
        pronto=quadrosCache[j].tex || (quadrosCache[j].falhou && SDL_GetTicks()-quadrosCache[j].falhouEm<15000u);break;
      }
      if(pronto)continue;
      q=calloc(1,sizeof *q);
      if(q){q->gen=geracao;q->indice=indice;q->cue=cues[indice];cueEmCurso=indice;}
      break;
    }
  }
  pthread_mutex_unlock(&trava);
  if (q) { pthread_t fio;
    if (pthread_create(&fio,NULL,baixarTile,q)==0) pthread_detach(fio);
    else { pthread_mutex_lock(&trava); if (q->gen==geracao) cueEmCurso=-1; pthread_mutex_unlock(&trava); free(q); }
  }
  return i>=0;
}
int seekr_previa(double segundos,GLuint *tex,double *cueSegundos) {
  SeekrPrevia faixa[SEEKR_PREVIAS];seekr_faixa(segundos,faixa);
  SeekrPrevia *centro=&faixa[SEEKR_PREVIAS/2];
  if(!centro->tex)return 0;
  if(tex)*tex=centro->tex;if(cueSegundos)*cueSegundos=centro->segundos;
  return 1;
}
void seekr_prefetch(double segundos) {
  int fazer=0;
  pthread_mutex_lock(&trava);
  if (!prefetchFeito && nCues>0) { prefetchFeito=1; fazer=1; }
  pthread_mutex_unlock(&trava);
  if (fazer) seekr_previa(segundos,NULL,NULL);
}
