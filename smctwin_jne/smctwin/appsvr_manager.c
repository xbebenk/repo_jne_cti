/*
  Manager message, first line is command
  next line is parameter
*/


#include "include/smct.h"
#include "ctblib_message.h"
#include "include/appsvr.h"
#include "acd/acd.h"
#include "cti/cti.h"

#pragma warning(disable : 4996)  // deprecated CRT function

#define MAX_LINES								2048
#define DEFAULT_MANAGER_PORT 		9800

typedef struct _t_ManagerSession{
	SOCKET  sock;
	
  char 	ipAddress[256];
  int		port;
  
  int  InLen;
  char InBuf[65025];
  char *LinePtr;
  char LineBuf[65025];
  char *Lines[MAX_LINES];
  int  LineCount;
  struct _t_ManagerSession *next;
  struct _t_ManagerSession *prev;
}t_ManagerSession;

typedef struct {
	int size;
	t_ManagerSession *first;
	t_ManagerSession *last;	
}t_ManagerSessionList;

/* module private data */
typedef struct _t_ManagerPvt{
	int listenPort;
	SOCKET listenSock;
	
	pthread_t mainThreadId;
}t_ManagerPvt;

typedef void (*t_CmdHandler)( t_ManagerSession *);

typedef struct _t_ManagerCommand{
	char 	cmd[256];
	char 	desc[256];
	t_CmdHandler cmdHandler;
  
  struct _t_ManagerCommand *next;
  struct _t_ManagerCommand *prev;
}t_ManagerCommand;

typedef struct {
	int size;
	t_ManagerCommand *first;
	t_ManagerCommand *last;	
}t_ManagerCommandList;

static t_ManagerPvt *managerPvt;
static t_ManagerSessionList ManagerSessions={0};
static t_ManagerCommandList ManagerCommands={0};

static int manager_RegisterCmd(char* cmd, char *desc, t_CmdHandler cmdHandler){
	t_ManagerCommand *mgrCmd;
	
	mgrCmd = listNewItem(t_ManagerCommand);
	strcpy(mgrCmd->cmd, cmd);
	strcpy(mgrCmd->desc, desc);
	mgrCmd->cmdHandler = cmdHandler;
	
	listInsertLast(&ManagerCommands, mgrCmd);
	return 0;
}

/*
static int manager_UnregisterCmd(char* cmd){
	
	return 0;
}
*/

static int manager_loadConfigDB(){
	tDbConn dbConn;
	tDbSet  dbSet;
  char sql[2048];
  int  sqlLen;
  
  logger_Print(5,1,"MANAGER>> Loading Configuration from DB\n");
	
	/* connect to DB */
	dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
  if (!dbConn){
  	logger_Error("MANAGER>> DB Connection failed\n");
  	return -1;
  } 
  
  sqlLen = sprintf(sql,"SELECT set_name, set_value  FROM settings WHERE set_modul = 'manager'");  
  dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
  while(dbLib->nextRow(dbSet)){
  	
  	if (!strcasecmp(dbLib->getStringFieldByIdx(dbSet, 0), "server.port")){
			managerPvt->listenPort = dbLib->getIntFieldByIdx(dbSet, 1);
		}

  }
  dbLib->closeQuery(dbSet); 
  
  dbLib->closeConnection(dbConn);
	return 0;
}

static char *manager_GetParam(t_ManagerSession *session, char *var){
	char cmp[128];
	int x, len;
	len = _snprintf(cmp, sizeof(cmp), "%s: ", var);
	for (x=0;x<session->LineCount;x++)
		if (!_strnicmp(cmp, session->Lines[x], len))
	  	return session->Lines[x] + len;
	return "";
}

static int manager_ParseCommand(t_ManagerSession *session, char **command){
	char *ptr;
	
	ptr = session->Lines[0];
	
	*command = session->Lines[0];
	while(*ptr && *ptr != ' ' && *ptr != '\n')++ptr;
  
  if(*ptr) *ptr = '\0';
	return 0;
}

static void manager_OnListAgents(t_ManagerSession *session){
	char *buf, header[100];
	int len, headerlen;
	
	buf = (char*)malloc(64000);
	if(!buf)return;
	len = acd_PrintAgents(buf, 64000);
	headerlen = sprintf(header, "list-agents OK %d\r\n", len);
	sockTcpWrite(session->sock, header, headerlen);
	sockTcpWrite(session->sock, buf, len);
	free(buf);
}

static void manager_OnListAgent(t_ManagerSession *session){
	char *buf, header[100];
	int len, headerlen;
	char *agentId;
	int nAgentId;
	
	buf = (char*)malloc(64000);
	if(!buf)return;
		
	agentId = manager_GetParam(session, "agent-id");
	nAgentId = atoi(agentId);
	len = acd_PrintAgent(buf, 64000, nAgentId);
	headerlen = sprintf(header, "list-agents OK %d\r\n", len);
	sockTcpWrite(session->sock, header, headerlen);
	sockTcpWrite(session->sock, buf, len);
	free(buf);
}

static void manager_OnReserveAgent(t_ManagerSession *session){
	char *buf, header[100];
	int len, headerlen;
	char *groupId, *alg, *skill, *ch;
	char ext[20]={0};
	
	buf = (char*)malloc(256);
	if(!buf)return;
		
	groupId = manager_GetParam(session, "group-id");
	alg 		= manager_GetParam(session, "algorithm");
	skill 	= manager_GetParam(session, "skill");
	ch = manager_GetParam(session, "ch");
	//logger_Print(5,1,"MGR: Reserve Agent request for group %s with algorithm %s\n", groupId, alg);
	acd_ReserveAgentExt(atoi(groupId), atoi(alg), NULL, ext, ch);
		//len       = sprintf(buf, "ext= %s\r\n\r\n", ext);

		len       = sprintf(buf, "ext= %s\r\n"
									"group= %s\r\n"
									"ch= %s\r\n\r\n", ext,groupId,ch);
		//logger_Print(5,1,"MGR: Reserve Agent request for group %s with algorithm %s, got ext = %s\n", groupId, alg, ext);
		//logger_Print(5,1,"ext= %s\n", ext);
		//logger_Print(5,1,"session->port %d\n", session->port);
		headerlen = sprintf(header, "reserve-agent OK %d\r\n", len);	
		sockTcpWrite(session->sock, header, headerlen);
		sockTcpWrite(session->sock, buf, len);
		free(buf);
}

/**
 load-agent
 agent-id: <agent-id>
 */
static void manager_OnLoadAgent(t_ManagerSession *session){
	char header[100];
	int headerlen;
	char *agentId;
		
	agentId = manager_GetParam(session, "agent-id");
	acd_LoadAgent(atoi(agentId));	
	headerlen = sprintf(header, "load-agent OK 0\r\n");	
	sockTcpWrite(session->sock, header, headerlen);	
}

/**
	Request available agent for incoming media
	
 media-request-agent
 group-id: <group>
 algorithm: <algorithm>
 skill: <skill>
 media: <media> 
 
 RETURN
 	agent-id
 
 */
static void manager_OnMediaAgentRequest(t_ManagerSession *session){
	char *buf, header[100];
	int len=0, headerlen;
	char *groupId, *alg, *skill, *media;
	int  agentId;
	
	buf = (char*)malloc(64000);
	if(!buf)return;
		
	groupId = manager_GetParam(session, "group-id");
	alg 		= manager_GetParam(session, "algorithm");
	skill 	= manager_GetParam(session, "skill");
	media 	= manager_GetParam(session, "media");	
	
	agentId = acd_GetAgentForMM(atoi(groupId), atoi(alg), NULL, media);
	if (agentId > 0){
		len 			= sprintf(buf,    "%d\r\n\r\n", agentId);
		headerlen = sprintf(header, "agent-request-for-media OK %d\r\n", len);
	}else{
		headerlen = sprintf(header, "agent-request-for-media NOK %d\r\n", len);		
	}
	
		
	sockTcpWrite(session->sock, header, headerlen);
	if(len > 0)
		sockTcpWrite(session->sock, buf, len);
	free(buf);
}

/**
	Alert agent for incoming media
	
 media-alert-agent
 agent-id: <agent>
 media: <media>
 media-id: <media-id>
 
 RETURN
 	none 
 */
static void manager_OnMediaAgentAlert(t_ManagerSession *session){
	char *buf, header[100];
	int len=0, headerlen;
	char *media, *mediaId;
	int agentId;
	
	buf = (char*)malloc(64000);
	if(!buf)return;
		
	agentId	= atoi(manager_GetParam(session, "agent-id"));
	media 	= manager_GetParam(session, "media");
	mediaId	= manager_GetParam(session, "media-id");	
	
	if (agentId > 0){
		tCtbMessage	*msgsend;
		char *_buf;
		int  _len;			
		
		/* push email_alert event to agent */
		msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
		_buf = (char*)malloc(1024);			
		
		ctbMsgInit(msgsend);			
		msgsend->Sender 	= MSG_SRC_CC_CCD;
		msgsend->Type	 		= MSGTYPE_EVENT_EMAILALERT;
		msgsend->Count		= 2;
		
		ctbMsgInsertNumeric(msgsend, 0, agentId);							// agent id		
		ctbMsgInsertNumeric(msgsend, 1, atoi(mediaId)); 	
		
		_len = ctbMsgEncode (msgsend, _buf, 512);	
		agent_DispatchMessage(agentId, _buf, _len);  
		free(msgsend);
		free(_buf);		
		
		headerlen = sprintf(header, "media-alert-agent OK %d\r\n", len);
	}else{
		headerlen = sprintf(header, "media-alert-agent NOK %d\r\n", len);		
	}	
		
	sockTcpWrite(session->sock, header, headerlen);
	free(buf);
}

/*
 change-agent-status
 agent-id: <id>
 status: <status>
*/
static void manager_OnChangeAgentStatus(t_ManagerSession *session){
	char *buf;	
	char *agentId, *status;	
	
	buf = (char*)malloc(64000);
	if(!buf)return;
		
	agentId = manager_GetParam(session, "agent-id");
	status 	= manager_GetParam(session, "status");
	
	free(buf);
}

/*
 
 change-agent-ext-status
 agent-id: <id>
 group-id: <id>
 status: <status>
*/
static void manager_OnChangeAgentExtStatus(t_ManagerSession *session){
	char *buf;	
	char *agentId, *groupId, *status;	
	int len;
	
	buf = (char*)malloc(64000);
	if(!buf)return;
		
	agentId = manager_GetParam(session, "agent-id");
	groupId = manager_GetParam(session, "group-id");
	status 	= manager_GetParam(session, "status");
	
	acd_AgentChangeExtStatus(atoi(groupId), atoi(agentId), atoi(status),"0");
	
	len = sprintf(buf, "change-agent-ext-status OK 0\r\n");	
	sockTcpWrite(session->sock, buf, len);
	free(buf);
}

/*
 
 change-autoacw-setting 
 group-id: <id>
 autoacw: <0/1>
 autoacw-time: <second>
 autoacw-reason: <reason>
*/
static void manager_OnChangeAutoACWSetting(t_ManagerSession *session){
	char *buf;	
	char *groupId, *autoacw, *autoacwtime, *autoacwreason;	
	int len;
	
	buf = (char*)malloc(64000);
	if(!buf)return;		
	
	groupId 			= manager_GetParam(session, "group-id");
	autoacw 			= manager_GetParam(session, "autoacw");
	autoacwtime 	= manager_GetParam(session, "autoacw-time");
	autoacwreason = manager_GetParam(session, "autoacw-reason");
	
	acd_ChangeAutoACWSetting(atoi(groupId), atoi(autoacw), atoi(autoacwtime), atoi(autoacwreason));
	
	len = sprintf(buf, "change-autoacw-setting OK 0\r\n");	
	sockTcpWrite(session->sock, buf, len);
	free(buf);
}

/**
 add-station - Add Station
 ext: <ext-number>
 ext-model: <model>
 ip-address: <ip-address>
 */
static void manager_OnAddStation(t_ManagerSession *session){
	char *extNumber, *ipAddress, *extModel;
	tCtbMessage	*msgsend;
	char *_buf;
	int  _len;
	
	extNumber = manager_GetParam(session, "ext");
	extModel  = manager_GetParam(session, "ext-model");
	ipAddress = manager_GetParam(session, "ip-address");		
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	_buf    = (char*)malloc(1024);			
	
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_MANAGER;
	msgsend->Type	 		= MSGTYPE_ADD_STATION;
	msgsend->Count		= 3;
	
	ctbMsgInsertString(msgsend, 0, extNumber);
	ctbMsgInsertString(msgsend, 1, extModel);
	ctbMsgInsertString(msgsend, 2, ipAddress);
	
	_len = ctbMsgEncode (msgsend, _buf, 512);	
	cti_Write(_buf, _len);  
	free(msgsend);
	free(_buf);
}

/**
 cha-station - Change Station location information
 ext: <ext-number>
 ip-address: <ip-address>
 */
static void manager_OnChangeStation(t_ManagerSession *session){
	char *extNumber, *ipAddress;
	
	extNumber = manager_GetParam(session, "ext");
	ipAddress = manager_GetParam(session, "ip-address");
}

/**
 del-station - Delete Station
 ext: <ext-number>
 */
static void manager_OnDeleteStation(t_ManagerSession *session){
	char *extNumber;
	tCtbMessage	*msgsend;
	char *_buf;
	int  _len;
	
	extNumber = manager_GetParam(session, "ext");	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	_buf    = (char*)malloc(1024);			
	
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_MANAGER;
	msgsend->Type	 		= MSGTYPE_REM_STATION;
	msgsend->Count		= 1;
	
	ctbMsgInsertString(msgsend, 0, extNumber);	
	
	_len = ctbMsgEncode (msgsend, _buf, 512);	
	cti_Write(_buf, _len);  
	free(msgsend);
	free(_buf);
}

static void manager_OnQuit(t_ManagerSession *session){
	logger_Print(5,1,"MANAGER>> Client quit\n");	
	sockClose(session->sock);
	session->sock = INVALID_SOCKET;	
}

static void manager_ProcessMessage(t_ManagerSession *session){
	char *command;
	t_ManagerCommand *mgrCmd;	

	if (session->LineCount <=0)
		return;
		
	manager_ParseCommand(session, &command);
	
	mgrCmd = listFirst(&ManagerCommands);
	while(mgrCmd){
		if(!strcasecmp(mgrCmd->cmd, command)){
			if(mgrCmd->cmdHandler)
				mgrCmd->cmdHandler(session);
			break;
		}
		mgrCmd = listNext(mgrCmd);
	}	
}

static int manager_ParseMessage(t_ManagerSession *session){
	char *ptr0, *ptr;
	int step, line_len, line_end;

  //get line from buffer
  ptr = ptr0 = session->InBuf;
  step = 0;
  line_end = 0;
  while(step < session->InLen){  	
    if (*ptr == '\n'){    	
    	
      //we got line
      line_len = (int)(ptr-ptr0) + 1;

      memcpy(session->LinePtr, ptr0, line_len-2); //not including \r\n
      session->LinePtr[line_len-2]     = 0;
      session->Lines[session->LineCount]= session->LinePtr;
      session->LinePtr += (line_len + 1);
      ++session->LineCount;

      if (line_len == 2){  //its should be \r\n
        //Got full message, process it
        manager_ProcessMessage(session);

        //processing done, reset data
        session->LinePtr   = session->LineBuf;
        session->LineCount = 0;
      }
      ptr0 = ptr+1;
      line_end = step+1;
    }
    ++ptr;++step;
  }

  if (line_end > 0 && line_end < session->InLen){
		memmove(session->InBuf, session->InBuf + line_end, session->InLen - line_end);
    session->InLen -= line_end;
  }else if (line_end == session->InLen)
    session->InLen = 0;

  return 0;
}

static void manager_MainLoop(void *param){
	fd_set  fds;
	struct 	timeval timeout;
	int 		nready;
	SOCKET	clieSocket;
	int 		cliePort;
	char 		clieIpAddress[16];
	char    buf[64000];
	int			nread;
	t_ManagerSession *session;
	
	if (!managerPvt)
		return;
	
	/* wait for incoming connection */
	
	for(;;){		
    FD_ZERO(&fds);
		FD_SET (managerPvt->listenSock, &fds);		
		
		//tambahkan pula kelien kita
    session=listFirst(&ManagerSessions);
    while(session){
			FD_SET (session->sock, &fds);			
			session = listNext(session);
    }

		timeout.tv_sec  = 10;
    timeout.tv_usec = 0;    
    if ((nready = select(0, &fds, NULL, NULL, &timeout)) > 0){
    	if (FD_ISSET(managerPvt->listenSock, &fds)){
	    	clieSocket = sockTcpServerAccept(managerPvt->listenSock, clieIpAddress, &cliePort);
			printf("MANAGER>> Accepting connection from %s:%d\n", clieIpAddress, cliePort);
	    	logger_Print(5,5,"MANAGER>> Accepting connection from %s:%d\n", clieIpAddress, cliePort);
	    	
	    	/* create new session data */
	    	session = listNewItem(t_ManagerSession);
	    	session->sock = clieSocket;
	    	strcpy(session->ipAddress, clieIpAddress);
	    	session->port = cliePort;
	    	session->LinePtr = session->LineBuf;
	    	
	    	/* insert to session list */    	
	    	listInsertLast(&ManagerSessions, session);
	    }
    	
    	session=listFirst(&ManagerSessions);
    	while(session){
    		if (FD_ISSET(session->sock, &fds)){
					nread = recv(session->sock, buf, sizeof(buf), 0);
    			if ( nread <= 0 ){
           			t_ManagerSession *tmp;
		           	
					//logger_Print(5,1,"MANAGER>> Connection from %s:%d closed #1\n", session->ipAddress, session->port);
					closesocket(session->sock);
					tmp  = session;
					session = listNext(session);
					listRemove(&ManagerSessions, tmp);
					free(tmp);
					continue;
				}else{
          			//Data available
          			//Copy available data to session's buffer          	
					memcpy(session->InBuf+session->InLen, buf, nread);
					session->InLen+=nread;
					manager_ParseMessage(session);						
						if(session->sock == INVALID_SOCKET){
							/* connection closed, delete it */
							t_ManagerSession *tmp;
           	
							//logger_Print(5,1,"MANAGER>> Connection from %s:%d closed #2\n", session->ipAddress, session->port);
							tmp  = session;
							session = listNext(session);
							listRemove(&ManagerSessions, tmp);
							free(tmp);
            				continue;
						}
				}
			}
			session = listNext(session);
		}
    }
	}
	return ;
}

static int manager_Init(){

	managerPvt = (t_ManagerPvt*)malloc(sizeof(t_ManagerPvt));
	if (!managerPvt)
		return -1;		
	memset(managerPvt, 0, sizeof(t_ManagerPvt));	
	managerPvt->listenPort = DEFAULT_MANAGER_PORT;
	
	manager_loadConfigDB();
	
	/* create server socket */
	managerPvt->listenSock = sockTcpServerCreate(managerPvt->listenPort, 10);	
	if (managerPvt->listenPort < 0)
		return -1;
	logger_Print(5,1,"managerPvt->listenPort %d\n",managerPvt->listenPort);
		
	/**
	 Register command handler
	 */
	
	/* agent command */
	manager_RegisterCmd("list-agent",  						 "List Agent command", manager_OnListAgent);
	manager_RegisterCmd("list-agents", 						 "List All Agents command", manager_OnListAgents);
	manager_RegisterCmd("reserve-agent", 					 "Reserve Agent command", manager_OnReserveAgent);
	manager_RegisterCmd("load-agent", 						 "Load Agent command", manager_OnLoadAgent);
	manager_RegisterCmd("change-agent-status", 		 "Change Agent Status command", manager_OnChangeAgentStatus);
	manager_RegisterCmd("change-agent-ext-status", "Change Agent Extension Status command", manager_OnChangeAgentExtStatus);
	
	/* agent station*/
	manager_RegisterCmd("add-station", 						 "Add station command", manager_OnAddStation);
	manager_RegisterCmd("cha-station", 						 "Change station command", manager_OnChangeStation);
	manager_RegisterCmd("del-station", 						 "Delete station command", manager_OnDeleteStation);
  
	/* call session command */
	//manager_RegisterCmd("list-call-sessions", "List active call-session command", manager_OnChangeAgentExtStatus);
	
	manager_RegisterCmd("quit", 								"Quit command", manager_OnQuit);
	
	/* media command */
  manager_RegisterCmd("media-request-agent", 	"Request Agent for Incoming Media", manager_OnMediaAgentRequest);
  manager_RegisterCmd("media-alert-agent", 		"Alert Agent for Incoming Media", manager_OnMediaAgentAlert);
  
  /* ACD setting */
  manager_RegisterCmd("change-autoacw-setting", 		"Alert Agent for Incoming Media", manager_OnChangeAutoACWSetting);  
  
	return 0;
}



int manager_Load(){

	if(	manager_Init() < 0){
		logger_Error("MGR>> Failed loading module\n");
		return -1;
	}
	
	smctPthreadCreateDetached(&managerPvt->mainThreadId, NULL, manager_MainLoop, NULL);
	return 0;
}
