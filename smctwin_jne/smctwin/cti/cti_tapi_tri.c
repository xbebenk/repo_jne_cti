
#include "winsock2.h"
#include "../include/smct.h"
#include "../ctblib_message.h"
#include "../include/appsvr.h"
#include "../include/appsvr_thread.h"
#include "../include/appsvr_list.h"
#include "../include/appsvr_sock.h"
#include "../include/appsvr_agent.h"
#include "../acd/acd.h"
#include "../osdep.h"

#include "cti.h"

#include "cti_tapi.h"
#include "cti_acdmis.h"



#pragma warning(disable : 4996)  // deprecated CRT function

typedef struct _t_TapiCallInfo{
	HLINE	hLine;
	int	  callId;
	int		trunkNo;
	int		direction;
	char	caller[64];
	char	called[64];
	char	connected[64];
}t_TapiCallInfo;


// FORWARD DECLARATION
static int tapi_ProcessCtiMsg(TCTIPvt *pvt, unsigned char *message, int len);

/* private variable */
static t_TapiDeviceList 	TapiDevices;
static t_CallSessionList 	CallSessions;

static HLINEAPP	hLineApp=0;
static HPHONEAPP	hPhoneApp=0;
static DWORD    dwTAPIVersion;

static t_TapiDevice* tapi_FindDeviceByNumber(int type, char* number){
	t_TapiDevice *device;

	device = listFirst(&TapiDevices);
	while(device){
		if ((device->type == type) &&
				!strcmp(device->number, number))
			return device;
		else
		device = listNext(device);
	}
	return NULL;
}

t_TapiDevice* tapi_FindAllDeviceByNumber(char* number){
	t_TapiDevice *device;

	device = listFirst(&TapiDevices);
	while(device){
		if (!strcmp(device->number, number))
			return device;
		else
		device = listNext(device);
	}
	return NULL;
}

static t_TapiDevice* tapi_FindDeviceByHandle(HLINE	hLine){	
	t_TapiDevice *device;

	device = listFirst(&TapiDevices);
	
	while(device){
		if (device->hLine == hLine)
			return device;
		else
			device = listNext(device);
	}
	
	return NULL;
}

static t_CallSession* tapi_FindCallSessionByCall(HCALL call){	
	t_CallSession *cs;

	cs = listFirst(&CallSessions);
	
	while(cs){
		if (cs->currCall == call)
			return cs;
		else
			cs = listNext(cs);
	}
	
	return NULL;
}

static t_CallSession* tapi_FindCallSessionByConnectedNumber(char *number){	
	t_CallSession *cs;

	cs = listFirst(&CallSessions);
	
	while(cs){
		if (!strcmp(cs->connected_number, number))
			return cs;
		else
			cs = listNext(cs);
	}
	
	return NULL;
}


//*************************
// UTILITY
//*************************

int tapi_GetCallInfo(HCALL hCall, t_TapiCallInfo *calInfo){
	LINECALLINFO *ci;
	size_t ci_size = sizeof(LINECALLINFO);
	DWORD result;
	BOOL done = FALSE;
	char *p;

	if (!calInfo) return 0;

	memset(calInfo, 0, sizeof(t_TapiCallInfo));

  while (!done) {
  
    ci = (LINECALLINFO *)calloc(ci_size, 1);
    if (ci == NULL) {
      logger_Print("TAPI>> tapi_GetCallInfo - out of memory\n");
      return -1;
    }

    ci->dwTotalSize = (DWORD)ci_size;
    result = lineGetCallInfo(hCall, ci);
   
    if ((result < 0) && (result != LINEERR_STRUCTURETOOSMALL)) {
      logger_Print("TAPI>> error 0x%08lx calling lineGetCallInfo\n");
      free(ci);
      return -1;
    }
  
    done = ((result == 0) && (ci->dwNeededSize <= ci->dwTotalSize));
  
    if (!done) {
      ci_size = ci->dwNeededSize;
      free(ci);
    }
  
  };

	calInfo->hLine     = ci->hLine;
	calInfo->trunkNo   = ci->dwTrunk;
	calInfo->callId		 = ci->dwCallID;
	calInfo->direction = ci->dwOrigin;

  if (ci->dwCallerIDFlags & (LINECALLPARTYID_ADDRESS | LINECALLPARTYID_PARTIAL)) {
    p = (char *)ci + ci->dwCallerIDOffset;    
		strncpy(calInfo->caller, p, ci->dwCallerIDSize);    
  }

	if (ci->dwCalledIDFlags & (LINECALLPARTYID_ADDRESS | LINECALLPARTYID_PARTIAL)) {
    p = (char *)ci + ci->dwCalledIDOffset;    
		strncpy(calInfo->called, p, ci->dwCalledIDSize);    
  }

	if (ci->dwConnectedIDFlags & (LINECALLPARTYID_ADDRESS | LINECALLPARTYID_PARTIAL)) {
    p = (char *)ci + ci->dwConnectedIDOffset;    
		strncpy(calInfo->connected, p, ci->dwConnectedIDSize);    
  }	

  free (ci);
	return 0;
}

static char* tapi_CreateSessionId(char *buf, int buflen){
	struct timeval stimeval;
	
	gettimeofday(&stimeval, NULL);	
	_snprintf(buf, buflen, "%010ld%06ld\n", stimeval.tv_sec, stimeval.tv_usec);
	if (buflen >= 17)
		buf[16] ='\0';
	return buf;
}

static t_CallSession* tapi_CreateCallSession(TCTIPvt *pvt, HLINE line, HCALL call, int direction){
	t_CallSession *cs = NULL;	
	
	cs = listNewItem(t_CallSession);
	tapi_CreateSessionId(cs->session_key, 32);
	cs->line      = line;
	cs->origCall  = call;
	cs->currCall  = call;	
	cs->direction = direction;	
		
	/* insert to call session list */
	logger_Print("TAPI>> Adding CallSession to list w/size=%d\n", listSize(&CallSessions));
	listInsertLast(&CallSessions, cs);	
	return cs;
}


int tapi_LoadDevices(TCTIPvt *cti_pvt){
	tDbConn dbConn;
	tDbSet  dbSet, dbSet2;
  char sql[2048];
  int  sqlLen;
  t_TapiDevice *device;
  
  logger_Print("Tapi>> Loading Devices\n");
  
  listInit(&TapiDevices);
  listInit(&CallSessions);  
	
	/* connect to DB */
	dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
  if (!dbConn){
  	logger_Print("Tapi>> DB Connection failed\n");
  	return -1;
  }  
  
  /* load VDN */
	sqlLen = sprintf(sql, "SELECT a.vdn vdn, b.hunting_number, a.is_direct, b.id, a.id, a.routing_alg, a.trash_target "
	                      "FROM   vdn_agent_group a, agent_group b "
	                      "WHERE  a.agent_group = b.id");
	dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){
  	t_Skill *skill;
  	
  	device = listNewItem(t_TapiDevice);  	
  	device->type = TAPIDEVICE_VDN;
  	sprintf(device->number, 		"%s", dbLib->getStringFieldByIdx(dbSet, 0));
		sprintf(device->huntNumber, "%s", dbLib->getStringFieldByIdx(dbSet, 1)?dbLib->getStringFieldByIdx(dbSet, 1):"");
		device->isDirect					= dbLib->getIntFieldByIdx(dbSet, 2);
		device->groupId  					= dbLib->getIntFieldByIdx(dbSet, 3);
		device->routingAlgorithm  = dbLib->getIntFieldByIdx(dbSet, 5);
		if (dbLib->getStringFieldByIdx(dbSet, 6)){
			sprintf(device->trashTarget, 		"%s", dbLib->getStringFieldByIdx(dbSet, 6));
		}	
		
		/* load agent skill */				
 		sqlLen = sprintf(sql,"SELECT skill, score FROM vdn_skill "
 			                   "WHERE vdn = '%s' ORDER by id", device->number);
		dbSet2 = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
	  while(dbLib->nextRow(dbSet2)){
	  	skill        = listNewItem(t_Skill);
	  	skill->id    = dbLib->getIntFieldByIdx(dbSet2, 0);
	  	skill->score = dbLib->getIntFieldByIdx(dbSet2, 1);
	  	listInsertLast(&device->skills, skill);
	  }
	  dbLib->closeQuery(dbSet2);
		
		logger_Print("\tFound VDN %s -> hunt=%s, direct=%s, group=%d, alg=%d\n", 
					device->number, device->huntNumber, device->isDirect?"yes":"no", device->groupId, device->routingAlgorithm);
		
		/* insert to device list */
		listInsertLast(&TapiDevices, device);
  }  
  dbLib->closeQuery(dbSet);
  
  /* load Agent Extensions */
  sqlLen = sprintf(sql, "SELECT	a.ext_number,	a.ext_location, a.ext_type, a.tapi_id "
  											"FROM	extension_agent a "
												"WHERE a.ext_status = 0 order by	a.ext_number");
	dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){
  	device = listNewItem(t_TapiDevice);
  	
  	device->type = TAPIDEVICE_AGENT;
  	sprintf(device->number, 		"%s", dbLib->getStringFieldByIdx(dbSet, 0));
		sprintf(device->ipAddress, "%s", dbLib->getStringFieldByIdx(dbSet, 1));
		device->model  = dbLib->getIntFieldByIdx(dbSet, 2);
		device->tapiId = dbLib->getIntFieldByIdx(dbSet, 3);
		
		logger_Print("\tFound Extension %s\n", device->number);
		
		/* insert to device list */
		listInsertLast(&TapiDevices, device);
  }  
  dbLib->closeQuery(dbSet);

	/* load IVR Extensions */
  sqlLen = sprintf(sql, "SELECT	a.ext_number,	a.ext_type, a.tapi_id "
  											"FROM	extension_ivr a "
												"WHERE a.ext_status = 0 order by	a.ext_number");
	dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){
  	device = listNewItem(t_TapiDevice);
  	
  	device->type = TAPIDEVICE_IVR;
  	sprintf(device->number, 		"%s", dbLib->getStringFieldByIdx(dbSet, 0));		
		device->model  = dbLib->getIntFieldByIdx(dbSet, 1);
		device->tapiId = dbLib->getIntFieldByIdx(dbSet, 2);
		
		logger_Print("\tFound IVR Extension %s\n", device->number);
		
		/* insert to device list */
		listInsertLast(&TapiDevices, device);
  }  
  dbLib->closeQuery(dbSet);
  
  /* load Agent Huntings */
  sqlLen = sprintf(sql, "select	a.hunting_number from agent_group a");
	dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){
  	device = listNewItem(t_TapiDevice);
  	
  	device->type = TAPIDEVICE_ACDHUNTING;
  	sprintf(device->number, 		"%s", dbLib->getStringFieldByIdx(dbSet, 0));		
		logger_Print("\tFound Hunting %s\n", device->number);
		
		/* insert to device list */
		listInsertLast(&TapiDevices, device);
  }  
  dbLib->closeQuery(dbSet);
  
  dbLib->closeConnection(dbConn);  
	return 0;
}

static int Tapi_InitDevice(TCTIPvt *cti_pvt){
	t_TapiDevice *device;
	DWORD  ret, dwVersion;
	LINEEXTENSIONID extId;

		
	logger_Print("TAPI>> Initializing device control\n");
	
	
	device = listFirst(&TapiDevices);
	while(device){	
		device->device_status = 0x0000; //init value
		switch(device->type){
			case TAPIDEVICE_VDN:				
				break;
			case TAPIDEVICE_AGENT:
				// get TAPI version supported by device				
				dwVersion = TAPI_CURRENT_VERSION;
				ret = lineNegotiateAPIVersion(hLineApp, device->tapiId, 0x00010003, 0x0FFF0FFF, &dwVersion, &extId);
				ret = lineOpen(hLineApp, device->tapiId, &device->hLine, dwVersion, 0, 0,LINECALLPRIVILEGE_OWNER | LINECALLPRIVILEGE_MONITOR ,
											 LINEMEDIAMODE_INTERACTIVEVOICE , NULL);				
				logger_Print("TAPI>> Opening Agent Device %s with Id 0x%04x, hLine=0x%08x using TAPI version %d.%d\n", 
											device->number, device->tapiId, device->hLine, (dwVersion>>16), (dwVersion & 0xFFFF));

				break;
			case TAPIDEVICE_IVR:
				// get TAPI version supported by device				
				dwVersion = TAPI_CURRENT_VERSION;
				ret = lineNegotiateAPIVersion(hLineApp, device->tapiId, 0x00010003, 0x0FFF0FFF, &dwVersion, &extId);
				ret = lineOpen(hLineApp, device->tapiId, &device->hLine, dwVersion, 0, 0,LINECALLPRIVILEGE_OWNER | LINECALLPRIVILEGE_MONITOR ,
											 LINEMEDIAMODE_INTERACTIVEVOICE , NULL);				
				logger_Print("TAPI>> Opening IVR Device %s with Id 0x%04x, hLine=0x%08x using TAPI version %d.%d\n", 
											device->number, device->tapiId, device->hLine, (dwVersion>>16), (dwVersion & 0xFFFF));
				break;
			default:				
				break;
		}
		device = listNext(device);		
	}
	return 0;
}

static void CALLBACK lineCallbackFunc(
    DWORD dwDevice, DWORD dwMsg, DWORD dwCallbackInstance, 
    DWORD dwParam1, DWORD dwParam2, DWORD dwParam3){
	// doing nothing
}

/**
	ACD-MIS koneksinya cuma satu arah, dari PBX ke aplikasi
	mengirimkan event
 */
//static int tapi_InitACDMIS(TCTIPvt *cti_pvt){
//	// create socket
//  cti_pvt->acdmisSock = sockTcpClientCreate();
//  
//  /* connect to asai */  
//  logger_Print("CTI>> Connecting to PBX[%s:%d] ...", cti_pvt->acdmisHost, cti_pvt->acdmisTcpPort);
//  if (sockTcpClientConnect(cti_pvt->acdmisSock, cti_pvt->acdmisHost, cti_pvt->acdmisTcpPort) < 0 ){
//  	closesocket(cti_pvt->acdmisSock);
//  	cti_pvt->acdmisSock = -1;
//    logger_Print(" FAIL\n");
//    return -1;
//  }else
//    logger_Print(" SUCCESS\n");
//
//	cti_pvt->hACDMISEvent = CreateEvent(NULL, FALSE, FALSE, NULL);	// for socket
//	if(WSAEventSelect(cti_pvt->acdmisSock, cti_pvt->hACDMISEvent, FD_READ|FD_CLOSE) != 0)
//		logger_Print("TAPI>> WSAEventSelect error: %d\n", WSAGetLastError());             
//
//	return 0;
//}

int tapi_Init(TCTIPvt *cti_pvt){
	HINSTANCE hInstance;	
	DWORD			dwNumDevs;
	LINEINITIALIZEEXPARAMS li;
	DWORD			ret;



	//Init TAPI function

	hInstance = GetModuleHandle(NULL);
  /* initialize TAPI connection */
	dwTAPIVersion = TAPI_CURRENT_VERSION;	
	li.dwTotalSize = sizeof(li);
  li.dwOptions =  LINEINITIALIZEEXOPTION_USEEVENT;
	if (ret = lineInitializeEx(&hLineApp, hInstance, lineCallbackFunc, "SmartAppSvr", &dwNumDevs, &dwTAPIVersion, &li)){
		logger_Print("lineInitialize failed: \n");
		logger_Print("Return: 0x%08x\n", ret);
		return 0;
	}

	cti_pvt->hTapiEvent = li.Handles.hEvent;
	logger_Print("Found %d device(s)\n", dwNumDevs);	


	//Init ACD-MIS connection
	//return tapi_InitACDMIS(cti_pvt);	
	return 0;
}

static int tapi_OnEventCallInfo(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;	
	t_CallSession *cs=NULL;
	LINECALLINFO ci;

	ci.dwTotalSize = sizeof(LINECALLINFO);
	lineGetCallInfo(msg->hDevice, &ci);
	
	if (!(device = tapi_FindDeviceByHandle(ci.hLine))){
		logger_Print("ASAI>> EVENT_CALLINFO: Device Not Found\n");
		return 2;
	}

	return 0;
}

/**
 LINE_APPNEWCALL
 */
static int tapi_OnEventAgentNewCall(TCTIPvt *cti_pvt, LPLINEMESSAGE msg, t_TapiDevice *device){
	t_CallSession *cs=NULL;
	t_TapiCallInfo calInfo;
	HCALL call;
	int stat=0;

	call = (HCALL)msg->dwParam2;	
	
	logger_Print("TAPI>> LINE_APPNEWCALL: New call on agent device %s\n", device->number);

	tapi_GetCallInfo(call, &calInfo);

	logger_Print("\tCallId:    0x%08x\n", calInfo.callId);
	logger_Print("\tCaller:    %s\n", calInfo.caller);	
	logger_Print("\tCalled:    %s\n", calInfo.called);	
	logger_Print("\tConnected: %s\n", calInfo.connected);	
	logger_Print("\tTrunk:     %d\n", calInfo.trunkNo);	
	if(device->device_status == 0x0000){
		stat=1;
	}
	device->device_status = 0x0001;//
	if (calInfo.direction == LINECALLORIGIN_OUTBOUND){
		//device->device_status = 0x0001;
		//handled on dialtone event
		return 0;
	}else if (calInfo.direction & (LINECALLORIGIN_EXTERNAL | LINECALLORIGIN_INBOUND)){
		logger_Print("TAPI>> LINE_APPNEWCALL: Inbound/Ext call\n");
	}else{
		logger_Print("TAPI>> LINE_APPNEWCALL: Internal call\n");

		//find call session which connected to the caller
		cs = tapi_FindCallSessionByConnectedNumber(calInfo.caller);
		if(cs){
			if(cs->direction == CALLDIR_INCOMING){
				//call ditransfer ke kita, ambil alih data call session
				cs->currCall = call;
			}
		}
	}

	device->activeCall = call;

	if (device->agentId > 0){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;
		
		logger_Print("ASAI>> Alerting: On agent device %s.\n",device->number);
		acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_RINGING,cs?cs->session_key:"0");
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);	
			
		/* cek device on CTI */
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLALERTING;					// alerting event
		msgsend->Count		= 14;
		
		ctbMsgInsertNumeric(msgsend, 0, 0);
		ctbMsgInsertString (msgsend, 1, cs?cs->calling_number:calInfo.caller);
		ctbMsgInsertNumeric(msgsend, 2, 0);
		ctbMsgInsertNumeric(msgsend, 3, cs?cs->trunk_number:0);	
		ctbMsgInsertString (msgsend, 4, cs?cs->called_number:"");
		ctbMsgInsertString (msgsend, 5, cs?cs->connected_number:device->number);
		ctbMsgInsertString (msgsend, 6, cs?cs->session_key:"");			/* session_key */
		ctbMsgInsertString (msgsend, 7, device->number);		        /* extension */
		ctbMsgInsertNumeric(msgsend, 8, device->agentId);		        /* agent id */
		ctbMsgInsertString (msgsend, 9, device->ipAddress);		      /* extension */
		ctbMsgInsertNumeric(msgsend,10, device->groupId);		        /* group id	*/
		ctbMsgInsertNumeric(msgsend,11, cs?cs->direction:1);				/* direction */
		ctbMsgInsertString (msgsend,12, NULL);											/* ivr_data */
		ctbMsgInsertNumeric(msgsend,13, cs?cs->langId:0);						/* call language, based on IVR selection */
		
		len = ctbMsgEncode(msgsend, buf, 1024);
		if(stat){
			agent_DispatchMessage(device->agentId, buf, len);
		}
		logger_Dump(buf, len);
		free(msgsend);
		free(buf);	
	}else{
		logger_Print("TAPI>> Alerting: No body registered logged on this device\n");
	}
	
	/* save event data to DB */
	if(cs){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;		
		
		cs->status = CALLSTATUS_AGENT_RINGING;
			
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);	
			
		/* cek device on CTI */
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLALERTING;					// alerting event
		msgsend->Count		= 7;		
  	
  	ctbMsgInsertNumeric(msgsend, 0, msg->hDevice);
  	ctbMsgInsertString (msgsend, 1, cs->session_key);			  
  	ctbMsgInsertNumeric(msgsend, 2, cs->status);						// call status
  	ctbMsgInsertString (msgsend, 3, device->number);				// c-number	
  	ctbMsgInsertNumeric(msgsend, 4, cs->direction);					// direction		
  	ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
  	ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group
  	len = ctbMsgEncode (msgsend, buf, 512);	
    dblog_PutMessage(buf, len);		
		free(msgsend);
		free(buf);	
	}
	return 0;
}

static int tapi_OnEventIvrNewCall(TCTIPvt *cti_pvt, LPLINEMESSAGE msg, t_TapiDevice *device){
	t_CallSession *cs=NULL;
	t_TapiCallInfo calInfo;
	HCALL call;
	int direction;

	logger_Print("TAPI>> LINE_APPNEWCALL: New call on IVR device %s\n", device->number);

	call = (HCALL)msg->dwParam2;	
	tapi_GetCallInfo(call, &calInfo);

	logger_Print("\tCallId:    0x%08x\n", calInfo.callId);
	logger_Print("\tCaller:    %s\n", calInfo.caller);	
	logger_Print("\tCalled:    %s\n", calInfo.called);	
	logger_Print("\tConnected: %s\n", calInfo.connected);	
	logger_Print("\tTrunk:     %d\n", calInfo.trunkNo);	

	if (calInfo.direction == LINECALLORIGIN_OUTBOUND){
		//handled on dialtone event
		return 0;
	}else if (calInfo.direction & (LINECALLORIGIN_EXTERNAL | LINECALLORIGIN_INBOUND)){
		direction = CALLDIR_INCOMING;
	}else
		direction = CALLDIR_INTERNAL;

	/* Call start dari IVR, create call session*/
	if((cs = tapi_CreateCallSession(cti_pvt, device->hLine, call, direction))){
		cs->trunk_number = 0;
		if(calInfo.trunkNo >0)
			cs->trunk_member = calInfo.trunkNo;
		else
			cs->trunk_member = 0;
		sprintf(cs->calling_number, "%s", calInfo.caller);
		sprintf(cs->called_number,  "%s", calInfo.called);
		sprintf(cs->vdn_number,     "%s", calInfo.called);
		cs->status    = CALLSTATUS_OFFERED;	
		cs->status    = CALLSTATUS_IVR_RINGING;	 //cause already on IVR
	}
	
	device->activeCall = call;

	/* save call offered event data to DB */
	if(cs){
    tCtbMessage	*msgsend;
		char *buf;
		int  len;		
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);	
	
    msgsend->Sender 	= MSG_SRC_CC_CTI;
  	msgsend->Type	 		= MSGTYPE_EVENT_CALLOFFERED;
  	msgsend->Count		= 8;
  	
  	ctbMsgInsertNumeric(msgsend, 0, 0);
  	ctbMsgInsertString (msgsend, 1, cs?cs->session_key:"");			   		// session_key
  	ctbMsgInsertNumeric(msgsend, 2, 0);
  	ctbMsgInsertNumeric(msgsend, 3, calInfo.trunkNo);
		ctbMsgInsertString (msgsend, 4, calInfo.caller);			//a-number  		
		ctbMsgInsertString (msgsend, 5, calInfo.called);			//b-number
  	ctbMsgInsertString (msgsend, 6, calInfo.called);			//c-number, on VDN c-number is d-number
  	ctbMsgInsertNumeric(msgsend, 7, cs?cs->direction:0);	// direction		
  	len = ctbMsgEncode (msgsend, buf, 512);	
    dblog_PutMessage(buf, len);		
			
		/* cek device on CTI */
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLALERTING;					// alerting event
		msgsend->Count		= 7;		
  	
  	ctbMsgInsertNumeric(msgsend, 0, 0);
  	ctbMsgInsertString (msgsend, 1, cs->session_key);			  
  	ctbMsgInsertNumeric(msgsend, 2, cs->status);						// call status
  	ctbMsgInsertString (msgsend, 3, device->number);				// c-number	
  	ctbMsgInsertNumeric(msgsend, 4, cs->direction);					// direction		
  	ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
  	ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group
  	len = ctbMsgEncode (msgsend, buf, 512);	
    dblog_PutMessage(buf, len);
		free(msgsend);
		free(buf);
	}
	
	return 0;
}


static int tapi_OnEventNewCall(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;	
		
	if (!(device = tapi_FindDeviceByHandle(msg->hDevice))){
		logger_Print("TAPI>> LINE_APPNEWCALL: Device not registered\n");
		return 2;
	}

	logger_Print("TAPI>> LINE_APPNEWCALL: Device handle 0x08%lx\n", msg->hDevice);
	switch(device->type){
		case TAPIDEVICE_AGENT:
			tapi_OnEventAgentNewCall(cti_pvt, msg, device);
			break;
		case TAPIDEVICE_IVR:
			tapi_OnEventIvrNewCall(cti_pvt, msg, device);
			break;
		default:
			logger_Print("TAPI>> LINE_APPNEWCALL: Device not supported\n");
			break;
	}

	return 0;
}

static int tapi_OnEventCallAlerting(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	
	

	return 0;
}

static int tapi_OnEventCallInitiated(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;	
	t_CallSession *cs=NULL;
	LINECALLINFO ci;
	DWORD callId;

	ci.dwTotalSize = sizeof(LINECALLINFO);
	lineGetCallInfo(msg->hDevice, &ci);
	
	callId = msg->hDevice;	
	if (!(device = tapi_FindDeviceByHandle(ci.hLine))){
		logger_Print("TAPI>> CallInitiated: Device Not Found\n");
		return 2;
	}

	if (device->type == TAPIDEVICE_AGENT && device->agentId > 0){		
		tCtbMessage	*msgsend;
		char *buf;
		int  len;
		device->device_status = 0x0001;
		
		logger_Print("TAPI>> CallInitiated: On agent device.\n");
		acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_OFFHOOK,cs?cs->session_key:"0");
		
		/**
		 Create Call Session, Call session created only if agent logged on
		 */		
		//if ((cs = asai_CreateCallSession(cti_pvt, asaidata->real_crv, iedata->call_id1, CALLDIR_OUTGOING))){		
		//	cs->status    = CALLSTATUS_AGENT_INITIATED;
		//}
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);	
			
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLINITIATED;					// call initiated event
		msgsend->Count		= 8;
		
		ctbMsgInsertNumeric(msgsend, 0, callId);
		ctbMsgInsertString (msgsend, 1, "");
		ctbMsgInsertString (msgsend, 2, device->number);	
		ctbMsgInsertNumeric(msgsend, 3, device->agentId);
		ctbMsgInsertNumeric(msgsend, 4, device->assignment_id);
		ctbMsgInsertNumeric(msgsend, 5, cs?cs->direction:0);	
		ctbMsgInsertString (msgsend, 6, device->ipAddress);
		ctbMsgInsertNumeric(msgsend, 7, device->groupId);
		
		len = ctbMsgEncode(msgsend, buf, 1024);
		agent_DispatchMessage(device->agentId, buf, len);
		
		if(cs){	    
		
	    msgsend->Sender 	= MSG_SRC_CC_CTI;
	  	msgsend->Type	 		= MSGTYPE_EVENT_CALLINITIATED;
	  	msgsend->Count		= 8;
	  	
	  	ctbMsgInsertNumeric(msgsend, 0, callId);
	  	ctbMsgInsertString (msgsend, 1, cs->session_key);
	  	ctbMsgInsertNumeric(msgsend, 2, cs->status);
	  	ctbMsgInsertNumeric(msgsend, 3, cs->direction);
	  	ctbMsgInsertString (msgsend, 4, device->number);
	  	ctbMsgInsertNumeric(msgsend, 5, device->agentId);
	  	ctbMsgInsertNumeric(msgsend, 6, device->groupId);
	  	ctbMsgInsertNumeric(msgsend, 7, device->assignment_id);
	  	
	  	len = ctbMsgEncode (msgsend, buf, 512);	
	    dblog_PutMessage(buf, len);
		}	
		
		free(msgsend);
		free(buf);	
	}
	

	return 0;
}

static int tapi_OnEventConnected(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_CallSession *cs=NULL;
	t_TapiCallInfo callInfo;
	HCALL call;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);	
	
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print("TAPI>> EVENT_CONNECTED: Device Not Found\n");
		return 2;
	}
	device->activeCall = call;
	logger_Print("TAPI>> EVENT_CONNECTED: Device %s\n", device->number);
	cs = tapi_FindCallSessionByCall(call);
	
	if (device->type == TAPIDEVICE_AGENT && device->agentId > 0){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;
		
		device->device_status = 0x0001;
		logger_Print("TAPI>> Connected: On agent device.\n");
		acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_TALKING,cs?cs->session_key:"0");
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);	
			
		/* Send Event To Agent */
		ctbMsgInit(msgsend);			
		msgsend->Sender = MSG_SRC_CTI;
		msgsend->Type	 	= MSGTYPE_EVENT_CALLCONNECTED;
		msgsend->Count	= 12;
		
		ctbMsgInsertNumeric(msgsend, 0, 0);
		ctbMsgInsertString (msgsend, 1, cs?cs->calling_number:"");
		ctbMsgInsertNumeric(msgsend, 2, cs?cs->trunk_number:0);
		ctbMsgInsertNumeric(msgsend, 3, cs?cs->trunk_member:0);	
		ctbMsgInsertString (msgsend, 4, cs?cs->called_number:"");
		ctbMsgInsertString (msgsend, 5, cs?cs->connected_number:"");		
		ctbMsgInsertString (msgsend, 6, cs?cs->session_key:"");
		ctbMsgInsertString (msgsend, 7, device->number);		     /* extension */
		ctbMsgInsertNumeric(msgsend, 8, device->agentId);
		ctbMsgInsertString (msgsend, 9, device->ipAddress);
		ctbMsgInsertNumeric(msgsend,10, device->groupId);
		ctbMsgInsertNumeric(msgsend,11, cs?cs->direction:1);
		
		
		len = ctbMsgEncode(msgsend, buf, 1024);
		agent_DispatchMessage(device->agentId, buf, len);	
		free(msgsend);
		free(buf);
	}
	
	/* save event data to DB */
	if(cs){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;
		
		if(device->type == TAPIDEVICE_IVR){
			cs->status = CALLSTATUS_IVR_CONNECTED;
			ivrnotif_NewCall(device->number, cs->session_key);
		}else if(device->type == TAPIDEVICE_AGENT)
			cs->status = CALLSTATUS_AGENT_CONNECTED;

		sprintf(cs->connected_number,  "%s", device->number);
			
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);			
		
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLCONNECTED;
		msgsend->Count		= 7;		
  	
  	ctbMsgInsertNumeric(msgsend, 0, 0);
  	ctbMsgInsertString (msgsend, 1, cs->session_key);			  
  	ctbMsgInsertNumeric(msgsend, 2, cs->status);						// call status
  	ctbMsgInsertString (msgsend, 3, cs->connected_number);	// c-number	
  	ctbMsgInsertNumeric(msgsend, 4, cs->direction);					// direction		
  	ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
  	ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group
  	len = ctbMsgEncode (msgsend, buf, 512);	
    dblog_PutMessage(buf, len);
		free(msgsend);
		free(buf);	
	}

	return 0;
}

static int tapi_OnEventCallDisconnected(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_CallSession *cs=NULL;
	t_TapiCallInfo callInfo;
	HCALL call;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);	
	
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print("TAPI>> EVENT_DISCONNECTED: Device Not Found\n");
		return 2;
	}

	logger_Print("TAPI>> EVENT_DISCONNECTED: Device %s\n", device->number);

	cs = tapi_FindCallSessionByCall(call);

	if (device->activeCall == call){
		device->activeCall = device->secondCall;	
		device->secondCall = 0;
	}



//	if (device->agentId > 0 && device->activeCall == 0){
	if (device->type == TAPIDEVICE_AGENT && device->agentId > 0){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;		
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);				
		
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLDISCONNECTED;
		msgsend->Count		= 8;
		
		ctbMsgInsertNumeric(msgsend, 0, 0);
		ctbMsgInsertString (msgsend, 1, device->number);    		// connected number	
		ctbMsgInsertString (msgsend, 2, cs?cs->session_key:"");	// session_key		
		ctbMsgInsertString (msgsend, 3, device->number);				// extension			
		ctbMsgInsertNumeric(msgsend, 4, device->agentId);				// agent id	
		ctbMsgInsertString (msgsend, 5, device->ipAddress);			// ip_address
		ctbMsgInsertNumeric(msgsend, 7, cs?cs->direction:1);		// direction
		ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group id
		
		len = ctbMsgEncode(msgsend, buf, 1024);
		printf("DEBUG>>Send Disconnected to agent\n");
		//agent_DispatchMessage(device->agentId, buf, len);	
		free(msgsend);
		free(buf);		
		/*
		//dipindahkan ke even call idle
		// set agent device status to idle 
		if(device->device_status == 0x0001){
			acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_IDLE);
			//	device->device_status = 0x0000;
			logger_Print("TAPI>> EVENT_DISCONNECTED: from connected\n");
		}else{
			logger_Print("TAPI>> EVENT_DISCONNECTED: Disconnect not from connected\n");
			acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_IDLE);
		}
		*/
	}
		

	/* delete call session */
	/* save event data to DB */
	if(cs){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;
		
		if(device->type == TAPIDEVICE_IVR){
			if(cs->status == CALLSTATUS_IVR_RINGING)
				cs->status = CALLSTATUS_IVR_ABANDON;
			else
				cs->status = CALLSTATUS_IVR_TERMINATED;
		}else if(device->type == TAPIDEVICE_AGENT){
			switch(cs->status){
				case CALLSTATUS_AGENT_RINGING:
				case CALLSTATUS_AGENT_INITIATED:
				case CALLSTATUS_AGENT_ORIGINATED:
				case CALLSTATUS_AGENT_TRUNKSEIZED:
					cs->status = CALLSTATUS_AGENT_ABANDON;
					break;
				default:
					cs->status = CALLSTATUS_AGENT_TERMINATED;			
			}
		}
			
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);			
		
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLDISCONNECTED;
		msgsend->Count		= 7;		
  	
  	ctbMsgInsertNumeric(msgsend, 0, 0);
  	ctbMsgInsertString (msgsend, 1, cs->session_key);			  
  	ctbMsgInsertNumeric(msgsend, 2, cs->status);						// call status
  	ctbMsgInsertString (msgsend, 3, device->number);				// c-number	
  	ctbMsgInsertNumeric(msgsend, 4, cs->direction);					// direction		
  	ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
  	ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group
  	len = ctbMsgEncode (msgsend, buf, 512);	
    dblog_PutMessage(buf, len);
		free(msgsend);
		free(buf);	
		
		listRemove(&CallSessions, cs);
		free(cs);
		logger_Print("TAPI>> Disconnect: Call Session deleted...\n");
	}	

	return 0;
}

/**
 Call no longer exist on a device
 */
static int tapi_OnEventCallIdle(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_TapiCallInfo callInfo;
	HCALL call;	
	t_CallSession *cs=NULL;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);
	lineDeallocateCall(call);
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print("TAPI>> LINECALLSTATE_IDLE: Device Not Found\n");
		return 2;
	}
	printf(" %s\n",device->number);
	device->device_status = 0x0000;

	cs = tapi_FindCallSessionByCall(call);
	if (!cs){
		logger_Print("TAPI>> Call Session not found... check if this call id is on hold\n");
		//return 0;
	}
	
	if (device->type == TAPIDEVICE_AGENT && device->agentId > 0){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;			
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);				
		
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLDISCONNECTED;
		msgsend->Count		= 8;
		
		ctbMsgInsertNumeric(msgsend, 0, 0);
		ctbMsgInsertString (msgsend, 1, device->number);    		// connected number	
		ctbMsgInsertString (msgsend, 2, "");	// session_key		
		ctbMsgInsertString (msgsend, 3, device->number);				// extension			
		ctbMsgInsertNumeric(msgsend, 4, device->agentId);				// agent id	
		ctbMsgInsertString (msgsend, 5, device->ipAddress);			// ip_address
		ctbMsgInsertNumeric(msgsend, 7, 1);		// direction
		ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group id
		
		len = ctbMsgEncode(msgsend, buf, 1024);
		printf("DEBUG>>Send Disconnected to agent %d at %s\n",device->agentId,device->number);
		agent_DispatchMessage(device->agentId, buf, len);	
		// set agent device status to idle (pindahan dari disconnect
		
		acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_IDLE,cs?cs->session_key:"0");
		
	}


	// delete call_session	
	logger_Print("TAPI>> Call Ended: Deleting Call Session ...\n");
	if(cs){
		listRemove(&CallSessions, cs);
		free(cs);
	}
	return 0;
}

static int tapi_OnEventCallDialing(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_TapiCallInfo callInfo;
	HCALL call;	
	t_CallSession *cs=NULL;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print("TAPI>> LINECALLSTATE_DIALING: Device Not Found\n");
		return 2;
	}

	logger_Print("TAPI>> LINECALLSTATE_DIALING: Device %s\n", device->number);
	logger_Print("\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print("\tCaller:    %s\n", callInfo.caller);	
	logger_Print("\tCalled:    %s\n", callInfo.called);	
	logger_Print("\tConnected: %s\n", callInfo.connected);	
	logger_Print("\tTrunk:     %d\n", callInfo.trunkNo);	

	
	return 0;
}


static int tapi_OnTapiEvent(TCTIPvt *cti_pvt){
	DWORD ret;

	LINEMESSAGE lineMsg;

	ret = lineGetMessage(hLineApp, &lineMsg, 0);
	
	switch(lineMsg.dwMessageID){ 
		case LINE_ADDRESSSTATE:
			printf("Address state\n");
			break;
		case LINE_CALLINFO:
			//logger_Print("TAPI>> Call Info\n");
			//tapi_OnEventCallInfo(cti_pvt, &lineMsg);			
			break;
		case LINE_CALLSTATE:			
			switch(lineMsg.dwParam1){
				case LINECALLSTATE_IDLE:
					logger_Print("TAPI>> IDLE");
					tapi_OnEventCallIdle(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_OFFERING:
					logger_Print("TAPI>> Ringing\n");
					tapi_OnEventCallAlerting(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_DISCONNECTED:
					logger_Print("TAPI>> Disconnected\n");
					tapi_OnEventCallDisconnected(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_DIALTONE:
					logger_Print("TAPI>> Offhook\n");
					tapi_OnEventCallInitiated(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_CONNECTED:
					//logger_Print("TAPI>> Connected\n");
					tapi_OnEventConnected(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_DIALING:
					//logger_Print("TAPI>> Dialing\n");
					tapi_OnEventCallDialing(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_ONHOLD:
					logger_Print("TAPI>> OnHold\n");
					break;
				case LINECALLSTATE_PROCEEDING:
					logger_Print("TAPI>> Originating\n");
					break;
				case LINECALLSTATE_BUSY:
					logger_Print("TAPI>> Busy\n");
					break;
				case LINECALLSTATE_ACCEPTED 					:
									logger_Print("TAPI>> LINECALLSTATE_ACCEPTED\n");
									break;                        
				case LINECALLSTATE_RINGBACK           :
						logger_Print("TAPI>> LINECALLSTATE_RINGBACK\n");
									break;
				case LINECALLSTATE_SPECIALINFO        :
									logger_Print("TAPI>> LINECALLSTATE_SPECIALINFO\n");
									break;
				case LINECALLSTATE_CONFERENCED        :
									logger_Print("TAPI>> Busy\n");
									break;                        
				case LINECALLSTATE_ONHOLDPENDCONF     :
									logger_Print("TAPI>> LINECALLSTATE_CONFERENCED\n");
									break;                        
				case LINECALLSTATE_UNKNOWN            :
									logger_Print("TAPI>> LINECALLSTATE_UNKNOWN\n");
									break;                        
				default:
					printf("LINECALLSTATE UNKWON :%d\n",lineMsg.dwParam1);

					break;
			}
			break;
		case LINE_APPNEWCALL:
			// new call on a device, IN/OUT, created spontaneously, not by API
			tapi_OnEventNewCall(cti_pvt, &lineMsg);
			break;
		case LINE_CLOSE:
			printf("LINE_CLOSE");
			break;
		case LINE_CREATE:
			printf("LINE_CREATE");
			break;
		case LINE_DEVSPECIFIC:
			printf("LINE_DEVSPECIFIC");
			break;				
		case LINE_DEVSPECIFICFEATURE:
			printf("LINE_DEVSPECIFICFEATURE");
			break;
		case LINE_GATHERDIGITS:
			printf("LINE_GATHERDIGITS");
			break;
		case LINE_GENERATE:
			printf("LINE_GENERATE");
			break;
		case LINE_LINEDEVSTATE:
			printf("LINE_LINEDEVSTATE\n");
			switch(lineMsg.dwParam1){						
				case LINEDEVSTATE_RINGING:
					printf("Ringing\n");
			}
			break;	
		default:
			printf("LINE_STATE UNKWON :%d\n",lineMsg.dwMessageID);

			break;
  }
	
	return 0;
}

//*******************************************************************
// TAPI related routines
//*******************************************************************

// return 0 if Idle
static int tapi_StationIdleStatus(TCTIPvt *pvt, t_TapiDevice *device){
	LINEDEVSTATUS lds;	
	int ret;
	
	lds.dwTotalSize = sizeof(LINEDEVSTATUS);
	ret = lineGetLineDevStatus(device->hLine, &lds);
	if (!ret){
		if ((lds.dwNumActiveCalls == 0)&&(lds.dwNumOnHoldCalls  == 0)&&(lds.dwNumOnHoldPendCalls == 0)){
			device->status			 = TAPIDEVICESTATUS_IDLE;
			device->activeCall = 0;	
			device->secondCall = 0;
			return 0;
		}else
			return 1;
	}	
	return -1;
}

int tapi_CheckStationIdleStatus(char *ext_no){
//	LINEDEVSTATUS lds;	
//	int ret;
	t_TapiDevice *device;

	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, ext_no);
	//lds.dwTotalSize = sizeof(LINEDEVSTATUS);
	//ret = lineGetLineDevStatus(device->hLine, &lds);
	//printf("DEBUG>>DEVICE %s ActiveCall=%d,OnHoldCAlls =%d,OnHoldPending=%d\n",ext_no,lds.dwNumActiveCalls,lds.dwNumOnHoldCalls,lds.dwNumOnHoldPendCalls);
	
		
		//if ((lds.dwNumActiveCalls == 0)&&(lds.dwNumOnHoldCalls  == 0)&&(lds.dwNumOnHoldPendCalls == 0)){
		if(device->device_status == 0x0000){
			//device->status			 = TAPIDEVICESTATUS_IDLE;
			//device->activeCall = 0;	
			//device->secondCall = 0;
			printf("DEBUG>>DEVICE %s is IDLE,Safe to tell IVR\n",ext_no);
			return 0;
		}else{
			printf("DEBUG>>DEVICE %s is NOT IDLE YET,Don't tell IVR\n",ext_no);
			return 1;
		}
		
	return -1;
}



int tapi_MainLoop(TCTIPvt *cti_pvt){
	DWORD ret;
	HANDLE evts[3];
	unsigned char 	msgBuf[4096], *pMsgBuf;
	int msgLen, nread, len, acdMsgLen;
	int dbPing = 0;
	char 	acdBuf[4096], *pAcdBuf, *pAcdBuf2, acdMsg[4096];

	
	//Load Devices
	Tapi_InitDevice(cti_pvt);
	
	evts[0] = cti_pvt->hTapiEvent;
	evts[1] = cti_pvt->hDataEvent;
	//evts[2] = cti_pvt->hACDMISEvent;	

	pAcdBuf = acdBuf;
	while(1){
		// wait event from PBX
		//ret =  WaitForMultipleObjects(3, evts, FALSE, 1000);
		ret =  WaitForMultipleObjects(2, evts, FALSE, 1000);
		switch(ret){
			case WAIT_OBJECT_0:
				tapi_OnTapiEvent(cti_pvt);
				break;
			case WAIT_OBJECT_0+1:
				// data available				
				ret = ReadFile(cti_pvt->hReader,  msgBuf, 2, &nread, NULL);
				//read all data
				msgLen = (msgBuf[0] << 8) + msgBuf[1];
				nread  = 0;
				len    = msgLen;
				pMsgBuf = msgBuf;
				while(len > 0){
					ret			= ReadFile(cti_pvt->hReader,  pMsgBuf, len, &nread, NULL);
					len			-= nread;
					pMsgBuf += nread;
					nread		= 0;
				}
				tapi_ProcessCtiMsg(cti_pvt, msgBuf, msgLen);
				break;
			case WAIT_OBJECT_0+2:
				//////data available on ACD-MIS
				////nread = recv(cti_pvt->acdmisSock, pAcdBuf, 1024, 0);
				////if (nread > 0){
				////	//periksa apakah ada ETX
	   ////     pAcdBuf[nread] = '\0';	        
	   ////     pAcdBuf2 = strchr(pAcdBuf, 0x03);
				////	if (pAcdBuf2){	        	
	   ////     	//we got complete message, process it
	   ////     	//copy message not including stx and etx
	   ////     	acdMsgLen = (int)(pAcdBuf2 - acdBuf) - 1;
	   ////     	memcpy(acdMsg, acdBuf+1, acdMsgLen);	        	
	   ////     	acdMsg[acdMsgLen] = '\0';	        	
				////		//cti_OnACDMISData(cti_pvt, acdMsg, acdMsgLen);
				////		
				////		if(pAcdBuf+nread == (pAcdBuf2+1)){
				////			//all data processed, reset buffer							
				////			pAcdBuf = acdBuf;
				////		}else{
				////			int lendata;
				////			//geser ke depan							
				////			lendata = (int)(pAcdBuf+nread - pAcdBuf2);
				////			if(lendata>0)memmove(acdBuf, pAcdBuf2, lendata);
				////			pAcdBuf = acdBuf+lendata;
				////		}
	   ////     }else{
	   ////     	//incomplete message, advance buffer position	        	
	   ////     	pAcdBuf += nread;
	   ////     }
				////}
				break;
			default:
				break;			
		}
		// wait data from client

		//dbPing
		++dbPing;
		if(dbPing == 2000){
			tCtbMessage	*msgsend;
			char *buf;
			int  _len;
		
			dbPing = 0;
			msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
			buf = (char*)malloc(1024);	
			
			/* Send Event To DBLOG */
			ctbMsgInit(msgsend);			
			msgsend->Sender = MSG_SRC_CTI;
			msgsend->Type	 	= MSGTYPE_DBPING;
			msgsend->Count	= 0;		
			
			_len = ctbMsgEncode(msgsend, buf, 1024);
			dblog_PutMessage(buf, _len);	
			free(msgsend);
			free(buf);	
		}
    		
	}	
	return 0;
}

static int tapi_OnCtiMsgUseExt(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
		
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_USE_EXT;
	msgsend->Count 		= 5;
	ctbMsgInsertNumeric(msgsend, 0, msg->Fields[1].a.iVal);		// agent id
	ctbMsgInsertString (msgsend, 1, msg->Fields[3].a.szVal);	// ext
	logger_Print("CTI>> Checking extension %s\n", msg->Fields[3].a.szVal);
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[3].a.szVal);
	if(!device){
		/* device not found, send reply */		
		logger_Print("CTI>> extension %s not found in list\n", msg->Fields[3].a.szVal);
		ctbMsgInsertNumeric(msgsend, 2, 1);		// result
		ctbMsgInsertNumeric(msgsend, 3, 0);		// error code		
		ctbMsgInsertNumeric(msgsend, 4, 0);		// device status
	}else{		
		if (device->agentId != 0){
			/* device in use */
			logger_Print("CTI>> extension %s already used by %d\n", msg->Fields[3].a.szVal,device->agentId);
			if(device->agentId == msg->Fields[1].a.iVal){
				ctbMsgInsertNumeric(msgsend, 2, 0);		// result
				logger_Print("CTI>> This extension used by same agent,granted\n");
			}else{
				ctbMsgInsertNumeric(msgsend, 2, 1);		// result
				logger_Print("CTI>> This extension used by another agent,rejected\n");
			}
			ctbMsgInsertNumeric(msgsend, 3, 0);		// error code		
			ctbMsgInsertNumeric(msgsend, 4, 0);		// device status
		}else{
			ctbMsgInsertNumeric(msgsend, 2, 0);		// result
			ctbMsgInsertNumeric(msgsend, 3, 0);		// error code
			device->agentId = msg->Fields[1].a.iVal;
			device->groupId = msg->Fields[2].a.iVal;
			//ctbMsgInsertNumeric(msgsend, 4, tapi_StationIdleStatus(pvt, device));		// device status			
			ctbMsgInsertNumeric(msgsend, 4, 0);		// TAPI can not get exact status, assume idle
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);	
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;
}

static int tapi_OnCtiMsgRelExt(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
		
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_REL_EXT;
	msgsend->Count 		= 2;
	logger_Print("CTI>> Releasing extension %s\n", msg->Fields[2].a.szVal);
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			device->agentId = 0;
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;
}

//new tj 2008-04-15
static int tapi_OnCtiMsgHoldCall(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	HCALL call;



	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_HOLD_CALL;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	logger_Print("TAPI>> HOLD REquest from ext %s,agent %d\n",msg->Fields[2].a.szVal,msg->Fields[1].a.iVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			call = (HCALL)device->activeCall;
			device->secondCall=call;
			lineHold(call);
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;

}

static int tapi_OnCtiMsgAnswerCall(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	HCALL call;	

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	logger_Print("TAPI>> ANSWER CALL REquest\n");	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_ANSWER_CALL;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			call = (HCALL)device->activeCall;
			printf("lineAnswer()\n");
			lineAnswer(call,NULL,0);
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;

}

static int tapi_OnCtiMsgRetrieveCall(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	HCALL call;

	

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	logger_Print("TAPI>> UnHOLD REquest\n");	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_RETR_CALL;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			call = (HCALL)device->secondCall;
			lineUnhold(call);
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;

}

static int tapi_OnCtiMsgDropCall(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	HCALL call;	

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	logger_Print("TAPI>> DROP CALL REquest\n");	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_HOLD_CALL;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			call = (HCALL)device->activeCall;
			lineDrop(call,NULL,0);
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;

}

static int tapi_OnCtiMsgMakeCall(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	LINECALLPARAMS LineParams;
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_MAKE_CALL;
	msgsend->Count 		= 2;
	logger_Print("CTI>>  MAke CAll REquest from extension %s to %s \n", msg->Fields[6].a.szVal,msg->Fields[2].a.szVal);
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[6].a.szVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			memset( &LineParams, 0, sizeof( LINECALLPARAMS ) );
			LineParams.dwTotalSize = sizeof( LINECALLPARAMS );			
			lineMakeCall(device->hLine, &device->activeCall,msg->Fields[2].a.szVal,0,&LineParams);
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;

}

static int tapi_OnCtiMsgAutoXferCall(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	LINECALLPARAMS LineParams;
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_MAKE_CALL;
	msgsend->Count 		= 2;
	logger_Print("CTI>>  Blind Transfer CAll REquest from extension %s to %s \n", msg->Fields[5].a.szVal,msg->Fields[3].a.szVal);
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[5].a.szVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			memset( &LineParams, 0, sizeof( LINECALLPARAMS ) );
			LineParams.dwTotalSize = sizeof( LINECALLPARAMS );			
			lineBlindTransfer(device->activeCall,msg->Fields[3].a.szVal,0);
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;
}

//end new 2008-04-15
static int tapi_ProcessCtiMsg(TCTIPvt *pvt, unsigned char *message, int len){
	tCtbMessage	*msg;
	
	msg = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	if (!msg){
		logger_Print("malloc failed!\n");
		return -1;
	}
	
	/* parse message */
	ctbMsgInit(msg);
	ctbMsgDecode(msg, message, len);
	
	/* process message */
	switch (msg->Sender){
		case MSG_SRC_AGENTHANDLER:		// message from agent handler
			switch(msg->Type){
				case MSGTYPE_USE_EXT:					
					tapi_OnCtiMsgUseExt(pvt, msg);
					break;
				case MSGTYPE_REL_EXT:
					tapi_OnCtiMsgRelExt(pvt, msg);
					break;
				case MSGTYPE_DROP_CALL:
					tapi_OnCtiMsgDropCall(pvt, msg);
					break;
				case MSGTYPE_ANSWER_CALL:
					tapi_OnCtiMsgAnswerCall(pvt, msg);
					break;
				case MSGTYPE_HOLD_CALL:
					tapi_OnCtiMsgHoldCall(pvt, msg);
					break;
				case MSGTYPE_RETR_CALL:
					tapi_OnCtiMsgRetrieveCall(pvt, msg);
					break;
				case MSGTYPE_AUTOXFER_CALL:
					tapi_OnCtiMsgAutoXferCall(pvt, msg);
					break;
				case MSGTYPE_MAKE_CALL:
					tapi_OnCtiMsgMakeCall(pvt, msg);
					break;
			}
			break;
		case MSG_SRC_MANAGER:
			switch(msg->Type){
				case MSGTYPE_ADD_STATION:
					//asai_OnMgrAddStation(pvt, msg);
					break;
				case MSGTYPE_REM_STATION:
					//asai_OnMgrRemStation(pvt, msg);
					break;
				case MSGTYPE_CHA_STATION:
					//asai_OnMgrChaStation(pvt, msg);
					break;
			}
			break;
		default:
			break;
	}
	
	
	ctbMsgFree(msg);
	free(msg);
	return 0;
}


/*==================log ==============

11:35:26 smartappsvr[1540]: TAPI>> Alerting: No body registered logged on this d
evice
11:35:26 smartappsvr[1540]: TAPI>> Ringing
11:35:26 smartappsvr[1540]: TAPI>> Originating
11:35:26 smartappsvr[1540]: TAPI>> Call Info
11:35:28 smartappsvr[1540]: TAPI>> Connected
11:35:28 smartappsvr[1540]: TAPI>> EVENT_CONNECTED: Device 208
11:35:28 smartappsvr[1540]: TAPI>> Call Info
11:35:28 smartappsvr[1540]: TAPI>> Connected
11:35:28 smartappsvr[1540]: TAPI>> EVENT_CONNECTED: Device 209
11:35:28 smartappsvr[1540]: TAPI>> Call Info
11:35:28 smartappsvr[1540]: TAPI>> Disconnected
11:35:28 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 209
11:35:28 smartappsvr[1540]: TAPI>> IDLE
11:35:28 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:35:28 smartappsvr[1540]: TAPI>> Disconnected
11:35:28 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 208
11:35:29 smartappsvr[1540]: TAPI>> IDLE
11:35:29 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:35:29 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810046
11:35:29 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on agent device 208

11:35:29 smartappsvr[1540]:     CallId:    0x00000000
11:35:29 smartappsvr[1540]:     Caller:    208
11:35:29 smartappsvr[1540]:     Called:
11:35:29 smartappsvr[1540]:     Connected:
11:35:29 smartappsvr[1540]:     Trunk:     -1
11:35:29 smartappsvr[1540]: TAPI>> Offhook
11:35:31 smartappsvr[1540]: TAPI>> Dialing
11:35:31 smartappsvr[1540]: TAPI>> LINECALLSTATE_DIALING: Device 208
11:35:31 smartappsvr[1540]:     CallId:    0x00000000
11:35:31 smartappsvr[1540]:     Caller:    208
11:35:31 smartappsvr[1540]:     Called:
11:35:31 smartappsvr[1540]:     Connected:
11:35:31 smartappsvr[1540]:     Trunk:     -1
11:35:32 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810024
11:35:32 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on agent device 209

11:35:32 smartappsvr[1540]:     CallId:    0x00000000
11:35:32 smartappsvr[1540]:     Caller:    208
11:35:32 smartappsvr[1540]:     Called:    209
11:35:32 smartappsvr[1540]:     Connected:
11:35:32 smartappsvr[1540]:     Trunk:     -1
11:35:32 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Internal call
11:35:32 smartappsvr[1540]: TAPI>> Alerting: No body registered logged on this d
evice
11:35:32 smartappsvr[1540]: TAPI>> Ringing
11:35:32 smartappsvr[1540]: TAPI>> Originating
11:35:32 smartappsvr[1540]: TAPI>> Call Info
11:35:34 smartappsvr[1540]: TAPI>> Disconnected
11:35:34 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 208
11:35:34 smartappsvr[1540]: TAPI>> IDLE
11:35:34 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:35:34 smartappsvr[1540]: TAPI>> Disconnected
11:35:34 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 209
11:35:34 smartappsvr[1540]: TAPI>> IDLE
11:35:34 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:35:47 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810024
11:35:47 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on agent device 209

11:35:47 smartappsvr[1540]:     CallId:    0x00000000
11:35:47 smartappsvr[1540]:     Caller:    209
11:35:47 smartappsvr[1540]:     Called:
11:35:47 smartappsvr[1540]:     Connected:
11:35:47 smartappsvr[1540]:     Trunk:     -1
11:35:47 smartappsvr[1540]: TAPI>> Offhook
11:35:49 smartappsvr[1540]: TAPI>> Dialing
11:35:49 smartappsvr[1540]: TAPI>> LINECALLSTATE_DIALING: Device 209
11:35:49 smartappsvr[1540]:     CallId:    0x00000000
11:35:49 smartappsvr[1540]:     Caller:    209
11:35:49 smartappsvr[1540]:     Called:
11:35:49 smartappsvr[1540]:     Connected:
11:35:49 smartappsvr[1540]:     Trunk:     -1
11:35:50 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x08103be
11:35:50 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on IVR device 222
11:35:50 smartappsvr[1540]:     CallId:    0x00000000
11:35:50 smartappsvr[1540]:     Caller:    209
11:35:50 smartappsvr[1540]:     Called:    602
11:35:50 smartappsvr[1540]:     Connected:
11:35:50 smartappsvr[1540]:     Trunk:     -1
11:35:50 smartappsvr[1540]: TAPI>> Adding CallSession to list w/size=0
11:35:50 smartappsvr[1540]: TAPI>> Ringing
11:35:50 smartappsvr[1540]: EVENTLOGGER>> Call offered data
11:35:50 smartappsvr[1540]: TAPI>> Originating
11:35:50 smartappsvr[1540]: EVENTLOGGER>> Call alerting data
11:35:51 smartappsvr[1540]: TAPI>> Call Info
11:35:58 smartappsvr[1540]: TAPI>> Disconnected
11:35:58 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 222
11:35:58 smartappsvr[1540]: TAPI>> Disconnect: Call Session deleted...
11:35:58 smartappsvr[1540]: EVENTLOGGER>> Call disconnected data
11:35:58 smartappsvr[1540]: TAPI>> IDLE
11:35:58 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:35:59 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x081039c
11:35:59 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on IVR device 223
11:35:59 smartappsvr[1540]:     CallId:    0x00000000
11:35:59 smartappsvr[1540]:     Caller:    209
11:35:59 smartappsvr[1540]:     Called:    602
11:35:59 smartappsvr[1540]:     Connected:
11:35:59 smartappsvr[1540]:     Trunk:     -1
11:35:59 smartappsvr[1540]: TAPI>> Adding CallSession to list w/size=0
11:35:59 smartappsvr[1540]: EVENTLOGGER>> Call offered data
11:35:59 smartappsvr[1540]: TAPI>> Ringing
11:35:59 smartappsvr[1540]: EVENTLOGGER>> Call alerting data
11:35:59 smartappsvr[1540]: TAPI>> Originating
11:36:06 smartappsvr[1540]: TAPI>> Disconnected
11:36:06 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 223
11:36:06 smartappsvr[1540]: TAPI>> Disconnect: Call Session deleted...
11:36:06 smartappsvr[1540]: EVENTLOGGER>> Call disconnected data
11:36:06 smartappsvr[1540]: TAPI>> IDLE
11:36:06 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:36:07 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810369
11:36:07 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on IVR device 224
11:36:07 smartappsvr[1540]:     CallId:    0x00000000
11:36:07 smartappsvr[1540]:     Caller:    209
11:36:07 smartappsvr[1540]:     Called:    602
11:36:07 smartappsvr[1540]:     Connected:
11:36:07 smartappsvr[1540]:     Trunk:     -1
11:36:07 smartappsvr[1540]: TAPI>> Adding CallSession to list w/size=0
11:36:07 smartappsvr[1540]: TAPI>> Ringing
11:36:07 smartappsvr[1540]: EVENTLOGGER>> Call offered data
11:36:07 smartappsvr[1540]: TAPI>> Originating
11:36:07 smartappsvr[1540]: EVENTLOGGER>> Call alerting data
11:36:15 smartappsvr[1540]: TAPI>> Disconnected
11:36:15 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 224
11:36:15 smartappsvr[1540]: TAPI>> Disconnect: Call Session deleted...
11:36:15 smartappsvr[1540]: EVENTLOGGER>> Call disconnected data
11:36:15 smartappsvr[1540]: TAPI>> IDLE
11:36:15 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:36:15 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810347
11:36:15 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on IVR device 225
11:36:15 smartappsvr[1540]:     CallId:    0x00000000
11:36:15 smartappsvr[1540]:     Caller:    209
11:36:15 smartappsvr[1540]:     Called:    602
11:36:15 smartappsvr[1540]:     Connected:
11:36:15 smartappsvr[1540]:     Trunk:     -1
11:36:15 smartappsvr[1540]: TAPI>> Adding CallSession to list w/size=0
11:36:15 smartappsvr[1540]: TAPI>> Ringing
11:36:15 smartappsvr[1540]: EVENTLOGGER>> Call offered data
11:36:15 smartappsvr[1540]: TAPI>> Originating
11:36:15 smartappsvr[1540]: EVENTLOGGER>> Call alerting data
11:36:19 smartappsvr[1540]: TAPI>> Disconnected
11:36:19 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 209
11:36:19 smartappsvr[1540]: TAPI>> IDLE
11:36:19 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:36:19 smartappsvr[1540]: TAPI>> Disconnected
11:36:19 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 225
11:36:19 smartappsvr[1540]: TAPI>> Disconnect: Call Session deleted...
11:36:19 smartappsvr[1540]: EVENTLOGGER>> Call disconnected data
11:36:19 smartappsvr[1540]: TAPI>> IDLE
11:36:19 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:36:56 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810024
11:36:56 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on agent device 209

11:36:56 smartappsvr[1540]:     CallId:    0x00000000
11:36:56 smartappsvr[1540]:     Caller:    209
11:36:56 smartappsvr[1540]:     Called:
11:36:56 smartappsvr[1540]:     Connected:
11:36:56 smartappsvr[1540]:     Trunk:     -1
11:36:56 smartappsvr[1540]: TAPI>> Offhook
11:36:59 smartappsvr[1540]: TAPI>> Dialing
11:36:59 smartappsvr[1540]: TAPI>> LINECALLSTATE_DIALING: Device 209
11:36:59 smartappsvr[1540]:     CallId:    0x00000000
11:36:59 smartappsvr[1540]:     Caller:    209
11:36:59 smartappsvr[1540]:     Called:
11:36:59 smartappsvr[1540]:     Connected:
11:36:59 smartappsvr[1540]:     Trunk:     -1
11:37:00 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810325
11:37:00 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on IVR device 226
11:37:00 smartappsvr[1540]:     CallId:    0x00000000
11:37:00 smartappsvr[1540]:     Caller:    209
11:37:00 smartappsvr[1540]:     Called:    602
11:37:00 smartappsvr[1540]:     Connected:
11:37:00 smartappsvr[1540]:     Trunk:     -1
11:37:00 smartappsvr[1540]: TAPI>> Adding CallSession to list w/size=0
11:37:00 smartappsvr[1540]: EVENTLOGGER>> Call offered data
11:37:00 smartappsvr[1540]: TAPI>> Ringing
11:37:00 smartappsvr[1540]: TAPI>> Originating
11:37:00 smartappsvr[1540]: EVENTLOGGER>> Call alerting data
11:37:00 smartappsvr[1540]: TAPI>> Call Info
11:37:08 smartappsvr[1540]: TAPI>> Disconnected
11:37:08 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 226
11:37:08 smartappsvr[1540]: TAPI>> Disconnect: Call Session deleted...
11:37:08 smartappsvr[1540]: EVENTLOGGER>> Call disconnected data
11:37:08 smartappsvr[1540]: TAPI>> IDLE
11:37:08 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:37:08 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810314
11:37:08 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on IVR device 227
11:37:08 smartappsvr[1540]:     CallId:    0x00000000
11:37:08 smartappsvr[1540]:     Caller:    209
11:37:08 smartappsvr[1540]:     Called:    602
11:37:08 smartappsvr[1540]:     Connected:
11:37:08 smartappsvr[1540]:     Trunk:     -1
11:37:08 smartappsvr[1540]: TAPI>> Adding CallSession to list w/size=0
11:37:08 smartappsvr[1540]: TAPI>> Ringing
11:37:08 smartappsvr[1540]: EVENTLOGGER>> Call offered data
11:37:08 smartappsvr[1540]: EVENTLOGGER>> Call alerting data
11:37:08 smartappsvr[1540]: TAPI>> Originating
11:37:16 smartappsvr[1540]: TAPI>> Disconnected
11:37:16 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 227
11:37:16 smartappsvr[1540]: TAPI>> Disconnect: Call Session deleted...
11:37:16 smartappsvr[1540]: EVENTLOGGER>> Call disconnected data
11:37:16 smartappsvr[1540]: TAPI>> IDLE
11:37:16 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:37:16 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x08102f2
11:37:16 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on IVR device 228
11:37:16 smartappsvr[1540]:     CallId:    0x00000000
11:37:16 smartappsvr[1540]:     Caller:    209
11:37:16 smartappsvr[1540]:     Called:    602
11:37:16 smartappsvr[1540]:     Connected:
11:37:16 smartappsvr[1540]:     Trunk:     -1
11:37:16 smartappsvr[1540]: TAPI>> Adding CallSession to list w/size=0
11:37:16 smartappsvr[1540]: TAPI>> Ringing
11:37:16 smartappsvr[1540]: EVENTLOGGER>> Call offered data
11:37:16 smartappsvr[1540]: TAPI>> Originating
11:37:16 smartappsvr[1540]: EVENTLOGGER>> Call alerting data
11:37:22 smartappsvr[1540]: TAPI>> Disconnected
11:37:22 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 209
11:37:22 smartappsvr[1540]: TAPI>> IDLE
11:37:22 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:37:22 smartappsvr[1540]: TAPI>> Disconnected
11:37:22 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 228
11:37:22 smartappsvr[1540]: TAPI>> Disconnect: Call Session deleted...
11:37:22 smartappsvr[1540]: EVENTLOGGER>> Call disconnected data
11:37:22 smartappsvr[1540]: TAPI>> IDLE
11:37:22 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:39:26 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810024
11:39:26 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on agent device 209

11:39:26 smartappsvr[1540]:     CallId:    0x00000000
11:39:26 smartappsvr[1540]:     Caller:    209
11:39:26 smartappsvr[1540]:     Called:
11:39:26 smartappsvr[1540]:     Connected:
11:39:26 smartappsvr[1540]:     Trunk:     -1
11:39:26 smartappsvr[1540]: TAPI>> Offhook
11:39:27 smartappsvr[1540]: TAPI>> Dialing
11:39:27 smartappsvr[1540]: TAPI>> LINECALLSTATE_DIALING: Device 209
11:39:27 smartappsvr[1540]:     CallId:    0x00000000
11:39:27 smartappsvr[1540]:     Caller:    209
11:39:27 smartappsvr[1540]:     Called:
11:39:27 smartappsvr[1540]:     Connected:
11:39:27 smartappsvr[1540]:     Trunk:     -1
11:39:29 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810046
11:39:29 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on agent device 208

11:39:29 smartappsvr[1540]:     CallId:    0x00000000
11:39:29 smartappsvr[1540]:     Caller:    209
11:39:29 smartappsvr[1540]:     Called:    208
11:39:29 smartappsvr[1540]:     Connected:
11:39:29 smartappsvr[1540]:     Trunk:     -1
11:39:29 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Internal call
11:39:29 smartappsvr[1540]: TAPI>> Alerting: No body registered logged on this d
evice
11:39:29 smartappsvr[1540]: TAPI>> Ringing
11:39:29 smartappsvr[1540]: TAPI>> Originating
11:39:29 smartappsvr[1540]: TAPI>> Call Info
11:39:31 smartappsvr[1540]: TAPI>> Connected
11:39:31 smartappsvr[1540]: TAPI>> EVENT_CONNECTED: Device 208
11:39:31 smartappsvr[1540]: TAPI>> Call Info
11:39:31 smartappsvr[1540]: TAPI>> Connected
11:39:31 smartappsvr[1540]: TAPI>> EVENT_CONNECTED: Device 209
11:39:31 smartappsvr[1540]: TAPI>> Call Info
11:40:17 smartappsvr[1540]: TAPI>> Disconnected
11:40:17 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 209
11:40:17 smartappsvr[1540]: TAPI>> IDLE
11:40:17 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:40:17 smartappsvr[1540]: TAPI>> Disconnected
11:40:17 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 208
11:40:17 smartappsvr[1540]: TAPI>> IDLE
11:40:17 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:40:17 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: Device handle 0x0810046
11:40:17 smartappsvr[1540]: TAPI>> LINE_APPNEWCALL: New call on agent device 208

11:40:17 smartappsvr[1540]:     CallId:    0x00000000
11:40:17 smartappsvr[1540]:     Caller:    208
11:40:17 smartappsvr[1540]:     Called:
11:40:17 smartappsvr[1540]:     Connected:
11:40:17 smartappsvr[1540]:     Trunk:     -1
11:40:17 smartappsvr[1540]: TAPI>> Offhook
11:40:19 smartappsvr[1540]: TAPI>> Disconnected
11:40:19 smartappsvr[1540]: TAPI>> EVENT_DISCONNECTED: Device 208
11:40:19 smartappsvr[1540]: TAPI>> IDLE
11:40:19 smartappsvr[1540]: TAPI>> Call Session not found... check if this call
id is on hold
11:55:39 smartappsvr[1540]: EVENTLOGGER>> DB PING


*/