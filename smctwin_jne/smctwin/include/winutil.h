#ifndef _WinUtilH_
#define _WinUtilH_

#ifdef WIN32

#define strcasecmp _stricmp
#define DllImport   __declspec( dllimport )
#define DllExport   __declspec( dllexport )

#endif

#endif//_WinUtilH_