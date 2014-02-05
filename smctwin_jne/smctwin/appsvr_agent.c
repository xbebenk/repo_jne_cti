/*
 Agent App connection module
 
 assume thread lock, malloc and memory are cheap
 
 be careful with malloc
 */

#include "winsock2.h"
#include <time.h>
#include "include/smct.h"
#include "ctblib_message.h"
#include "include/appsvr.h"
#include "include/appsvr_thread.h"
#include "include/appsvr_agent.h"
#include "acd/acd.h"
#include "cti/cti.h"
#include "cti/cti_tapi.h"

#pragma warning(disable : 4996)  // deprecated CRT function


static TAgentHandlerPvt *agentHandlerPvt;
static t_AgentSessionList AgentSessions;

/* forward declaration */
static void agent_MainLoop(void *ignore);
static void agent_SessionHandler(void *ignore);
static int agent_MessageProcessing(t_AgentSession *session, char *message, int len);
static int agent_CTIMessageProcessing(t_AgentSession *session, char *message, int len);
static int agent_loadConfigDB();
static int agent_OnReadyMessage(t_AgentSession *session, tCtbMessage	*msg);

static t_AgentSession *agent_FindSessionByAgentId(t_AgentSessionList *list, int agentId){
	t_AgentSession *session;	
	
	session = listFirst(list);
	while(session){		
		if (session->agentId == agentId)
			break;
		session = listNext(session);
	}
	
	return session;
}

static t_AgentSession *agent_FindSessionByIP(t_AgentSessionList *list, char *IpAddress){
	t_AgentSession *session;	
	
	session = listFirst(list);
	while(session){		
		if(!strcmp(session->ipAddress, IpAddress))
			break;
		session = listNext(session);
	}
	
	return session;
}

static int agent_ListAgentLogin(t_AgentSessionList *list){
	t_AgentSession *session;	
	FILE *fh=NULL;	

	fh=fopen("agent_list.txt","w");
	if(!fh){
		printf("error creating file agent_list.txt\n");
		return 1;
	}
	session = listFirst(list);
	fprintf(fh,"Agent_id\tUserID\tAgentName\tAgentGroup\tIPAddress\tAgentExt\n");
	
	while(session){		
		fprintf(fh,"%d\t%s\t%s\t%d\t%s\t%s\n",session->agentId,session->agentUserId,session->agentName,session->agentGroup,session->ipAddress,session->agentExt);
		session = listNext(session);
	}
	fclose(fh);
	return 0;
}

int init_agenthandler(){
	
	agentHandlerPvt = (TAgentHandlerPvt*)malloc(sizeof(TAgentHandlerPvt));
	if (!agentHandlerPvt)
		return -1;
		
	memset(agentHandlerPvt, 0, sizeof(TAgentHandlerPvt));
	/* init with default value */
	agentHandlerPvt->listenPort = DEFAULT_AGENT_PORT;
	
	/* init agent session list */
	listInitWithLock(&AgentSessions);
	
	/* init from config */
	agent_loadConfigDB();
	
	
	/* create tcp listen socket */
	logger_Print(2,1,"AGENT>> Listening on port %d\n", agentHandlerPvt->listenPort);
	agentHandlerPvt->listenSock = sockTcpServerCreate(agentHandlerPvt->listenPort, 10);	
	if (agentHandlerPvt->listenSock <= 0)
		return -1;		
	
	return 0;
}

static int agent_loadConfigDB(){
	tDbConn dbConn;
	tDbSet  dbSet;
  char sql[2048];
  int  sqlLen;
  
  logger_Print(2,1,"AGENT>> Loading Configuration from DB\n");
	
	/* connect to DB */
	dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
  if (!dbConn){
  	logger_Error("AGENT>> DB Connection failed\n");
  	return -1;
  } 
  
  sqlLen = sprintf(sql,"SELECT set_name, set_value  FROM settings WHERE set_modul = 'agent'");  
  dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){
  	
  	if (!strcasecmp(dbLib->getStringFieldByIdx(dbSet, 0), "server.port")){
			agentHandlerPvt->listenPort = dbLib->getIntFieldByIdx(dbSet, 1);
		}
  }
  dbLib->closeQuery(dbSet); 
  
  dbLib->closeConnection(dbConn);
	return 0;
}


int agent_Load(){
	int ret;
	//Sleep(3000);
	ret = init_agenthandler();
	if (ret < 0){
		logger_Error("AGENT>> Fail loading agenthandler module\n");
		return -1;
	}
	
	/* create server thread */
	smctPthreadCreateDetached(&agentHandlerPvt->mainThreadId, NULL, agent_MainLoop, NULL);
	return 0;
}

static void agent_MainLoop(void *param){	
	fd_set  fds;
	struct 	timeval timeout;
	int 		nready;
	SOCKET 	clieSocket;
	int 		cliePort;
	char 		clieIpAddress[16];	
	t_AgentSession *session;
	int			pingTime;
	
	if (!agentHandlerPvt)
		return ;
	
	/* wait for incoming connection */
	pingTime = (int)time(NULL);
	for(;;){		
		FD_ZERO(&fds);
		FD_SET (agentHandlerPvt->listenSock, &fds);    
		timeout.tv_sec  = 10;
		timeout.tv_usec = 0;    
		if ((nready = select(0, &fds, NULL, NULL, &timeout)) > 0){    	
    		clieSocket = sockTcpServerAccept(agentHandlerPvt->listenSock, clieIpAddress, &cliePort);
    		logger_Print(2,1,"AGENT>> Accepting connection from %s:%d\n", clieIpAddress, cliePort);

    		/* create new session data */
    		session = listNewItem(t_AgentSession);
			session->sock = clieSocket;
    		strcpy(session->ipAddress, clieIpAddress);
    		session->port = cliePort;
    		pthread_mutex_init(&session->lock,  NULL);
	    	
    		/* insert to session list */
    		listLock(&AgentSessions);
    		listInsertLast(&AgentSessions, session);
    		listUnlock(&AgentSessions);
	    	
    		/* handle session on new thread */    	
    		smctPthreadCreateDetached(&session->threadId, NULL, agent_SessionHandler, (void*)session);    	
		}
	}
	return;
}

#define MAX_MESSAGE_SIZE 32768

static void agent_SessionHandler(void *param){
	t_AgentSession *session = (t_AgentSession*)param;
	int	readLen, readNextLen=0, msgDataLen=0, decodedLen=0;
	unsigned char 	*readBuf, *sysBuf;
	unsigned char 	*pReadBuf;
	unsigned short  msgLen = 0, sysBufLen=0;
	int	pingTime;
	//tCtbMessage	*msgsend;
	//char *buf;
	//int  len;
	int ret1;
	
	readBuf = (unsigned char*)malloc(MAX_MESSAGE_SIZE);
	sysBuf  = (unsigned char*)malloc(8192);
	
	if (!readBuf){
		logger_Error("Unable to allocate memory\n");
		return;
	}	
	
	pReadBuf = readBuf;		
	msgLen = 0;
	pingTime = (int)time(NULL);

	//allocate event
	session->evtSocket[0] = CreateEvent(NULL, FALSE, FALSE, NULL);	// for socket
	session->evtSocket[1] = CreateEvent(NULL, FALSE, FALSE, NULL);	// for interrupt
	//associate with socket	
	
	if(WSAEventSelect(session->sock, session->evtSocket[0], FD_READ|FD_CLOSE) != 0)
		printf("WSAEventSelect error: %d\n", WSAGetLastError());             

	for(;;){		
	
		DWORD ret;
		ret = WaitForMultipleObjects(2, session->evtSocket, FALSE, 1000);
		// can't use case, we need break to broke the loop
		if(ret == WAIT_OBJECT_0){      
			// Read from socket				
			if (msgLen == 0){
  				/* read message length */
  				readLen 		= recv(session->sock, (char*)&msgLen, sizeof(msgLen), 0);
				msgLen   		= ntohs(msgLen);
				msgDataLen  = 0;
				if(msgLen > MAX_MESSAGE_SIZE){
					logger_Print(2,1,"AGENT>> Message too big, ignoring\n");
					msgLen = 0;
					continue;
  				}

				readNextLen = msgLen;
				pReadBuf    = readBuf;
			}else{
      			/* read message data */
      			readLen 		= recv( session->sock, pReadBuf, readNextLen, 0);
				msgDataLen 	+= readLen;
				pReadBuf   	+= readLen;
				readNextLen -= readLen;
			}

			if (readLen > 0){
				/* handle message */
				if ((msgLen > 0) && (msgDataLen == msgLen)){
					/* receive full message, message processed only when full message received */	      		
					agent_MessageProcessing(session, readBuf, msgLen);

					/* reset data */
					msgLen     = 0;
					msgDataLen = 0;
				}
			}else{
				/* connection error or closed */				
				if (session->agentId > 0){
					
					logger_Print(2,1,"AGENT-%d>> CONNECTION ERROR OR CLOSED! CLEANING UP group=%d ext=%s, ip=%s, port=%d\n", 
						session->agentId,session->agentGroup, session->agentExt,session->ipAddress,session->port);	
				}
				break;
			}

		}else if (ret == (WAIT_OBJECT_0+1)){
			// Read from buffer			
			pthread_mutex_lock(&session->lock);
			if(session->bufSize > 0){
				memcpy(sysBuf, session->buf, session->bufSize);
				sysBufLen = session->bufSize;
				session->bufSize = 0;
			}
			pthread_mutex_unlock(&session->lock);
			
			if (sysBufLen > 0){
				int pos;
				/* process message */
				pos = 0;
				while(sysBufLen > 0){
					decodedLen = agent_CTIMessageProcessing(session, sysBuf+pos, sysBufLen);
					//logger_Print(2,1,"AGENT-%d>> Processing %d byte message\n",session->agentId, decodedLen);
					sysBufLen -= decodedLen;
					pos       += decodedLen;
				}
				sysBufLen = 0;
			}
		}else if(ret == WAIT_TIMEOUT){
		}else{
			printf("Wait error: %d\n", GetLastError());    
    } //end loop for ;;    
		
		/* ping */
    if((time(NULL)-pingTime) > PING_INTERVAL){
		tCtbMessage	*msgsend;
		char *buf;
		int  len, ret;

    	pingTime=(int)time(NULL);
    	++session->pingCount;   	
	
		/* HEARTBEAT mesasge */
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);
		
		ctbMsgInit(msgsend);
		// Login ACK message untuk dikirim:
		msgsend->Sender = MSG_SRC_CC_CTI;
		msgsend->Type	 	= 0x01;						// HEARTBEAT
		msgsend->Count	= 1;

		ctbMsgInsertNumeric(msgsend, 0, session->agentId);			// agent id

		// send to AGENT
		len = ctbMsgEncode(msgsend, buf+2, 1024);
		buf[0] = len << 8;
		buf[1] = (char)len;
		ret = sockTcpWrite(session->sock, buf, len+2);
	
		free(msgsend);
		free(buf);	
    	/* kirim heartbeat message */    	
    	if(ret<0){
    		logger_Print(2,1,"AGENT-%d>> socket error\n", session->agentId);
    		break;
    	}
    	
    	if(session->pingCount > MAX_PING_COUNT){
    		logger_Print(2,1,"AGENT-%d>> too slow, go away...\n", session->agentId);
    		break;
    	}
    }
    
    if((session->agentAutoAcwTime > 0) &&
    	 ((time(NULL) - session->agentAutoAcwStart) > session->agentAutoAcwTime)){
			logger_Print(2,1,"AGENT-%d>> AutoACW expired\n",session->agentId);
			printf("AGENT-%d>> AutoACW expired\n",session->agentId);
			session->agentAutoAcwTime = 0;
			agent_OnReadyMessage(session, NULL);
		}
	}	


	//ini ngabisin, karena kena break:
	/* make sure logout from acd */
	
	if(session->agentId > 0){
		ret1 = acd_AgentLogout(session->agentGroup, session->agentId);
		if(ret1 == 0){
			logger_Print(2,1,"AGENT-%d>> ret1=%d, acd_AgentLogout group %d\n", session->agentId,ret1,session->agentGroup);
			tapi_ReleaseExt(session->agentId,session->agentExt);
			wallboard_agentstatus(session->agentExt,6,session->agentId);
		}
		logger_Print(2,1,"AGENT-%d>> Ext=%s Handling session on socket done! cleaning up\n", session->agentId,session->agentExt);
	}
	
	
	/* remove session from list */
	listLock(&AgentSessions);
	listRemove(&AgentSessions, session);
	listUnlock(&AgentSessions);
	shutdown(session->sock, 0);
	pthread_mutex_destroy(&session->lock);
	free(session);
	free(readBuf);
	free(sysBuf);	
	return;
}

static int agent_OnLoginMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	int device_status;
	int device_idle_status;
	t_AgentSession *sess;

	listLock(&AgentSessions);
	sess = agent_FindSessionByIP(&AgentSessions,session->ipAddress);
	listUnlock(&AgentSessions);
		if(sess){
			if(sess->agentId > 0){
				logger_Print(2,1,"AGENT-%d>> Extension %s is already in use by %d on %s\n",msg->Fields[0].a.iVal,sess->agentExt, sess->agentId, sess->ipAddress);
				logger_Print(2,1,"AGENT-%d>> NOT Processing any further...\n",msg->Fields[0].a.iVal);
				return 0;
			}
		}
	if((appContext->numagentlogin ) >= appContext->licensedagent){
		logger_Print(2,1,"LICENSE>> Total agent login exceed %d licensed agent\n", appContext->licensedagent);
		printf("ACD>>maksimum allowed agent login is reached,licensed=%d,current login:%d agent, rejecting new agent login\n",appContext->licensedagent,appContext->numagentlogin);
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		buf = (char*)malloc(1024);
		ctbMsgInit(msgsend);
		
		// Login ACK message untuk dikirim:
		msgsend->Sender = MSG_SRC_CC_CTI;
		msgsend->Type	 	= 0x02;												// Login ACK
		msgsend->Count	= 5;
		
		ctbMsgInsertNumeric(msgsend, 0, msg->Fields[0].a.iVal);					// agent id
		ctbMsgInsertNumeric(msgsend, 1, 1);										// result
		ctbMsgInsertString (msgsend, 2, msg->Fields[1].a.szVal);				// ext-number
		ctbMsgInsertString (msgsend, 3, session->ipAddress);
		ctbMsgInsertNumeric(msgsend, 4, 0);										// group id
		// send to AGENT
		len = ctbMsgEncode(msgsend, buf+2, 1024);
		buf[0] = len << 8;
		buf[1] = (char)len;
		sockTcpWrite(session->sock, buf, len+2);
		
		ctbMsgInit(msgsend);
		// Also send Logout ACK message untuk dikirim:
		msgsend->Sender = MSG_SRC_CC_CTI;
		msgsend->Type	 	= 0x03;												// Logout ACK
		msgsend->Count	= 5;
		
		ctbMsgInsertNumeric(msgsend, 0, msg->Fields[0].a.iVal);					// agent id
		ctbMsgInsertNumeric(msgsend, 1, 0);										// result
		ctbMsgInsertString (msgsend, 2, msg->Fields[1].a.szVal);
		ctbMsgInsertString (msgsend, 3, session->ipAddress);
		ctbMsgInsertNumeric(msgsend, 4, 0);										// group id
		// send to AGENT
		len = ctbMsgEncode(msgsend, buf+2, 1024);
		buf[0] = len << 8;
		buf[1] = (char)len;
		sockTcpWrite(session->sock, buf, len+2);
		free(msgsend);
		free(buf);
		return 0;
	}
	/*20130828 : Tri -> ganti jadi fungsi nggak pake socket
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	session->agentId 	= msg->Fields[0].a.iVal;	
	session->agentGroup = msg->Fields[2].a.iVal;
	
	// cek device on CTI 
	logger_Print(2,1,"AGENT-%d>> Send data MSGTYPE_USE_EXT %s to CTI\n", session->agentId,msg->Fields[1].a.szVal);
	ctbMsgInit(msgsend);
	msgsend->Sender = MSG_SRC_AGENTHANDLER;
	msgsend->Type 	= MSGTYPE_USE_EXT;
	msgsend->Count 	= 4;
	ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);			// sock
	ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId
	ctbMsgInsertNumeric(msgsend, 2, msg->Fields[2].a.iVal);			// groupId
	ctbMsgInsertString (msgsend, 3, msg->Fields[1].a.szVal); 		// ext no			

	// send to CTI
	len = ctbMsgEncode(msgsend, buf, 1024);
	cti_Write(buf, len);
	
	free(msgsend);
	free(buf);	
	*/
	//komunikasi CTI via fungsi
	device_status = tapi_UseExt(msg->Fields[0].a.iVal,msg->Fields[2].a.iVal,msg->Fields[1].a.szVal);
	device_idle_status = tapi_CheckStationIdleStatus(msg->Fields[1].a.szVal);
	
	session->agentId 	= msg->Fields[0].a.iVal;	
	session->agentGroup = msg->Fields[2].a.iVal;

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	ctbMsgInit(msgsend);
	// Login ACK message untuk dikirim:
	msgsend->Sender = MSG_SRC_CC_CTI;
	msgsend->Type	 	= MSGTYPE_ACK_AGENTLOGIN;
	msgsend->Count	= 5;
	
	ctbMsgInsertNumeric(msgsend, 0, session->agentId);					// agent id
	ctbMsgInsertNumeric(msgsend, 1, device_status);				// result
	ctbMsgInsertString (msgsend, 2, msg->Fields[1].a.szVal);			// agent ext
	ctbMsgInsertString (msgsend, 3, session->ipAddress);
	ctbMsgInsertNumeric(msgsend, 4, session->agentGroup);
	// send to AGENT
	len = ctbMsgEncode(msgsend, buf+2, 1024);
	buf[0] = len << 8;
	buf[1] = (char)len;
	sockTcpWrite(session->sock, buf, len+2);
	
	logger_Print(2,1,"AGENT>> agent_OnCTIUseExtMessage result = %d\n",device_status);
	if (device_status == 0){
		int status;
		
		logger_Print(2,1,"AGENT-%d>> Extension %s can be used\n",session->agentId,msg->Fields[1].a.szVal);
		strcpy(session->agentExt, msg->Fields[1].a.szVal);	
		//printf("AGENT>> acd_AgentLogin:agent=%s, group=%d, agent_id=%d, ext=%s, ip=%s\n",session->agentName,session->agentGroup, session->agentId, session->agentExt, session->ipAddress);
		status = acd_AgentLogin(session->agentGroup, session->agentId, session->agentExt, session->ipAddress);
		logger_Print(2,4,"AGENT-%d>> agent after login status=%d acd_AgentLogin on agent_OnCTIUseExtMessage\n",session->agentId, status);
		if(status >= 0){
			wallboard_agentstatus(session->agentExt,status,session->agentId);
		}
		if (device_idle_status == 0){
			acd_AgentChangeExtStatus(session->agentGroup, session->agentId, ACD_PHONESTATUS_IDLE,"0");
		}/*else{
			acd_AgentChangeExtStatus(session->agentGroup, session->agentId, ACD_PHONESTATUS_TALKING,"0");
		}*/
		


		if (status == ACD_AGENTSTATUS_READY){
			ctbMsgInit(msgsend);
			msgsend->Sender = MSG_SRC_CTI;
			msgsend->Type	 	= MSGTYPE_ACK_AGENTREADY; /* READY */
			msgsend->Count	= 5;
			ctbMsgInsertNumeric(msgsend, 0, session->agentId);				// agent id		
			ctbMsgInsertNumeric(msgsend, 1, 0);  							// result	
			ctbMsgInsertString (msgsend, 2, session->agentExt);  			// extension
			ctbMsgInsertString (msgsend, 3, session->ipAddress);			// ip addr
			ctbMsgInsertNumeric(msgsend, 4, session->agentGroup);
			len = ctbMsgEncode(msgsend, buf+2, 1024);
			buf[0] = len << 8;
			buf[1] = (char)len;
			sockTcpWrite(session->sock, buf, len+2);
		}else if (status == ACD_AGENTSTATUS_NOTREADY){
			ctbMsgInit(msgsend);
			msgsend->Sender = MSG_SRC_CTI;
			msgsend->Type	 	= MSGTYPE_ACK_AGENTNOTREADY;	/* NOTREADY*/
			msgsend->Count	= 6;
			ctbMsgInsertNumeric(msgsend, 0, session->agentId);
			ctbMsgInsertNumeric(msgsend, 1, 0); // reason
			ctbMsgInsertNumeric(msgsend, 2, 0); // result	
			ctbMsgInsertString (msgsend, 3, session->agentExt);
			ctbMsgInsertString (msgsend, 4, session->ipAddress);
			ctbMsgInsertNumeric(msgsend, 5, session->agentGroup);
			len = ctbMsgEncode(msgsend, buf+2, 1024);
			buf[0] = len << 8;
			buf[1] = (char)len;
			sockTcpWrite(session->sock, buf, len+2);
		}		
	}else{
		session->agentId = 0;
		logger_Print(2,1,"AGENT-%d>> Extension can not be used\n",session->agentId);
	}
	
	free(msgsend);
	free(buf);
	//end komunikasi CTI via fungsi
	return 0;
}

static int agent_OnLogoutMessage(t_AgentSession *session, tCtbMessage	*msg){
	//tCtbMessage	*msgsend;
	//char *buf;
	//int  len;
	int release_status;
	
	if(session->agentId <= 0){
		return 0;
	}
	
	release_status = tapi_ReleaseExt(msg->Fields[0].a.iVal,msg->Fields[1].a.szVal);
	/*
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	//release device on CTI 
	ctbMsgInit(msgsend);
	msgsend->Sender 	= MSG_SRC_AGENTHANDLER;
	msgsend->Type 		= MSGTYPE_REL_EXT;
	msgsend->Count 	= 3;
	ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);			// sock
	ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId
	ctbMsgInsertString (msgsend, 2, msg->Fields[1].a.szVal); 		// ext no			

	// send to CTI
	len = ctbMsgEncode(msgsend, buf, 1024);
	cti_Write(buf, len);
	
	free(msgsend);
	free(buf);
	*/
	return 0;
}

static int agent_OnReadyMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	int  ret=1;
	
	if(session->agentId <= 0){
		return 0;
	}
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId > 0){
		ret = acd_AgentReady(session->agentGroup, session->agentId);
	}
	if(ret == 0){
		wallboard_agentstatus(session->agentExt,1,session->agentId);
	}
		
	session->agentAutoAcwTime = 0;
	
	/* Ready ACK */
	ctbMsgInit(msgsend);
	msgsend->Sender = MSG_SRC_CTI;
	msgsend->Type	 	= 0x06;									// autoin ACK
	msgsend->Count	= 5;
	ctbMsgInsertNumeric(msgsend, 0, session->agentId);			// agent id		
	ctbMsgInsertNumeric(msgsend, 1, ret);  						// result	
	ctbMsgInsertString (msgsend, 2, session->agentExt);  		// extension
	ctbMsgInsertString (msgsend, 3, session->ipAddress);		// ip addr
	ctbMsgInsertNumeric(msgsend, 4, session->agentGroup);		// group id		
		
		
	len = ctbMsgEncode(msgsend, buf+2, 1024);
	buf[0] = len << 8;
	buf[1] = (char)len;
	sockTcpWrite(session->sock, buf, len+2);
	
	free(msgsend);
	free(buf);	
	return 0;
}

static int agent_OnNotReadyMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	int  ret=1;
	
	if(session->agentId <= 0){
		return 0;
	}

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId == msg->Fields[0].a.iVal){
		ret = acd_AgentNotReady(session->agentGroup, session->agentId, msg->Fields[1].a.iVal);
	}
	if(ret == 0){
		wallboard_agentstatus(session->agentExt,2,session->agentId);
	}
		
	session->agentAutoAcwTime = 0;
	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender = MSG_SRC_CTI;
	msgsend->Type	 	= 0x05;										// AUX ACK
	msgsend->Count	= 6;
	ctbMsgInsertNumeric(msgsend, 0, session->agentId);				// agent id		
	ctbMsgInsertNumeric(msgsend, 1, msg->Fields[1].a.iVal);			// reason
	ctbMsgInsertNumeric(msgsend, 2, ret);  							// result	
	ctbMsgInsertString (msgsend, 3, session->agentExt);  			// extension
	ctbMsgInsertString (msgsend, 4, session->ipAddress);  			// ip addr
	ctbMsgInsertNumeric(msgsend, 5, session->agentGroup);			// group id		
		
		
	len = ctbMsgEncode(msgsend, buf+2, 1024);
	buf[0] = len << 8;
	buf[1] = (char)len;
	sockTcpWrite(session->sock, buf, len+2);
	
	free(msgsend);
	free(buf);	
	return 0;
}

static int agent_OnACWMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	int  ret=1;
	
	if(session->agentId <= 0){
		return 0;
	}

	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId == msg->Fields[0].a.iVal){
		ret = acd_AgentACW(session->agentGroup, session->agentId, msg->Fields[1].a.iVal);
	}
	if(ret == 0){
		wallboard_agentstatus(session->agentExt,3,session->agentId);
	}

	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	msgsend->Sender = MSG_SRC_CTI;
	msgsend->Type	 	= 0x04;										// ACW ACK
	msgsend->Count	= 6;
	ctbMsgInsertNumeric(msgsend, 0, session->agentId);				// agent id		
	ctbMsgInsertNumeric(msgsend, 1, msg->Fields[1].a.iVal);  		// reason
	ctbMsgInsertNumeric(msgsend, 2, ret);  							// result	
	ctbMsgInsertString (msgsend, 3, session->agentExt);  			// extension
	ctbMsgInsertString (msgsend, 4, session->ipAddress);  			// ip addr
	ctbMsgInsertNumeric(msgsend, 5, 0);								// group id		
		
		
	len = ctbMsgEncode(msgsend, buf+2, 1024);
	buf[0] = len << 8;
	buf[1] = (char)len;
	sockTcpWrite(session->sock, buf, len+2);
	
	free(msgsend);
	free(buf);	
	return 0;
}

static int agent_OnAnswerCallMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
		logger_Print(2,1,"ANSWERCALL: Agent not found\n");
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_ANSWER_CALL;
		msgsend->Count 	= 3;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);			// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId	
		ctbMsgInsertString (msgsend, 2, session->agentExt);
		logger_Print(2,1,"ANSWERCALL by %d %s\n", msg->Fields[0].a.iVal, session->agentExt); 
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

static int agent_OnDropCallMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_DROP_CALL;
		msgsend->Count 	= 3;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);			// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId	
		ctbMsgInsertString (msgsend, 2, session->agentExt);

		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

static int agent_OnHoldCallMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_HOLD_CALL;
		msgsend->Count 	= 3;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);			// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId		
		ctbMsgInsertString (msgsend, 2, session->agentExt); 			// extension
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

static int agent_OnRetrieveCallMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_RETR_CALL;
		msgsend->Count 	= 3;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);			// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId	
		ctbMsgInsertString (msgsend, 2, session->agentExt);				//
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

/* transfer call */
static int agent_OnAutoXferCallMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_AUTOXFER_CALL;
		msgsend->Count 	= 6;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);			// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId		
		ctbMsgInsertString (msgsend, 2, msg->Fields[1].a.szVal);		// tac
		ctbMsgInsertString (msgsend, 3, msg->Fields[1].a.szVal);		// destno
		ctbMsgInsertString (msgsend, 4, msg->Fields[3].a.szVal);		// password
		ctbMsgInsertString (msgsend, 5, session->agentExt); //
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

/* conference call */
static int agent_OnConferenceCallMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_CONFERENCE_CALL;
		msgsend->Count 	= 6;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);			// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId
		ctbMsgInsertString (msgsend, 2, msg->Fields[1].a.szVal);		// destno
		ctbMsgInsertString (msgsend, 3, msg->Fields[2].a.szVal);		// tac
		ctbMsgInsertString (msgsend, 4, msg->Fields[3].a.szVal);		// password
		ctbMsgInsertString (msgsend, 5, session->agentExt);
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

/* transfer complete call */
static int agent_OnCompleteTransferMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_TRANS_COMPLETE;
		msgsend->Count 	= 3;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);		// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);		// agentId
		ctbMsgInsertString (msgsend, 2, session->agentExt);
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

/* conference complete call */
static int agent_OnCompleteConferenceMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_CONF_COMPLETE;
		msgsend->Count 	= 3;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);		// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);		// agentId
		ctbMsgInsertString (msgsend, 2, session->agentExt);
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

/* swap hold */
static int agent_OnSwapHoldMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_SWAP_HOLD;
		msgsend->Count 	= 4;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);		// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);		// agentId
		ctbMsgInsertString (msgsend, 2, session->agentExt);
		ctbMsgInsertString(msgsend, 3, msg->Fields[1].a.szVal);	// typeButtonRequest		
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

/* retrieve back */
static int agent_OnRetrieveBackMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* cek device on CTI */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_RETRVBACK;
		msgsend->Count 	= 4;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);		// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);		// agentId
		ctbMsgInsertString (msgsend, 2, session->agentExt);
		ctbMsgInsertString(msgsend, 3, msg->Fields[1].a.szVal);	// typeButtonRequest		
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	
	free(msgsend);
	free(buf);	
	return 0;
}

/**
 Dial request message handler
 in: 	0 - agent id
 			1 - tac
 			2 - destination number
 			3 - dial-password
 			4 - assignment-id
 			
 */
static int agent_OnDialMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	if (session->agentId != msg->Fields[0].a.iVal){
		/* send negative ack */
	}else{
		/* Dial Request */
		ctbMsgInit(msgsend);
		msgsend->Sender = MSG_SRC_AGENTHANDLER;
		msgsend->Type 	= MSGTYPE_MAKE_CALL;
		msgsend->Count 	= 7;
		ctbMsgInsertNumeric(msgsend, 0, (UINT32)session->sock);							// sock
		ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);			// agentId		
		ctbMsgInsertString (msgsend, 2, msg->Fields[1].a.szVal);		// tac
		ctbMsgInsertString (msgsend, 3, msg->Fields[2].a.szVal);		// destno
		ctbMsgInsertString (msgsend, 4, msg->Fields[3].a.szVal);		// password
		ctbMsgInsertNumeric(msgsend, 5, msg->Fields[4].a.iVal);			// assignment-id
		ctbMsgInsertString (msgsend, 6, session->agentExt);
		// send to CTI
		len = ctbMsgEncode(msgsend, buf, 1024);
		cti_Write(buf, len);
	}
	free(msgsend);
	free(buf);	
	return 0;
}

static int agent_OnCTIUseExtMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	// Login ACK message untuk dikirim:
	msgsend->Sender = MSG_SRC_CC_CTI;
	msgsend->Type	 	= MSGTYPE_ACK_AGENTLOGIN;
	msgsend->Count	= 5;
	
	ctbMsgInsertNumeric(msgsend, 0, session->agentId);					// agent id
	ctbMsgInsertNumeric(msgsend, 1, msg->Fields[2].a.iVal);				// result
	ctbMsgInsertString (msgsend, 2, msg->Fields[1].a.szVal);			// agent ext
	ctbMsgInsertString (msgsend, 3, session->ipAddress);
	ctbMsgInsertNumeric(msgsend, 4, session->agentGroup);
	// send to AGENT
	len = ctbMsgEncode(msgsend, buf+2, 1024);
	buf[0] = len << 8;
	buf[1] = (char)len;
	sockTcpWrite(session->sock, buf, len+2);
	
	logger_Print(2,1,"AGENT>> agent_OnCTIUseExtMessage result = %d\n",msg->Fields[2].a.iVal);
	if (msg->Fields[2].a.iVal == 0){
		int status;
		
		logger_Print(2,1,"AGENT-%d>> Extension %s can be used\n",session->agentId,msg->Fields[1].a.szVal);
		strcpy(session->agentExt, msg->Fields[1].a.szVal);	
		//printf("AGENT>> acd_AgentLogin:agent=%s, group=%d, agent_id=%d, ext=%s, ip=%s\n",session->agentName,session->agentGroup, session->agentId, session->agentExt, session->ipAddress);
		status = acd_AgentLogin(session->agentGroup, session->agentId, session->agentExt, session->ipAddress);
		logger_Print(2,4,"AGENT-%d>> agent after login status=%d acd_AgentLogin on agent_OnCTIUseExtMessage\n",session->agentId, status);
		if(status >= 0){
			wallboard_agentstatus(session->agentExt,status,session->agentId);
		}
		if (msg->Fields[4].a.iVal == 0){
			acd_AgentChangeExtStatus(session->agentGroup, session->agentId, ACD_PHONESTATUS_IDLE,"0");
		}else{
			acd_AgentChangeExtStatus(session->agentGroup, session->agentId, ACD_PHONESTATUS_TALKING,"0");
		}
		


		if (status == ACD_AGENTSTATUS_READY){
			ctbMsgInit(msgsend);
			msgsend->Sender = MSG_SRC_CTI;
			msgsend->Type	 	= MSGTYPE_ACK_AGENTREADY; /* READY */
			msgsend->Count	= 5;
			ctbMsgInsertNumeric(msgsend, 0, session->agentId);				// agent id		
			ctbMsgInsertNumeric(msgsend, 1, 0);  							// result	
			ctbMsgInsertString (msgsend, 2, session->agentExt);  			// extension
			ctbMsgInsertString (msgsend, 3, session->ipAddress);			// ip addr
			ctbMsgInsertNumeric(msgsend, 4, session->agentGroup);
			len = ctbMsgEncode(msgsend, buf+2, 1024);
			buf[0] = len << 8;
			buf[1] = (char)len;
			sockTcpWrite(session->sock, buf, len+2);
		}else if (status == ACD_AGENTSTATUS_NOTREADY){
			ctbMsgInit(msgsend);
			msgsend->Sender = MSG_SRC_CTI;
			msgsend->Type	 	= MSGTYPE_ACK_AGENTNOTREADY;	/* NOTREADY*/
			msgsend->Count	= 6;
			ctbMsgInsertNumeric(msgsend, 0, session->agentId);
			ctbMsgInsertNumeric(msgsend, 1, 0); // reason
			ctbMsgInsertNumeric(msgsend, 2, 0); // result	
			ctbMsgInsertString (msgsend, 3, session->agentExt);
			ctbMsgInsertString (msgsend, 4, session->ipAddress);
			ctbMsgInsertNumeric(msgsend, 5, session->agentGroup);
			len = ctbMsgEncode(msgsend, buf+2, 1024);
			buf[0] = len << 8;
			buf[1] = (char)len;
			sockTcpWrite(session->sock, buf, len+2);
		}		
	}else{
		session->agentId = 0;//
		//
		//
		//
		//gara-gara ini ya??????????
		logger_Print(2,1,"AGENT-%d>> Extension can not be used\n",session->agentId);
	}
	
	free(msgsend);
	free(buf);
	
	return 0;
}

static int agent_OnCTIRelExtMessage(t_AgentSession *session, tCtbMessage	*msg){
	tCtbMessage	*msgsend;
	char *buf;
	int  len,ret;
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);
	
	
	/* cek device on CTI */
	ctbMsgInit(msgsend);
	// Login ACK message untuk dikirim:
	msgsend->Sender = MSG_SRC_CC_CTI;
	msgsend->Type	 	= MSGTYPE_ACK_AGENTLOGOUT;
	msgsend->Count	= 5;
	
	ctbMsgInsertNumeric(msgsend, 0, session->agentId);					// agent id
	ctbMsgInsertNumeric(msgsend, 1, msg->Fields[0].a.iVal);				// result
	ctbMsgInsertString (msgsend, 2, session->agentExt);
	ctbMsgInsertString (msgsend, 3, session->ipAddress);
	ctbMsgInsertNumeric(msgsend, 4, 0);									// group id
	// send to AGENT
	len = ctbMsgEncode(msgsend, buf+2, 1024);
	buf[0] = len << 8;
	buf[1] = (char)len;
	sockTcpWrite(session->sock, buf, len+2);
	
	free(msgsend);
	free(buf);
	
	logger_Print(2,1,"AGENT-%d>> Release extension %s result = %d\n",session->agentId,session->agentExt,msg->Fields[0].a.iVal);
	
	ret = acd_AgentLogout(session->agentGroup, session->agentId);
	if(ret == 0){
		wallboard_agentstatus(session->agentExt,6,session->agentId);
	}

	logger_Print(2,1,"AGENT-%d>> ret=%d acd_AgentLogout: group=%d\n", session->agentId,ret,session->agentGroup);
	
	if(ret < 0){
		logger_Print(2,1,"AGENT-%d>> ret=%d AGENT ALREADY LOGOUT\n",session->agentId,ret);
		wallboard_agentstatus(session->agentExt,6,session->agentId);
	}
	
	pthread_mutex_lock(&session->lock);
	session->agentId = 0;	
	session->agentGroup = 0;
	strcpy(session->agentExt, "");
	pthread_mutex_unlock(&session->lock);	
	
	return 0;
}

static int agent_MessageProcessing(t_AgentSession *session, char *message, int len){
	tCtbMessage	*msg;
	
	msg = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	if (!msg){
		logger_Error("malloc failed!\n");
		return -1;
	}
	
	/* parse message */
	ctbMsgInit(msg);

	ctbMsgDecode(msg, (unsigned char *)message, len);
	if(msg->Type != 1){
		logger_Print(2,1,"AGENT message:mesage=%f sender=%d type=%d count=%d Field[0]=%d\n",(unsigned char *)message, msg->Sender,msg->Type,msg->Count,msg->Fields[0].a.iVal);
	}
	/* process message */
	switch (msg->Sender){
		case MSG_SRC_CC_AGENT:		// message from agent desktop
		switch(msg->Type){
			case 0x01: /* heartbeat */
				session->pingCount = 0;
				break;
			case 0x02:			// agent login
				logger_Print(2,1,"AGENT-%d>> Login Request\n", msg->Fields[0].a.iVal);
				agent_OnLoginMessage(session, msg);
				break;
			case 0x03:			// agent logout
				if(session->agentId > 0){				
					logger_Print(2,1,"AGENT-%d>> Logout Request\n", session->agentId);
					agent_OnLogoutMessage(session, msg);
				}
				break;
			case 0x04:			// agent acw
				logger_Print(2,1,"AGENT-%d>> ACW Request\n", session->agentId);
				agent_OnACWMessage(session, msg);
				break;
			case 0x05:			// agent aux
				logger_Print(2,1,"AGENT-%d>> AUX Request\n", session->agentId);
				agent_OnNotReadyMessage(session, msg);
				break;
			case 0x06:			// agent autoin
				logger_Print(2,1,"AGENT-%d>> Ready Request\n", session->agentId);
				agent_OnReadyMessage(session, msg);
				break;
			case 0x07:			// agent manual-in
				logger_Print(2,1,"CTI>> Got Agent Manual-in\n");
				//cti_on_agent_manual_in(msg, ip_address);
				break;
				
			/* Call related operation */
			case 0x08:			// agent hold
				logger_Print(2,1,"AGENT-%d>> Hold Request\n", session->agentId);
				agent_OnHoldCallMessage(session, msg);
				break;
			case 0x09:			// agent retrieve hold
				logger_Print(2,1,"AGENT-%d>> Retrieve Hold Request\n", session->agentId);
				agent_OnRetrieveCallMessage(session, msg);				
				break;
			case 0x0a:			// agent disconnect/drop call
				logger_Print(2,1,"AGENT-%d>> Hangup Request\n", session->agentId);
				agent_OnDropCallMessage(session, msg);
				break;
			case 0x0b:			// agent transfer call
				logger_Print(2,1,"AGENT-%d>> Transfer Call Request\n", session->agentId);
				agent_OnAutoXferCallMessage(session, msg);
				break;
			case 0x35:			// agent conference call
				logger_Print(2,1,"AGENT-%d>> Conference Call Request\n", session->agentId);
				agent_OnConferenceCallMessage(session, msg);
				break;
			case 0x36:			// agent complete transfer
				logger_Print(2,1,"AGENT-%d>> Complete Transfer Request\n", session->agentId);
				agent_OnCompleteTransferMessage(session, msg);
				break;
			case 0x37:			// agent complete conference
				logger_Print(2,1,"AGENT-%d>> Complete Conference Request\n", session->agentId);
				agent_OnCompleteConferenceMessage(session, msg);
				break;
			case 0x38:			// agent swap hold
				logger_Print(2,1,"AGENT-%d>> SwapHold Request\n", session->agentId);
				agent_OnSwapHoldMessage(session, msg);
				break;
			case 0x39:			// agent retrieve back
				logger_Print(2,1,"AGENT-%d>> Retrieve Request\n", session->agentId);
				agent_OnRetrieveBackMessage(session, msg);
				break;



			case 0x0d:			// agent internal dial 
				logger_Print(2,1,"AGENT-%d>> Dial Internal\n", session->agentId);
				//cti_on_agent_internal_dial(msg, ip_address);
				break;
			case MSGTYPE_AGENTREQ_DIAL:
				logger_Print(2,1,"AGENT-%d>> Dial Request\n", session->agentId);
				agent_OnDialMessage(session, msg);
				//cti_on_agent_external_dial(msg, ip_address);
				break;
			case 0x0f:			// spv silent-monitor agent
				logger_Print(2,1,"CTI>> Got Supervisor silent-monitor agent\n");
				//cti_on_agent_silent_monitor(msg, ip_address);
				break;
			case 0x10:			// spv pick queued-call up
				logger_Print(2,1,"CTI>> Got Supervisor pick queued-call up\n");
				//cti_on_agent_pick_queue(msg, ip_address);
				break;
			case 0x11:			// agent outbound
				logger_Print(2,1,"CTI>> Got Agent Outbound Status\n");
				//cti_on_agent_outbound(msg, ip_address);
				break;
			case 0x12:			// spv coaching agent
				logger_Print(2,1,"CTI>> Got Supervisor coaching agent\n");
				//cti_on_agent_coaching(msg, ip_address);
				break;
			case 0x15:			// agent consult call
				logger_Print(2,1,"CTI>> Got Agent Consult Call\n");
				//cti_on_agent_consult_call(msg, ip_address);
				break;
			case 0x16:			// agent add extension to conference call
				logger_Print(2,1,"CTI>> Got Agent Add Extension to Conference Call\n");
				//cti_on_agent_conference_call(msg, ip_address);
				break;
			case 0x1C:			// force agent application to close
				logger_Print(2,1,"CTI>> Got Agent-App Force Closing\n");
				//cti_on_agent_app_force_close(msg, ip_address);
				break;
			case 0x1D:			// force an agent to logout
				logger_Print(2,1,"CTI>> Got Agent Force Logout\n");
				//cti_on_agent_force_logout(msg, ip_address);
				break;
			case 0x20:			// merge agent call
				logger_Print(2,1,"CTI>> Got Agent Merge Call\n");
				//cti_on_agent_merge_call(msg, ip_address);
				break;
			case 0x21:			// answer agent call
				logger_Print(2,1,"CTI>> Got Agent Answer Call\n");
				agent_OnAnswerCallMessage(session, msg);
				//cti_on_agent_answer_call(msg, ip_address);
				break;
			case 0x0040:
				logger_Print(2,1,"CTI>> Transfer Back to Ivr\n");
				//cti_on_agent_tranfer_call_ivr(msg, ip_address);
				break;
			case 0x0041:
				logger_Print(2,1,"CTI>> Account Inquiry Request\n");
				//cti_on_agent_acc_inq(msg, ip_address);
				break;
			case MSGTYPE_SET_ASSIGNID:
				logger_Print(2,1,"CTI>> Set Assignement ID Request\n");
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

static int agent_CTIMessageProcessing(t_AgentSession *session, char *message, int len){
	tCtbMessage	*msg;
	int decodedLen;
	
	msg = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	if (!msg){
		logger_Error("malloc failed!\n");
		return -1;
	}
	
	/* parse message */
	ctbMsgInit(msg);
	decodedLen = ctbMsgDecode(msg, (unsigned char *)message, len);
	//logger_Print(2,1,"CTBMESSAGE>> Processing msg sender = %d,Type = 0x%04x\n",msg->Sender,msg->Type);
	/* process message */
	switch (msg->Sender){
		case MSG_SRC_CTI:		// message from CTI
			switch(msg->Type){
				case MSGTYPE_USE_EXT:			// agent login
					//logger_Print(2,1,"AGENT>> Use ext result\n");
					agent_OnCTIUseExtMessage(session, msg);
					break;
				case MSGTYPE_REL_EXT:
					//logger_Print(2,1,"AGENT>> Release ext result\n");
					agent_OnCTIRelExtMessage(session, msg);
					break;
					
				case MSGTYPE_EVENT_CALLALERTING:
				case MSGTYPE_EVENT_CALLCONNECTED:
				case MSGTYPE_EVENT_CALLDISCONNECTED:
				case MSGTYPE_EVENT_CALLINITIATED:
				case MSGTYPE_EVENT_CALLHELD:
				case MSGTYPE_EVENT_CALLRECONNECTED:					
					/* for this kind of message, just forward it*/
					{
						char *buf;
						
						buf = (char*)malloc(len+2);						
						buf[0] = len << 8;
						buf[1] = (char)len;
						memcpy(buf+2, message, len);
						sockTcpWrite(session->sock, buf, len+2);
						free(buf);
					}
					break;
			}
			break;
		case MSG_SRC_CC_CCD:		// message from Contact Distributor
			switch(msg->Type){
				case MSGTYPE_NOTIF_AUTOACW:
					/* autoacw activated */
					logger_Print(2,1,"AGENT-%d>> AutoACW processing\n", session->agentId);
					printf("AGENT-%d>> AutoACW processing\n", session->agentId);
					wallboard_agentstatus(session->agentExt,3,session->agentId);
					session->agentAutoAcwTime  = msg->Fields[1].a.iVal;
					session->agentAutoAcwStart = (int)time(NULL);
					break;
				default:
					/* for this kind of message, just forward it*/
					{
						char *buf;
						
						/* change source to CTI */
						msg->Sender = MSG_SRC_CC_CTI;
						buf = (char*)malloc(len+2);
						/* re-encode message*/
						len = ctbMsgEncode(msg, buf+2, 1024);
						buf[0] = len << 8;
						buf[1] = (char)len;						
						sockTcpWrite(session->sock, buf, len+2);
						free(buf);
					}
					break;
			}
		default:
			break;
	}
	
	
	ctbMsgFree(msg);
	free(msg);
	return decodedLen;
}

int agent_DispatchMessage(int agentId, char* msg, int len){
	t_AgentSession *session;
	
	listLock(&AgentSessions);	
	
	session = agent_FindSessionByAgentId(&AgentSessions, agentId);
	if (session){
		pthread_mutex_lock(&session->lock);
		//logger_Print(2,1,"AGENT-%d>> DispatchMessage %d byte(s) to current %d\n", session->agentId, len, session->bufSize);
		if (len < (8192 - session->bufSize)){
			memcpy(session->buf+session->bufSize, msg, len);			
			session->bufSize += len;
		}
		pthread_mutex_unlock(&session->lock);
		SetEvent(session->evtSocket[1]);
	}
	
	listUnlock(&AgentSessions);
	return 0;
}


