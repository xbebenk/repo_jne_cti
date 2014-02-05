#ifndef __SmctH__
#define __SmctH__

#undef UNICODE

#ifdef WIN32
	#define SMCT_MODULE_PATH	"c:\\smartcenter\\shared\\modules"
#else
	#define SMCT_MODULE_PATH	"/usr/lib/smartcenter/modules/"
#endif

#ifdef __cplusplus 
	extern "C" {     
#endif

#ifdef WIN32

#include "winutil.h"

#endif



#include "smctcommon.h"
#include "smctdb.h"
#include "smctini.h"
#include "smctmessage.h"

DllExport char* libsmctGetVersion(void);
DllExport char* libsmctGetVersionExtra(void);

#ifdef __cplusplus
	}
#endif

#endif//__SmctH__
