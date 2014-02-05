#ifndef _AppsvrLoggerH_
#define _AppsvrLoggerH_

#define LOG_MODULE_ACD			1
#define LOG_MODULE_AGENT		2
#define LOG_MODULE_CTI			3
#define LOG_MODULE_DB			4
#define LOG_MODULE_MANAGER		5


int logger_Load();
void logger_Print(int logmodule, int loglevel, const char *fmt, ...);
void logger_Error(const char *fmt, ...);
void logger_Dump(char *buf, int len);

#endif//_AppsvrLoggerH_

