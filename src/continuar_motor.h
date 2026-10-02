#ifndef NV_CONTINUAR_MOTOR_H
#define NV_CONTINUAR_MOTOR_H
#include "progresso.h"
/* Inputs are snapshots. Metadata enriches an identity; it never selects it. */
enum { CW_CONTA=0, CW_TRAKT=1, CW_SIMKL=2 };
typedef struct { ProgRegistro progresso; int fonte, seguinte; } CwEstado;
/* Latest state for each exact content identity, then recent first.
 * Completed series are returned as seeds for explicit next-episode lookup. */
int cw_selecionar(const CwEstado *inputs,int n,int *indices,int max);
int cw_estado_visivel(const CwEstado *input);
#endif
