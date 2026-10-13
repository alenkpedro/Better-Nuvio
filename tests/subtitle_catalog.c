/* Provider responses captured for the user's comparison, with private URLs
 * replaced. Runs the real async lookup and parser without account credentials. */
#include "../src/addons.c"
#include <assert.h>
#include <unistd.h>
static char *fixture[2];
static atomic_int calls,oldCalls;
static char queried[ADD_REQUEST_URL_MAX];
char *rede_baixar(const char *url,int seconds) {
  assert(seconds==20);atomic_fetch_add(&calls,1);
  if(strstr(url,"filename=old")){atomic_fetch_add(&oldCalls,1);usleep(80000);}
  int provider=strstr(url,"community.test")?0:1;
  if(provider==1)snprintf(queried,sizeof queried,"%s",url);
  return strdup(fixture[provider]);
}
static char *readFixture(const char *path) {
  FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
  char *s=calloc((size_t)n+1,1);assert(s);assert(fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;
}
static void waitReady(void){for(int n=0;n<3000 && !addons_legendas_prontas();n++)usleep(1000);assert(addons_legendas_prontas());}
int main(void) {
  fixture[0]=readFixture("tests/fixtures/subtitles-idiotas-community.json");
  fixture[1]=readFixture("tests/fixtures/subtitles-idiotas-submaker.json");
  nAddon=2;
  for(int i=0;i<2;i++){addon[i].ativo=addon[i].legenda=1;snprintf(addon[i].base,sizeof addon[i].base,"https://%s.test",i?"submaker":"community");snprintf(addon[i].nome,sizeof addon[i].nome,"%s",i?"SubMaker | ElfHosted":"Stremio Community Subtitles");}
  snprintf(addon[1].query,sizeof addon[1].query,"?configuration=fixture");
  addons_buscar_legendas_fonte("tt6628920","movie","Idiots (2026) & 1080p.mkv","a+b/123",7345678901ULL);
  waitReady();assert(addons_n_legendas()==32);int pt=0,unknown=0;
  for(int i=0;i<addons_n_legendas();i++){const Legenda *l=addons_legenda(i);if(!strcmp(l->idioma,"pt-br"))pt++;if(!strcmp(l->idioma,"und"))unknown++;assert(l->id[0] && l->provedor[0]);}
  assert(pt==17 && unknown==1);
  assert(strstr(queried,"/videoHash=a%2Bb%2F123&videoSize=7345678901&filename=Idiots%20%282026%29%20%26%201080p.mkv.json?configuration=fixture"));
  int before=atomic_load(&calls);addons_buscar_legendas("tt6628920","movie");assert(atomic_load(&calls)==before);
  addons_buscar_legendas_fonte("tt6628920","movie","old.mkv","",0);
  while(!atomic_load(&oldCalls))usleep(1000);
  addons_buscar_legendas_fonte("tt6628920","movie","new.mkv","",0);waitReady();assert(strstr(queried,"filename=new.mkv.json"));
  char *bulk=calloc(20000,1);assert(bulk);size_t used=snprintf(bulk,20000,"{\"subtitles\":[");
  for(int i=0;i<80;i++)used+=snprintf(bulk+used,20000-used,"%s{\"id\":\"bulk-%d\",\"lang\":\"Make Portuguese (Brazilian)\",\"url\":\"https://example.invalid/%d.srt\"}",i?",":"",i,i);
  snprintf(bulk+used,20000-used,"]}");free(fixture[1]);fixture[1]=bulk;
  addons_buscar_legendas_fonte("tt6628920","movie","next.mkv","",0);waitReady();assert(addons_n_legendas()==81);
  char headers[2048];subtitle_headers("{\"headers\":{\"Referer\":\"https://example.invalid/\",\"User-Agent\":\"fixture\\\" agent\"}}",NULL,headers,sizeof headers);
  assert(!strcmp(headers,"Referer: https://example.invalid/\nUser-Agent: fixture\" agent"));
  subtitle_headers("{\"behaviorHints\":{\"proxyHeaders\":{\"request\":{\"Origin\":\"https://example.invalid\"}}}}",NULL,headers,sizeof headers);assert(!strcmp(headers,"Origin: https://example.invalid"));
  pthread_join(fioLeg,NULL);free(fixture[0]);free(fixture[1]);
  puts("PASS: Enhanced provider IDs, 17 Portuguese options, unknown action, exact file hints, source generation, 80 options per provider, subtitle headers");return 0;
}
