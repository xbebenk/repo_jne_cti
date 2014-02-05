#ifndef _OsDepH_
#define _OsDepH_

/**
 OS dependent library header
 */

#ifdef WIN32

struct timezone{
	int tz_minuteswest;
	int	tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);

#endif


#endif//_OsDepH_
