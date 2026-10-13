#ifndef NV_MEDIA_CLOCK_H
#define NV_MEDIA_CLOCK_H
/* Backend clock: timestamped media samples, bounded extrapolation, no
 * smoothing or accumulated correction across a seek. */
void media_clock_sample(double position,double now,int running);
void media_clock_running(double now,int running);
double media_clock_read(double now);
#endif
