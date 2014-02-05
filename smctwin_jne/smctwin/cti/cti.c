

#include "../include/smct.h"
#include "../ctblib_message.h"
#include "../include/appsvr.h"
#include "../include/appsvr_thread.h"
#include "cti.h"
#include "../acd/acd.h"

#include <Tapi.h>
#include "cti_tapi.h"

#pragma warning(disable : 4996)  // deprecated CRT function

typedef struct _t_Pbx{
	int 			id;
	char 			name[64];
	char 			desc[64];
	char 			protocol[64];
	TCTIPvt 	*priv;								/* private data */
	
	struct _t_Pbx 	*next;
  struct _t_Pbx 	*prev;
}t_Pbx;

typedef struct {
	int 		size;
	t_Pbx 	*first;
	t_Pbx 	*last;	
}t_PbxList;

static t_PbxList Pbxs;

static int cti_loadConfig(){
	
  
	return 0;
}

int cti_CheckStationIdleStatus(char *ext_no){
	return (tapi_CheckStationIdleStatus(ext_no));
}

int cti_Setcallqueue(char *ext_no, int group_id){
	return (tapi_setcallqueue(ext_no,group_id));
}

static int cti_loadConfigDB(){
	tDbConn dbConn;
	tDbSet  dbSet;
  char sql[2048];
  int  sqlLen;
  
  logger_Print(3,1,"CTI>> Loading Configuration from DB\n");
	
	/* connect to DB */
	dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
  if (!dbConn){
  	printf("CTI>> DB Connection failed\n");
  	return -1;
  }
  
  sqlLen = sprintf(sql,"SELECT set_name, set_value FROM settings WHERE set_modul = 'cti'");  
  dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){  	
		
  }
  dbLib->closeQuery(dbSet);
  dbLib->closeConnection(dbConn);
	return 0;
}


static int init_cti(){
	tDbConn dbConn;
	tDbSet  dbSet, dbSet2;
  char sql[2048];
  int  sqlLen;
	int ret;
	
	
	/* read configuration */
	cti_loadConfig();
	ret = cti_loadConfigDB();
	if (ret < 0)
		return -1;
		
	/* load all PBXs */
	logger_Print(3,1,"CTI>> Loading PBXs\n");
	
	/* connect to DB */
	dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
  if (!dbConn){
  	logger_Print(3,1,"CTI>> DB Connection failed\n");
  	return -1;
  }
  
  sqlLen = sprintf(sql,"SELECT id, pbx_name, pbx_desc, link_protocol FROM pbx  where status = 1");  
  dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){
  	t_Pbx *pbx;
  	
  	pbx = listNewItem(t_Pbx);    
    pbx->id = dbLib->getIntFieldByIdx(dbSet, 0);
    sprintf(pbx->name, "%s", dbLib->getStringFieldByIdx(dbSet, 1));
    sprintf(pbx->desc, "%s", dbLib->getStringFieldByIdx(dbSet, 2));
    sprintf(pbx->protocol, "%s", dbLib->getStringFieldByIdx(dbSet, 3));    
    pbx->priv = (TCTIPvt*)malloc(sizeof(TCTIPvt));
    memset(pbx->priv, 0, sizeof(TCTIPvt));
    
    logger_Print(3,1,"CTI>> Loading PBX: [%s] - [%s] using protocol %s\n",
                 pbx->name, pbx->desc, pbx->protocol);
                 
    /* Load PBX protocol configuration */
    if (!strcasecmp(pbx->protocol, "AVAYA-ASAI")){
    	logger_Print(3,1,"CTI>> Loading configuration for protocol %s\n", pbx->protocol);
    	sqlLen = sprintf(sql,"SELECT set_name, set_value FROM pbx_settings WHERE pbx = %d", pbx->id);
  		dbSet2 = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  		while(dbLib->nextRow(dbSet2)){
				/*
  			if (!strcasecmp(dbLib->getStringFieldByIdx(dbSet2, 0), "host"))
	    		sprintf(pbx->priv->asaiHost,"%s", dbLib->getStringFieldByIdx(dbSet2, 1));
	    	else if (!strcasecmp(dbLib->getStringFieldByIdx(dbSet2, 0), "port"))
	    		pbx->priv->asaiTcpPort = dbLib->getIntFieldByIdx(dbSet2, 1);
	    	else if (!strcasecmp(dbLib->getStringFieldByIdx(dbSet2, 0), "link"))
	    		pbx->priv->asaiLinkNumber = dbLib->getIntFieldByIdx(dbSet2, 1);
	    	else if (!strcasecmp(dbLib->getStringFieldByIdx(dbSet2, 0),"tcp.tunnel.version"))
	    		pbx->priv->asaiTcpTunnelVersion = dbLib->getIntFieldByIdx(dbSet2, 1);	
				*/
			
  		}
  		dbLib->closeQuery(dbSet2);
    }
    	
		/* insert to session list */
    listInsertLast(&Pbxs, pbx);  	
  }
  dbLib->closeQuery(dbSet);  
  dbLib->closeConnection(dbConn);
		
	return 0;
}

static void cti_MainLoop(void *param){
	TCTIPvt *pvt=(TCTIPvt*) param;
	int ret;
	
	logger_Print(3,1,"CTI>> Main thread Started\n");
	
	if (CreatePipe(&pvt->hReader, &pvt->hWriter, NULL, 10000) == 0) {
		logger_Print(3,1,"CTI>> Unable to create control socket: %s\n", strerror(errno));
		free(pvt);
		return;
	}	

	pthread_mutex_init(&pvt->hPipeLock, NULL);
	pvt->hDataEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	
	ret = tapi_Init(pvt);
	if (ret < 0){
		logger_Print(3,1,"CTI>> Initialization failed, thread stopped\n");
		return;
	}
	
	tapi_LoadDevices(pvt);
	tapi_MainLoop(pvt);
	
	logger_Print(3,1,"CTI>> Main thread Stopped\n");	
	return;
}

int cti_Load(){
	t_Pbx 	*pbx;
	
	logger_Print(3,1,"CTI>> Initializing\n");
	init_cti();
	
	logger_Print(3,1,"CTI>> Loading all CTI connection\n");
	
	
	/* create server thread */
	logger_Print(3,1,"CTI>> Starting main thread\n");
	
	/** 
		start all PBXs connection 
		current version only support one PBX connection
		*/
	
	pbx = listFirst(&Pbxs);
	if(!pbx){
		logger_Print(3,1,"CTI>> No PBX available\n");
		return 0;
	}
	
	smctPthreadCreate(&pbx->priv->mainThreadId, NULL, cti_MainLoop, (void*)pbx->priv, 2 * 1024 * 1024, PTHREAD_CREATE_DETACHED);
	return 0;
}

int cti_Write(char *msg, int len){
	t_Pbx 	*pbx;
	int			nWritten;
	char		buflen[2];
	
	pbx = listFirst(&Pbxs);
	if(!pbx){
		logger_Print(3,1,"CTI>> No PBX available\n");
		return 0;
	}	
	
	buflen[0] = (char)(len >> 8);
	buflen[1] = (char)(len & 0x000000ff);
	pthread_mutex_lock(&pbx->priv->hPipeLock);	
		WriteFile(pbx->priv->hWriter, buflen, 2, &nWritten, NULL);
		WriteFile(pbx->priv->hWriter, msg, len, &nWritten, NULL);
		SetEvent(pbx->priv->hDataEvent);
	pthread_mutex_unlock(&pbx->priv->hPipeLock);
	
	return 0;
}

