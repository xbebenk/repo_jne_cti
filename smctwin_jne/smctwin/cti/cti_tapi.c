
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
#include <time.h>


#pragma warning(disable : 4996)  // deprecated CRT function

int ivr_active_call=0;
int agent_active_call=0;
int total_active_call=0;
int que_call=0;

typedef struct _t_TapiCallInfo{
	HLINE	hLine;
	int		callId;
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
static t_TapiDevice* tapi_FindDeviceByPort(int type, char* ch){
	t_TapiDevice *device;

	device = listFirst(&TapiDevices);
	while(device){
		if ((device->type == type) &&
			!strcmp(device->ivr_port, ch))
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
static t_CallSession* tapi_FindCallSessionByCallID(int call_id){	
	t_CallSession *cs;
	int i=1;

	cs = listFirst(&CallSessions);	
	while(cs){
		if (cs->call_id == call_id)
			return cs;
		else
			cs = listNext(cs);
		i++;
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

static t_CallSession* tapi_FindCallSessionByDeviceNumber(char *number){	
	t_CallSession *cs;

	cs = listFirst(&CallSessions);
	printf("1\n");
	while(cs){
		if (!strcmp(cs->connected_number, number)){
			printf("2\n");
			return cs;
		}else{
			printf("3\n");
			cs = listNext(cs);
		}
	}
	return NULL;
}
static t_CallSession* tapi_FindCallSessionByCallerNumber(char *number){	
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
      logger_Print(3,4,"TAPI>> tapi_GetCallInfo - out of memory\n");
      return -1;
    }

    ci->dwTotalSize = (DWORD)ci_size;
    result = lineGetCallInfo(hCall, ci);
   
    if ((result < 0) && (result != LINEERR_STRUCTURETOOSMALL)) {
      logger_Print(3,4,"TAPI>> error 0x%08lx calling lineGetCallInfo\n");
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

  free(ci);
	return 0;
}

static char* tapi_CreateSessionId(char *buf, HCALL call, int buflen){
	struct timeval stimeval;
	
	gettimeofday(&stimeval, NULL);	
	_snprintf(buf, buflen, "%010ld%d", stimeval.tv_sec,call);
	
	//1357922555 65545
	if (buflen >= 16)
		buf[16] ='\0';
	logger_Print(3,4,"session_key created = %s\n", buf);
	return buf;
}

static t_CallSession* tapi_CreateCallSession(TCTIPvt *pvt, HLINE line, HCALL call, int direction){
	t_CallSession *cs = NULL;	
	
	cs = listNewItem(t_CallSession);
	tapi_CreateSessionId(cs->session_key,call,32);
	cs->line      = line;
	cs->origCall  = call;
	cs->currCall  = call;	
	cs->direction = direction;	
		
	/* insert to call session list */
	printf("TAPI>> Adding CallSession %s, cs_count=%d\n", cs->session_key, listSize(&CallSessions)+1);
	logger_Print(3,5,"TAPI>> Adding CallSession %s, cs_count=%d\n", cs->session_key, listSize(&CallSessions)+1);
	listInsertLast(&CallSessions, cs);	
	return cs;
}


int tapi_LoadDevices(TCTIPvt *cti_pvt){
	tDbConn dbConn;
	tDbSet  dbSet, dbSet2;
  char sql[2048];
  int  sqlLen;
  t_TapiDevice *device;
  
  logger_Print(3,4,"Tapi>> Loading Devices\n");
  
  listInit(&TapiDevices);
  listInit(&CallSessions);  
	
	/* connect to DB */
	dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
  if (!dbConn){
  	logger_Print(3,4,"Tapi>> DB Connection failed\n");
  	return -1;
  }  
  
  /* load VDN */
	sqlLen = sprintf(sql, "SELECT a.vdn vdn, b.hunting_number, a.is_direct, b.id, a.id, a.routing_alg, a.trash_target, a.tapi_id "
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
		device->tapiId = dbLib->getIntFieldByIdx(dbSet, 7);

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
		
		logger_Print(3,5,"\tFound VDN %s -> hunt=%s, direct=%s, group=%d, alg=%d\n", 
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
		
		logger_Print(3,5,"\tFound Extension %s, ip=%s\n", device->number, device->ipAddress);
		
		/* insert to device list */
		listInsertLast(&TapiDevices, device);
  }  
  dbLib->closeQuery(dbSet);

	/* load IVR Extensions */
  sqlLen = sprintf(sql, "SELECT	a.ext_number, a.ext_type, a.tapi_id,a.ext_port "
  											"FROM	extension_ivr a "
												"WHERE a.ext_status = 0 order by	a.ext_number");
	dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){
  	device = listNewItem(t_TapiDevice);
  	
  	device->type = TAPIDEVICE_IVR;
  	sprintf(device->number, 		"%s", dbLib->getStringFieldByIdx(dbSet, 0));		
		device->model  = dbLib->getIntFieldByIdx(dbSet, 1);
		device->tapiId = dbLib->getIntFieldByIdx(dbSet, 2);
	sprintf(device->ivr_port, "%s", dbLib->getStringFieldByIdx(dbSet, 3));
		
		logger_Print(3,5,"\tFound IVR Extension %s\n", device->number);
		
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
		logger_Print(3,5,"\tFound Hunting %s\n", device->number);
		
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
	int cnt=0;
		
	logger_Print(3,4,"TAPI>> Initializing device control\n");
	device = listFirst(&TapiDevices);
	while(device){	
		device->device_status = 0x0000; //init value
		switch(device->type){
			case TAPIDEVICE_VDN:
				// get TAPI version supported by device				
				break;
			case TAPIDEVICE_AGENT:
				// get TAPI version supported by device				
				dwVersion = TAPI_CURRENT_VERSION;
				ret = lineNegotiateAPIVersion(hLineApp, device->tapiId, 0x00010003, 0x00030001, &dwVersion, &extId);
				ret = lineOpen(hLineApp, device->tapiId, &device->hLine, dwVersion, 0, 0,6,// LINECALLPRIVILEGE_OWNER | LINECALLPRIVILEGE_MONITOR ,
											 LINEMEDIAMODE_INTERACTIVEVOICE , NULL);				
				
				logger_Print(3,4,"TAPI>> LineOpen %s, Id 0x%04x, hLine=0x%08x TAPI v.%d.%d\n", 
											device->number, device->tapiId, device->hLine, (dwVersion>>16), (dwVersion & 0xFFFF));
				cnt++;
				break;
			case TAPIDEVICE_IVR:
				// get TAPI version supported by device				
				dwVersion = TAPI_CURRENT_VERSION;
				ret = lineNegotiateAPIVersion(hLineApp, device->tapiId, 0x00010003, 0x00030001, &dwVersion, &extId);
				ret = lineOpen(hLineApp, device->tapiId, &device->hLine, dwVersion, 0, 0, LINECALLPRIVILEGE_OWNER | LINECALLPRIVILEGE_MONITOR ,
											 LINEMEDIAMODE_INTERACTIVEVOICE , NULL);				
				
				logger_Print(3,4,"TAPI>> LineOpen %s, Id 0x%04x, hLine=0x%08x TAPI v%d.%d\n", 
											device->number, device->tapiId, device->hLine, (dwVersion>>16), (dwVersion & 0xFFFF));
				cnt++;
				break;
			default:				
				break;
		}
		device = listNext(device);		
	}
	printf("Loaded devices: %d\n",cnt);
	return 0;
}

static void CALLBACK lineCallbackFunc(
    DWORD dwDevice, DWORD dwMsg, DWORD dwCallbackInstance, 
    DWORD dwParam1, DWORD dwParam2, DWORD dwParam3){
	// doing nothing
}

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
		logger_Print(3,4,"lineInitialize failed: \n");
		logger_Print(3,4,"Return: 0x%08x\n", ret);
		return 0;
	}

	cti_pvt->hTapiEvent = li.Handles.hEvent;
	logger_Print(3,4,"Found %d device(s)\n", dwNumDevs);	


	//Init ACD-MIS connection
	//return tapi_InitACDMIS(cti_pvt);	
	return 0;
}

static int tapi_OnEventCallInfo(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_CallSession *cs=NULL;
	t_TapiDevice   *device;
	t_TapiCallInfo callInfo;
	HCALL          call;
	//tCtbMessage	*msgsend;
	//char *buf;
	//int  len;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print(3,4,"TAPI>> EVENT_CALLINFO: Device Not Found\n");
		return 2;
	}
	
	if(device->type == TAPIDEVICE_AGENT && device->agentId > 0){
		if(callInfo.direction == LINECALLORIGIN_INBOUND || callInfo.direction == LINECALLORIGIN_EXTERNAL){
			logger_Print(3,5,"TAPI>> EVENT_CALLINFO on Device %s direction = 0x%08x\n", device->number,callInfo.direction);
			logger_Print(3,5,"\tdirection:    0x%08x\n", callInfo.direction);
			logger_Print(3,5,"\tdirection:    LINECALLORIGIN_INBOUND 80 or LINECALLORIGIN_EXTERNAL 04\n");
			logger_Print(3,5,"\tActiveCall:    %d\n",device->activeCall);
			logger_Print(3,5,"\tCall:    %d\n",call);
			logger_Print(3,5,"\tCaller:    %s\n",callInfo.caller);
			logger_Print(3,5,"\tCalled:    %s\n",callInfo.called);
			logger_Print(3,5,"\tConnected:    %s\n",callInfo.connected);
		}
		cs = tapi_FindCallSessionByCall(call);
		if(cs){
			strcpy(cs->connected_number, callInfo.connected);
			wallboard_extstatus(device->number,cs->status,cs->connected_number);

			//msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
			//buf = (char*)malloc(1024);	
			//	
			//// cek device on CTI 
			//ctbMsgInit(msgsend);			
			//msgsend->Sender = MSG_SRC_CTI;
			//msgsend->Type	= MSGTYPE_EVENT_CALLCONNECTED;
			//msgsend->Count	= 12;
			//
			//ctbMsgInsertNumeric(msgsend, 0, 0);
			//ctbMsgInsertString (msgsend, 1, cs?cs->calling_number:"****");
			//ctbMsgInsertNumeric(msgsend, 2, cs?cs->trunk_number:0);
			//ctbMsgInsertNumeric(msgsend, 3, cs?cs->trunk_member:0);	
			//ctbMsgInsertString (msgsend, 4, cs?cs->called_number:"****");
			//ctbMsgInsertString (msgsend, 5, cs?cs->connected_number:"****");		
			//ctbMsgInsertString (msgsend, 6, cs?cs->session_key:"");
			//ctbMsgInsertString (msgsend, 7, device->number);		     /* extension */
			//ctbMsgInsertNumeric(msgsend, 8, device->agentId);
			//ctbMsgInsertString (msgsend, 9, device->ipAddress);
			//ctbMsgInsertNumeric(msgsend,10, device->groupId);
			//ctbMsgInsertNumeric(msgsend,11, cs?cs->direction:1);
			//
			//
			//len = ctbMsgEncode(msgsend, buf, 1024);
			//agent_DispatchMessage(device->agentId, buf, len);	
			//free(msgsend);
			//free(buf);
		}
	}
	//
	return 0;
}

/**
LINE_APPNEWCALL
*/

static int tapi_OnEventAgentNewCall(TCTIPvt *cti_pvt, LPLINEMESSAGE msg, t_TapiDevice *device){
	t_CallSession	*cs=NULL;
	t_TapiCallInfo	calInfo;
	HCALL			call;
	int	direction;
	char last_session[256];

	call = (HCALL)msg->dwParam2;
	device->activeCall = call;
	tapi_GetCallInfo(call, &calInfo);

	logger_Print(3,4,"TAPI>> LINE_APPNEWCALL on Agent Device %s\n", device->number);
	switch(calInfo.direction){
		case LINECALLORIGIN_OUTBOUND:
			logger_Print(3,5,"\tCALL DIRECTION: OUTBOND = %d\n",calInfo.direction);
			break;
		case LINECALLORIGIN_INTERNAL:
			logger_Print(3,5,"\tCALL DIRECTION: INTERNAL = %d\n",calInfo.direction);
			break;
		case LINECALLORIGIN_EXTERNAL:
			logger_Print(3,5,"\tCALL DIRECTION: EXTERNAL = %d\n",calInfo.direction);
			break;
		case LINECALLORIGIN_UNKNOWN:
			logger_Print(3,5,"\tCALL DIRECTION: UNKNOWN = %d\n",calInfo.direction);
			break;
		case LINECALLORIGIN_UNAVAIL:
			logger_Print(3,5,"\tCALL DIRECTION: UNAVAIL = %d\n",calInfo.direction);
			break;
		case LINECALLORIGIN_CONFERENCE:
			logger_Print(3,5,"\tCALL DIRECTION: CONFERENCE = %d\n",calInfo.direction);
			break;
		case LINECALLORIGIN_INBOUND:
			logger_Print(3,5,"\tCALL DIRECTION: INBOUND = %d\n",calInfo.direction);
			break;
		default:
			logger_Print(3,5,"\tCALL DIRECTION: %d\n", calInfo.direction);
			break;
	}
	logger_Print(3,5,"\tCallId:    0x%08x\n", calInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", calInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", calInfo.called);	
	logger_Print(3,5,"\tConnected: %s\n", calInfo.connected);	
	logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
	logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
	logger_Print(3,5,"\tconsultCall:  %d\n\n", device->consultCall);

	//create call session on agent
	if (calInfo.direction & (LINECALLORIGIN_OUTBOUND | LINECALLORIGIN_UNKNOWN)){
		//handled on dialtone event
		return 0;
	}
	if(calInfo.direction == LINECALLORIGIN_INTERNAL){
		cs=tapi_FindCallSessionByCallerNumber(calInfo.caller);
			if(cs){
				sprintf(last_session,cs->session_key);
				logger_Print(3,4,"last_session:  %s, device=%s\n\n", last_session, cs->connected_number);
			}else{
				sprintf(last_session,"last_session");
			}
	}
	switch(calInfo.direction){
		case LINECALLORIGIN_OUTBOUND:
		case LINECALLORIGIN_UNKNOWN:
			direction = CALLDIR_OUTGOING;
			break;
		case LINECALLORIGIN_EXTERNAL:
		case LINECALLORIGIN_INBOUND:
		case LINECALLORIGIN_INTERNAL:
			direction = CALLDIR_INCOMING;
			break;
		default:
			direction = CALLDIR_INTERNAL;
			break;
	}

	if((cs = tapi_CreateCallSession(cti_pvt, device->hLine, call, direction))){
		cs->trunk_number = 0;
		cs->call_id = calInfo.callId;
		sprintf(cs->calling_number, "%s", calInfo.caller);
		sprintf(cs->called_number,  "%s", calInfo.called);
		if(direction == CALLDIR_INCOMING){
			cs->status = CALLSTATUS_AGENT_RINGING;
		}
			
	}
	
	if (device->agentId > 0 && direction == CALLDIR_INCOMING){
	//if (device->agentId > 0){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;
		
		printf("TAPI>> Alerting: On agent device %s.\n",device->number);
		
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);	
			
		// cek device on CTI 
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 	= MSGTYPE_EVENT_CALLALERTING;					// alerting event
		msgsend->Count		= 14;
		
		ctbMsgInsertNumeric(msgsend, 0, 0);
		ctbMsgInsertString (msgsend, 1, cs?cs->calling_number:calInfo.caller);
		ctbMsgInsertNumeric(msgsend, 2, 0);
		ctbMsgInsertNumeric(msgsend, 3, cs?cs->trunk_number:-1);	
		ctbMsgInsertString (msgsend, 4, cs?cs->called_number:"");
		ctbMsgInsertString (msgsend, 5, cs?cs->connected_number:device->number);
		ctbMsgInsertString (msgsend, 6, cs?cs->session_key:"");			// session_key 
		ctbMsgInsertString (msgsend, 7, device->number);		        // extension 
		ctbMsgInsertNumeric(msgsend, 8, device->agentId);		        // agent id 
		ctbMsgInsertString (msgsend, 9, device->ipAddress);		      // extension 
		ctbMsgInsertNumeric(msgsend,10, device->groupId);		        // group id	
		ctbMsgInsertNumeric(msgsend,11, cs?cs->direction:1);				// direction 
		ctbMsgInsertString (msgsend,12, NULL);											// ivr_data 
		ctbMsgInsertNumeric(msgsend,13, cs?cs->langId:0);						// call language, based on IVR selection 
		
		len = ctbMsgEncode(msgsend, buf, 1024);
		
		agent_DispatchMessage(device->agentId, buf, len);
		logger_Dump(buf, len);
		free(msgsend);
		free(buf);	
		
	}else{
		logger_Print(3,4,"TAPI>> Alerting: No body registered logged on this device %s\n",device->number);
	}

	// save event data to DB 
	if(cs){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;		
			
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);	
		ctbMsgInit(msgsend);	

		msgsend->Sender 	= MSG_SRC_CC_CTI;
		msgsend->Type	 		= MSGTYPE_EVENT_CALLOFFERED;
		msgsend->Count		= 9;
	  	
		ctbMsgInsertNumeric(msgsend, 0, 0);
		ctbMsgInsertString (msgsend, 1, cs->session_key);			   		// session_key
		ctbMsgInsertNumeric(msgsend, 2, 0);
		ctbMsgInsertNumeric(msgsend, 3, calInfo.trunkNo);
		ctbMsgInsertString (msgsend, 4, calInfo.caller);			//a-number  		
		ctbMsgInsertString (msgsend, 5, calInfo.called);			//b-number
		ctbMsgInsertString (msgsend, 6, calInfo.caller);			//c-number, on VDN c-number is d-number
		ctbMsgInsertNumeric(msgsend, 7, cs?cs->direction:0);	// direction
		ctbMsgInsertString (msgsend, 8, last_session?last_session:"0");
		len = ctbMsgEncode (msgsend, buf, 512);	
		dblog_PutMessage(buf, len);		

		//cek device on CTI 
				
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 	= MSGTYPE_EVENT_CALLALERTING;					// alerting event
		msgsend->Count		= 8;		
  	
  		ctbMsgInsertNumeric(msgsend, 0, msg->hDevice);
  		ctbMsgInsertString (msgsend, 1, cs->session_key);	
		ctbMsgInsertNumeric(msgsend, 2, cs?cs->status:0);					// call status
  		ctbMsgInsertString (msgsend, 3, device->number);				// c-number	
  		ctbMsgInsertNumeric(msgsend, 4, cs->direction);					// direction		
  		ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
  		ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group
		ctbMsgInsertString (msgsend, 7, cs?cs->calling_number:calInfo.caller);
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
		

	call = (HCALL)msg->dwParam2;	
	tapi_GetCallInfo(call, &calInfo);

	logger_Print(3,4,"TAPI>> LINE_APPNEWCALL on IVR Device %s\n", device->number);
	logger_Print(3,5,"\tCallId:    0x%08x\n", calInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", calInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", calInfo.called);	
	logger_Print(3,5,"\tConnected: %s\n", calInfo.connected);	

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
			cs->call_id = calInfo.callId;
			cs->is_reached = 2; // sebelum sampe di agent cs reach di set 2 supaya bila call putus di ivr call session di delete
			sprintf(cs->calling_number, "%s", calInfo.caller);
			sprintf(cs->called_number,  "%s", calInfo.called);
			sprintf(cs->vdn_number,     "%s", calInfo.called);
			sprintf(cs->connected_number,  "%s", device->number);
			//cs->status    = CALLSTATUS_OFFERED;	
			cs->status    = CALLSTATUS_IVR_RINGING;	 //cause already on IVR
		}
		device->activeCall = call;

		/* save call offered event data to DB */
		if(cs){
			tCtbMessage	*msgsend;
			char *buf;
			int  len;		
			
			//insert active session to device, untuk keperluan save ke database apabila callnya queue
			sprintf(device->session_key, "%s", cs->session_key);

			msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
			buf = (char*)malloc(1024);	
			ctbMsgInit(msgsend);

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
			
			free(msgsend);
			free(buf);
		}	
	return 0;
}


static int tapi_OnEventNewCall(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;	
		
	if (!(device = tapi_FindDeviceByHandle(msg->hDevice))){
		logger_Print(3,4,"TAPI>> LINE_APPNEWCALL: Device not registered\n");
		return 2;
	}

	//logger_Print(3,4,"TAPI>> LINE_APPNEWCALL: Device handle 0x08%lx\n", msg->hDevice);
	switch(device->type){
		case TAPIDEVICE_AGENT:
			tapi_OnEventAgentNewCall(cti_pvt, msg, device);
			++agent_active_call;
			++total_active_call;
			break;
		case TAPIDEVICE_IVR:
			tapi_OnEventIvrNewCall(cti_pvt, msg, device);
			++ivr_active_call;
			++total_active_call;
			break;
		case TAPIDEVICE_VDN:
			break;
		default:
			logger_Print(3,4,"TAPI>> LINE_APPNEWCALL: Device not supported\n");
			break;
	}
	printf("TAPI>> Active Call agent=%d ivr=%d total_active_call=%d\n",agent_active_call,ivr_active_call,total_active_call);
	return 0;
}

static int tapi_OnEventCallAlerting(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_CallSession *cs=NULL;
	t_TapiDevice   *device;
	t_TapiCallInfo callInfo;
	HCALL          call;


	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print(3,4,"TAPI>> LINECALLSTATE_ALERTING: Device Not Found\n");
		return 2;
	}
	logger_Print(3,5,"TAPI>> LINECALLSTATE_ALERTING on Device %s\n", device->number);
	logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", callInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", callInfo.called);	
	logger_Print(3,5,"\tConnected: %s\n", callInfo.connected);
	
	printf("[%s] Alerting, caller=%s callee=%s active_call=%d\n",device->number,callInfo.caller,callInfo.called,total_active_call);
	logger_Print(3,4,"[%s] Alerting, caller=%s callee=%s active_call=%d\n",device->number,callInfo.caller,callInfo.called,total_active_call);
	
	cs = tapi_FindCallSessionByCall(call);
	if(cs){
		if(device->type == TAPIDEVICE_IVR){
	//save to db	
			tCtbMessage	*msgsend;
			char *buf;
			int  len;		
			
			msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
			buf = (char*)malloc(1024);	

			ctbMsgInit(msgsend);			
			msgsend->Sender 	= MSG_SRC_CTI;
			msgsend->Type	 		= MSGTYPE_EVENT_CALLALERTING;					// alerting event
			msgsend->Count		= 7;		
		  	
			ctbMsgInsertNumeric(msgsend, 0, 0);
			ctbMsgInsertString (msgsend, 1, cs->session_key);			  
			ctbMsgInsertNumeric(msgsend, 2, cs?cs->status:1);						// call status
			ctbMsgInsertString (msgsend, 3, device->number);				// c-number	
			ctbMsgInsertNumeric(msgsend, 4, cs?cs->direction:1);					// direction		
			ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
			ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group				
			len = ctbMsgEncode (msgsend, buf, 512);	
			dblog_PutMessage(buf, len);

			free(msgsend);
			free(buf);

			wallboard_extstatus(device->number,cs->status,callInfo.caller);
		}else{
			cs->status = CALLSTATUS_AGENT_RINGING;
			acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_RINGING,cs?cs->session_key:"0");
			wallboard_extstatus(device->number,cs->status,callInfo.caller);
		}
	}
	return 0;
}


static int tapi_OnEventCallInitiated(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;	
	t_CallSession *cs=NULL;
	t_TapiCallInfo callInfo;
	HCALL call;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		printf("TAPI>> LINECALLSTATE_DIALING: Device Not Found\n");
		return 2;
	}
	logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALING: Device %s\n", device->number);
	logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", callInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", callInfo.called);	
	logger_Print(3,5,"\tConnected: %s\n\n", callInfo.connected);	
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
		logger_Print(3,4,"TAPI>> EVENT_CONNECTED: Device Not Found\n");
		return 2;
	}

	if (call == device->heldCall) device->heldCall=0;
	if (call == device->confCall) device->confCall=0;
	device->activeCall = call;
	switch(device->type){
		case TAPIDEVICE_AGENT:  //internal call
			logger_Print(3,4,"TAPI>> EVENT_CONNECTED on Agent Device %s\n", device->number);
			break;
		case TAPIDEVICE_IVR:  //inbound / external call
			logger_Print(3,4,"TAPI>> EVENT_CONNECTED on IVR Device %s\n", device->number);
			break;
		default:
			logger_Print(3,4,"TAPI>> EVENT_CONNECTED: Device %s\n", device->number);
			break;
	}

	logger_Print(3,5,"\tDirection:    %d\n", callInfo.direction);
	logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", callInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", callInfo.called);	
	logger_Print(3,5,"\tCall:    %d\n", call);	
	logger_Print(3,5,"\tConnected: %s\n\n", callInfo.connected);

	printf("[%s] Connected, caller=%s called=%s\n",device->number,callInfo.caller,callInfo.called);
	
	cs = tapi_FindCallSessionByCall(call);
	if(cs){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;
		
		if(callInfo.direction & (LINECALLORIGIN_EXTERNAL | LINECALLORIGIN_INBOUND)){
			cs->direction = CALLDIR_INCOMING;
		}
		
		sprintf(cs->connected_number,callInfo.connected);
		sprintf(cs->calling_number,callInfo.caller);
	
		switch(cs->status){
			case CALLSTATUS_IVR_RINGING:
				cs->status = CALLSTATUS_IVR_CONNECTED;
				sprintf(cs->connected_number,device->number);
				
				//ivrnotif_NewCall(device->number, cs->session_key);
				break;
			case CALLSTATUS_AGENT_RINGING:
				cs->status = CALLSTATUS_AGENT_CONNECTED;
				break;
			case CALLSTATUS_AGENT_ORIGINATED:
				
				cs->status = CALLSTATUS_AGENT_CONNECTED;
				cs->direction = CALLDIR_OUTGOING;
				sprintf(cs->connected_number,device->number);
				break;
		}
		
		wallboard_extstatus(device->number,cs->status,callInfo.caller);

		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);			
		
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 	= MSGTYPE_EVENT_CALLCONNECTED;
		msgsend->Count		= 8;		
	  	
		ctbMsgInsertNumeric(msgsend, 0, 0);
		ctbMsgInsertString (msgsend, 1, cs->session_key);			  
		ctbMsgInsertNumeric(msgsend, 2, cs->status);						// call status
		ctbMsgInsertString (msgsend, 3, cs->connected_number);	// c-number	
		ctbMsgInsertNumeric(msgsend, 4, cs->direction);					// direction		
		ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
		ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group
		ctbMsgInsertString (msgsend, 7, device->number);
		
		len = ctbMsgEncode (msgsend, buf, 512);	
		dblog_PutMessage(buf, len);

		free(msgsend);
		free(buf);		
	
		if (device->type == TAPIDEVICE_AGENT && device->agentId > 0){
			tCtbMessage	*msgsend;
			char *buf;
			int  len;
			
			device->device_status = 0x0001;
			
			msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
			buf = (char*)malloc(1024);	
				
			/* Send Event To Agent */
			ctbMsgInit(msgsend);			
			msgsend->Sender = MSG_SRC_CTI;
			msgsend->Type	= MSGTYPE_EVENT_CALLCONNECTED;
			msgsend->Count	= 12;
			
			ctbMsgInsertNumeric(msgsend, 0, 0);
			ctbMsgInsertString (msgsend, 1, cs?cs->calling_number:"****");
			ctbMsgInsertNumeric(msgsend, 2, cs?cs->trunk_number:0);
			ctbMsgInsertNumeric(msgsend, 3, cs?cs->trunk_member:0);	
			ctbMsgInsertString (msgsend, 4, cs?cs->called_number:"****");
			ctbMsgInsertString (msgsend, 5, cs?cs->connected_number:"****");		
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
			//change agent status
			acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_TALKING,cs?cs->session_key:"0");
		}
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
		logger_Print(3,4,"TAPI>> EVENT_DISCONNECTED: Device Not Found\n");
		return 2;
	}
	
	switch(device->type){
		case TAPIDEVICE_AGENT:  //internal call
			logger_Print(3,4,"TAPI>> EVENT_DISCONNECTED on Agent Device %s, %d\n", device->number,call);
			break;
		case TAPIDEVICE_IVR:  //inbound / external call
			logger_Print(3,4,"TAPI>> EVENT_DISCONNECTED on IVR Device %s\n", device->number);
			break;
		default:
			logger_Print(3,4,"TAPI>> EVENT_DISCONNECTED: Device %s\n", device->number);
			break;
	}
	
	logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", callInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", callInfo.called);	
	logger_Print(3,5,"\tConnected: %s\n\n", callInfo.connected);	
	printf("[%s] Disconnect, caller=%s called=%s\n",device->number,callInfo.caller,callInfo.called);	
	
	cs = tapi_FindCallSessionByCall(call);
	if(cs){
		if (device->activeCall == call){
			device->activeCall = device->secondCall;	
			device->secondCall = 0;
		}
	
		if (device->type == TAPIDEVICE_AGENT && device->agentId > 0){
			tCtbMessage	*msgsend;
			char *buf;
			int  len;		
			
			msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
			buf = (char*)malloc(1024);				
			
			ctbMsgInit(msgsend);			
			msgsend->Sender 	= MSG_SRC_CTI;
			msgsend->Type	 	= MSGTYPE_EVENT_CALLDISCONNECTED;
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
			agent_DispatchMessage(device->agentId, buf, len);	
			free(msgsend);
			free(buf);		
			//acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_IDLE,cs?cs->session_key:"0");	
		}
	}

	return 0;
}

static int tapi_OnEventCallHeld(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){

	t_TapiDevice 	*device;
	t_TapiCallInfo  callInfo;
	HCALL           call;	
	t_CallSession   *cs=NULL;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLD: Device Not Found\n");
		return 2;
	}

	logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLD: Device %s\n", device->number);
	/*logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
	logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
	logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
	logger_Print(3,5,"\tconfCall:     %d\n\n", device->confCall);*/

	if (call == device->activeCall) {
		device->activeCall=device->heldCall;
		logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLD (activeCall <= heldCall)\n");
	}

	device->heldCall = call;
	device->call_id = callInfo.callId;

	device->device_status = 0x0000;

	logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLD: Device %s\n", device->number);
	/*logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
	logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
	logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
	logger_Print(3,5,"\tconfCall:     %d\n\n", device->confCall);*/

	cs = tapi_FindCallSessionByCall(call);
	if (!cs){
		//logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLD Call Session not found\n");
		//return 0;
	}else{
		//printf("Mark call session as  held\n");
		cs->is_onhold = 1;
	}
	if (device->type == TAPIDEVICE_AGENT && device->agentId > 0){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;
		
		//logger_Print(3,4,"CTI>> Held: On agent device.\n");		
		acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_HELD,cs?cs->session_key:NULL);
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);				
		
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type	 	= MSGTYPE_EVENT_CALLHELD;
		msgsend->Count		= 8;
		
		ctbMsgInsertNumeric(msgsend, 0, 0);
		ctbMsgInsertString (msgsend, 1, device->number);
		ctbMsgInsertString (msgsend, 2, cs?cs->session_key:NULL);
		ctbMsgInsertString (msgsend, 3, device->number);
		ctbMsgInsertNumeric(msgsend, 4, device->agentId);
		ctbMsgInsertNumeric(msgsend, 5, 0);		
		ctbMsgInsertString (msgsend, 6, device->ipAddress);
		ctbMsgInsertNumeric(msgsend, 7, device->groupId);
		
		len = ctbMsgEncode(msgsend, buf, 1024);
		agent_DispatchMessage(device->agentId, buf, len);	
		free(msgsend);
		free(buf);	
	}
	return 0;
}

static int tapi_OnEventHoldPendConf(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_TapiCallInfo	callInfo;
	HCALL			call;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);	
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLDPENDCONF: Device Not Found\n");
		return 2;
	}
	
	if (call == device->activeCall) {
		device->activeCall=device->heldCall;
		logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLDPENDCONF (activeCall <= heldCall)\n");
	}
	device->heldCall = call;

	/*logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLDPENDCONF: Device %s\n", device->number);
	logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
	logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
	logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
	logger_Print(3,5,"\tconfCall:     %d\n", device->confCall);*/


	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);				
		
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type	 	= MSGTYPE_EVENT_HOLDPENDCONF;
	msgsend->Count		= 8;
		
	ctbMsgInsertNumeric(msgsend, 0, 0);
	ctbMsgInsertString (msgsend, 1, device->number);    			// connected number	
	ctbMsgInsertString (msgsend, 2, "");							// session_key		
	ctbMsgInsertString (msgsend, 3, device->number);				// extension			
	ctbMsgInsertNumeric(msgsend, 4, device->agentId);				// agent id	
	ctbMsgInsertString (msgsend, 5, device->ipAddress);				// ip_address
	ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group id
	ctbMsgInsertNumeric(msgsend, 7, 1);								// direction
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(device->agentId, buf, len);	
	free(msgsend);
	free(buf);

	return 0;
}

static int tapi_OnEventHoldPendTransfer(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_TapiCallInfo	callInfo;
	HCALL			call;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);	
	
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLDPENDTRANSFER: Device Not Found\n");
		return 2;
	}
	
	if (call == device->activeCall) {
		device->activeCall=device->heldCall;
		logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLDPENDTRANSFER (activeCall <= heldCall)\n");
	}
	device->heldCall = call;

	logger_Print(3,4,"TAPI>> LINECALLSTATE_ONHOLDPENDTRANSFER: Device %s\n", device->number);
	/*logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
	logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
	logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
	logger_Print(3,5,"\tconfCall:     %d\n\n", device->confCall);*/


	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);				
		
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type	 	= MSGTYPE_EVENT_HOLDPENDTRANSFER;
	msgsend->Count		= 8;
		
	ctbMsgInsertNumeric(msgsend, 0, 0);
	ctbMsgInsertString (msgsend, 1, device->number);    			// connected number	
	ctbMsgInsertString (msgsend, 2, "");							// session_key		
	ctbMsgInsertString (msgsend, 3, device->number);				// extension			
	ctbMsgInsertNumeric(msgsend, 4, device->agentId);				// agent id	
	ctbMsgInsertString (msgsend, 5, device->ipAddress);				// ip_address
	ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group id
	ctbMsgInsertNumeric(msgsend, 7, 1);								// direction
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(device->agentId, buf, len);	
	free(msgsend);
	free(buf);

	return 0;
}

static int tapi_OnEventCallIdle(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_TapiCallInfo callInfo;
	HCALL call;	
	t_CallSession *cs=NULL;
	char pesan[256];

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);
	lineDeallocateCall(call);

	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print(3,4,"TAPI>> LINECALLSTATE_IDLE: Device Not Found\n");
		return 2;
	}
	
	device->device_status = 0x0000;
	device->activeCall = 0;
	device->consultCall = 0;
	device->heldCall = 0;
	device->confCall = 0;

	switch(device->type){
		case TAPIDEVICE_AGENT:  //internal call
			logger_Print(3,4,"TAPI>> LINECALLSTATE_IDLE: Device %s\n", device->number);
			--agent_active_call;
			--total_active_call;
			break;
		case TAPIDEVICE_IVR:  //inbound / external call
			logger_Print(3,4,"TAPI>> LINECALLSTATE_IDLE: Device %s\n", device->number);
			--ivr_active_call;
			--total_active_call;
			break;
		default:
			logger_Print(3,4,"TAPI>> LINECALLSTATE_IDLE: Device %s\n", device->number);
			break;
	}

	printf("TAPI>> Active Call agent=%d ivr=%d total_active_call=%d\n",agent_active_call,ivr_active_call,total_active_call);
	logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", callInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", callInfo.called);	
	logger_Print(3,5,"\tConnected: %s\n", callInfo.connected);
	logger_Print(3,5,"\tdirection: %d\n\n", callInfo.direction);
	printf("[%s] Idle, caller=%s called=%s\n",device->number,callInfo.caller,callInfo.called);

	cs = tapi_FindCallSessionByCall(call);
	if(cs){
		tCtbMessage	*msgsend;
		char *buf;
		int  len;

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
			ctbMsgInsertNumeric(msgsend, 7, cs->direction);		// direction
			ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group id
			
			len = ctbMsgEncode(msgsend, buf, 1024);
			agent_DispatchMessage(device->agentId, buf, len);	
			// set agent device status to idle (pindahan dari disconnect
			free(msgsend);
			free(buf);
			acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_IDLE,cs?cs->session_key:"0");
		}
		
		//delete call_session	
		switch(device->type){
			case TAPIDEVICE_AGENT: 
				//save to db
				switch(cs->status){
					case CALLSTATUS_AGENT_RINGING:
					case CALLSTATUS_AGENT_TRUNKSEIZED:
						cs->status = CALLSTATUS_AGENT_ABANDON;
						break;
					case CALLSTATUS_AGENT_INITIATED:
					case CALLSTATUS_AGENT_ORIGINATED:
						cs->status = CALLSTATUS_AGENT_FAILED;
						break;
					case CALLSTATUS_AGENT_CONNECTED:
						cs->status = CALLSTATUS_AGENT_TERMINATED;
						break;
				}
				//////
				wallboard_extstatus(device->number,cs->status,callInfo.caller);

				msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
				buf = (char*)malloc(1024);			

				ctbMsgInit(msgsend);			
				msgsend->Sender 	= MSG_SRC_CTI;
				msgsend->Type	 	= MSGTYPE_EVENT_CALLDISCONNECTED;
				msgsend->Count		= 8;
				ctbMsgInsertNumeric(msgsend, 0, 0);
				ctbMsgInsertString (msgsend, 1, cs->session_key);			  
				ctbMsgInsertNumeric(msgsend, 2, cs->status);					// call status
				ctbMsgInsertString (msgsend, 3, device->number);				// c-number	
				ctbMsgInsertNumeric(msgsend, 4, cs?cs->direction:callInfo.direction);					// direction		
				ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
				ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group
				ctbMsgInsertString (msgsend, 7, callInfo.caller);				// c-number	
				len = ctbMsgEncode (msgsend, buf, 512);	
				dblog_PutMessage(buf, len);
				
				free(msgsend);
				free(buf);	
				sprintf(pesan,"TAPI>>[%s] Idle Delete cs %s",device->number,cs->session_key);
				listRemove(&CallSessions, cs);
				free(cs);
				printf("%s cs_count=%d\n",pesan,listSize(&CallSessions));
				logger_Print(3,1,"%s cs_count=%d\n",pesan,listSize(&CallSessions));
				break;
			case TAPIDEVICE_IVR: //call idle di ivr
				if(cs->is_reached == 2){//is_reached = 2 berarti baru sampe ivr saja langsung hangup by customer
					//save to db
					switch(cs->status){
						case CALLSTATUS_IVR_RINGING:
							cs->status = CALLSTATUS_IVR_ABANDON;
							break;
						case CALLSTATUS_IVR_CONNECTED:
							cs->status = CALLSTATUS_IVR_TERMINATED;
							break;
					}
					if(device->onqueue == 1){
						--que_call;
						cs->status = CALLSTATUS_QUEUE_TERMINATED;
					}

					msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
					buf = (char*)malloc(1024);			

					ctbMsgInit(msgsend);
					
					msgsend->Sender 	= MSG_SRC_CTI;
					msgsend->Type	 	= MSGTYPE_EVENT_CALLDISCONNECTED;
					msgsend->Count		= 8;
					ctbMsgInsertNumeric(msgsend, 0, 0);
					ctbMsgInsertString (msgsend, 1, cs->session_key);			  
					ctbMsgInsertNumeric(msgsend, 2, cs->status);					// call status
					ctbMsgInsertString (msgsend, 3, device->number);				// c-number	
					ctbMsgInsertNumeric(msgsend, 4, cs->direction);					// direction		
					ctbMsgInsertNumeric(msgsend, 5, device->agentId);				// agent
					ctbMsgInsertNumeric(msgsend, 6, device->groupId);				// group
					ctbMsgInsertString (msgsend, 7, device->ivr_port);
					len = ctbMsgEncode (msgsend, buf, 512);	
					dblog_PutMessage(buf, len);

					if(device->onqueue == 1){
						msgsend->Sender 	= MSG_SRC_CC_CTI;
						msgsend->Type	 	= MSGTYPE_EVENT_DISCONNECTED_QUEUE;
						msgsend->Count		= 4;
					  	
						ctbMsgInsertNumeric(msgsend, 0, 0);
						ctbMsgInsertString (msgsend, 1, device->ivr_port);			   		
						ctbMsgInsertString (msgsend, 2, device->number);
						ctbMsgInsertNumeric(msgsend, 3, device->queue_group);

						len = ctbMsgEncode (msgsend, buf, 512);	
						dblog_PutMessage(buf, len);
						device->onqueue = 0;
					}

					free(msgsend);
					free(buf);	
					////////

					wallboard_extstatus(device->number,cs->status,callInfo.caller);

					sprintf(pesan,"TAPI>>[%s] Idle Delete cs %s",device->number,cs->session_key);
					listRemove(&CallSessions, cs);
					free(cs);
					printf("%s cs_count=%d\n",pesan,listSize(&CallSessions));
					logger_Print(3,1,"%s cs_count=%d\n",pesan,listSize(&CallSessions));
					device->onqueue = 0;
				}//end if cs reached					
				break;	
			}//end switch
	}//end if cs
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
		logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALING: Device Not Found\n");
		return 2;
	}

	//logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALING: Device %s\n", device->number);
	/*logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", callInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", callInfo.called);	
	logger_Print(3,5,"\tConnected: %s\n\n", callInfo.connected);	*/


	return 0;
}
static int tapi_OnEventCallOriginating(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_TapiCallInfo callInfo;
	HCALL call;	
	t_CallSession *cs=NULL;

	call = (HCALL)msg->hDevice;	
	tapi_GetCallInfo(call, &callInfo);
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		printf("TAPI>> LINECALLSTATE_PROCEEDING: Device Not Found\n");
		return 2;
	}
	
	logger_Print(3,4,"TAPI>> LINECALLSTATE_PROCEEDING: Device %s\n", device->number);
	logger_Print(3,5,"\tCallId:    0x%08x\n", callInfo.callId);
	logger_Print(3,5,"\tCaller:    %s\n", callInfo.caller);	
	logger_Print(3,5,"\tCalled:    %s\n", callInfo.called);	
	logger_Print(3,5,"\tConnected: %s\n", callInfo.connected);
	logger_Print(3,5,"\tdirection: %d\n\n", callInfo.direction);

	printf("[%s] originating, caller=%s called=%s\n",device->number,callInfo.caller,callInfo.called);
	
	
	if(device->type == TAPIDEVICE_AGENT && device->agentId > 0){
		
		if((cs = tapi_CreateCallSession(cti_pvt, device->hLine, call, callInfo.direction))){
			cs->call_id = callInfo.callId;
			
			sprintf(cs->calling_number, "%s", callInfo.caller);
			sprintf(cs->called_number,  "%s", callInfo.called);
			sprintf(cs->vdn_number,     "%s", callInfo.called);
			cs->status = CALLSTATUS_AGENT_ORIGINATED;	 
			device->have_callsession = 1;
			device->activeCall = call;
			switch(callInfo.direction){
				case LINECALLORIGIN_OUTBOUND:
					cs->direction = CALLDIR_OUTGOING;
					break;
				case LINECALLORIGIN_INTERNAL:
					cs->direction = CALLDIR_INTERNAL;
					break;
				default:
					break;
			}
		}
		
		//cs = tapi_FindCallSessionByCall(call);
		if(cs){	    
			
			tCtbMessage	*msgsend;
			char *buf;
			int  len;
			
			msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
			buf = (char*)malloc(1024);	
			ctbMsgInit(msgsend);

			msgsend->Sender 	= MSG_SRC_CC_CTI;
  			msgsend->Type	 		= MSGTYPE_EVENT_CALLINITIATED;
  			msgsend->Count		= 8;
		  	
			ctbMsgInsertNumeric(msgsend, 0, callInfo.callId);
  			ctbMsgInsertString (msgsend, 1, cs->session_key);
  			ctbMsgInsertNumeric(msgsend, 2, cs->status);
  			ctbMsgInsertNumeric(msgsend, 3, cs->direction);
  			ctbMsgInsertString (msgsend, 4, device->number);
  			ctbMsgInsertNumeric(msgsend, 5, device->agentId);
  			ctbMsgInsertNumeric(msgsend, 6, device->groupId);
  			ctbMsgInsertNumeric(msgsend, 7, device->assignment_id);
		  	
  			len = ctbMsgEncode (msgsend, buf, 512);	
			dblog_PutMessage(buf, len);

			msgsend->Sender 	= MSG_SRC_CC_CTI;
			msgsend->Type	 		= MSGTYPE_EVENT_CALLORIGINATED;
  			msgsend->Count		= 8;
		  	
			ctbMsgInsertNumeric(msgsend, 0, callInfo.callId);
  			ctbMsgInsertString (msgsend, 1, cs->session_key);
  			ctbMsgInsertNumeric(msgsend, 2, cs->status);
  			ctbMsgInsertNumeric(msgsend, 3, cs->direction);
  			ctbMsgInsertString (msgsend, 4, device->number);
			ctbMsgInsertString (msgsend, 5, callInfo.called);
  			ctbMsgInsertNumeric(msgsend, 6, device->agentId);
  			ctbMsgInsertNumeric(msgsend, 7, device->groupId);
			printf("%d\n",cs->status);
  			len = ctbMsgEncode (msgsend, buf, 512);	
			dblog_PutMessage(buf, len);
			free(msgsend);
			free(buf);
			acd_AgentChangeExtStatus(device->groupId, device->agentId, ACD_PHONESTATUS_DIALING,cs?cs->session_key:"0");
		}	
	}
	if(device->type == TAPIDEVICE_IVR){
		if(device->onqueue == 1){
			--que_call;
			device->onqueue = 0;
		}
	}

	return 0;
}

static int tapi_OnEventDialTone(TCTIPvt *cti_pvt, LPLINEMESSAGE msg){
	t_TapiDevice 	*device;
	t_TapiCallInfo  callInfo;
	HCALL           call;	

	call = (HCALL)msg->hDevice;
	tapi_GetCallInfo(call, &callInfo);
	if (!(device = tapi_FindDeviceByHandle(callInfo.hLine))){
		logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALTONE: Device Not Found\n");
		return 2;
	}
	
	device->swaptype=0;
	if (call == !device->heldCall) {
		logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALTONE: lineSetupTransfer NO heldCall\n");
	} else if (call == !device->consultCall) {
		logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALTONE: lineSetupTransfer NO consultCall\n");
	} else {
		if (call == device->activeCall)
			logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALTONE (activeCall) Device: [%s >> %s]\n",device->number,device->dialNumber);
		if (call == device->heldCall)
			logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALTONE (heldCall) Device[%s >> %s]\n",device->number,device->dialNumber);
		if (call == device->consultCall) 
			logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALTONE (consultCall) Device[%s >> %s]\n",device->number,device->dialNumber);

		lineDial(device->consultCall, device->dialNumber, 0);

		logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
		logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
		logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
		logger_Print(3,5,"\tconfCall:     %d\n\n", device->confCall);
	}

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
			//logger_Print(3,4,"TAPI>> Call Info\n");
			//tapi_OnEventCallInfo(cti_pvt, &lineMsg);			
			break;
		case LINE_CALLSTATE:			
			switch(lineMsg.dwParam1){
				case LINECALLSTATE_IDLE:
					//logger_Print(3,4,"TAPI>> IDLE\n");
					tapi_OnEventCallIdle(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_OFFERING:
					//logger_Print(3,4,"TAPI>> Ringing\n");
					tapi_OnEventCallAlerting(cti_pvt, &lineMsg); 
					break;
				case LINECALLSTATE_DISCONNECTED:
					//logger_Print(3,4,"TAPI>> Disconnected\n");
					tapi_OnEventCallDisconnected(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_DIALTONE:
					logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALTONE\n");
					//printf("TAPI>> Dialing\n\n\n");
					
					tapi_OnEventDialTone(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_CONNECTED:
					//logger_Print(3,4,"TAPI>> Connected\n");
					tapi_OnEventConnected(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_DIALING:
					//logger_Print(3,4,"TAPI>> LINECALLSTATE_DIALING\n");
					//printf("TAPI>> Dialing.....\n");
					tapi_OnEventCallInitiated(cti_pvt, &lineMsg);
					tapi_OnEventCallDialing(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_ONHOLD:
					//logger_Print(3,4,"TAPI>> OnHold\n");
					tapi_OnEventCallHeld(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_PROCEEDING:
					tapi_OnEventCallOriginating(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_BUSY:
					//logger_Print(3,4,"TAPI>> Busy\n");
					//tapi_OnEventCallBusy(cti_pvt, &lineMsg);
					break;
				case LINECALLSTATE_ACCEPTED:
					logger_Print(3,4,"TAPI>> LINECALLSTATE_ACCEPTED\n");
					break;                        
				case LINECALLSTATE_RINGBACK:
					logger_Print(3,4,"TAPI>> LINECALLSTATE_RINGBACK\n");
					break;
				case LINECALLSTATE_SPECIALINFO:
					logger_Print(3,4,"TAPI>> LINECALLSTATE_SPECIALINFO\n");
					break;
				case LINECALLSTATE_CONFERENCED:
					logger_Print(3,4,"TAPI>> Busy\n");
					break;                        
				case LINECALLSTATE_ONHOLDPENDCONF:
					//logger_Print(3,4,"TAPI>> LINECALLSTATE_PENDING_CONFERENCE\n");
					tapi_OnEventHoldPendConf(cti_pvt, &lineMsg);
					break;                        
				case LINECALLSTATE_ONHOLDPENDTRANSFER :
					tapi_OnEventHoldPendTransfer(cti_pvt, &lineMsg);
					break;                        
				case LINECALLSTATE_UNKNOWN:
					logger_Print(3,4,"TAPI>> LINECALLSTATE_UNKNOWN\n");
					break;                        
				default:
					printf("LINECALLSTATE UNKNOWN :%ld\n",lineMsg.dwParam1);

					break;
			}
			break;
		case LINE_APPNEWCALL:
			// new call on a device, IN/OUT, created spontaneously, not by API
			//printf("newcall\n");
			tapi_OnEventNewCall(cti_pvt, &lineMsg);
			break;
		case LINE_CLOSE:
			printf("LINE_CLOSE\n");
			logger_Print(3,4,"LINE_CLOSE\n");
			break;
		case LINE_CREATE:
			printf("LINE_CREATE\n");
			logger_Print(3,4,"LINE_CREATE\n");
			break;
		case LINE_DEVSPECIFIC:
			printf("LINE_DEVSPECIFIC\n");
			break;				
		case LINE_DEVSPECIFICFEATURE:
			printf("LINE_DEVSPECIFICFEATURE\n");
			break;
		case LINE_GATHERDIGITS:
			printf("LINE_GATHERDIGITS\n");
			break;
		case LINE_GENERATE:
			printf("LINE_GENERATE\n");
			break;
		case LINE_REPLY:
			//printf("LINE_REPLY\n");
			break;
		case LINE_LINEDEVSTATE:
			printf("LINE_LINEDEVSTATE\n");
			/*switch(lineMsg.dwParam1){						
				case LINEDEVSTATE_RINGING:
					printf("Ringing\n");
			}*/
			break;	
		default:
			printf("LINE_STATE UNKNOWN :%d\n",lineMsg.dwMessageID);
			break;
  }
	
	return 0;
}

//*******************************************************************
// TAPI related routines
//*******************************************************************

// return 0 if Idle
int tapi_StationIdleStatus(char *ext_no){
	LINEADDRESSSTATUS lads;	
	t_TapiDevice *device;
	int ret;
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, ext_no);

	lads.dwTotalSize = sizeof(LINEADDRESSSTATUS);
	ret = lineGetAddressStatus(device->hLine,device->hPhone, &lads);

	//printf("\n\nDEBUG>> ret=0x%08x device=%s active_calls=%d\n\n", ret,device->number,lads.dwNumActiveCalls);
	/*if (ret == 0){
		if ((lads.dwNumActiveCalls == 0)&&(lds.dwNumOnHoldCalls  == 0)&&(lds.dwNumOnHoldPendCalls == 0)){
			device->status			 = TAPIDEVICESTATUS_IDLE;
			device->activeCall = 0;	
			device->secondCall = 0;
			return 0;
		}else
			return 1;
	}*/	
	return -1;
}

int tapi_CheckStationIdleStatus(char *ext_no){
	LINEDEVSTATUS lds;	
	int ret;
	t_TapiDevice *device;

	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, ext_no);
	lds.dwTotalSize = sizeof(LINEDEVSTATUS);
	ret = lineGetLineDevStatus(device->hLine, &lds);
	//printf("DEBUG>>DEVICE %s ActiveCall=%d,OnHoldCAlls =%d,OnHoldPending=%d\n",ext_no,lds.dwNumActiveCalls,lds.dwNumOnHoldCalls,lds.dwNumOnHoldPendCalls);
	//logger_Print(3,5,"ACDCEK>> DEVICE %s ActiveCall=%d\n",ext_no,lds.dwNumActiveCalls);
		
	if (lds.dwNumActiveCalls > 0){
		//device->status = TAPIDEVICESTATUS_BUSY;
		return 1;
	}else{
		//device->status = TAPIDEVICESTATUS_IDLE;
		//device->activeCall = 0;	
		//device->secondCall = 0;
		return 0;
	}
	return -1;
}


int tapi_setcallqueue(char *ext_no, int group_id){
	t_CallSession *cs=NULL;
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	char calling[50];

	device=tapi_FindDeviceByPort(TAPIDEVICE_IVR,ext_no);
	if(device){
		cs = tapi_FindCallSessionByCall(device->activeCall);
		if(cs){
			strcpy(calling,cs->calling_number);
		}else{
			return 0;
		}

		if(device->onqueue == 0){
			++que_call; 
			printf("Call from %s Queued on=%s, que_group=%d, curr_que=%d\n",calling,device->number,group_id,que_call);
			logger_Print(3,1,"Call from %s Queued on=%s, que_group=%d, curr_que=%d\n",calling,device->number,group_id,que_call);
			device->queue_group = group_id;
			wallboard_extstatus(device->number,CALLSTATUS_QUEUED,calling);

			msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
			buf = (char*)malloc(1024);	
			ctbMsgInit(msgsend);	
			msgsend->Sender 	= MSG_SRC_CC_CTI;
			msgsend->Type	 	= MSGTYPE_EVENT_QUEUED;
			msgsend->Count		= 5;
		  	
			ctbMsgInsertNumeric(msgsend, 0, 0);
			ctbMsgInsertString (msgsend, 1, device->session_key);			   		
			ctbMsgInsertString (msgsend, 2, device->ivr_port);
			ctbMsgInsertString (msgsend, 3, device->number);
			ctbMsgInsertNumeric (msgsend, 4, device->queue_group);

			len = ctbMsgEncode (msgsend, buf, 512);	
			dblog_PutMessage(buf, len);				
			free(msgsend);
			free(buf);
			device->onqueue = 1;	
		}
	}
	return 0;
}

int tapi_checkLineStatus(char *ext_no){
	t_TapiDevice *device;
	LINEDEVSTATUS lds;	
	int ret;

	device = tapi_FindDeviceByNumber(TAPIDEVICE_IVR, ext_no);
	if(device){
		//printf("TAPICHECK>> Device Handle Line = 0x%08x\n", device->hLine);
		lds.dwTotalSize = sizeof(LINEDEVSTATUS);
		ret = lineGetLineDevStatus(device->hLine,&lds);
		printf("TAPICHECK>> hLine=0x%08x, ret=%ld, %ld\n", device->hLine,ret, lds.dwNumOpens);
		logger_Print(3,4,"TAPICHECK>> hLine=0x%08x, ret=%ld, %ld\n", device->hLine,ret, lds.dwNumOpens);
		if(!ret == 0){
			exit(0);
		}
	}
return 0;
}
int tapi_MainLoop(TCTIPvt *cti_pvt){
	DWORD ret;
	HANDLE evts[2];
	unsigned char 	msgBuf[4096], *pMsgBuf;
	int msgLen, nread, len;
	int dbPing = 0;

	//Load Devices
	Tapi_InitDevice(cti_pvt);
	
	evts[0] = cti_pvt->hTapiEvent;
	evts[1] = cti_pvt->hDataEvent;

	while(1){

		// wait event from PBX
		//ret =  WaitForMultipleObjects(3, evts, FALSE, 1000);
		ret =  WaitForMultipleObjects(2, evts, FALSE, 1000);
		switch(ret){
			case WAIT_OBJECT_0+0:
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
			case WAIT_TIMEOUT:
				logger_Print(3,6,"Wait Timeout, WaitForMultipleObjects...\n");
				break;
			default:
				ExitProcess(0);	
				logger_Print(3,6,"EXIT, because no tapi event...\n");
			
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

static int tapi_OnCtiMsgUseExt(TCTIPvt *pvt, tCtbMessage *msg){
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
	logger_Print(2,1,"AGENT-%d>> Checking extension %s\n", msg->Fields[1].a.iVal,msg->Fields[3].a.szVal);
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[3].a.szVal);
	if(!device){
		/* device not found, send reply */		
		
		ctbMsgInsertNumeric(msgsend, 2, 1);		// result
		ctbMsgInsertNumeric(msgsend, 3, 0);		// error code		
		ctbMsgInsertNumeric(msgsend, 4, 0);		// device status
	}else{		
		if (device->agentId != 0){
			logger_Print(2,1,"AGENT-%d>> extension %s already used by %d on %s\n", 
				msg->Fields[1].a.iVal,msg->Fields[3].a.szVal,device->agentId, device->ipAddress);
			
			//if(device->agentId == msg->Fields[1].a.iVal){
			//	ctbMsgInsertNumeric(msgsend, 2, 0);		// result
			//	logger_Print(2,1,"AGENT-%d>> This extension used by same agent,granted\n",msg->Fields[1].a.iVal);
			//}else{
			//	ctbMsgInsertNumeric(msgsend, 2, 1);		// result
			//	logger_Print(2,1,"AGENT-%d>> This extension used by another agent,rejected\n",msg->Fields[1].a.iVal);
			//}
			ctbMsgInsertNumeric(msgsend, 2, 0);		// result
			ctbMsgInsertNumeric(msgsend, 3, 0);		// error code		
			ctbMsgInsertNumeric(msgsend, 4, tapi_CheckStationIdleStatus(msg->Fields[3].a.szVal));		// device status
			device->agentId = msg->Fields[1].a.iVal;
			device->groupId = msg->Fields[2].a.iVal;
			//ctbMsgInsertNumeric(msgsend, 4, 0);		// device status

		}else{
			ctbMsgInsertNumeric(msgsend, 2, 0);		// result
			ctbMsgInsertNumeric(msgsend, 3, 0);		// error code
			ctbMsgInsertNumeric(msgsend, 4, tapi_CheckStationIdleStatus(msg->Fields[3].a.szVal));		// device status
			device->agentId = msg->Fields[1].a.iVal;
			device->groupId = msg->Fields[2].a.iVal;
			//ctbMsgInsertNumeric(msgsend, 4, 0);		// TAPI can not get exact status, assume idle
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);	
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;
}

//20130828 : Tri >>fungsi cek extension untuk dipanggil agent
int tapi_UseExt(int agentID,int groupID,char *extNo){
	t_TapiDevice *device;
	int result;
	logger_Print(2,1,"AGENT-%d>> Checking extension %s\n", agentID,extNo);
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, extNo);
	if(!device){
		result=1;
	}else{		
		if (device->agentId != 0){
			logger_Print(2,1,"AGENT-%d>> extension %s already used by %d on %s\n", 
				agentID,extNo,device->agentId, device->ipAddress);
		}
		logger_Print(2,1,"AGENT-%d>> device %s groupid=%d\n",device->agentId,device->number,device->groupId); 
		result = 0;
		device->agentId = agentID;
		device->groupId = groupID;
				
	}
	return result;
}

int tapi_ReleaseExt(int agentId, char *extNo){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	int result;
	
	

	logger_Print(2,1,"AGENT-%d>> Checking extension %s\n",agentId, extNo);
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, extNo);
	if(!device){
		result=1;
	}else{		
		if (device->agentId != 0){
			logger_Print(2,1,"AGENT-%d>> Clearing extension %s on %s\n", 
				agentId,extNo,device->ipAddress);
		}
		
		result = 0;
		device->agentId = 0;
		device->groupId = 0;
		
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);
		ctbMsgInit(msgsend);
		msgsend->Sender 	= MSG_SRC_CTI;
		msgsend->Type 		= MSGTYPE_REL_EXT;
		msgsend->Count 		= 2;
		//send message result --- perlu nggak sih padahal kan agentnya dah logout:((
		ctbMsgInsertNumeric(msgsend, 0, 0);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code

		len = ctbMsgEncode(msgsend, buf, 1024);
		agent_DispatchMessage(agentId, buf, len);	
		free(msgsend);
		free(buf);				
	}
	return result;
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
	logger_Print(2,1,"AGENT-%d>> Mau release extension %s\n", msg->Fields[1].a.iVal,msg->Fields[2].a.szVal);
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code	
		logger_Print(2,1,"AGENT-%d>> extension %s Not Found\n", msg->Fields[1].a.iVal,msg->Fields[2].a.szVal);
	}else{
		
		if (device->agentId != msg->Fields[1].a.iVal && device->agentId != 0 ){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			logger_Print(2,1,"AGENT-%d>> Try to release release extension %s while ext belongs to %d ext=%s\n",
				msg->Fields[1].a.iVal,msg->Fields[2].a.szVal, device->agentId,device->number);
		}else{
			if(device->agentId == 0){
				free(msgsend);
				free(buf);
				logger_Print(2,1,"AGENT-%d>> extension %s already released\n", msg->Fields[1].a.iVal,msg->Fields[2].a.szVal);
				return 0;
			}else{
				ctbMsgInsertNumeric(msgsend, 0, 0);		// result
				ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
				device->agentId = 0;
				logger_Print(2,1,"AGENT-%d>> Releasing extension %s return OK = 0\n", msg->Fields[1].a.iVal,msg->Fields[2].a.szVal);
			}
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
	logger_Print(3,4,"TAPI>> HOLD REquest from ext %s,agent %d\n",msg->Fields[2].a.szVal,msg->Fields[1].a.iVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		logger_Print(3,4,"TAPI>> HOLD REquest device not found\n");
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code	
			logger_Print(3,4,"TAPI>> HOLD REquest agent not found\n");
		}else if (device->agentId == msg->Fields[1].a.iVal){
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code

			call = (HCALL)device->activeCall;
			//device->secondCall=call;
			
			logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
			logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
			logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
			logger_Print(3,5,"\tconfCall:     %d\n", device->confCall);

			lineHold(call);
			device->heldCall=call;
			logger_Print(3,4,"TAPI>> Hold Call DONE\n");
		}else{
			logger_Print(3,4,"TAPI>> Hold Call UNKNOWN\n");
		}
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);	
	
	return 0;
}


static int tapi_OnCtiMsgCompleteTransfer(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	// cek device on CTI
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_TRANS_COMPLETE;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	logger_Print(3,4,"CTI>>  TRANSFER COMPLETE REquest: AgentID[%d] - Device[%s]\n",msg->Fields[1].a.iVal, msg->Fields[2].a.szVal);	
	if(!device){
		logger_Print(3,4,"CTI>>  Transfer Complete Request: Device Not Found\n");
		// device not found, send reply
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		logger_Print(3,4,"Device not found!\n");
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			logger_Print(3,4,"CTI>>  Transfer Complete Request: Agent Not Found\n");
			//wrong agent
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code


			if (device->swaptype==0) {
				logger_Print(3,4,"device->swaptype==0\n");
				if (!device->heldCall) {
					logger_Print(3,4,"TAPI>> lineCompleteTransfer: NO heldCall\n");
				} else if (!device->consultCall) {
					if (!device->activeCall) {
						logger_Print(3,4,"TAPI>> lineSetupTransfer: NO consultCall\n");
						return 0;
					} else {
						device->consultCall = device->activeCall;
						logger_Print(3,4,"TAPI>> lineCompleteTransfer: consultCall <= activeCall\n");
					}
				}
				lineCompleteTransfer(device->heldCall, device->consultCall, NULL, LINETRANSFERMODE_TRANSFER);
			} else if (device->swaptype==1) {
				logger_Print(3,4,"device->swaptype==1\n");
				device->consultCall = device->activeCall;
				
				lineUnhold(device->heldCall);
				//lineDrop(device->activeCall,NULL,0);
				lineCompleteTransfer(device->consultCall, device->heldCall, NULL, LINETRANSFERMODE_TRANSFER);
			}


			logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
			logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
			logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
			logger_Print(3,5,"\tconfCall:     %d\n", device->confCall);
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);
	
	return 0;
}

static int tapi_OnCtiMsgConference(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	LINECALLPARAMS LineParams;
//	HCALL call;

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	// cek device on CTI
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_MAKE_CALL;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[5].a.szVal);
	logger_Print(3,4,"CTI>>  CONFERENCE CALL REquest: AgentID[%d] - Device[%s >> %s]\n",msg->Fields[1].a.iVal, msg->Fields[5].a.szVal, msg->Fields[2].a.szVal);	
	if(!device){
		logger_Print(3,4,"CTI>>  Conference Call Request: Device Not Found\n");
		// device not found, send reply
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code	
		logger_Print(3,4,"Device Not Found!\n");
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			logger_Print(3,4,"CTI>>  Conference Call Request: Agent Not Found\n");
			//wrong agent
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code

			memset(device->dialNumber, '\0', 8);
			sprintf(device->dialNumber, "%s", msg->Fields[2].a.szVal);

			/*if (device->activeCall && device->consultCall) {
				lineSetupConference (device->activeCall, 0, &device->confCall, &device->consultCall, 3, 0);
			} else {
				if (!device->consultCall) 
					logger_Print(3,4,"TAPI>> lineSetupConference: NO consultCall\n");
				if (!device->activeCall) 
					logger_Print(3,4,"TAPI>> lineSetupConference: NO activeCall\n");
			}*/

			if (device->activeCall) {
				lineSetupConference (device->activeCall, 0, &device->confCall, &device->consultCall, 3, 0);
				//logger_Print(3,4,"DEBUG>> lineSetupConference: activeCall\n");
			} else if (device->heldCall) {
				lineSetupConference (device->heldCall, 0, &device->confCall, &device->consultCall, 3, 0);
				//logger_Print(3,4,"DEBUG>> lineSetupConference: heldCall\n");
			} else logger_Print(3,4,"CTI>>  No activeCall or heldCall\n");

			logger_Print(3,4,"CTI>>  CONFERENCE (MakeCall) to %s\n",device->dialNumber);
			memset( &LineParams, 0, sizeof( LINECALLPARAMS ) );
			LineParams.dwTotalSize = sizeof( LINECALLPARAMS );			
			lineMakeCall(device->hLine, &device->activeCall,device->dialNumber,0,&LineParams);

			logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
			logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
			logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
			logger_Print(3,5,"\tconfCall:     %d\n", device->confCall);

		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);
	
	return 0;
}

static int tapi_OnCtiMsgCompleteConference(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	// cek device on CTI
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_CONF_COMPLETE;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	logger_Print(3,4,"CTI>>  CONFERENCE COMPLETE REquest: AgentID[%d] - Device[%s]\n",msg->Fields[1].a.iVal, msg->Fields[2].a.szVal);	
	if(!device){
		logger_Print(3,4,"CTI>>  Conference Complete Request: Device Not Found\n");
		// device not found, send reply
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
		logger_Print(3,4,"Device Not Found!\n");
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			logger_Print(3,4,"CTI>>  Conference Complete Request: Agent Not Found\n");
			//wrong agent
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code


			//method A
			/*if (device->activeCall && device->heldCall) {
			//if (device->activeCall && device->confCall) {
				lineAddToConference (device->heldCall, device->activeCall);
				//lineAddToConference (device->confCall, device->activeCall);
				logger_Print(3,4,"DEBUG>> CompleteConference\n");
			} else {
				if (!device->activeCall) 
					logger_Print(3,4,"TAPI>> CompleteConference: NO activeCall\n");
				if (!device->confCall) 
					logger_Print(3,4,"TAPI>> CompleteConference: NO confCall\n");
				logger_Print(3,4,"DEBUG>> NO CompleteConference\n");
			}*/


			//method B
			if (device->activeCall && device->heldCall) {
				lineCompleteTransfer(device->heldCall, device->activeCall, &device->confCall, LINETRANSFERMODE_CONFERENCE);
				//logger_Print(3,4,"DEBUG>> CompleteConference\n");
			} else {
				if (!device->activeCall) 
					logger_Print(3,4,"TAPI>> CompleteConference: NO activeCall\n");
				if (!device->heldCall) 
					logger_Print(3,4,"TAPI>> CompleteConference: NO heldCall\n");
				//logger_Print(3,4,"DEBUG>> NO CompleteConference\n");
			}
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);
	
	return 0;
}

static int tapi_OnCtiMsgSwapHold(TCTIPvt *pvt, tCtbMessage	*msg){
	t_TapiDevice *device;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	// cek device on CTI
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_SWAP_HOLD;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	logger_Print(3,4,"CTI>>  SWAP HOLD REquest: AgentID[%d] - Device[%s]\n",msg->Fields[1].a.iVal, msg->Fields[2].a.szVal);	
	//logger_Print(3,4,"DEBUG>>  TypeRequestButton: %s\n",msg->Fields[3].a.szVal);
	if(!device){
		logger_Print(3,4,"CTI>>  SwapHold Request: Device Not Found\n");
		// device not found, send reply
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
		logger_Print(3,4,"Device Not Found!\n");
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			logger_Print(3,4,"CTI>>  SwapHold Request: Agent Not Found\n");
			//wrong agent
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code

			//logger_Print(3,4,"DEBUG>> lineSwapHold() #0\n");
			logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
			logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
			logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
			logger_Print(3,5,"\tconfCall:     %d\n", device->confCall);

			if (!device->heldCall) {
				logger_Print(3,4,"TAPI>> lineSwapHold: NO heldCall\n");
			} else if (!device->activeCall) {
				logger_Print(3,4,"TAPI>> lineSwapHold: NO activeCall\n");
			} else {
				lineHold(device->activeCall);
				//lineHold(device->heldCall);
				//device->activeCall = device->heldCall;
				lineSwapHold(device->activeCall, device->heldCall);
				//lineUnhold(device->activeCall);
				lineUnhold(device->heldCall);
			}
			
			device->swaptype=1;

			//logger_Print(3,4,"DEBUG>> lineSwapHold() #1\n");
			logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
			logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
			logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
			logger_Print(3,5,"\tconfCall:     %d\n", device->confCall);
		}		
	}
	
	len = ctbMsgEncode(msgsend, buf, 1024);
	agent_DispatchMessage(msg->Fields[1].a.iVal, buf, len);	
	free(msgsend);
	free(buf);
	
	return 0;
}

static int tapi_OnCtiMsgRetrieveBack(TCTIPvt *pvt, tCtbMessage	*msg){
	
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
	logger_Print(3,4,"TAPI>> UnHOLD REquest\n");	
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
			//call = (HCALL)device->secondCall;
			call = (HCALL)device->heldCall;
			lineUnhold(call);
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
	int ret;

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	//logger_Print(3,4,"CTI>>  ANSWER CALL REquest\n");	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_ANSWER_CALL;
	msgsend->Count 		= 2;
		
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[2].a.szVal);
	if(!device){
		logger_Print(3,4,"CTI>>  Answer Call Request: Device Not Found\n");
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
		
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			logger_Print(3,4,"CTI>>  Answer Call Request: Agent Not Found\n");
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code		
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			call = (HCALL)device->activeCall;
			ret=lineSetCallPrivilege(call,LINECALLPRIVILEGE_OWNER);
			logger_Print(3,4,"lineSetCallPrivilege(call,LINECALLPRIVILEGE_OWNER) ret=0x%08x,call=%d\n",ret,call);
			ret=lineAnswer(call,NULL,0);
			logger_Print(3,4,"lineAnswer(call,NULL,0) ret=0x%08x,call=%d\n",ret,call);
			logger_Print(3,4,"CTI>>  Answer Call DONE.\n");
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
	long ret;

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	logger_Print(3,4,"TAPI>> DROP CALL Request\n");	
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
			ret = lineDrop(call,NULL,0);
			logger_Print(3,4,"TAPI>> %ld DROP CALL Request done..\n",ret);	
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
	int  len,ret;
	LINECALLPARAMS LineParams;
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_MAKE_CALL;
	msgsend->Count 		= 2;
	logger_Print(3,2,"CTI>>  MAke CAll REquest from extension %s to %s \n", msg->Fields[6].a.szVal,msg->Fields[3].a.szVal);
	
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
			ret=lineMakeCall(device->hLine, &device->activeCall,msg->Fields[3].a.szVal,0,&LineParams);
			
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
	HCALL call;

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	
	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_CTI;
	msgsend->Type 		= MSGTYPE_MAKE_CALL;
	msgsend->Count 		= 2;
	logger_Print(3,4,"CTI>>  Transfer CAll REquest from extension %s to %s \n", msg->Fields[5].a.szVal,msg->Fields[3].a.szVal);
	
	device = tapi_FindDeviceByNumber(TAPIDEVICE_AGENT, msg->Fields[5].a.szVal);
	if(!device){
		/* device not found, send reply */		
		ctbMsgInsertNumeric(msgsend, 0, 1);		// result
		ctbMsgInsertNumeric(msgsend, 1, 0);		// error code	
		logger_Print(3,4,"CTI>>  TRANSFER: device not found\n");
	}else{		
		if (device->agentId != msg->Fields[1].a.iVal){
			/* wrong agent */
			ctbMsgInsertNumeric(msgsend, 0, 1);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code	
			logger_Print(3,4,"CTI>>  TRANSFER: wrong agent\n");
		}else{
			ctbMsgInsertNumeric(msgsend, 0, 0);		// result
			ctbMsgInsertNumeric(msgsend, 1, 0);		// error code
			//memset( &LineParams, 0, sizeof( LINECALLPARAMS ) );
			//LineParams.dwTotalSize = sizeof( LINECALLPARAMS );			
			//lineBlindTransfer(device->activeCall,msg->Fields[3].a.szVal,0);

			memset(device->dialNumber, '\0', 8);
			sprintf(device->dialNumber, "%s", msg->Fields[2].a.szVal);

			if (device->activeCall) {
				call = (HCALL)device->activeCall;
				lineSetupTransfer(call, &device->consultCall, 0);
				//logger_Print(3,4,"DEBUG>> lineSetupTransfer: activeCall\n");
			} else if (device->heldCall) {
				call = (HCALL)device->heldCall;
				lineSetupTransfer(call, &device->consultCall, 0);
				//logger_Print(3,4,"DEBUG>> lineSetupTransfer: heldCall\n");
			} else logger_Print(3,4,"CTI>>  No activeCall or heldCall\n");

			logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
			logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
			logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
			logger_Print(3,5,"\tconfCall:     %d\n", device->confCall);

			logger_Print(3,4,"CTI>>  TRANSFER (MakeCall) to %s\n",device->dialNumber);
			memset( &LineParams, 0, sizeof( LINECALLPARAMS ) );
			LineParams.dwTotalSize = sizeof( LINECALLPARAMS );			
			lineMakeCall(device->hLine, &device->activeCall,device->dialNumber,0,&LineParams);

			logger_Print(3,5,"\tactiveCall:	  %d\n", device->activeCall);
			logger_Print(3,5,"\theldCall:	  %d\n", device->heldCall);
			logger_Print(3,5,"\tconsultCall:  %d\n", device->consultCall);
			logger_Print(3,5,"\tconfCall:     %d\n", device->confCall);
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
		logger_Print(3,4,"malloc failed!\n");
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
					logger_Print(2,1,"AGENT-%d>> MSGTYPE_USE_EXT 0x%04x\n",msg->Fields[1].a.iVal,msg->Type);
					tapi_OnCtiMsgUseExt(pvt, msg);
					break;
				case MSGTYPE_REL_EXT:
					logger_Print(2,1,"AGENT-%d>> MSGTYPE_REL_EXT 0x%04x\n",msg->Fields[1].a.iVal,msg->Type);
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
				case MSGTYPE_TRANS_COMPLETE:
					tapi_OnCtiMsgCompleteTransfer(pvt, msg);
					break;
				case MSGTYPE_CONFERENCE_CALL:
					tapi_OnCtiMsgConference(pvt, msg);
					break;
				case MSGTYPE_CONF_COMPLETE:
					tapi_OnCtiMsgCompleteConference(pvt, msg);
					break;
				case MSGTYPE_SWAP_HOLD:
					tapi_OnCtiMsgSwapHold(pvt, msg);
					break;
				case MSGTYPE_RETRVBACK:
					//tapi_OnCtiMsgRetrieveBack(pvt, msg);
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

