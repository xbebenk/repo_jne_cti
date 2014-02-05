#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <process.h>
#include "osdep.h"



#include "include\smct.h"
#include "ctblib_message.h"
#include "include/appsvr.h"

#pragma warning(disable : 4996)  // deprecated CRT function

#define DEFAULT_LOG_FILE	"smartappsvr"
//#define DEFAULT_LOG_FILE	"monitor"
#define DB_LOG_FILE	"cti_db"
#ifdef WIN32
#define DEFAULT_LOG_DIR   "C:\\smartcenter\\log"
#else
	#define DEFAULT_LOG_DIR   "/opt/smartcenter/log"
#endif


typedef struct _t_LoggerPvt{
	/* CTI information */
	char ctiIp[16];
	int  ctiUdpPort;
	
	int listenPort;
	int listenSock;
	
	int udpSock;
	int udpPort;
	pthread_t mainThreadId;
}_t_LoggerPvt;


//static char dateFormat[256] = "%T";
static char dateFormat[256] = "%H:%M:%S";
static CRITICAL_SECTION logLock;
static FILE *logfile = NULL;
static FILE *dblogfile = NULL;
static int currentDay;

static int logger_Init(){
	char tmp[256];	
	time_t t;
	struct tm tm;
	char date[256];

	InitializeCriticalSection(&logLock);
	
	if(appContext->isLogged){		
		time(&t);
		localtime_s(&tm, &t);
		currentDay = tm.tm_mday;
		strftime(date, sizeof(date), "%Y%m%d", &tm);		
		
		_snprintf(tmp, sizeof(tmp), "%s\\%s_%s.log", DEFAULT_LOG_DIR, DEFAULT_LOG_FILE, date);		
		logfile = fopen((char *)tmp, "a");
		if (!logfile)
			printf("error opening %s\n", tmp);
	}else{		
		logfile = stderr;
	}
	return 0;
}

static int dblogger_Init(){
	char tmp[256];	
	time_t t;
	struct tm tm;
	char date[256];

	InitializeCriticalSection(&logLock);
	
	if(appContext->isLogged){		
		time(&t);
		localtime_s(&tm, &t);
		currentDay = tm.tm_mday;
		strftime(date, sizeof(date), "%Y%m%d", &tm);		
		
		_snprintf(tmp, sizeof(tmp), "%s\\%s_%s.log", DEFAULT_LOG_DIR, DB_LOG_FILE, date);		
		dblogfile = fopen((char *)tmp, "a");
		if (!dblogfile)
			printf("error opening %s\n", tmp);
	}else{		
		dblogfile = stderr;
	}
	return 0;
}

void logger_Print(int logmodule, int loglevel, const char *fmt, ...){
	time_t t;
	struct tm tm;
	struct timeval stimeval;
	char date[256];	
	char mm[6];
	int buflen=10;
	va_list ap;
	
	pthread_mutex_lock(&logLock);	
	
	if(logfile){
		
		time(&t);
		localtime_s(&tm, &t);
		strftime(date, sizeof(date), dateFormat, &tm);

		gettimeofday(&stimeval, NULL);	
		_snprintf(mm, buflen, "%04d", stimeval.tv_usec);
		if(appContext->loglevel >= loglevel){
			if (appContext->isLogged && tm.tm_mday != currentDay){
				char tmp[256];	
				/* different day, change file */
				fclose(logfile);
				currentDay = tm.tm_mday;
				strftime(date, sizeof(date), "%Y%m%d", &tm);			
				_snprintf(tmp, sizeof(tmp), "%s/%s_%s.log", DEFAULT_LOG_DIR, DEFAULT_LOG_FILE, date);
				logfile = fopen((char *)tmp, "a");
				if (!logfile){
					pthread_mutex_unlock(&logLock);
					return;
				}
			}
			//setiap module diset loggernya jalan apa ngga
			if(logmodule == LOG_MODULE_ACD && appContext->log_module_acd == 0){
				pthread_mutex_unlock(&logLock);
				return;
			}
			if(logmodule == LOG_MODULE_AGENT && appContext->log_module_agent == 0){
				pthread_mutex_unlock(&logLock);
				return;
			}
			if(logmodule == LOG_MODULE_CTI && appContext->log_module_cti == 0){
				pthread_mutex_unlock(&logLock);
				return;
			}
			if(logmodule == LOG_MODULE_DB && appContext->log_module_db == 0){
				pthread_mutex_unlock(&logLock);
				return;
			}
			if(logmodule == LOG_MODULE_MANAGER && appContext->log_module_manager == 0){
				pthread_mutex_unlock(&logLock);
				return;
			}
			
			va_start(ap, fmt);
			fprintf(logfile, "%s.%s : ", date,mm);
			vfprintf(logfile, fmt, ap);
			fflush(logfile);
			va_end(ap);
			pthread_mutex_unlock(&logLock);
			return;
		}
	}
	pthread_mutex_unlock(&logLock);
}

void db_logger_Print(const char *fmt, ...){
	time_t t;
	struct tm tm;
	char date[256];	
	va_list ap;
	
	pthread_mutex_lock(&logLock);	
	
	if(dblogfile){
		time(&t);
		localtime_s(&tm, &t);
		strftime(date, sizeof(date), dateFormat, &tm);
			if (appContext->isLogged && tm.tm_mday != currentDay){
				char tmp[256];	
				/* different day, change file */
				fclose(dblogfile);
				currentDay = tm.tm_mday;
				strftime(date, sizeof(date), "%y%m%d", &tm);			
				_snprintf(tmp, sizeof(tmp), "%s/%s-%s.log", DEFAULT_LOG_DIR, DB_LOG_FILE, date);
				dblogfile = fopen((char *)tmp, "a");
				if (!dblogfile){
					pthread_mutex_unlock(&logLock);
					return;
				}
			}

			va_start(ap, fmt);
			fprintf(dblogfile, "%s [%d]: ", date, _getpid());
			vfprintf(dblogfile, fmt, ap);
			fflush(dblogfile);
			va_end(ap);
			pthread_mutex_unlock(&logLock);
			return;
	}
	pthread_mutex_unlock(&logLock);
}

void logger_Error(const char *fmt, ...){
	time_t t;
	struct tm tm;
	char date[256];
	va_list ap;
	
	pthread_mutex_lock(&logLock);

	time(&t);
	localtime_s(&tm, &t);
	strftime(date, sizeof(date), dateFormat, &tm);

	if(logfile != NULL){		
		if (appContext->isLogged && tm.tm_mday != currentDay){
			char tmp[256];	
			/* different day, change file */
			fclose(logfile);
			currentDay = tm.tm_mday;
			strftime(date, sizeof(date), "%y%m%d", &tm);			
			_snprintf(tmp, sizeof(tmp), "%s/%s-%s.log", DEFAULT_LOG_DIR, DEFAULT_LOG_FILE, date);
			logfile = fopen((char *)tmp, "a");
			if (!logfile){
				pthread_mutex_unlock(&logLock);
				return;
			}
		}
		
		
		va_start(ap, fmt);
		fprintf(logfile, "%s [%d]: ", date, _getpid());
		vfprintf(logfile, fmt, ap);
		fflush(logfile);
		va_end(ap);
		pthread_mutex_unlock(&logLock);
		return;
	}
	
	va_start(ap, fmt);	
	vfprintf(stderr, fmt, ap);
	fflush(stderr);
	va_end(ap);
	pthread_mutex_unlock(&logLock);
	
}

void logger_Debug(int level){
}

void logger_Dump(char *buf, int len){
	time_t t;
	struct tm tm;
	char date[256];
	int i;
	
	pthread_mutex_lock(&logLock);
	time(&t);
	localtime_s(&tm, &t);
	strftime(date, sizeof(date), dateFormat, &tm);

	if(logfile){
		if (appContext->isLogged && tm.tm_mday != currentDay){
			char tmp[256];	
			/* different day, change file */
			fclose(logfile);
			currentDay = tm.tm_mday;
			strftime(date, sizeof(date), "%y%m%d", &tm);			
			_snprintf(tmp, sizeof(tmp), "%s/%s-%s.log", DEFAULT_LOG_DIR, DEFAULT_LOG_FILE, date);
			logfile = fopen((char *)tmp, "a");
			if (!logfile){
				pthread_mutex_unlock(&logLock);
				return;
			}
		}
		
		fprintf(logfile, "%s smartappsvr[%d]: length=%d byte(s)\n", date, getpid(), len);
		for (i=0; i < len; i++){
			if (i && ((i%20) == 0))
      	fprintf(logfile, "\n");
			fprintf(logfile, "%02x ",(unsigned char)buf[i]);
    }
		fprintf(logfile, "\n------\n");
		
	}	
	pthread_mutex_unlock(&logLock);
}

int logger_Load(){
	logger_Init();
	//dblogger_Init();
	return 0;
}
