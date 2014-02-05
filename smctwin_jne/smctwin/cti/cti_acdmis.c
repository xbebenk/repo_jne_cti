
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

#pragma warning(disable : 4996)  // deprecated CRT function

#define ACDMIS_LEN_CMD			2
#define ACDMIS_LEN_TRUNKNO	3
#define ACDMIS_LEN_DIDNO		4
#define ACDMIS_LEN_OPRID		20
#define ACDMIS_LEN_EXTNO		8
#define ACDMIS_LEN_ACDGRP		2
#define ACDMIS_LEN_RECVNO		18
#define ACDMIS_LEN_SENDNO		18
#define ACDMIS_LEN_DATE			10

static t_CallSessionList 	CallSessions;

static char *RTrimSpace(char *str, int len){
	char *ptr;

	ptr = str+len-1;
	while(ptr>str && isspace(*ptr)){
		*ptr='\0';
		--ptr;
	}
	return str;
}

// Forward declaration
static int cti_OnCallOffered(TCTIPvt *pvt, int trunkno, char *didno, char *date);
static int cti_OnTrunkReleased(TCTIPvt *pvt, int trunkno, char *date);
static int cti_OnCallRingingIncoming(TCTIPvt *pvt, int trunkno, char *extno, int direction, char *date);
static int cti_OnCallAnsweredIncoming(TCTIPvt *pvt, int trunkno, char *extno, int direction, char *date);
static int cti_OnCallDisconnectedIncoming(TCTIPvt *pvt, int trunkno, char *extno, int direction, char *date);

static char* cti_CreateSessionId(char *buf, int buflen){
	struct timeval stimeval;
	
	gettimeofday(&stimeval, NULL);	
	_snprintf(buf, buflen, "%010ld%06ld\n", stimeval.tv_sec, stimeval.tv_usec);
	logger_Print(3,1,"ACDMIS>> Create SessionId %010ld%06ld\n\n", stimeval.tv_sec, stimeval.tv_usec);
	if (buflen >= 17)
		buf[16] ='\0';
	return buf;
}

static t_CallSession* cti_CreateCallSession(TCTIPvt *pvt, int trunkno, int direction){
	t_CallSession *cs = NULL;	
	
	cs = listNewItem(t_CallSession);
	cti_CreateSessionId(cs->session_key, 32);
	cs->trunk_number = 0;
	cs->trunk_member = trunkno;
	cs->direction = direction;	
		
	/* insert to call session list */
	logger_Print(3,1,"ACDMIS>> Adding CallSession to list w/size=%d\n\n", listSize(&CallSessions));
	listInsertLast(&CallSessions, cs);	
	return cs;
}

static t_CallSession* cti_FindCallSessionByTrunk(int trunkgroup, int trunkno){	
	t_CallSession *cs;

	cs = listFirst(&CallSessions);
	
	while(cs){
		if (cs->trunk_number == trunkgroup && cs->trunk_member == trunkno)
			return cs;
		else
			cs = listNext(cs);
	}
	
	return NULL;
}


/** asumsi len pasti selalu > 2 */
int cti_OnACDMISData(TCTIPvt *pvt, char *msg, int len){
	char cmd[3];			/* 2 digit */
	char trunkNo[4];	/* 3 digit */
	char didNo[5];		/* 4 digit /nomer DID(ISDN) */	
	char extNo[9];		/* 8 digit */
	char acdGroup[3];	/* 2 digit */
	//char recvNo[19];	/* 18 digit / anumber */ 
	//char sendNo[19];	/* 18 digit / bnumber */
	char date[11];		/* 10 digit */

	memcpy(cmd, msg, ACDMIS_LEN_CMD);cmd[ACDMIS_LEN_CMD] = 0;

	if      (!strcmp(cmd, "P2")){
		// external incoming call/call offered
		memcpy(trunkNo, msg+2,  ACDMIS_LEN_TRUNKNO);trunkNo[ACDMIS_LEN_TRUNKNO] = 0;
		memcpy(didNo,   msg+5,  ACDMIS_LEN_DIDNO);  date[ACDMIS_LEN_DIDNO] = 0;
		memcpy(date,    msg+11, ACDMIS_LEN_DATE);   date[ACDMIS_LEN_DATE] = 0;

		RTrimSpace(trunkNo, ACDMIS_LEN_TRUNKNO);
		RTrimSpace(didNo, ACDMIS_LEN_DIDNO);
		cti_OnCallOffered(pvt, atoi(trunkNo), didNo, date);
	}else if(!strcmp(cmd, "P5")){
		// ringing (incoming)
		memcpy(trunkNo, msg+2, ACDMIS_LEN_TRUNKNO);trunkNo[ACDMIS_LEN_TRUNKNO] = 0;
		memcpy(extNo,   msg+29, ACDMIS_LEN_EXTNO); extNo[ACDMIS_LEN_EXTNO] = 0;
		memcpy(date,    msg+39, ACDMIS_LEN_DATE);  date[ACDMIS_LEN_DATE] = 0;
		RTrimSpace(trunkNo, ACDMIS_LEN_TRUNKNO);
		RTrimSpace(extNo, ACDMIS_LEN_EXTNO);
		cti_OnCallRingingIncoming(pvt, atoi(trunkNo), extNo, CALLDIR_INCOMING, date);
	}else if(!strcmp(cmd, "P6")){
		// answer call/connected (incoming)
		memcpy(trunkNo, msg+2, ACDMIS_LEN_TRUNKNO);trunkNo[ACDMIS_LEN_TRUNKNO] = 0;
		memcpy(extNo,   msg+29, ACDMIS_LEN_EXTNO); extNo[ACDMIS_LEN_EXTNO] = 0;
		memcpy(date,    msg+39, ACDMIS_LEN_DATE);  date[ACDMIS_LEN_DATE] = 0;
		RTrimSpace(trunkNo, ACDMIS_LEN_TRUNKNO);
		RTrimSpace(extNo, ACDMIS_LEN_EXTNO);
		cti_OnCallAnsweredIncoming(pvt, atoi(trunkNo), extNo, CALLDIR_INCOMING, date);
	}else if(!strcmp(cmd, "P7")){
		// call disconnected (incoming)
		memcpy(trunkNo, msg+2, ACDMIS_LEN_TRUNKNO);trunkNo[ACDMIS_LEN_TRUNKNO] = 0;
		memcpy(extNo,   msg+29, ACDMIS_LEN_EXTNO); extNo[ACDMIS_LEN_EXTNO] = 0;
		memcpy(date,    msg+39, ACDMIS_LEN_DATE);  date[ACDMIS_LEN_DATE] = 0;
		RTrimSpace(trunkNo, ACDMIS_LEN_TRUNKNO);
		RTrimSpace(extNo, ACDMIS_LEN_EXTNO);
		cti_OnCallDisconnectedIncoming(pvt, atoi(trunkNo), extNo, CALLDIR_INCOMING, date);
	}else if(!strcmp(cmd, "PB")){
		// hold
	}else if(!strcmp(cmd, "PC")){
		// reconnect
	}else if(!strcmp(cmd, "PP")){
		// originating(internal)
	}else if(!strcmp(cmd, "PQ")){
		// answered by remote(internal/caller)
	}else if(!strcmp(cmd, "PR")){
		// end of call(internal)
	}else if(!strcmp(cmd, "PS")){
		// answered(internal)
	}else if(!strcmp(cmd, "PU")){
		// trunk seizure
	}else if(!strcmp(cmd, "PV")){
		// answered(external)
	}else if(!strcmp(cmd, "PW")){
		// disconnected(external)
	}else if(!strcmp(cmd, "PX")){
		// abandon before ringing(ACD)
	}else if(!strcmp(cmd, "PY")){
		// abandon on ringing(ACD)
	}else if(!strcmp(cmd, "Pt")){
		// Incoming call CLI
	}else if(!strcmp(cmd, "Pu")){
		// Trunk free
		memcpy(trunkNo, msg+2, ACDMIS_LEN_TRUNKNO);trunkNo[ACDMIS_LEN_TRUNKNO] = 0;
		memcpy(date,    msg+7, ACDMIS_LEN_DATE);   date[ACDMIS_LEN_DATE] = 0;

		RTrimSpace(trunkNo, ACDMIS_LEN_TRUNKNO);
		cti_OnTrunkReleased(pvt, atoi(trunkNo), date);

	}else if(!strcmp(cmd, "Pv")){
		// station busy/offhook		
		memcpy(extNo, msg+22, 8);	extNo[8] = 0;
		memcpy(acdGroup, msg+30, 2);	acdGroup[2] = 0;
		memcpy(date, msg+32, 10);	date[10] = 0;

		logger_Print(3,1,"ACDMIS>> Offhook\n");
		logger_Print(3,1,"ACDMIS>> Ext no: [%s]\n", extNo);
	}
	
	return 0;
}

static int cti_OnCallOffered(TCTIPvt *pvt, int trunkno, char *didno, char *date){
	t_CallSession *cs=NULL;
	int direction;
	tCtbMessage	*msgsend;
	char *buf;
	int  len;		

	direction = CALLDIR_INCOMING;
	logger_Print(3,1,"ACDMIS>> Call Offered from external, trunkno=%d\n", trunkno);

	cs = cti_CreateCallSession(pvt, trunkno, direction);
	cs->status = CALLSTATUS_OFFERED;	
		
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);	

  msgsend->Sender 	= MSG_SRC_CC_CTI;
	msgsend->Type	 		= MSGTYPE_EVENT_CALLOFFERED;
	msgsend->Count		= 8;
	
	ctbMsgInsertNumeric(msgsend, 0, 0);
	ctbMsgInsertString (msgsend, 1, cs->session_key);			// session_key
	ctbMsgInsertNumeric(msgsend, 2, cs->trunk_number);
	ctbMsgInsertNumeric(msgsend, 3, cs->trunk_member);
	ctbMsgInsertString (msgsend, 4, "");			//a-number  		
	ctbMsgInsertString (msgsend, 5, "");			//b-number
	ctbMsgInsertString (msgsend, 6, didno);		//c-number, on VDN c-number is d-number
	ctbMsgInsertNumeric(msgsend, 7, cs?cs->direction:0);	// direction		
	len = ctbMsgEncode (msgsend, buf, 512);	
  dblog_PutMessage(buf, len);

	return 0;
}

static int cti_OnCallRingingIncoming(TCTIPvt *pvt, int trunkno, char *extno, int direction, char *date){
	t_CallSession* cs=NULL;
	t_TapiDevice* device;

	logger_Print(3,1,"ACDMIS>> Call Ringing on ext=%s, trunkno=%d\n", extno, trunkno);

	if(direction == CALLDIR_INCOMING)
		cs = cti_FindCallSessionByTrunk(0, trunkno);

	if(!cs){
		logger_Print(3,1,"ACDMIS>> Call session not found\n");
		return 0;
	}

	device = tapi_FindAllDeviceByNumber(extno);
	if(!device){
		logger_Print(3,1,"ACDMIS>> Device not found\n");
		return 0;
	}

	if(device->type == TAPIDEVICE_IVR){
		logger_Print(3,1,"ACDMIS>> Ringing on IVR\n");
	}else if(device->type == TAPIDEVICE_AGENT){
		logger_Print(3,1,"ACDMIS>> Ringing on Agent\n");
	}
	return 0;
}

static int cti_OnCallAnsweredIncoming(TCTIPvt *pvt, int trunkno, char *extno, int direction, char *date){
	t_CallSession* cs=NULL;
	t_TapiDevice* device;

	logger_Print(3,1,"ACDMIS>> Call Answered on ext=%s, trunkno=%d\n", extno, trunkno);

	if(direction == CALLDIR_INCOMING)
		cs = cti_FindCallSessionByTrunk(0, trunkno);

	if(!cs){
		logger_Print(3,1,"ACDMIS>> Call session not found\n");
		return 0;
	}

	device = tapi_FindAllDeviceByNumber(extno);
	if(!device){
		logger_Print(3,1,"ACDMIS>> Device not found\n");
		return 0;
	}

	if(device->type == TAPIDEVICE_IVR){
		logger_Print(3,1,"ACDMIS>> Answered on IVR\n");
	}else if(device->type == TAPIDEVICE_AGENT){
		logger_Print(3,1,"ACDMIS>> Answered on Agent\n");
	}
	return 0;
}

static int cti_OnCallDisconnectedIncoming(TCTIPvt *pvt, int trunkno, char *extno, int direction, char *date){
	t_CallSession* cs=NULL;
	t_TapiDevice* device;

	logger_Print(3,1,"ACDMIS>> Call Disconnected on ext=%s, trunkno=%d\n", extno, trunkno);

	if(direction == CALLDIR_INCOMING)
		cs = cti_FindCallSessionByTrunk(0, trunkno);

	if(!cs){
		logger_Print(3,1,"ACDMIS>> Call session not found\n");
		return 0;
	}

	device = tapi_FindAllDeviceByNumber(extno);
	if(!device){
		logger_Print(3,1,"ACDMIS>> Device not found\n");
		return 0;
	}

	if(device->type == TAPIDEVICE_IVR){
		logger_Print(3,1,"ACDMIS>> Disconnected on IVR\n");
	}else if(device->type == TAPIDEVICE_AGENT){
		logger_Print(3,1,"ACDMIS>> Disconnected on Agent\n");
	}
	return 0;
}

static int cti_OnTrunkReleased(TCTIPvt *pvt, int trunkno, char *date){
	t_CallSession* cs=NULL;
	
	logger_Print(3,1,"ACDMIS>> Trunk Released, trunkno=%d\n", trunkno);
	cs = cti_FindCallSessionByTrunk(0, trunkno);
	if(!cs){
		logger_Print(3,1,"ACDMIS>> Call session not found\n");
		return 0;
	}

	listRemove(&CallSessions, cs);
	free(cs);
	logger_Print(3,1,"ACDMIS>> Call Session deleted...\n");
	return 0;
}