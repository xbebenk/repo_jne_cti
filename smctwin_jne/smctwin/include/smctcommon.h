#ifndef _SmctCommonH_
#define _SmctCommonH_

typedef signed char      INT8;
typedef signed short     INT16;

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;

#ifndef WIN32

typedef signed int       INT32;
typedef signed long long INT64;

typedef unsigned long      UINT32;
typedef unsigned long long UINT64;

#else
	#include <windows.h>
#endif//WIN32

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#ifndef MAX
#define MAX(a,b)  (a>b?a:b)
#endif//MAX

#endif//_SmctCommonH_
