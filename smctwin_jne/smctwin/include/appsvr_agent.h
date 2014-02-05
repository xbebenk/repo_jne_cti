#ifndef _AppsvrAgentH_
#define _AppsvrAgentH_

#define DEFAULT_AGENT_PORT 		16000
#define PING_INTERVAL		30
#define MAX_PING_COUNT		40

typedef struct _t_AgentSession{
	SOCKET 	sock;
	char 	ipAddress[32];
	int 	port;
	HANDLE	evtSocket[2]; //0-socket, 1-interupt
	
	/* agent information */
	int  agentId;
	int  agentGroup;
	char agentUserId[16];
	char agentPasswd[32];
	char agentName[64];
	char agentExt[16];
	int  agentAutoAcwTime;
	int  agentAutoAcwStart;
	
	int active;
	
	/* agent status maintained by ACD modules */	
	
	/* session data */
	char buf[8192];
	int  bufSize;
	int  pingCount;
	
	pthread_t threadId;
	pthread_mutex_t lock;
	
	struct _t_AgentSession *next;
	struct _t_AgentSession *prev;
}t_AgentSession;

typedef struct {
	int size;
	t_AgentSession *first;
	t_AgentSession *last;
	pthread_mutex_t lock;
}t_AgentSessionList;


typedef struct TAgentHandlerPvt{
	int			listenPort;
	SOCKET	listenSock;
	
	pthread_t mainThreadId;
}TAgentHandlerPvt;


int agent_Load();
int agent_DispatchMessage(int agentId, char* msg, int len);


#endif//_AppsvrAgentH_
