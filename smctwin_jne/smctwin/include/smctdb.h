#ifndef __SmctDbH__
#define __SmctDbH__

typedef struct _tDbLib tDbLib;

typedef void*	tDbConn;
typedef void*	tDbSet;

struct _tDbLib{
	/* connection */
	tDbConn (*openConnection) 	(char *host, char *user, char *passwd, char *dbname, int port);
	void    (*closeConnection)	(tDbConn conn);
	void    (*isConnected)	    (tDbConn conn);
	/* query */
	tDbSet  (*openQuery) 	(tDbConn hConn, tDbSet hDbSet, char *sql, unsigned long len);
	int  		(*execQuery) 	(tDbConn hConn, char *sql, unsigned long len);
	void 		(*closeQuery)	(tDbSet hDbSet);
	/* dataset */
	int 		(*isEof)			(tDbSet hDbSet);	
	int 		(*nextRow)		(tDbSet hDbSet);
	int 		(*getIntFieldByIdx)	(tDbSet hDbSet, int nField);
  int 		(*getIntFieldByName)(tDbSet hDbSet, const char* szField);
  const char* (*getStringFieldByIdx)	(tDbSet hDbSet, int nField);
  const char* (*getStringFieldByName)	(tDbSet hDbSet, const char* szField);
  int 		(*isFieldNullByIdx)	(tDbSet hDbSet, int nField);
  int 		(*isFieldNullByName)(tDbSet hDbSet, const char* szField);
};


DllExport tDbLib *smctdbInit(char *dbName);
DllExport void smctdbDestroy(void);

#endif//__SmctDbH__
