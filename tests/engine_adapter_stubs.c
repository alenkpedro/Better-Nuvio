/* Link adapters for catalog/metadata unit tests that deliberately omit the
 * watch repository/service. Real engine integration tests link their engines. */
#include "progresso.h"
#include "watch_service.h"
__attribute__((weak)) int cw_service_ready(int p,unsigned v){(void)p;(void)v;return 0;}
__attribute__((weak)) void cw_service_refresh(void){}
__attribute__((weak)) int cw_service_remove(const char *id,int s,int e){(void)id;(void)s;(void)e;return 0;}
__attribute__((weak)) void cw_service_reset(void){}
__attribute__((weak)) int cw_service_snapshot(CatItem *out,int max){(void)out;(void)max;return 0;}
__attribute__((weak)) double prog_fracao(const ProgRegistro *r){
 if(!r)return 0;double x=r->durSeg>0?r->posSeg/r->durSeg:r->percentual>=0?r->percentual/100:0;
 return x<0?0:x>1?1:x;
}
__attribute__((weak)) int prog_gravar_registro_local(const ProgRegistro *r){(void)r;return 0;}
