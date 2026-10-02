#include "continuar_motor.h"
#include <string.h>
#include <stdlib.h>
int cw_estado_visivel(const CwEstado *e) {
  if(!e || !e->progresso.contentId[0] || e->progresso.removido)return 0;
  if(e->seguinte)return e->progresso.episodio>0;
  double f=prog_fracao(&e->progresso),end=e->fonte==CW_SIMKL?.80:.90;
  if(e->progresso.durSeg>0 && e->progresso.durSeg<60)return 0;
  return f>=.02 && f<end;
}
int cw_selecionar(const CwEstado *v,int n,int *out,int max) {
  int *latest=calloc(n?n:1,sizeof *latest),count=0,k=0;
  if(!latest)return 0;
  for(int i=0;i<n;i++) {
    const ProgRegistro *r=&v[i].progresso;
    if(!r->contentId[0])continue;
    int j;for(j=0;j<count;j++)if(!strcmp(v[latest[j]].progresso.contentId,r->contentId) && !strcmp(v[latest[j]].progresso.tipo,r->tipo))break;
    if(j==count)latest[count++]=i;
    else {
      const CwEstado *old=&v[latest[j]];
      if(r->lastWatchedMs>old->progresso.lastWatchedMs || (r->lastWatchedMs==old->progresso.lastWatchedMs && v[i].fonte<old->fonte))latest[j]=i;
    }
  }
  for(int i=1;i<count;i++) {
    int x=latest[i],j=i;
    while(j>0 && v[x].progresso.lastWatchedMs>v[latest[j-1]].progresso.lastWatchedMs){latest[j]=latest[j-1];j--;}
    latest[j]=x;
  }
  for(int i=0;i<count && k<max;i++) {
    const CwEstado *e=&v[latest[i]];
    int seed=e->fonte==CW_CONTA && e->progresso.episodio>0 && prog_concluido(&e->progresso);
    if(cw_estado_visivel(e)||seed)out[k++]=latest[i];
  }
  free(latest);return k;
}
