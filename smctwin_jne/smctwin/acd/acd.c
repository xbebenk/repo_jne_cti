/*
ASUMSI:
	-
	-
*/


#include <time.h>

#include "../include/smct.h"
#include "../include/appsvr.h"
#include "../ctblib_message.h"
#include "../include/appsvr_thread.h"
#include "../cti/cti.h"
#include "acd.h"
#include "../osdep.h"

#pragma warning(disable : 4996)  // deprecated CRT function

#define MAX_SKILL 32

typedef struct _t_Agent{
	/* data */
   int   								id;
   int									agent_level; //1=agent 2=spv
   int   								index;
   char									userid[30];
   char									name[64];
   int									afterLoginStatus;		/* status after login, ready or not ready */
   int									afterBusyStatus;		/* status setelah busy/handling contact */
   
   int	 								status;
   int	 								statusReason;   
   long									statusTime;			/* unixtimestamp */   
   
   int	 								prevStatus;
   int	 								prevReason;
   long									prevStatusTime;			/* unixtimestamp */   
   
   long									idleTime;				/* lama waktu agent idle (sec) sejak pertama kali login */
   long 								readyTimeSec; 	/* time when agent ready (sec) */
   long									readyTimeUSec; 	/* time when agent ready (usec) */
   int									callOffered;		/* jml ACD call yg diberikan agent sejak login */
   int									callHandled;		/* jml ACD call yg berhasil dihandle agent sejak login */
   int									onACD;					/* agent sedang dalam ACD call */
   
   /* device:telephony */
   char	 								extNumber[16];
   int									extStatus;
   long									extStatusTime;			/* unixtimestamp */
   char	 								extIpAddress[32];   /* ext location */
   /* device:email */
   /* device:sms */
   /* device:fax */
   /* device:chat */   
   
   /* skill information */
   t_SkillList			skills;
   
   /* statistic data for current day */
   long		statAcdCallOffered;
   long		statAcdCallAnswered;
   long		statAcdCallAbandoned;
   long		statCallDialed;
   
   long		statReadyTime;
   long		statReadyTimeMax;
   long		statReadyCount;   
   long		statACWTime;
   long		statACWTimeMax;
   long		statACWCount;
   long		statNotReadyTime;
   long		statNotReadyTimeMax;
   long		statNotReadyCount;
   
   long		statTalkTime;
   long		statTalkTimeMax;
   long		statTalkCount;
   
   long		statHoldTime;
   long		statHoldTimeMax;
   long		statHoldCount;   
   
   struct _t_Agent 	*next;
   struct _t_Agent 	*prev;
} t_Agent;

typedef struct {
	int size;
	t_Agent *first;
	t_Agent *last;
	pthread_mutex_t lock;
}t_AgentList;

/* variable penyimpan statistik */
typedef struct {
	int 	longestReadyTime;
	int 	longestReadyAgent;	
  char	longestReadyAgentName[64];
	int 	longestNotReadyTime;
	int 	longestNotReadyAgent;
	char	longestNotReadyAgentName[64];
	int 	longestACWTime;	
	int 	longestACWAgent;
	char	longestACWAgentName[64];
	int 	longestBusyTime;
	int 	longestBusyAgent;
	char	longestBusyAgentName[64];
}t_AgentSessionStatistic;


typedef struct _t_AgentGroup{
  int			id;
  int  		hunting_number;  
  t_Agent	*currentAgent;  			/* index agent yang terpilih */
  int			agent_available;      /* indikasi apakah ada agent yang available, 0=tidak ada*/
  int 		agent_active;					/* agent yang login di aplikasi */
  int 		agent_ready;					/* agent yang ready for inbound */
  int 		agent_acw;
  int 		agent_notready;
  int			agent_busy;
  int 		agent_outbound;
  
  int	    autoACW;							/* apakah group ini autoacw */
  int	    autoACWTime;
  int	    autoACWReason;
  int		readyIdleTime;
  
  int			overflowGroup;				/* id for overflow group */
  
  t_AgentList	agents;						
  t_AgentList	activeAgents;			/* daftar agent yang aktif/logged on */
  
  /* statistic */
  t_AgentSessionStatistic  stat;
  
  struct _t_AgentGroup  *next;
  struct _t_AgentGroup  *prev;
} t_AgentGroup;

typedef struct {
	int size;
	t_AgentGroup *first;
	t_AgentGroup *last;
	pthread_mutex_t lock;
}t_AgentGroupList;


typedef struct _t_SkillInfo{
	int id;											/* skill id */
	/* statistic data */
	int	agentActive;						/* agent active on this skill */
	int	agentReady;							/* agent ready on this skill */
	int	agentACW;								/* agent ACW on this skill */
	int	agentNotReady;					/* agent NotReady on this skill */
	
	struct _t_SkillInfo *next;
  struct _t_SkillInfo *prev;
}t_SkillInfo;

typedef struct {
	int size;
	t_SkillInfo *first;
	t_SkillInfo *last;
	pthread_mutex_t lock;
}t_SkillInfoList;



/* local variables */
static t_AgentGroupList AgentGroups;
static t_AgentList AgentLists;
//static t_SkillInfoList  SkillInfo;
static pthread_t 				mainThreadId;
static int							agentIdx=-1;
static pthread_mutex_t lockGroupData;

/* forward declaration */
static void acd_MainLoop(void *param);

static int acd_Init(){	
	InitializeCriticalSection(&lockGroupData);
	return 0;
}

static unsigned long int licensedagent(char *license,char *md5key,char *key){
	unsigned long int numagent=0;
	char sql[512];

	int len=0,sqlLen=0;
	unsigned long int numencrypt;
	tDbConn dbConn;
	tDbSet  dbSet;
	FILE *fh;
	char c;
	int i=0;
	char csn[128];
	char mac[128];
	char tmp[128];
	int status=0;
	
	//bypass
	return 5;
	
	printf("ACD>>Checking licensed agent\n");
	system("ipconfig /all|grep Physical >c:\\mac.txt"); 
	dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
  if (!dbConn){
  	printf("ACD>> DB Connection failed\n");
  	return -1;
  } 

	if ((fh = fopen("c:\\mac.txt", "rt"))
       == NULL)
   {
	   printf("Cannot open c:\\mac.txt.\n");
      exit(0);
   }
/*
	strncpy(md5key,license,32);
	len = strlen(license);
	md5key[32]='\0';
	for(i=32;i<len;i++){
		key[i-32]=license[i];
	}
	key[i-32]='\0';
*/

	while( fscanf(fh, "%[^:]%c%[^\n]%c", csn,&c,tmp, &c) != EOF ) {
		if(i>0){
			sscanf(tmp,"%c%s",&c,mac);	
			printf("ADAPTER-%d>>MAC_ADRESS:%s\n",i,mac);
			//bandingkan ndg md5
			sqlLen = sprintf(sql,"select md5('%s')",mac);			
			dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);	
			while(dbLib->nextRow(dbSet)){				
				if(!strcmp(md5key,dbLib->getStringFieldByIdx(dbSet, 0))){			
					status=1;
				}
			}
		  dbLib->closeQuery(dbSet);  
	}
		i++;
		if(i>5)break;
	}

	if(status){
		numencrypt = atoi(key);	
		numagent=numencrypt/97;	
		numagent=numagent-8653;	
		numagent=numagent/17;	
		numagent=numagent-9823;	
		numagent=numagent/367;	  
	}else{
		printf("LICENSED KEY NOT VALID\n");
	}

	fclose(fh);	
	dbLib->closeConnection(dbConn); 
	return numagent;
}

static int acd_LoadAgents(){
	tDbConn dbConn;
	tDbSet  dbSet, dbSet2;
  char sql[2048];
  int  sqlLen;
  
  int	group_id, agent_id, overflow_group, agent_level;
  char userid[30]={0}, name[64]={0};
  int autoacw=0, autoacwtime=0, autoacwreason=0, readyIdleTime=0;
  int loginStatus;
	int prev_group, prev_agent;	
	t_Skill *skill;
	
	t_AgentGroup *group=NULL;
	t_Agent      *agent=NULL;	

  printf("ACD>> Loading Agents\n");
  
  /* init agent session list */
	listInitWithLock(&AgentGroups);
	
	/* connect to DB */
	dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
  if (!dbConn){
  	printf("ACD>> DB Connection failed\n");
  	return -1;
  }  
  
  group_id = prev_group = prev_agent = -1;
  
  /* Load All Agents from DB */
	sqlLen = sprintf(sql,"SELECT a.id , IFNULL(a.hunting_number,a.id), "
	                     "       b.id agent_id, a.code, b.login_status, a.overflow_group, b.userid, b.name, "
	                     "       a.autoacw, a.autoacwtime, a.autoacwreason,b.occupancy, a.readyidletime "
											 "FROM agent_group a, agent b "
											 "WHERE a.id = b.agent_group "
											 "ORDER BY a.id, b.id");
	dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);	
	
	while(dbLib->nextRow(dbSet)){  	
  		group_id 				= dbLib->getIntFieldByIdx(dbSet, 0);
		agent_id 				= dbLib->getIntFieldByIdx(dbSet, 2);
		loginStatus 		= dbLib->getIntFieldByIdx(dbSet, 4);
		overflow_group 	= dbLib->getIntFieldByIdx(dbSet, 5);
		_snprintf(userid, sizeof(userid)-1, "%s", dbLib->getStringFieldByIdx(dbSet, 6));
		_snprintf(name, sizeof(name)-1, "%s", dbLib->getStringFieldByIdx(dbSet, 7));
		autoacw					= dbLib->getIntFieldByIdx(dbSet, 8);
		autoacwtime			= dbLib->getIntFieldByIdx(dbSet, 9);
		autoacwreason		= dbLib->getIntFieldByIdx(dbSet,10);
		agent_level			= dbLib->getIntFieldByIdx(dbSet,11);
		readyIdleTime		= dbLib->getIntFieldByIdx(dbSet,12);
		
		if ((group != NULL) && (group_id == prev_group)){
			if (agent_id == prev_agent){
				
			}else{
				
				/* create new agent data */
		  	agent 									= listNewItem(t_Agent);
		  	agent->id 							= agent_id;
		   	agent->index 						= ++agentIdx;
		   	agent->status 					= 0;
			agent->agent_level				= agent_level;
		   	agent->afterLoginStatus	= loginStatus;
		   	strcpy(agent->userid, userid);
		   	strcpy(agent->name, name);
    		
   			/* load agent skill */				
	 			sqlLen = sprintf(sql,"SELECT skill, score FROM agent_skill "
	 			                     "WHERE agent = %d ORDER by id", agent_id);
				dbSet2 = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
			  while(dbLib->nextRow(dbSet2)){
			  	skill        = listNewItem(t_Skill);
			  	skill->id    = dbLib->getIntFieldByIdx(dbSet2, 0);
			  	skill->score = dbLib->getIntFieldByIdx(dbSet2, 1);
			  	listInsertLast(&agent->skills, skill);			  	
			  }
			  dbLib->closeQuery(dbSet2);
			   			
	 			// add this agent to group
			   printf("Adding agent_id=%d, group_id=%d, userid=%s\n", agent->id, group->id, agent->userid);
			  //printf("Adding agent level=%d, id=%d, init_status=%d, skill=%d, userid=%s\n", agent->agent_level, agent->id, agent->afterLoginStatus, skill->id, agent->userid);
			  //logger_Print(1,5,"Adding agent level=%d, id=%d, init_status=%d, skill=%d, userid=%s\n", agent->agent_level, agent->id, agent->afterLoginStatus, skill->id, agent->userid);
	 			listLock(&group->agents);
		    listInsertLast(&group->agents, agent);
	 			listUnlock(&group->agents);
			}
		}else{
			/* found new group */
			
			/* create new group data */
    	group = listNewItem(t_AgentGroup);			 
		group->id 							= group_id;
	  	group->hunting_number 	= dbLib->getIntFieldByIdx(dbSet, 1);
	  	group->currentAgent 		= NULL;    /* index agent yang terpilih trakhir */
	  	group->agent_available 	= 0;
	  	group->overflowGroup 		= overflow_group;
	  	group->autoACW					= autoacw;
	  	group->autoACWTime			= autoacwtime;
	  	group->autoACWReason    = autoacwreason;
		group->readyIdleTime = readyIdleTime;
	  	
	  	listInitWithLock(&group->agents);
		printf("\nGroup-%d --[AACWstatus=%d, AACWtime=%d, AACWreason=%d, readyIdleTime=%d]\n", group->id, group->autoACW, group->autoACWTime, group->autoACWReason, group->readyIdleTime);
	 		/* create new agent data */
	  	agent 									= listNewItem(t_Agent);	  	
	  	agent->id 							= agent_id;
	   	agent->index 						= ++agentIdx;
		agent->agent_level				= agent_level;
	   	agent->status 					= 0;
	   	agent->afterLoginStatus	= loginStatus;	   	
	   	strcpy(agent->userid, userid);
		  strcpy(agent->name, name);
 			
			/* load agent skill */				
	 		sqlLen = sprintf(sql,"SELECT skill, score FROM agent_skill "
	 			                     "WHERE agent = %d ORDER by id", agent_id);
			dbSet2 = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
		  while(dbLib->nextRow(dbSet2)){
		  	skill        = listNewItem(t_Skill);
		  	skill->id    = dbLib->getIntFieldByIdx(dbSet2, 0);
		  	skill->score = dbLib->getIntFieldByIdx(dbSet2, 1);
		  	listInsertLast(&agent->skills, skill);
		  }
		  dbLib->closeQuery(dbSet2);			
		  
		  printf("Adding agent_id=%d, group_id=%d, userid=%s\n", agent->id, group->id, agent->userid);
 		//	printf("Adding agent level=%d, id=%d, init_status=%d, skill=%d, userid=%s\n", agent->agent_level, agent->id, agent->afterLoginStatus, skill->id, agent->userid);	
 			//logger_Print(1,5,"Adding agent level=%d, id=%d, init_status=%d, skill=%d, userid=%s\n", agent->agent_level, agent->id, agent->afterLoginStatus, skill->id, agent->userid);
			// add this agent to group
	    listLock(&group->agents);
	    listInsertLast(&group->agents, agent);
 			listUnlock(&group->agents);
 			
 			/* insert to group list */
 			listLock(&AgentGroups);
    	listInsertLast(&AgentGroups, group);
    	listUnlock(&AgentGroups);
		
 		}

		prev_group = group_id;
		prev_agent = agent_id;
  }  
  dbLib->closeQuery(dbSet);
  
  dbLib->closeConnection(dbConn);  
	return 0;
}

//#define LOGGROUP_FILENAME "/opt/smartcenter/log/group_info.txt"
#define LOGGROUP_FILENAME "c:\\smartcenter\\log\\group_info.txt"

static int acd_LogGroupsData(t_AgentGroupList *groups){	
	FILE *log = NULL;
	char tmp[256];
	tCtbMessage	*msgsend;
	char *buf;
	int  len;
	t_AgentGroup *group;
	
	pthread_mutex_lock(&lockGroupData);
	_snprintf(tmp, sizeof(tmp), "%s", LOGGROUP_FILENAME);	
	log = fopen((char *)tmp, "w+");
	if (log){
		group = listFirst(groups);
		while(group){
			fprintf(log, "[Group-%d]\r\n", group->id);
			fprintf(log, "active=%d\r\n", group->agent_active);
			fprintf(log, "ready=%d\r\n", group->agent_ready);
			fprintf(log, "acw=%d\r\n", group->agent_acw);
			fprintf(log, "notready=%d\r\n", group->agent_notready);
			fprintf(log, "busy=%d\r\n", group->agent_busy);
			fprintf(log, "longest-ready-time=%d\r\n", group->stat.longestReadyTime);
			fprintf(log, "longest-ready-agent=%d\r\n", group->stat.longestReadyAgent);
			fprintf(log, "longest-ready-agent-name=%s\r\n", group->stat.longestReadyAgentName);
			fprintf(log, "longest-notready-time=%d\r\n", group->stat.longestNotReadyTime);
			fprintf(log, "longest-notready-agent=%d\r\n", group->stat.longestNotReadyAgent);
			fprintf(log, "longest-notready-agent-name=%s\r\n", group->stat.longestNotReadyAgentName);
			fprintf(log, "longest-acw-time=%d\r\n", group->stat.longestACWTime);
			fprintf(log, "longest-acw-agent=%d\r\n", group->stat.longestACWAgent);
			fprintf(log, "longest-acw-agent-name=%s\r\n", group->stat.longestACWAgentName);
			fprintf(log, "longest-busy-time=%d\r\n", group->stat.longestBusyTime);
			fprintf(log, "longest-busy-agent=%d\r\n", group->stat.longestBusyAgent);
			fprintf(log, "longest-busy-agent-name=%s\r\n", group->stat.longestBusyAgentName);
			fprintf(log, "\r\n");
			group = listNext(group);
		}
		fflush(log);		
		fclose(log);
	}
	pthread_mutex_unlock(&lockGroupData);
		
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);			
	
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_CC_CCD;
	msgsend->Type	 		= MSGTYPE_DATA_GROUPINFO;
	msgsend->Count		= 8;
	
	group = listFirst(groups);
	while(group){
		ctbMsgInsertNumeric(msgsend, 0, group->id);
		ctbMsgInsertNumeric(msgsend, 1, group->agents.size + group->activeAgents.size);
		ctbMsgInsertNumeric(msgsend, 2, group->agent_active);
		ctbMsgInsertNumeric(msgsend, 3, group->agent_ready);
		ctbMsgInsertNumeric(msgsend, 4, group->agent_acw);
		ctbMsgInsertNumeric(msgsend, 5, group->agent_notready);
		ctbMsgInsertNumeric(msgsend, 6, group->agent_busy);
		ctbMsgInsertNumeric(msgsend, 7, group->agent_outbound);	
		len = ctbMsgEncode (msgsend, buf, 512);	
	  dblog_PutMessage(buf, len);
		group = listNext(group);
	}	
	
	free(msgsend);
	free(buf);
	return 0;
}

static int acd_LogAgentData(t_Agent *agent, int group, int type){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	if(!agent){
		printf("ACD>>acd_LogAgentData() Agent pointer is null\n");
		return 1;
	}
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);			
	
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_CC_CCD;
	msgsend->Type	 		= type;
	msgsend->Count		= 11;
	
	ctbMsgInsertNumeric(msgsend, 0, agent->id);			// agent id
	ctbMsgInsertNumeric(msgsend, 1, group); 			
	ctbMsgInsertNumeric(msgsend, 2, agent->status);// agent ext
	ctbMsgInsertNumeric(msgsend, 3, agent->statusReason);
	ctbMsgInsertNumeric(msgsend, 4, agent->statusTime);
	ctbMsgInsertString (msgsend, 5, agent->extNumber);
	ctbMsgInsertString (msgsend, 6, agent->extIpAddress);	
	ctbMsgInsertNumeric(msgsend, 7, agent->prevStatus);// agent ext
	ctbMsgInsertNumeric(msgsend, 8, agent->prevReason);
	ctbMsgInsertNumeric(msgsend, 9, agent->prevStatusTime);
	ctbMsgInsertNumeric(msgsend,10, agent->callHandled);	
	len = ctbMsgEncode (msgsend, buf, 512);	
  dblog_PutMessage(buf, len);
	free(msgsend);
	free(buf);
	return 0;
}

static int acd_AgentEvent(t_Agent *agent, int group){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
		
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);			
	
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_CC_CCD;
	msgsend->Count		= 6;
	
	switch(agent->status){
		case ACD_AGENTSTATUS_READY:
			msgsend->Type	 		= MSGTYPE_ACK_AGENTREADY;
			break;
		case ACD_AGENTSTATUS_NOTREADY:
			msgsend->Type	 		= MSGTYPE_ACK_AGENTNOTREADY;
			break;
		case ACD_AGENTSTATUS_ACW:
			msgsend->Type	 		= MSGTYPE_ACK_AGENTACW;
			break;
		case ACD_AGENTSTATUS_BUSY:
			msgsend->Type	 		= MSGTYPE_EVENT_AGENTBUSY;
			break;
		case ACD_AGENTSTATUS_ON_EMAIL:
			msgsend->Type	 		= MSGTYPE_EVENT_AGENTBUSY;
			break;
		case ACD_AGENTSTATUS_ON_FAX:
			msgsend->Type	 		= MSGTYPE_EVENT_AGENTBUSY;
			break;
		case ACD_AGENTSTATUS_ON_SMS:
			msgsend->Type	 		= MSGTYPE_EVENT_AGENTBUSY;
			break;
	}	
	
	ctbMsgInsertNumeric(msgsend, 0, agent->id);							// agent id		
	ctbMsgInsertNumeric(msgsend, 1, agent->statusReason); 	// reason
	ctbMsgInsertNumeric(msgsend, 2, 0);  										// result	
	ctbMsgInsertString (msgsend, 3, agent->extNumber);  		// extension
	ctbMsgInsertString (msgsend, 4, agent->extIpAddress);  	// ip addr
	ctbMsgInsertNumeric(msgsend, 5, group);  								// group id		
	
	len = ctbMsgEncode (msgsend, buf, 512);	
	agent_DispatchMessage(agent->id, buf, len);  
	free(msgsend);
	free(buf);
	return 0;
}

static int acd_AgentNotifAutoAcw(t_Agent *agent, int acwTime){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
	
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);			
	
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_CC_CCD;
	msgsend->Type	 		= MSGTYPE_NOTIF_AUTOACW;
	msgsend->Count		= 2;
	
	ctbMsgInsertNumeric(msgsend, 0, agent->id); // agent id	
	ctbMsgInsertNumeric(msgsend, 1, acwTime);
	
	len = ctbMsgEncode (msgsend, buf, 512);	
	agent_DispatchMessage(agent->id, buf, len);  
	free(msgsend);
	free(buf);
	return 0;
}


/**
 Log agent extension status and other data
 */
static int acd_LogAgentExtData(t_Agent *agent, int group,char *session_key){
	tCtbMessage	*msgsend;
	char *buf;
	int  len;	
		
	msgsend = (tCtbMessage*)malloc(sizeof(tCtbMessage));
	buf = (char*)malloc(1024);			
	
	ctbMsgInit(msgsend);			
	msgsend->Sender 	= MSG_SRC_CC_CCD;
	msgsend->Type	 		= MSGTYPE_DATA_EXTSTATUS;
	msgsend->Count		= 7;
	
	ctbMsgInsertNumeric(msgsend, 0, agent->id);			// agent id
	ctbMsgInsertNumeric(msgsend, 1, group);
	ctbMsgInsertNumeric(msgsend, 2, agent->statusTime);
	ctbMsgInsertString (msgsend, 3, agent->extNumber);
	ctbMsgInsertString (msgsend, 4, agent->extIpAddress);	
	ctbMsgInsertNumeric(msgsend, 5, agent->extStatus);
	ctbMsgInsertString (msgsend, 6, session_key);

	len = ctbMsgEncode (msgsend, buf, 512);	
	dblog_PutMessage(buf, len);
	free(msgsend);
	free(buf);
	return 0;
}

static int acd_PrintGroupStatistic(){
	t_AgentGroup *group=NULL;
	//t_Agent      *agent=NULL;	
	//t_Skill      *skill=NULL;

	listLock(&AgentGroups);
	group = listFirst(&AgentGroups);
	
	printf("ACD >> GROUP STATISTICS\n");
	while (group){
		printf("Group %d, Agent available=%d, Agent Ready=%d, Agent Not Ready=%d, Agent ACW=%d, Agent On Call=%d\n",
			group->id,group->agent_active,group->agent_ready,group->agent_notready,group->agent_acw,group->agent_busy);
		group = listNext(group);
	}
	listUnlock(&AgentGroups);
	return 0;
}
static int acd_PrintAllAgents(){
	t_AgentGroup *group=NULL;
	t_Agent      *agent=NULL;	
	t_Skill      *skill=NULL;

	listLock(&AgentGroups);
	group = listFirst(&AgentGroups);

	printf("ACD >> ==== AGENTS ====\n");
	while (group){
		// print group
		printf("Group %d\n",group->id);
		printf("\tAvailable agent=%d\n",group->agent_active);
		printf("\tAutoACW=%d\n",group->autoACW);
		printf("\tAutoACWTime=%d\n",group->autoACWTime);
		printf("\tAutoACWReason=%d\n",group->autoACWReason);
		printf("Agents:\n");
		// print agent
		agent = listFirst(&group->agents);
		while (agent){
			printf(" - Agent %d, index=%d, status=%d, work=%d, ext_no=%s, n_call=%d status_time=%ld ip_addr=%s\n",
				agent->id, agent->index, agent->status, agent->extStatus, agent->extNumber, agent->callHandled, agent->statusTime, agent->extIpAddress);
			  skill = listFirst(&agent->skills);
			  printf("\tSkill: ");
			  while(skill){			  	
			  	printf("%d-%d ", skill->id, skill->score);
			  	skill = listNext(skill);
			  }
			  printf("\n");
			agent = listNext(agent);
		}
		group = listNext(group);
	}
	listUnlock(&AgentGroups);

	return 0;
}


/** 
	BE CAREFUL!!!!!! list not locked,
	make sure list is locked before calling this function
 */
static t_Agent *acd_FindAgentByID(t_AgentList *agentlist, int id){	
	t_Agent      *agent=NULL;	
	
	if(!agentlist){
		//logger_Print(1,5,"ACD>> agent group not found??\n");
		return NULL;	
	}
	agent = listFirst(agentlist);
	while (agent){
		if (agent->id == id)
			return agent;
		agent = listNext(agent);
	}
	return NULL;
}

/**
 Cari agent di semua group yang ada
 */
static t_Agent *acd_FindGroupAndAgentByID(t_AgentGroup **agentGroup, int id){
	t_AgentGroup *group=NULL;
	t_Agent      *agent=NULL;	
	
	
	group = listFirst(&AgentGroups);	
	while (group){		
		agent = listFirst(&group->agents);
		while (agent){
			if (agent->id == id)
				break;
			agent = listNext(agent);
		}
		if(agent && agentGroup){
			*agentGroup = group;
			break;
		}
		group = listNext(group);
	}
	return agent;
}

/**
 Hitung jumlah agent yg sudah login
 */
static int acd_count_agent_login(){
	t_AgentGroup *group=NULL;
	t_Agent      *agent=NULL;
	int i=0;
	
	
	group = listFirst(&AgentGroups);	
	while (group){		
		agent = listFirst(&group->agents);
		while (agent){
			i++;
			agent = listNext(agent);
		}		
		group = listNext(group);
	}
	return i;
}
/**
	AgentGroups must be locked by caller
 */
static t_AgentGroup *acd_FindGroupByID(int id){
	t_AgentGroup *group=NULL;
	
	
	group = listFirst(&AgentGroups);
	while (group){
		if (group->id == id)
			break;		
		group = listNext(group);
	}
	return group;
}

static t_Agent *acd_FindIdleAgent_FirstAvail(t_AgentGroup *group){	
	t_Agent      *agent=NULL;
	
	if(!group)return NULL;
	agent = listFirst(&group->activeAgents);
	while (agent){
		if (agent->status == ACD_AGENTSTATUS_READY && agent->extStatus == ACD_PHONESTATUS_IDLE){
		//if (agent->status == ACD_AGENTSTATUS_READY &&(cti_CheckStationIdleStatus(agent->extNumber)==0)){
			group->currentAgent = agent;
			break;
		}
		agent = listNext(agent);
	}
	return agent;
}

static t_Agent *acd_FindIdleAgent_RoundRobin(t_AgentGroup *group){	
	t_Agent      *agent=NULL;		
	
	if(!group)return NULL;
	
	/*start from current selected agent */
	if (group->currentAgent)
		agent = listNext(group->currentAgent);
	else
		agent = listFirst(&group->activeAgents);
	while (agent){
		if (agent->status == ACD_AGENTSTATUS_READY && agent->extStatus == ACD_PHONESTATUS_IDLE){
		//if (agent->status == ACD_AGENTSTATUS_READY &&(cti_CheckStationIdleStatus(agent->extNumber)==0)){
			group->currentAgent = agent;
			break;
		}
		agent = listNext(agent);
	}
	if(!agent && group->currentAgent){
		/* not found, start again from the beginning */
		agent = listFirst(&group->activeAgents);
		while (agent && (agent != listNext(group->currentAgent))){
			if (agent->status == ACD_AGENTSTATUS_READY && agent->extStatus == ACD_PHONESTATUS_IDLE){
			//if (agent->status == ACD_AGENTSTATUS_READY &&(cti_CheckStationIdleStatus(agent->extNumber)==0)){
				group->currentAgent = agent;
				break;
			}
			agent = listNext(agent);
		}
			
		if(agent == listNext(group->currentAgent))
			agent=NULL;
	}
	
	return agent;
}

/* find agent with longest ready time since login or last call */
static t_Agent *acd_FindIdleAgent_LongestIdle(t_AgentGroup *group){
	t_Agent	*agent=NULL, *candidate=NULL;
	struct 	timeval stimeval;
	long		idleTime;
	long 		candIdleTime, candReadyTimeUSec;

	if(!group)return NULL;
		
	gettimeofday(&stimeval, NULL);	
	candReadyTimeUSec = 0;
	candIdleTime  		= 0;	
	agent = listFirst(&group->activeAgents);
	while (agent){
		idleTime = agent->idleTime + (stimeval.tv_sec - agent->readyTimeSec);
		if ((agent->status    == ACD_AGENTSTATUS_READY) && 
			  (agent->extStatus == ACD_PHONESTATUS_IDLE) &&
			 // (cti_CheckStationIdleStatus(agent->extNumber)==0)&&
				((idleTime > candIdleTime) || 
				 ((idleTime == candIdleTime) && (agent->readyTimeUSec<candReadyTimeUSec))
				)
			 ){
			candidate 				= agent;
			candIdleTime 			= idleTime;
			candReadyTimeUSec = agent->readyTimeUSec;
		}
		agent = listNext(agent);
	}	
	return candidate;
}

static t_Agent *acd_FindIdleAgent_FewestCall(t_AgentGroup *group){
	t_Agent      *agent=NULL, *candidate=NULL;
	int	candCall=999999;
	
	if(!group)return NULL;
	
	agent = listFirst(&group->activeAgents);
	while (agent){
		if (agent->status == ACD_AGENTSTATUS_READY && 
			  agent->extStatus == ACD_PHONESTATUS_IDLE &&
			  //(cti_CheckStationIdleStatus(agent->extNumber)==0)&&
			  agent->callHandled < candCall){
			candidate = agent;
			candCall = agent->callHandled;
		}
		agent = listNext(agent);
	}	
	return candidate;
}

//// find agent with fewest call and longest idle 
//static t_Agent *acd_FindIdleAgent_LIFC(t_AgentGroup *group){
//	t_Agent      *agent=NULL, *candidate=NULL;
//	int	candCall=999999;
//	struct timeval stimeval;
//	long	idleTime, readyTime;
//	long 	candIdleTime, candReadyTimeUSec;
//	int ext_stat,agent_count=0;
//
//	if(!group)return NULL;
//		
//	gettimeofday(&stimeval, NULL);	
//	candReadyTimeUSec = 0;
//	candIdleTime  		= 0;
//	
//	agent = listFirst(&group->activeAgents);
//	while (agent){
//		++agent_count;
//		idleTime = agent->idleTime + (stimeval.tv_sec - agent->readyTimeSec);
//		readyTime = (stimeval.tv_sec - agent->readyTimeSec);		
//		
//		printf("ACD>>%02d group=%d [%s]status=%02d status=%d handled=%d idle=%d ready=%d [%d]%s\n",
//			agent_count,group->id,agent->extNumber,agent->extStatus,agent->status,agent->callHandled,
//			idleTime,readyTime,agent->id,agent->userid);
//
//		ext_stat=cti_CheckStationIdleStatus(agent->extNumber);
//		if (agent->status == ACD_AGENTSTATUS_READY && agent->extStatus == ACD_PHONESTATUS_IDLE){
//			if (agent->status == ACD_AGENTSTATUS_READY && (ext_stat==0)){
//				
//				if(idleTime > 5){
//					if (agent->callHandled < candCall){
//						
//						candidate = agent;
//						candCall = agent->callHandled;
//						candIdleTime 			= idleTime;
//						candReadyTimeUSec = agent->readyTimeUSec;
//						
//						
//
//					}else if (agent->callHandled == candCall){
//						idleTime = agent->idleTime + (stimeval.tv_sec - agent->readyTimeSec);
//						if (idleTime > candIdleTime || (idleTime == candIdleTime && (agent->readyTimeUSec > candReadyTimeUSec))){
//
//						
//							candidate 				= agent;
//							candIdleTime 			= idleTime;
//							candReadyTimeUSec = agent->readyTimeUSec;
//							
//							
//						}
//					}
//				}
//			}
//		}
//		agent = listNext(agent);
//	}
//	printf("ACD>>group-%d, active=%02d ready=%02d aux=%02d acw=%02d busy=%02d\n",group->id,
//		agent_count,group->agent_ready,group->agent_notready,group->agent_acw,group->agent_busy);
//	return candidate;
//}

static t_Agent *acd_FindIdleAgent_LIFC(t_AgentGroup *group){
	t_Agent      *agent=NULL, *candidate=NULL;
	int	candCall=999999;
	struct timeval stimeval;
	long	idleTime, readyTime;
	long 	candIdleTime, candReadyTime;
	int ext_stat,agent_count=0, agent_busy=0;

	if(!group)return NULL;
		
	gettimeofday(&stimeval, NULL);	
	candReadyTime = 0;
	candIdleTime  		= 0;
	//printf("ACD>> reserve agent LIFC for group %d\n", group->id);
	
	agent = listFirst(&group->activeAgents);
	while (agent){
		++agent_count;
		idleTime = agent->idleTime + (stimeval.tv_sec - agent->readyTimeSec);
		readyTime = (stimeval.tv_sec - agent->readyTimeSec);		
		ext_stat=cti_CheckStationIdleStatus(agent->extNumber);
		
		printf("ACD>>%02d group=%d [%s]status=%02d status=%d handled=%d idle=%d ready=%d [%d]%s\n",
			agent_count,group->id,agent->extNumber,agent->extStatus,agent->status,agent->callHandled,
			idleTime,readyTime,agent->id,agent->userid);
		/*logger_Print(3,5,"ACD>>%02d group=%d [%s]status=%02d status=%d handled=%d idle=%d ready=%d [%d]%s\n",
			agent_count,group->id,agent->extNumber,agent->extStatus,agent->status,agent->callHandled,
			idleTime,readyTime,agent->id,agent->userid);*/
		if(agent->extStatus != 25){
			++agent_busy;
		}
		if(agent->status == ACD_AGENTSTATUS_READY && agent->extStatus == ACD_PHONESTATUS_IDLE){
			if(ext_stat==0){
				if(idleTime > group->readyIdleTime){
					if (agent->callHandled < candCall){
						candidate = agent;
						candCall = agent->callHandled;
						candIdleTime = idleTime;
						candReadyTime = readyTime;						
					}else if (agent->callHandled == candCall){
						//idleTime = agent->idleTime + (stimeval.tv_sec - agent->readyTimeSec);
						if ((idleTime > candIdleTime || (idleTime == candIdleTime) && (readyTime > candReadyTime))){
							candidate 	= agent;
							candCall = agent->callHandled;
							candIdleTime	= idleTime;
							candReadyTime = readyTime;						
						}
					}
				}
			}
		}
		agent = listNext(agent);
	}
	printf("ACD>>group-%d, active=%02d ready=%02d aux=%02d acw=%02d busy=%02d\n",group->id,
		agent_count,group->agent_ready,group->agent_notready,group->agent_acw,agent_busy);
	return candidate;
}
static int acd_AgentChangeStatus(int groupId, int agentId, int statusId, int reason){	
	t_Agent				*agent=NULL;
	t_AgentGroup 	*group=NULL;
	struct timeval stimeval;

	gettimeofday(&stimeval, NULL);	
	listLock(&AgentGroups);	
	group = acd_FindGroupByID(groupId);
	
	if (group)
		agent = acd_FindAgentByID(&group->activeAgents, agentId);	
	if (agent){		

		if (agent->status == ACD_AGENTSTATUS_READY && statusId != ACD_AGENTSTATUS_READY){
			/* status agent berubah dari ready, hitung berapa lama dia telah ready */
			agent->idleTime += (stimeval.tv_sec - agent->readyTimeSec);
		}		
		
		if (statusId == ACD_AGENTSTATUS_READY){
			agent->readyTimeSec  = stimeval.tv_sec;
			agent->readyTimeUSec = stimeval.tv_usec;
		}
	//tjd 18-06 : when agent not ready still have call	
//		if (agent->status == ACD_AGENTSTATUS_BUSY && agent->statusReason == 0 ){
			/* jika agent sedang on call busy make yang diset adalah status sebelumnya */
//			agent->prevStatus 		= statusId;
//			agent->prevReason 		= reason;
//			agent->prevStatusTime = agent->statusTime;
//		}else{
			agent->prevStatus 		= agent->status;
			agent->prevReason 		= agent->statusReason;
			agent->prevStatusTime = agent->statusTime;
			agent->status 				= statusId;
			agent->statusReason 	= reason;
			agent->statusTime 		= stimeval.tv_sec;
			
			
			if(agent->prevStatus != statusId){
				int statusDuration; 
				
				statusDuration = agent->statusTime - agent->prevStatusTime;
				/*group data*/		
				switch(statusId){
					case ACD_AGENTSTATUS_READY:						
						++group->agent_ready;
						printf("AGENT-%d>> %s group=%d Ready\n",agent->id, agent->userid, group->id);
						break;
					case ACD_AGENTSTATUS_NOTREADY:
						++group->agent_notready;
						printf("AGENT-%d>> %s group=%d NotReady\n",agent->id, agent->userid,group->id);
						break;
					case ACD_AGENTSTATUS_ACW:
						++group->agent_acw;
						printf("AGENT-%d>> %s group=%d ACW\n",agent->id, agent->userid, group->id);
						break;			
				}
				switch(agent->prevStatus){
					case ACD_AGENTSTATUS_READY:
						--group->agent_ready;
						//printf("ACD>>group-%d, active=%02d ready=%02d aux=%02d acw=%02d busy=%02d\n",group->id,
						//		group->agent_active,group->agent_ready,group->agent_notready,group->agent_acw,group->agent_busy);
						if(statusDuration > group->stat.longestReadyTime){
							group->stat.longestReadyTime  = statusDuration;
							group->stat.longestReadyAgent = agent->id;
							strcpy(group->stat.longestReadyAgentName, agent->name);
						}
						break;
					case ACD_AGENTSTATUS_NOTREADY:
						--group->agent_notready;
						//printf("ACD>>group-%d, active=%02d ready=%02d aux=%02d acw=%02d busy=%02d\n",group->id,
								//group->agent_active,group->agent_ready,group->agent_notready,group->agent_acw,group->agent_busy);
						if(statusDuration > group->stat.longestNotReadyTime){
							group->stat.longestNotReadyTime  = statusDuration;
							group->stat.longestNotReadyAgent = agent->id;
							strcpy(group->stat.longestNotReadyAgentName, agent->name);
						}
						break;
					case ACD_AGENTSTATUS_ACW:
						--group->agent_acw;
						//printf("ACD>>group-%d, active=%02d ready=%02d aux=%02d acw=%02d busy=%02d\n",group->id,
								//group->agent_active,group->agent_ready,group->agent_notready,group->agent_acw,group->agent_busy);
						if(statusDuration > group->stat.longestACWTime){
							group->stat.longestACWTime  = statusDuration;
							group->stat.longestACWAgent = agent->id;
							strcpy(group->stat.longestACWAgentName, agent->name);
						}
						break;			
				}
				acd_LogGroupsData(&AgentGroups);
				//acd_PrintGroupStatistic();
			}
			acd_LogAgentData(agent, group->id, MSGTYPE_DATA_AGENTSTATUS);
		}
	listUnlock(&AgentGroups);
	
	return 0;
}

int acd_AgentChangeExtStatus(int groupId, int agentId, int statusId,char *session_key){
	t_Agent				*agent=NULL;
	t_AgentGroup 	*group=NULL;
	struct timeval stimeval;

	gettimeofday(&stimeval, NULL);	

	listLock(&AgentGroups);	
	group = acd_FindGroupByID(groupId);
	if (group)
		agent = acd_FindAgentByID(&group->activeAgents, agentId);	
	
	if (agent){
		int statusDuration;			
		
		statusDuration = stimeval.tv_sec - agent->statusTime;
		
		/** device on call **/
		if (statusId         == ACD_PHONESTATUS_TALKING && 
			  agent->extStatus != ACD_PHONESTATUS_TALKING && 
			  agent->extStatus != ACD_PHONESTATUS_HELD &&
			  agent->onACD){
			agent->callHandled++;
			agent->prevStatusTime = agent->statusTime;
			agent->statusTime 	  = stimeval.tv_sec;

			//notify IVR
			//ivrnotif_AgentConnected(agent->extNumber, agent->id, agent->userid);
		}
		
		/** device become idle **/
		if ((statusId == ACD_PHONESTATUS_IDLE) && (agent->extStatus != ACD_PHONESTATUS_IDLE)){			
			agent->onACD = false;	
			if (agent->status == ACD_AGENTSTATUS_BUSY){
				--group->agent_busy;
				/* kembalikan status agent ke status sebelum dia BUSY */
				if(agent->prevStatus != ACD_AGENTSTATUS_NULL){
					agent->status 			= agent->prevStatus;
					agent->statusReason	= agent->prevReason;
				}else{
					agent->status 		= agent->afterLoginStatus;				
					agent->prevStatus 	= ACD_AGENTSTATUS_BUSY;
					agent->statusTime 	= stimeval.tv_sec;				
				}
				/* agent become available agent */
				if (agent->status == ACD_AGENTSTATUS_READY)			
					++group->agent_available;
				
				if(statusDuration > group->stat.longestBusyTime){
					group->stat.longestBusyTime 	= statusDuration;
					group->stat.longestBusyAgent	= agent->id;
					strcpy(group->stat.longestBusyAgentName, agent->name);
				}
				
				switch(agent->status){
					case ACD_AGENTSTATUS_READY:
						
						if(group->autoACW){
							agent->status 			= ACD_AGENTSTATUS_ACW;
							agent->statusReason =	group->autoACWReason;
							acd_AgentNotifAutoAcw(agent, group->autoACWTime);
							//--group->agent_ready;
							++group->agent_acw;
							//printf("ACD>> %d %s status = ACD_AGENTSTATUS_ACW\n",agent->id, agent->userid);
						}else{								
								++group->agent_ready;
								//printf("ACD>> %d %s status = ACD_AGENTSTATUS_READY\n", agent->id,agent->userid);	
						}
						break;
					case ACD_AGENTSTATUS_NOTREADY:
						++group->agent_notready;
						break;
					case ACD_AGENTSTATUS_ACW:
						++group->agent_acw;
						break;
					case ACD_AGENTSTATUS_BUSY:
						--group->agent_busy;
						printf("---------------|  --group->agent_busy  |\n");
						break;
				}
				acd_LogGroupsData(&AgentGroups);
				acd_AgentEvent(agent, group->id);
				acd_LogAgentData(agent, group->id, MSGTYPE_DATA_AGENTSTATUS);
			}
			
			if(agent->extStatus == ACD_PHONESTATUS_TALKING){
				/* sebelumnya agent udah bicara, reset data */
				agent->idleTime      = 0; 
				agent->readyTimeSec  = stimeval.tv_sec;
				agent->readyTimeUSec = stimeval.tv_usec;
			}

			//ivrnotif_AgentDisconnected(agent->extNumber, agent->id);
		}
			
		/** agent become unavailable because device become busy **/
		if ((statusId != ACD_PHONESTATUS_IDLE) && 
			  (agent->extStatus == ACD_PHONESTATUS_NULL ||
			   agent->extStatus == ACD_PHONESTATUS_IDLE || 
			   agent->extStatus == ACD_PHONESTATUS_RESERVED)){
			if (agent->status != ACD_AGENTSTATUS_BUSY){
				++group->agent_busy;
				
				if (agent->status == ACD_AGENTSTATUS_READY)			
					--group->agent_available;
				
				agent->prevStatus 		= agent->status;
				agent->prevReason   	= agent->statusReason;			
				agent->status 				= ACD_AGENTSTATUS_BUSY;
				//printf("ACD>> %d %s status = ACD_AGENTSTATUS_BUSY\n",agent->id,agent->userid);
				agent->statusReason 	=	0;  //busy on call
				agent->statusTime 		= stimeval.tv_sec;
				switch(agent->prevStatus){
					case ACD_AGENTSTATUS_READY:
						--group->agent_ready;
						if(statusDuration > group->stat.longestReadyTime){
							group->stat.longestReadyTime 	= statusDuration;
							group->stat.longestReadyAgent = agent->id;
							strcpy(group->stat.longestReadyAgentName, agent->name);
						}
						break;
					case ACD_AGENTSTATUS_NOTREADY:
						--group->agent_notready;
						if(statusDuration > group->stat.longestNotReadyTime){
							group->stat.longestNotReadyTime	= statusDuration;
							group->stat.longestNotReadyAgent= agent->id;
							strcpy(group->stat.longestNotReadyAgentName, agent->name);
						}
						break;
					case ACD_AGENTSTATUS_ACW:
						--group->agent_acw;
						if(statusDuration > group->stat.longestACWTime){
							group->stat.longestACWTime 	= statusDuration;
							group->stat.longestACWAgent = agent->id;
							strcpy(group->stat.longestACWAgentName, agent->name);
						}
						break;			
				}
				acd_LogGroupsData(&AgentGroups);
				acd_AgentEvent(agent, group->id);
				acd_LogAgentData(agent, group->id, MSGTYPE_DATA_AGENTSTATUS);
				/* push event agent busy */
			}
		}
		
		/** save status **/
		if (agent->extStatus != statusId){
			agent->extStatus     = statusId;
			agent->extStatusTime = stimeval.tv_sec;
			if(agent->extStatus < 50)
				acd_LogAgentExtData(agent, group->id,session_key);
		}
	}
	listUnlock(&AgentGroups);
	return 0;
}


/* Exported function */

/* load agent from DB */
int acd_LoadAgent(int agentId){
	tDbConn dbConn;
	tDbSet  dbSet, dbSet2;
  char sql[2048];
  int  sqlLen;
  t_Skill *skill;
  
  int	groupId;
  int loginStatus;
	
	t_AgentGroup *group=NULL;
	t_Agent      *agent=NULL;	

  printf("ACD>> Loading Agent %d\n", agentId);
  
  /* periksa apakah agent already loaded */
  listLock(&AgentGroups);	
  agent = acd_FindGroupAndAgentByID(NULL, agentId);
  listUnlock(&AgentGroups);
  
  if(!agent){
  	/* connect to DB */
		dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
	  if (!dbConn){
	  	printf("ACD>> DB Connection failed\n");
	  	return -1;
	  }
	  
	  sqlLen = sprintf(sql,"SELECT a.id agent_id, a.agent_group group_id, a.login_status,a.occupancy "
												 "FROM agent a "
												 "WHERE a.id = %d ", agentId);
		dbSet = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
		if(dbLib->nextRow(dbSet)){			
			groupId 		= dbLib->getIntFieldByIdx(dbSet, 1);			
			loginStatus = dbLib->getIntFieldByIdx(dbSet, 2);
			
			/* create new agent data */
	  	agent 									= listNewItem(t_Agent);
	  	agent->id 							= agentId;
	   	agent->index 						= ++agentIdx;
	   	agent->status 					= 0;
	   	agent->afterLoginStatus	= loginStatus;
		agent->agent_level				= dbLib->getIntFieldByIdx(dbSet,4);
	   	
	   	/* load agent skill */				
	 		sqlLen = sprintf(sql,"SELECT skill, score FROM agent_skill "
	 			                     "WHERE agent = %d ORDER by id", agentId);
			dbSet2 = dbLib->openQuery(dbConn, NULL, sql, sqlLen);
		  while(dbLib->nextRow(dbSet2)){
		  	skill        = listNewItem(t_Skill);
		  	skill->id    = dbLib->getIntFieldByIdx(dbSet2, 0);
		  	skill->score = dbLib->getIntFieldByIdx(dbSet2, 1);
		  	listInsertLast(&agent->skills, skill);
		  }
		  dbLib->closeQuery(dbSet2);
		  
		  // find its group
		  listLock(&AgentGroups);
		  group = acd_FindGroupByID(groupId);
	  	if(group){
	 			// add this agent to group
	 			listLock(&group->agents);
		    listInsertLast(&group->agents, agent);
	 			listUnlock(&group->agents);
	 		}
	 		listUnlock(&AgentGroups);
		}
		dbLib->closeQuery(dbSet);
		dbLib->closeConnection(dbConn);
  }  
	return 0;
}

/* agent login on device */
int acd_AgentLogin(int groupId, int agentId, char *device, char *location){
	t_Agent					*agent=NULL;
	t_AgentGroup 		*group=NULL;
	int 						ret=-1;
	struct timeval 	stimeval;

	gettimeofday(&stimeval, NULL);


	listLock(&AgentGroups);
	/* cari agent di daftar agent */
	group = acd_FindGroupByID(groupId);
	if (group)
		logger_Print(1,5,"ACD>> Try to locate agent=%d\n",agentId);
		agent = acd_FindAgentByID(&group->agents, agentId);	
		if (agent){
			printf("AGENT-%d>> %s Login Request ext=%s\n", agent->id, agent->userid, device);
			logger_Print(1,5,"ACD>> %s Agent=%d found on group=%d\n",agent->userid,agentId,groupId);
			
			if(agent->agent_level==1){
				appContext->numagentlogin++;
			}
			
			printf("ACD>> Group=%d Total Agent Login=%d\n",groupId,appContext->numagentlogin);
			logger_Print(1,2,"ACD>> Group=%d Total Agent Login=%d\n",groupId,appContext->numagentlogin);

			/* pindahkan ke active list */
			listRemove(&group->agents, agent);
			listInsertLast(&group->activeAgents, agent);
			sprintf(agent->extNumber,		"%s", device);
			sprintf(agent->extIpAddress,"%s", location);
			agent->callHandled 		= 0;
			agent->idleTime				= 0;
			agent->readyTimeSec 	= 0;
			agent->readyTimeUSec 	= 0;
			agent->extStatus 		= ACD_PHONESTATUS_NULL;
			agent->status			= agent->afterLoginStatus;
			agent->statusReason 	= 0;
			agent->statusTime 		= stimeval.tv_sec;
			agent->prevStatus		= ACD_AGENTSTATUS_NULL;
			ret = agent->status;
			
			
			/*group data*/
			++group->agent_active;
			switch(agent->status){
				case ACD_AGENTSTATUS_READY:				
					++group->agent_ready;				
					break;
				case ACD_AGENTSTATUS_NOTREADY:
					++group->agent_notready;
					break;
				case ACD_AGENTSTATUS_ACW:
					++group->agent_acw;
					break;
			}

		acd_LogGroupsData(&AgentGroups);
		acd_LogAgentData(agent, group->id, MSGTYPE_DATA_AGENTLOGIN);
	}	
	listUnlock(&AgentGroups);	
	
	if (agent){
		wallboard_agentstatus(agent->extNumber,0,agentId);
		return ret;
	}else{
		logger_Print(1,2,"ACD>> Agent=%d not found on group=%d\n",agentId,groupId);
		return -1;
	}
}

int acd_AgentLogout(int groupId, int agentId){
	t_Agent					*agent=NULL;
	t_AgentGroup 		*group=NULL;
	struct timeval 	stimeval;
	int statusDuration;
	
	gettimeofday(&stimeval, NULL);	
	listLock(&AgentGroups);	
	/* cari agent di daftar activeAgent */
	logger_Print(1,5,"AGENT-%d>> Logout Request from group=%d\n", agentId,groupId);
	group = acd_FindGroupByID(groupId);
	if (group)
		//logger_Print(1,5,"ACD>> agent group=%d\n",groupId);
		agent = acd_FindAgentByID(&group->activeAgents, agentId);	
	if (agent){
		//logger_Print(1,5,"ACD>> agent id=%d\n",agentId);
		if (group->currentAgent == agent)
			group->currentAgent = agent->next;
		//logger_Print(1,5,"ACD>> agent id=%d\n",agent->id);	
		/* pindahkan ke list biasa */
		listRemove(&group->activeAgents, agent);
		listInsertLast(&group->agents, agent);
		
		if(agent->agent_level ==1){
			appContext->numagentlogin--;
		}
		printf("AGENT-%d>> %s Logout Request ext=%s\n", agent->id, agent->userid, agent->extNumber);
		logger_Print(1,2,"ACD>> Total Agent Login=%d\n",appContext->numagentlogin);
		printf("ACD>> Total Agent Login=%d\n",appContext->numagentlogin);
		statusDuration = stimeval.tv_sec - agent->statusTime;
		
		/*group data*/
		--group->agent_active;
		switch(agent->status){
			case ACD_AGENTSTATUS_READY:
				--group->agent_ready;
				if(statusDuration > group->stat.longestReadyTime){
					group->stat.longestReadyTime  = statusDuration;
					group->stat.longestReadyAgent = agent->id;
					strcpy(group->stat.longestReadyAgentName, agent->name);
				}
				break;
			case ACD_AGENTSTATUS_NOTREADY:
				--group->agent_notready;
				if(statusDuration > group->stat.longestNotReadyTime){
					group->stat.longestNotReadyTime  = statusDuration;
					group->stat.longestNotReadyAgent = agent->id;
					strcpy(group->stat.longestNotReadyAgentName, agent->name);
				}
				break;
			case ACD_AGENTSTATUS_ACW:
				--group->agent_acw;
				if(statusDuration > group->stat.longestACWTime){
					group->stat.longestACWTime  = statusDuration;
					group->stat.longestACWAgent = agent->id;
					strcpy(group->stat.longestACWAgentName, agent->name);
				}
				break;
			case ACD_AGENTSTATUS_BUSY:
				--group->agent_busy;				
				break;
		}
		acd_LogGroupsData(&AgentGroups);
		
		/* reset data */
		agent->extNumber[0] 	= '\0';
		agent->extIpAddress[0]= '\0';
		agent->callHandled 		= 0;
		agent->idleTime				= 0;
		agent->readyTimeSec 	= 0;
		agent->readyTimeUSec 	= 0;
		agent->extStatus 			= ACD_PHONESTATUS_NULL;
		agent->status      		= 0;
		agent->statusReason 	= 0;		
		agent->statusTime 		= stimeval.tv_sec;		
		agent->prevStatus      		= 0;
		acd_LogAgentData(agent, group->id, MSGTYPE_DATA_AGENTLOGOUT);
	}
	listUnlock(&AgentGroups);	
	
	if (agent)
		return 0;
	else
		return -1;
}

int acd_AgentReady(int groupId, int agentId){
	int ret;
	
	ret = acd_AgentChangeStatus(groupId, agentId, ACD_AGENTSTATUS_READY, 0);
	logger_Print(1,5,"AGENT-%d>> ret=%d | acd_AgentChangeStatus, group_id=%d, ACD_AGENTSTATUS_READY\n",
		agentId, ret, groupId);
	return ret;
}

int acd_AgentNotReady(int groupId, int agentId, int reason){
	int ret;
	
	ret = acd_AgentChangeStatus(groupId, agentId, ACD_AGENTSTATUS_NOTREADY, reason);
	logger_Print(1,5,"AGENT-%d>> ret=%d | acd_AgentChangeStatus, group_id=%d, ACD_AGENTSTATUS_NOTREADY, reason=%d\n",
		agentId, ret, groupId, reason);
	return ret;
}

int acd_AgentACW(int groupId, int agentId, int reason){
	int ret;
	
	ret = acd_AgentChangeStatus(groupId, agentId, ACD_AGENTSTATUS_ACW, reason);
	logger_Print(1,5,"AGENT-%d>> ret=%d | acd_AgentChangeStatus, group_id=%d, ACD_AGENTSTATUS_ACW, reason=%d\n",
		agentId, ret, groupId, reason);
	return ret;
}

int acd_AgentBusy(int groupId, int agentId, int reason){
	int ret;
	
	ret = acd_AgentChangeStatus(groupId, agentId, ACD_AGENTSTATUS_BUSY, reason);
	logger_Print(1,5,"AGENT-%d>> ret=%d | acd_AgentChangeStatus, group_id=%d, ACD_AGENTSTATUS_BUSY, reason=%d\n",
		agentId, ret, groupId, reason);
	return ret;
}

/**
 @Desc: returning reserved extension number 
 @Input: agentGroup, algorithm, reqskills
 @Output: ext
*/
int acd_ReserveAgentExt(int agentGroup, int algorithm, t_SkillList *reqskills, char *ext, char *ch){
	t_Agent				*agent=NULL;
	t_AgentGroup 	*group=NULL;
	struct timeval stimeval;	

	gettimeofday(&stimeval, NULL);
	
	listLock(&AgentGroups);	
	group = acd_FindGroupByID(agentGroup);
	if (!group)
		return 0;
	/* find idle agent with specific algorithm */	
	switch(algorithm){
		case  ACD_ALG_FIRSTAVAIL:
			agent = acd_FindIdleAgent_FirstAvail(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_FirstAvail(group);
			}
			break;
		case ACD_ALG_ROUNDROBIN:
			agent = acd_FindIdleAgent_RoundRobin(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_RoundRobin(group);
			}
			break;
		case ACD_ALG_LONGESTIDLE:
			agent = acd_FindIdleAgent_LongestIdle(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_LongestIdle(group);
			}
			break;
		case ACD_ALG_FEWESTCALL:
			agent = acd_FindIdleAgent_FewestCall(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_FewestCall(group);
			}
			break;
		case ACD_ALG_LI_FEWESTCALL:
			agent = acd_FindIdleAgent_LIFC(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_LIFC(group);
			}			
			break;
	}
	if (agent){
		//masukkan ke list reserved agent untuk diperiksa nantinya //
		//agent->extStatus			= ACD_PHONESTATUS_RESERVED;
		
		printf("ACD>> %d %s reserved ext=%s\n",agent->id,agent->userid,agent->extNumber);
		logger_Print(1,2,"ACD>> %d %s reserved ext=%s\n",agent->id,agent->userid, agent->extNumber);
		agent->extStatus = ACD_PHONESTATUS_RESERVED;
		agent->extStatusTime	= stimeval.tv_sec;
		agent->onACD     = true;
		++agent->callOffered;
		if (ext){
			strcpy(ext, agent->extNumber);
			wallboard_extstatus(ch,CALLSTATUS_AGENT_ROUTED,agent->extNumber);		
		}
	}
	else{
		cti_Setcallqueue(ch,agentGroup);
		}
	listUnlock(&AgentGroups);
	
	return 0;
}

int acd_GetAgentForMM(int agentGroup, int algorithm, t_SkillList *reqskills, char *media){
	t_Agent				*agent=NULL;
	int						agentId=0;
	t_AgentGroup 	*group=NULL;
	struct timeval stimeval;	

	gettimeofday(&stimeval, NULL);
	if (!media) return 0;
	
	listLock(&AgentGroups);
	group = acd_FindGroupByID(agentGroup);
	if (!group)
		return 0;
	/* find idle agent with specific algorithm */
	switch(algorithm){
		case  ACD_ALG_FIRSTAVAIL:
			agent = acd_FindIdleAgent_FirstAvail(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_FirstAvail(group);
			}
			break;
		case ACD_ALG_ROUNDROBIN:
			agent = acd_FindIdleAgent_RoundRobin(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_RoundRobin(group);
			}
			break;
		case ACD_ALG_LONGESTIDLE:
			agent = acd_FindIdleAgent_LongestIdle(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_LongestIdle(group);
			}
			break;
		case ACD_ALG_FEWESTCALL:
			agent = acd_FindIdleAgent_FewestCall(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_FewestCall(group);
			}
			break;
		case ACD_ALG_LI_FEWESTCALL:
			agent = acd_FindIdleAgent_LIFC(group);
			if (!agent && group->overflowGroup){
				/*try on overflow group*/
				group = acd_FindGroupByID(group->overflowGroup);
				agent = acd_FindIdleAgent_LIFC(group);
			}			
			break;
	}
	if (agent){
		agent->status = ACD_AGENTSTATUS_BUSY;
		agentId = agent->id;
		if (!strcasecmp(media, "email")){
			agent->statusReason = 1;  //email			
		}
		acd_AgentEvent(agent, agentGroup);
	}
	listUnlock(&AgentGroups);
	
	return agentId;
}

int cek_mac(){
	FILE *fh;
	char c;
	int i=0;
	char csn[128];
	char mac[128];
	char tmp[128];

	//int board_num;
	system("ipconfig /all|grep Physical >c:\\mac.txt"); 
	if ((fh = fopen("c:\\mac.txt", "rt"))
       == NULL)
   {
	   printf("Cannot open c:\\mac.txt.\n");
      exit(0);
   }
	
	while( fscanf(fh, "%[^:]%c%[^\n]%c", csn,&c,tmp, &c) != EOF ) {
		if(i>0){
			sscanf(tmp,"%c%s",&c,mac);	
			printf("MAC-%d>>ADRESS:%s\n",i,mac);
		}
		i++;
		if(i>5)break;
	}
	
	fclose(fh);	
	return 0;
	
	
}


int acd_Load(){
	char key[128];
	char md5key[128];
	int len=0,i;
	printf("ACD>> Initializing\n");
//	cek_mac();
	acd_Init();
	printf("ACD>> acd_LoadAgents()\n");
	acd_LoadAgents();
	printf("ACD>> acd_LogGroupsData()\n");
	acd_LogGroupsData(&AgentGroups); /* refresh status */
	//acd_PrintAllAgents();
	
	strncpy(md5key,appContext->licensedkey,32);
	len = strlen(appContext->licensedkey);
	md5key[32]='\0';
	for(i=32;i<len;i++){
		key[i-32]=appContext->licensedkey[i];
	}
	key[i-32]='\0';

	appContext->licensedagent = 100;//licensedagent(appContext->licensedkey,md5key,key);
	appContext->numagentlogin=0;
	printf("ACD>> Number Licensed Agent: %d\n",appContext->licensedagent);
	smctPthreadCreateDetached(&mainThreadId, NULL, acd_MainLoop, NULL);
	return 0;
}

/**
 Print Active Agents
 */
int acd_PrintAgents(char *buf, int buflen){
	int write_len, len;
	t_Agent				*agent=NULL;
	t_AgentGroup 	*group=NULL;
	struct 	timeval stimeval;	
		
	gettimeofday(&stimeval, NULL);	
	
	write_len = 0;
	listLock(&AgentGroups);	
	group = listFirst(&AgentGroups);	
	while (group){
		agent = listFirst(&group->activeAgents);
		len   = _snprintf(buf+write_len, buflen-write_len, "Group: %d\r\n", group->id);
		if (len <0)break;
		write_len+=len;
		while (agent){
			if (agent->status > 0){
				len = _snprintf(buf+write_len, buflen-write_len, 
					"id=%3d, idx=%3d, stat=%3d, rsn=%3d, stat0=%3d, rsn0=%3d, ip=%15s, ext=%6s, extstat=%3d, call=%d, idle=%ld\r\n", 
					agent->id, agent->index, agent->status, agent->statusReason, agent->prevStatus, agent->prevReason, 
					agent->extIpAddress, agent->extNumber, agent->extStatus, agent->callHandled, stimeval.tv_sec - agent->readyTimeSec);
				if (len <0)break;
				write_len+=len;
			}
			agent = listNext(agent);
		}
		
		group = listNext(group);
	}
	listUnlock(&AgentGroups);
	
	return write_len;
}

int acd_PrintAgent(char *buf, int buflen, int agentId){
	int write_len, len;
	t_Agent				*agent=NULL;
	t_AgentGroup 	*group=NULL;
	
	write_len = 0;
	listLock(&AgentGroups);	
	do{
		agent = acd_FindGroupAndAgentByID(&group, agentId);	
		if (agent){
			len   = _snprintf(buf+write_len, buflen-write_len, "Group: %d\r\n", group->id);
			if(len<0)break;
			write_len+=len;		
			len = _snprintf(buf+write_len, buflen-write_len, "Agent=%3d, idx=%3d, status=%3d, ip=%15s, ext=%6s, ext-status=%3d\r\n", 
			agent->id, agent->index, agent->status, agent->extIpAddress, agent->extNumber, agent->extStatus);			
			if (len <0)break;
			write_len+=len;		
		}
	}while(0);
	listUnlock(&AgentGroups);	
	
	return write_len;
}

static int acd_ResetGroupStatisticData(){
	t_AgentGroup 	*group=NULL;
	
	listLock(&AgentGroups);	
	group = listFirst(&AgentGroups);
	while(group){
		group->stat.longestReadyTime				 = 0;
		group->stat.longestReadyAgent				 = 0;	
	  group->stat.longestReadyAgentName[0] = '\0';
		group->stat.longestNotReadyTime 		 = 0;
		group->stat.longestNotReadyAgent 		 = 0;
		group->stat.longestNotReadyAgentName[0] = '\0';
		group->stat.longestACWTime 					= 0;	
		group->stat.longestACWAgent 				= 0;
		group->stat.longestACWAgentName[0] 	= '\0';
		group->stat.longestBusyTime 				= 0;
		group->stat.longestBusyAgent 				= 0;
		group->stat.longestBusyAgentName[0] = '\0';		
		group = listNext(group);
	}
	acd_LogGroupsData(&AgentGroups);
	listUnlock(&AgentGroups);
	
	return 0;
}

/*
	ACD main loop, berguna untuk scheduling
*/
static int currentDay=0;

static void acd_MainLoop(void *param){
	t_Agent				*agent=NULL;
	t_AgentGroup 	*group=NULL;
	time_t t;
	struct tm tm;
	
	while(true){
		Sleep(5000);
		time(&t);
		localtime_s(&tm, &t);
		
		/* setiap jam log data status agent */
		
		/** 
		 periksa apakah sudah berubah tanggal, jika udah reset data statistic
		 */
		if (currentDay != tm.tm_mday){
			currentDay = tm.tm_mday;
			acd_ResetGroupStatisticData();
		}

		/* release agent yang reserved */
		listLock(&AgentGroups);	
		group = listFirst(&AgentGroups);	
		while (group){
			agent = listFirst(&group->activeAgents);			
			while (agent){
				if ((agent->extStatus == ACD_PHONESTATUS_RESERVED) &&
					  (t - agent->extStatusTime > MAX_RESERVED_TIME)){
					agent->extStatus		 = ACD_PHONESTATUS_IDLE;
					agent->extStatusTime = (long)t;
					
					logger_Print(1,2,"ACD: Releasing RESERVED agent %d %s\n", agent->id, agent->userid);
				}
				agent = listNext(agent);
			}
			
			group = listNext(group);
		}
		listUnlock(&AgentGroups);
	}
	
}

int acd_ChangeAutoACWSetting(int groupId, int autoacw, int acwtime, int defreason){	
	t_AgentGroup 	*group=NULL;
	
	listLock(&AgentGroups);	
	group = acd_FindGroupByID(groupId);
	if (group){
		group->autoACW 				= autoacw;
		group->autoACWTime 		= acwtime;
		group->autoACWReason 	= defreason;
	}
	listUnlock(&AgentGroups);
	return 0;
}


