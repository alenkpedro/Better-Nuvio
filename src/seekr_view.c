#include "seekr_view.h"
#include "layout.h"
#include "text.h"
#include <math.h>
#include <stdio.h>

GfxRect seekr_view_trilho(float y){return (GfxRect){224.0f,y,NV_TELA_W-448.0f,6.0f};}
static void tempo(char *dest,size_t n,double segundos) {
  int s=(int)fmax(0,fmin(segundos,86400));
  if(s>=3600)snprintf(dest,n,"%d:%02d:%02d",s/3600,s/60%60,s%60);
  else snprintf(dest,n,"%02d:%02d",s/60,s%60);
}
void seekr_view_desenhar(const SeekrPrevia q[SEEKR_PREVIAS],double pos,double dur,
                        float y,float alpha,float r,float g,float b) {
  const float largura=480.0f,altura=270.0f,passo=490.0f;
  float topo=y-altura-90.0f;
  // Os cartoes de fora continuam alem da tela, como uma faixa de filme.
  // O framebuffer corta suas bordas sem alterar a proporcao das imagens.
  for(int k=0;k<SEEKR_PREVIAS;k++) {
    if(!q[k].valido)continue;
    int centro=k==SEEKR_PREVIAS/2;
    float w=centro?largura:largura-12.0f,h=w*9.0f/16.0f;
    GfxRect tile={NV_TELA_W*.5f+(k-SEEKR_PREVIAS/2)*passo-w*.5f,topo+(altura-h)*.5f,w,h};
    if(q[k].tex) {
      gfx_tex_aspect_atual=q[k].aspecto;
      gfx_rect(tile,q[k].tex,GFX_CARD,0,0,0,14.0f/h,1,1,1,alpha*(centro?1.0f:.86f));
      gfx_tex_aspect_atual=0;
    } else gfx_cor(tile,14.0f/h,.11f,.12f,.14f,.90f*alpha);
    if(centro)gfx_anel_fora(tile,14.0f/h,3.0f,3.0f,.98f,.98f,1.0f,alpha);
  }
  GfxRect trilho=seekr_view_trilho(y);
  gfx_cor(trilho,.5f,.88f,.92f,.97f,.92f*alpha);
  float fracao=dur>0?(float)fmax(0,fmin(pos/dur,1)):0;
  if(fracao>0)gfx_cor((GfxRect){trilho.x,trilho.y,trilho.w*fracao,trilho.h},.5f,r,g,b,alpha);
  char atual[24],total[24];tempo(atual,sizeof atual,pos);tempo(total,sizeof total,dur);
  TxtLinha esq=txt_linha(TXT_PG_RELOGIO,atual,244,248,252,255);
  TxtLinha dir=txt_linha(TXT_PG_RELOGIO,total,244,248,252,255);
  txt_desenhar_alpha(esq,trilho.x-28.0f-esq.w,y+(trilho.h-esq.h)*.5f,alpha);
  txt_desenhar_alpha(dir,trilho.x+trilho.w+28.0f,y+(trilho.h-dir.h)*.5f,alpha);
}
