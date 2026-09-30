#ifndef BN_SUBTITLE_CATALOG_H
#define BN_SUBTITLE_CATALOG_H
#include "addons.h"
#include "js.h"
#include "linguas.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The Enhanced groups human labels (e.g. Make Portuguese (Brazilian)) by
 * their language, then sorts options by the provider's subtitle ID. */
static inline void subtitle_language(const char *raw,const char *id,char *out,size_t cap) {
  static const struct {const char *name,*code;} names[]={
    {"brazilian","pt-br"},{"brazil","pt-br"},{"brasil","pt-br"},
    {"portugal","pt-pt"},{"portuguese","pt"},{"portugues","pt"},{"português","pt"},
    {"english","en"},{"spanish","es"},{"espanol","es"},{"español","es"},{"italian","it"},
    {"french","fr"},{"german","de"},{"japanese","ja"},{"korean","ko"},
    {"chinese","zh"},{"russian","ru"},{"arabic","ar"},{"hindi","hi"},
    {"dutch","nl"},{"swedish","sv"},{"norwegian","no"},{"danish","da"},
    {"finnish","fi"},{"polish","pl"},{"turkish","tr"},{"hebrew","he"},
    {"thai","th"},{"czech","cs"},{"greek","el"},{"hungarian","hu"},
    {"romanian","ro"},{"ukrainian","uk"},{"vietnamese","vi"},
    {"indonesian","id"},{"indonesia","id"},{"malay","ms"}
  };
  char text[256];size_t n=0;
  for(;raw && raw[n] && n+1<sizeof text;n++)text[n]=(char)tolower((unsigned char)raw[n]);
  text[n]=0;
  const char *code=NULL;
  for(size_t i=0;i<sizeof names/sizeof *names;i++) {
    const char *p=text;size_t len=strlen(names[i].name);
    while((p=strstr(p,names[i].name))) {
      if((p==text || !isalnum((unsigned char)p[-1])) && !isalnum((unsigned char)p[len])) {code=names[i].code;break;}
      p++;
    }
    if(code)break;
  }
  if(!code) {
    int simple=n>0 && n<16;
    for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)text[i]) && text[i]!='-' && text[i]!='_')simple=0;
    if(simple && strcasecmp(text,"unknown") && strcasecmp(text,"unk") && strcasecmp(text,"und")) {
      const char *candidate=ling_grupo_codigo(text);
      /* An arbitrary provider action is not a language code. */
      if(strlen(candidate)==2 || !strcmp(candidate,"pt-br") || !strcmp(candidate,"pt-pt"))code=candidate;
    }
  }
  if(code && !strcmp(code,"pt") && id && (strstr(id,"_pob") || strstr(id,"pt-BR") || strstr(id,"pt-br")))code="pt-br";
  snprintf(out,cap,"%s",code?code:"und");
}
static inline const char *subtitle_metadata(const Legenda *l) {
  char normalized[16];subtitle_language(l->id,NULL,normalized,sizeof normalized);
  return strcmp(normalized,"und") && !strcmp(normalized,l->idioma)?"":l->id;
}
static inline int subtitle_option_compare(const void *a,const void *b) {
  const Legenda *x=a,*y=b;
  return strcasecmp(subtitle_metadata(x),subtitle_metadata(y));
}
/* HTTP header dictionaries use provider-defined keys. Preserve either shape
 * used by the Enhanced, without allowing a value to add a second header. */
static inline void subtitle_headers(const char *obj,const char *end,char *out,size_t cap) {
  char raw[4096];out[0]=0;
  if(!js_bruto(obj,end,"headers",raw,sizeof raw) &&
     !js_bruto(obj,end,"request",raw,sizeof raw))return;
  const char *p=strchr(raw,'{');if(!p)return;p++;
  size_t used=0;
  while(*p) {
    while(*p && (*p==' '||*p=='\r'||*p=='\n'||*p=='\t'||*p==','))p++;
    if(*p!='"')break;
    const char *key=++p;while(*p && *p!='"')p++;
    size_t len=(size_t)(p-key);char name[96],value[2048];
    if(*p!='"'||!len||len>=sizeof name)break;
    memcpy(name,key,len);name[len]=0;
    for(size_t i=0;i<len;i++)if(!isalnum((unsigned char)name[i]) && name[i]!='-')return;
    if(!js_texto_linhas(raw,NULL,name,value,sizeof value) || strpbrk(value,"\r\n"))return;
    int n=snprintf(out+used,cap-used,"%s%s: %s",used?"\n":"",name,value);
    if(n<0 || (size_t)n>=cap-used){out[used]=0;return;}used+=(size_t)n;
    p++;while(*p && *p!=':')p++;if(!*p)break;p++;
    while(*p && isspace((unsigned char)*p))p++;if(*p!='"')break;p++;
    while(*p && *p!='"'){if(*p=='\\' && p[1])p++;p++;}if(*p)p++;
  }
}
static inline void subtitle_url_encode(const char *s,char *out,size_t cap) {
  static const char hex[]="0123456789ABCDEF";size_t n=0;
  for(;s && *s;s++) {
    unsigned char c=(unsigned char)*s;
    if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~'){if(n+1>=cap)break;out[n++]=c;}
    else {if(n+3>=cap)break;out[n++]='%';out[n++]=hex[c>>4];out[n++]=hex[c&15];}
  }
  out[n]=0;
}
#endif
