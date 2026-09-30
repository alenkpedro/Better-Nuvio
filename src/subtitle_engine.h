#ifndef BN_SUBTITLE_ENGINE_H
#define BN_SUBTITLE_ENGINE_H
/* Selection and window ownership, independent of the track-selection panel.
 * Embedded Android cues belong to Media3. webOS/Mac windows are extracted by
 * the unmodified official Nuvio service. All app cues use video media time. */
void subtitle_engine_reset(void);
void subtitle_engine_select(int embedded, const char *externalUrl);
void subtitle_engine_select_headers(int embedded,const char *externalUrl,const char *headers);
void subtitle_engine_tick(double mediaSeconds, int delayMs);
int subtitle_engine_selected(void);
int subtitle_engine_native(void);
int subtitle_engine_loading(void);
int subtitle_engine_take_error(void);
#endif
