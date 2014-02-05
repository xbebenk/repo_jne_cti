#ifndef _AppSvrH_
#define _AppSvrH_

//#define DEFAULT_CONFIGFILE "c:\\smartcenter\\cfg\\monitor.conf"
#define DEFAULT_CONFIGFILE "c:\\smartcenter\\cfg\\smartcenter.conf"

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

/* thread stuff*/
#ifdef WIN32

#include <windows.h>

	#define PTHREAD_CREATE_DETACHED     0
	#define pthread_t                   HANDLE
	#define pthread_attr_t              int
	#define pthread_mutex_t             CRITICAL_SECTION
	#define pthread_mutex_init(lock,a)  InitializeCriticalSection(lock)
	#define pthread_mutex_lock(lock)    EnterCriticalSection(lock)
	#define pthread_mutex_unlock(lock)  LeaveCriticalSection(lock)
	#define pthread_mutex_destroy(lock) DeleteCriticalSection(lock)

#else
	#include <pthread.h>
#endif

#include "smct.h"

typedef struct ServerContext{
	int listenSock;
}ServerContext;


typedef struct ApplicationContext{
	/* db connection*/
	char dbDriver[512];
	char dbHost[512];
	char dbUser[256];
	char dbPasswd[256];
	char dbName[256];
	
	int running;
	int	isLogged;
	int loglevel;
	int log_module_acd;
	int log_module_cti;
	int log_module_agent;
	int log_module_manager;
	int log_module_db;

	char ivrHost[512];
	int  ivrPort;
	char wallboardHost1[256];
	char wallboardHost2[256];
	char wallboardHost3[256];
	char wallboardHost4[256];
	int wallboardPort;
	char licensedkey[128];
	int  licensedagent;
	int  numagentlogin;
}ApplicationContext;


extern ServerContext *serverContext;
extern ApplicationContext *appContext;
extern char *appConfigFile;

/* Db tools */
extern tDbLib *dbLib;

/* our header file */

#include "appsvr_list.h"
#include "appsvr_util.h"
#include "appsvr_logger.h"
#include "appsvr_thread.h"
//#include "appsvr_msghandler.h"
//#include "appsvr_client.h"
#include "appsvr_sock.h"

#include "appsvr_agent.h"
#include "appsvr_manager.h"
#include "appsvr_dblog.h"

#include "appsvr_statistic.h"

void ivrnotif_AgentAvailable(int group);

void ivrnotif_AgentConnected(char *ext, int agentId, char *agentName);
void ivrnotif_AgentDisconnected(char *ext, int agentId);
void ivrnotif_NewCall(char *ext, char *sessionId);
void wallboard_extstatus(char *ext, int extstatus, char *anumber);
void wallboard_agentstatus(char *ext, int agentstatus, int agentid);

#endif//_AppSvrH_
