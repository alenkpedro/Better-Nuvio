#ifndef BN_WATCH_SERVICE_H
#define BN_WATCH_SERVICE_H
#include "catalogo.h"
/* Account/provider snapshots and display enrichment are independent of
 * discovery. Only pump() publishes to the UI, on the application thread. */
void cw_service_refresh(void);
void cw_service_pump(int canPublish);
void cw_service_reset(void);
int cw_service_ready(int profile,unsigned addonsVersion);
/* out == NULL returns the complete snapshot count, for exact allocation. */
int cw_service_snapshot(CatItem *out,int max);
int cw_service_remove(const char *id,int season,int episode);
int cw_service_build(CatItem *out,int max,unsigned char *localized);
#endif
