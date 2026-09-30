#ifndef NV_SYNCPROG_H
#define NV_SYNCPROG_H
#include "vistoep.h"
/* Profile-captured RPC transport. Pull publishes a snapshot for the UI thread;
 * push acknowledges precisely the revisions sent, including durable deletes. */
int syncprog_puxar(void);
int syncprog_empurrar(void);
int syncprog_aplicar(int *casaram);
int syncprog_puxadas(void);
int syncprog_remover(const char *chave);
void syncprog_esquecer(void);
int syncep_empurrar(const char *imdb,const char *tipo,const VistoPar *pares,int qtd,int visto);
int syncvisto_titulo(const char *imdb,const char *tipo,int visto);
#endif
