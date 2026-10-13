#include "media_clock.h"
#include <pthread.h>
#include <math.h>
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static double position,sampledAt;
static int moving;
static double readAt(double now){double dt=now-sampledAt;if(dt<0)dt=0;if(dt>.25)dt=.25;return position+(moving?dt:0);}
void media_clock_sample(double value,double now,int running){if(!isfinite(value)||value<0||!isfinite(now))return;pthread_mutex_lock(&mu);position=value;sampledAt=now;moving=running;pthread_mutex_unlock(&mu);}
void media_clock_running(double now,int running){pthread_mutex_lock(&mu);position=readAt(now);sampledAt=now;moving=running;pthread_mutex_unlock(&mu);}
double media_clock_read(double now){pthread_mutex_lock(&mu);double value=readAt(now);pthread_mutex_unlock(&mu);return value;}
