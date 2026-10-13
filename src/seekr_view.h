#ifndef NV_SEEKR_VIEW_H
#define NV_SEEKR_VIEW_H
#include "seekr.h"
#include "gfx.h"
GfxRect seekr_view_trilho(float y);
void seekr_view_desenhar(const SeekrPrevia quadros[SEEKR_PREVIAS],double posicao,
                        double duracao,float y,float alpha,float r,float g,float b);
#endif
