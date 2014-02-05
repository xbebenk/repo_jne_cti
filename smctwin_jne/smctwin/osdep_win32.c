/**
 OS Dependent Library
 */

#include "osdep.h"
#include <windows.h>
#include <time.h>

int gettimeofday(struct timeval *tv, struct timezone *tz){
	SYSTEMTIME st;

	if (!tv)
		return -1;
	
	tv->tv_sec  = (long)time(NULL);
	GetSystemTime(&st);
	tv->tv_usec = st.wMilliseconds;

	return 0;
}