#include "legenda.h"
#include "rede.h"
#include "assrender.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#define DOCUMENT_LIMIT (4*1024*1024)
#define CUE_LIMIT 20000
/* One immutable document, one owner, the player's media time. A load cannot
 * publish into another selection. libass remains the ASS renderer. */
typedef struct { LegendaCue *cue; double *ends; int n,ass; char *body; } Document;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Document *active;
static unsigned generation=1,rendererGeneration;
static int state=LEG_OFF,rendererPending;
static void destroy(Document *d){if(d){free(d->cue);free(d->ends);free(d->body);free(d);}}
static char *trim(char *s){while(isspace((unsigned char)*s))s++;char *e=s+strlen(s);while(e>s&&isspace((unsigned char)e[-1]))*--e=0;return s;}
static int emit(char *out,size_t *n,unsigned cp){
  if(cp<=0x7f)out[(*n)++]=(char)cp;
  else if(cp<=0x7ff){out[(*n)++]=(char)(0xc0|(cp>>6));out[(*n)++]=(char)(0x80|(cp&63));}
  else if(cp<=0xffff){out[(*n)++]=(char)(0xe0|(cp>>12));out[(*n)++]=(char)(0x80|((cp>>6)&63));out[(*n)++]=(char)(0x80|(cp&63));}
  else if(cp<=0x10ffff){out[(*n)++]=(char)(0xf0|(cp>>18));out[(*n)++]=(char)(0x80|((cp>>12)&63));out[(*n)++]=(char)(0x80|((cp>>6)&63));out[(*n)++]=(char)(0x80|(cp&63));}
  return 1;
}
static int utf8Valid(const unsigned char *s,size_t n){
  for(size_t i=0;i<n;){unsigned c=s[i++],cp;int k;
    if(c<128)continue;if(c>=0xc2&&c<=0xdf){k=1;cp=c&31;}else if(c>=0xe0&&c<=0xef){k=2;cp=c&15;}else if(c>=0xf0&&c<=0xf4){k=3;cp=c&7;}else return 0;
    if(i+(size_t)k>n)return 0;for(int j=0;j<k;j++){if((s[i]&0xc0)!=0x80)return 0;cp=(cp<<6)|(s[i++]&63);}
    if(cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||(k==2&&cp<0x800)||(k==3&&cp<0x10000))return 0;
  }return 1;
}
/* Byte-length decoding also handles UTF-16 files containing NUL bytes. */
char *legenda_decodificar(const void *bytes,size_t len){
  if(!bytes||!len||len>DOCUMENT_LIMIT)return NULL;
  const unsigned char *b=bytes;char *out=malloc(len*3+4);if(!out)return NULL;size_t n=0,start=0;
  int utf16=len>=2&&((b[0]==0xff&&b[1]==0xfe)||(b[0]==0xfe&&b[1]==0xff));
  if(utf16){int little=b[0]==0xff;for(size_t i=2;i+1<len;i+=2){unsigned c=little?(b[i]|b[i+1]<<8):(b[i]<<8|b[i+1]);
    if(c>=0xd800&&c<=0xdbff&&i+3<len){unsigned low=little?(b[i+2]|b[i+3]<<8):(b[i+2]<<8|b[i+3]);if(low>=0xdc00&&low<=0xdfff){c=0x10000+((c-0xd800)<<10)+(low-0xdc00);i+=2;}else c=0xfffd;}
    else if(c>=0xd800&&c<=0xdfff)c=0xfffd;if(c)emit(out,&n,c);
  }}else{if(len>=3&&b[0]==0xef&&b[1]==0xbb&&b[2]==0xbf)start=3;
    if(utf8Valid(b+start,len-start)){memcpy(out,b+start,len-start);n=len-start;}
    else {static const unsigned cp1252[32]={0x20ac,0xfffd,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0xfffd,0x17d,0xfffd,0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0xfffd,0x17e,0x178};
      for(size_t i=start;i<len;i++){unsigned c=b[i];if(c>=128&&c<160)c=cp1252[c-128];if(c)emit(out,&n,c);}}
  }
  size_t j=0;for(size_t i=0;i<n;i++){if(out[i]=='\r'){out[j++]='\n';if(i+1<n&&out[i+1]=='\n')i++;}else out[j++]=out[i];}out[j]=0;return out;
}
static double timestamp(const char *s){
  while(isspace((unsigned char)*s))s++;char *e;double parts[3];int n=0;
  while(n<3){parts[n]=strtod(s,&e);if(e==s||parts[n]<0||!isfinite(parts[n]))return -1;n++;if(*e!=':')break;s=e+1;}
  double seconds=n==3?parts[0]*3600+parts[1]*60+parts[2]:n==2?parts[0]*60+parts[1]:parts[0];
  /* strtod stops before SRT's comma fraction. */
  if(*e==','){double scale=.1;for(e++;isdigit((unsigned char)*e);e++,scale*=.1)seconds+=(*e-'0')*scale;}
  return n>=2?seconds:-1;
}
static int compare(const void *a,const void *b){const LegendaCue *x=a,*y=b;return x->inicio<y->inicio?-1:x->inicio>y->inicio?1:x->ordem-y->ordem;}
static void initCue(LegendaCue *c,int order){memset(c,0,sizeof *c);c->cor=-1;c->posX=c->posY=-1;c->ordem=order;}
static int append(LegendaCue **v,int *n,int *cap,const LegendaCue *c){
  if(*n>=CUE_LIMIT)return 0;if(c->inicio<0||c->fim<=c->inicio||!c->texto[0])return 1;
  if(*n==*cap){int size=*cap?*cap*2:128;if(size>CUE_LIMIT)size=CUE_LIMIT;void *p=realloc(*v,(size_t)size*sizeof **v);if(!p)return 0;*v=p;*cap=size;}(*v)[(*n)++]=*c;return 1;
}
static void cleanText(const char *s,char *out,size_t cap,int ass){
  size_t n=0;int drawing=0;
  for(;*s&&n+1<cap;s++){
    if(ass&&*s=='{'){const char *e=strchr(s,'}');if(e){const char *p=s;while((p=strstr(p,"\\p"))&&p<e){if(isdigit((unsigned char)p[2]))drawing=atoi(p+2);p+=2;}s=e;continue;}}
    if(ass&&*s=='\\'&&(s[1]=='N'||s[1]=='n'||s[1]=='h')){out[n++]=s[1]=='h'?' ':'\n';s++;continue;}
    if(drawing)continue;
    if(!ass&&*s=='<'){const char *e=strchr(s,'>');if(e){if(!strncasecmp(s,"<br",3))out[n++]='\n';s=e;continue;}}
    if(!ass&&*s=='&'){static const struct {const char *from,*to;} entities[]={{"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},{"&apos;","'"},{"&#39;","'"},{"&nbsp;"," "}};int found=0;
      for(unsigned i=0;i<sizeof entities/sizeof *entities;i++){size_t m=strlen(entities[i].from);if(!strncmp(s,entities[i].from,m)){out[n++]=*entities[i].to;s+=m-1;found=1;break;}}if(found)continue;}
    out[n++]=*s;
  }out[n]=0;char *t=trim(out);if(t!=out)memmove(out,t,strlen(t)+1);
  /* Never leave a truncated UTF-8 sequence at the buffer boundary. */
  while(n&&((unsigned char)out[n-1]&0xc0)==0x80)n--;if(n&&((unsigned char)out[n-1]>=0xc0)&&!utf8Valid((unsigned char*)out,strlen(out)))out[n-1]=0;
}
static int parseSrt(char *body,LegendaCue **v){
  int n=0,cap=0,order=0,inCue=0,skip=0;LegendaCue c;char *cursor=body,*line;
  while((line=strsep(&cursor,"\n"))){char *t=trim(line);
    if(!*t){if(inCue&&!append(v,&n,&cap,&c))break;inCue=0;skip=0;continue;}
    if(!strncmp(t,"NOTE",4)||!strcmp(t,"STYLE")||!strcmp(t,"REGION")){skip=1;continue;}if(skip)continue;
    char *arrow=strstr(t,"-->");if(arrow){if(inCue&&!append(v,&n,&cap,&c))break;initCue(&c,order++);*arrow=0;c.inicio=timestamp(t);c.fim=timestamp(arrow+3);inCue=1;continue;}
    if(inCue){char text[768];cleanText(t,text,sizeof text,0);size_t used=strlen(c.texto);if(used&&used+1<sizeof c.texto)c.texto[used++]='\n';snprintf(c.texto+used,sizeof c.texto-used,"%s",text);}
  }if(inCue)append(v,&n,&cap,&c);return n;
}
typedef struct { char name[80]; short alignment,bold,italic; int color; } AssStyle;
static int bgrColor(const char *s){while(*s=='&'||*s=='H'||*s=='h')s++;unsigned bgr=(unsigned)strtoul(s,NULL,16);return (int)(((bgr&255)<<16)|(bgr&0xff00)|((bgr>>16)&255));}
static int parseAss(char *body,LegendaCue **v){
  int n=0,cap=0,order=0,start=1,end=2,text=9,fields=10,events=0;float rx=0,ry=0;
  AssStyle styles[128];int ns=0,styleSection=0,styleColumns=0;
  int styleName=0,styleColor=3,styleBold=7,styleItalic=8,styleAlignment=18,eventStyle=3;
  char *cursor=body,*line;
  while((line=strsep(&cursor,"\n"))){char *t=trim(line);
    if(*t=='['){events=!strcasecmp(t,"[Events]");styleSection=!strcasecmp(t,"[V4+ Styles]")||!strcasecmp(t,"[V4 Styles]");continue;}
    if(styleSection&&!strncasecmp(t,"Format:",7)){char *p=t+7,*f;styleColumns=0;
      while((f=strsep(&p,","))&&styleColumns<32){f=trim(f);if(!strcasecmp(f,"Name"))styleName=styleColumns;else if(!strcasecmp(f,"PrimaryColour"))styleColor=styleColumns;else if(!strcasecmp(f,"Bold"))styleBold=styleColumns;else if(!strcasecmp(f,"Italic"))styleItalic=styleColumns;else if(!strcasecmp(f,"Alignment"))styleAlignment=styleColumns;styleColumns++;}continue;}
    if(styleSection&&!strncasecmp(t,"Style:",6)&&ns<128){char *p=t+6,*part[32];int k=0;
      while(p&&k<32){part[k++]=trim(strsep(&p,","));}
      if(styleName<k&&styleColor<k&&styleAlignment<k){AssStyle *style=&styles[ns++];memset(style,0,sizeof *style);snprintf(style->name,sizeof style->name,"%s",part[styleName]);style->color=bgrColor(part[styleColor]);style->alignment=(short)atoi(part[styleAlignment]);style->bold=styleBold<k&&atoi(part[styleBold])!=0;style->italic=styleItalic<k&&atoi(part[styleItalic])!=0;}continue;}

    if(!strncasecmp(t,"PlayResX:",9))rx=(float)atof(t+9);if(!strncasecmp(t,"PlayResY:",9))ry=(float)atof(t+9);
    if(events&&!strncasecmp(t,"Format:",7)){char *p=t+7,*f;fields=0;while((f=strsep(&p,","))&&fields<32){f=trim(f);if(!strcasecmp(f,"Start"))start=fields;if(!strcasecmp(f,"End"))end=fields;if(!strcasecmp(f,"Text"))text=fields;if(!strcasecmp(f,"Style"))eventStyle=fields;fields++;}continue;}
    if(strncasecmp(t,"Dialogue:",9))continue;
    char *part[32]={0},*p=t+9;int got=0;while(got<fields&&got<32){part[got++]=p;if(got==fields)break;char *comma=strchr(p,',');if(!comma)break;*comma=0;p=comma+1;}
    if(start>=got||end>=got||text>=got)continue;LegendaCue c;initCue(&c,order++);c.inicio=timestamp(part[start]);c.fim=timestamp(part[end]);c.resX=rx;c.resY=ry;
    if(eventStyle<got)for(int i=0;i<ns;i++)if(!strcmp(styles[i].name,trim(part[eventStyle]))){c.an=styles[i].alignment;c.cor=styles[i].color;c.negrito=styles[i].bold;c.italico=styles[i].italic;break;}
    const char *over=part[text],*tag;
    if((tag=strstr(over,"\\an")))c.an=(short)atoi(tag+3);
    else if((tag=strstr(over,"\\a"))&&isdigit((unsigned char)tag[2])){int a=atoi(tag+2);c.an=(short)(a>=9?a-5:a>=5?a+2:a);}
    if((tag=strstr(over,"\\pos(")))sscanf(tag+5,"%f,%f",&c.posX,&c.posY);
    if((tag=strstr(over,"\\b"))&&isdigit((unsigned char)tag[2]))c.negrito=(short)(atoi(tag+2)!=0);
    if((tag=strstr(over,"\\i"))&&isdigit((unsigned char)tag[2]))c.italico=(short)(atoi(tag+2)!=0);
    if((tag=strstr(over,"\\c&H"))||(tag=strstr(over,"\\1c&H"))){const char *h=strstr(tag,"&H");unsigned bgr=(unsigned)strtoul(h+2,NULL,16);c.cor=(int)(((bgr&255)<<16)|(bgr&0xff00)|((bgr>>16)&255));}
    cleanText(over,c.texto,sizeof c.texto,1);if(!append(v,&n,&cap,&c))break;
  }return n;
}
int legenda_eh_ass(const char *body){
  if(!body)return 0;
  for(const char *line=body;*line;){while(*line==' '||*line=='\t')line++;
    if(!strncasecmp(line,"[Events]",8)||!strncasecmp(line,"[Script Info]",13)||!strncasecmp(line,"Dialogue:",9))return 1;
    const char *end=strchr(line,'\n');if(!end)break;line=end+1;
  }return 0;
}
static int extract(const char *body,LegendaCue **out,int ass){
  *out=NULL;if(!body)return 0;size_t n=strnlen(body,DOCUMENT_LIMIT+1);if(n>DOCUMENT_LIMIT)return 0;char *copy=legenda_decodificar(body,n);if(!copy)return 0;
  int count=ass?parseAss(copy,out):parseSrt(copy,out);free(copy);if(count>1)qsort(*out,count,sizeof **out,compare);return count;
}
int legenda_extrair_srt(const char *body,LegendaCue **out){return extract(body,out,0);}
int legenda_extrair_ass(const char *body,LegendaCue **out){return extract(body,out,1);}
int legenda_extrair(const char *body,LegendaCue **out){return extract(body,out,legenda_eh_ass(body));}
static Document *document(char *utf8){
  if(!utf8)return NULL;Document *d=calloc(1,sizeof *d);if(!d){free(utf8);return NULL;}d->body=utf8;d->ass=legenda_eh_ass(utf8);d->n=legenda_extrair(utf8,&d->cue);
  if(d->n){d->ends=malloc((size_t)d->n*sizeof *d->ends);if(!d->ends){destroy(d);return NULL;}double end=0;for(int i=0;i<d->n;i++){if(d->cue[i].fim>end)end=d->cue[i].fim;d->ends[i]=end;}}
  return d;
}
static unsigned publish(Document *d,unsigned owner,int newOwner){
  pthread_mutex_lock(&lock);if(owner!=generation||(!newOwner&&state==LEG_OFF)){pthread_mutex_unlock(&lock);destroy(d);return 0;}
  if(newOwner)generation++;Document *old=active;active=d;state=d&&(d->n||d->ass)?LEG_READY:LEG_ERROR;rendererPending=1;unsigned result=generation;
  pthread_mutex_unlock(&lock);destroy(old);return result;
}
unsigned legenda_geracao(void){pthread_mutex_lock(&lock);unsigned g=generation;pthread_mutex_unlock(&lock);return g;}
int legenda_estado(void){pthread_mutex_lock(&lock);int s=state;pthread_mutex_unlock(&lock);return s;}
int legenda_ligada_em(unsigned g){pthread_mutex_lock(&lock);int yes=g==generation&&state!=LEG_OFF;pthread_mutex_unlock(&lock);return yes;}
void legenda_desligar(void){pthread_mutex_lock(&lock);generation++;Document *old=active;active=NULL;state=LEG_OFF;rendererPending=1;pthread_mutex_unlock(&lock);destroy(old);assrender_geracao(legenda_geracao());}
static unsigned begin(void){legenda_desligar();pthread_mutex_lock(&lock);state=LEG_LOADING;unsigned g=generation;pthread_mutex_unlock(&lock);return g;}
unsigned legenda_definir_corpo_se(const char *body,unsigned owner){Document *d=document(body?legenda_decodificar(body,strlen(body)):NULL);return publish(d,owner,1);}
int legenda_atualizar_corpo_se(const char *body,unsigned owner){Document *d=document(body?legenda_decodificar(body,strlen(body)):NULL);return publish(d,owner,0)!=0;}
int legenda_instalar_janela(const char *body,unsigned owner){
  Document *d=document(body?legenda_decodificar(body,strlen(body)):NULL);
  if(!d)return 0;
  pthread_mutex_lock(&lock);
  if(owner!=generation){pthread_mutex_unlock(&lock);destroy(d);return 0;}
  Document *old=active;active=d;state=LEG_READY;rendererPending=1;
  pthread_mutex_unlock(&lock);destroy(old);return 1;
}
void legenda_definir_corpo(const char *body){unsigned g=begin();legenda_definir_corpo_se(body,g);}
void legenda_atualizar_corpo(const char *body){unsigned g=legenda_geracao();legenda_atualizar_corpo_se(body,g);}
/* Rendering installation is confined to the player thread. */
void legenda_bombear(void){
  pthread_mutex_lock(&lock);if(!rendererPending){pthread_mutex_unlock(&lock);return;}rendererPending=0;unsigned g=generation;
  if(rendererGeneration!=g)assrender_geracao(g);
  if(active&&active->ass){if(rendererGeneration==g)assrender_atualizar(active->body,strlen(active->body),g);else assrender_carregar(active->body,strlen(active->body),g);}
  else assrender_limpar();rendererGeneration=g;pthread_mutex_unlock(&lock);
}
typedef struct{unsigned owner;char url[4096];} Request;
static void *download(void *p){Request *r=p;RedeControle ctl={.max_bytes=DOCUMENT_LIMIT};long n=0;RedeMedida measure;
  char *bytes=rede_baixar_bin_medido_controle(r->url,15,NULL,&ctl,&n,&measure);char *utf8=bytes?legenda_decodificar(bytes,(size_t)n):NULL;free(bytes);
  Document *d=document(utf8);publish(d,r->owner,0);free(r);return NULL;
}
void legenda_carregar(const char *url){unsigned g=begin();Request *r=calloc(1,sizeof *r);if(!r||!url||!*url){free(r);publish(NULL,g,0);return;}r->owner=g;snprintf(r->url,sizeof r->url,"%s",url);pthread_t t;if(pthread_create(&t,NULL,download,r)){free(r);publish(NULL,g,0);}else pthread_detach(t);}
static int upper(double t){int lo=0,hi=active?active->n:0;while(lo<hi){int m=lo+(hi-lo)/2;if(active->cue[m].inicio<=t)lo=m+1;else hi=m;}return lo;}
int legenda_cues(double pos,int delay,LegendaCue *out,int max){
  if(!out||max<=0||!isfinite(pos))return 0;double t=pos-delay/1000.0;int n=0;
  pthread_mutex_lock(&lock);if(active&&state==LEG_READY){int end=upper(t),lo=0,hi=end;while(lo<hi){int m=lo+(hi-lo)/2;if(active->ends[m]<=t)lo=m+1;else hi=m;}
    for(int i=lo;i<end;i++)if(active->cue[i].fim>t){LegendaCue c=active->cue[i];int at=n;if(at>=max)at=max-1;while(at>0&&out[at-1].ordem>c.ordem){if(at<max)out[at]=out[at-1];at--;}if(n<max||at<max-1){out[at]=c;if(n<max)n++;}}
  }pthread_mutex_unlock(&lock);return n;
}
int legenda_texto(double pos,int delay,char *out,size_t cap){if(!out||!cap)return 0;out[0]=0;LegendaCue c;if(!legenda_cues(pos,delay,&c,1))return 0;snprintf(out,cap,"%s",c.texto);return 1;}
int legenda_falas(double pos,int offset,LegendaCue *out,int max,int *focus,int *first,int *total){
  if(focus)*focus=-1;if(first)*first=0;if(total)*total=0;if(!out||max<=0)return 0;
  pthread_mutex_lock(&lock);int n=active?active->n:0;if(total)*total=n;if(!n){pthread_mutex_unlock(&lock);return 0;}int at=upper(pos)-1;if(at<0)at=0;if(at<n-1&&pos>=active->cue[at].fim&&fabs(active->cue[at+1].inicio-pos)<fabs(pos-active->cue[at].fim))at++;
  at+=offset;if(at<0)at=0;if(at>=n)at=n-1;int start=at-max/2;if(start<0)start=0;if(start+max>n)start=n>max?n-max:0;int k=n-start;if(k>max)k=max;memcpy(out,active->cue+start,(size_t)k*sizeof *out);
  if(focus)*focus=at-start;if(first)*first=start;pthread_mutex_unlock(&lock);return k;
}
