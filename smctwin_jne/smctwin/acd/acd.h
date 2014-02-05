#ifndef _AcdH_
#define _AcdH_

typedef struct _t_Skill{
	int id;
	int score;
	struct _t_Skill *next;
  struct _t_Skill *prev;
}t_Skill;

typedef struct {
	int size;
	t_Skill *first;
	t_Skill *last;	
}t_SkillList;


/* published function */

#define MAX_RESERVED_TIME				30

#define ACD_AGENTSTATUS_NULL			0
#define ACD_AGENTSTATUS_READY			1		/* ready for incoming contact */
#define ACD_AGENTSTATUS_NOTREADY	2		/* not ready for receiving contact */
#define ACD_AGENTSTATUS_ACW				3		/* status after serving contact */
#define ACD_AGENTSTATUS_BUSY			4		/* serving contact */
#define ACD_AGENTSTATUS_ON_EMAIL	5		/* serving email */
#define ACD_AGENTSTATUS_ON_FAX		6		/* serving fax */
#define ACD_AGENTSTATUS_ON_SMS		7		/* serving sms */

#define ACD_PHONESTATUS_NULL					0
#define ACD_PHONESTATUS_OFFHOOK				4 	/* initiated */
#define ACD_PHONESTATUS_RINGING				5
#define ACD_PHONESTATUS_DIALING				6
#define ACD_PHONESTATUS_TALKING				7
#define ACD_PHONESTATUS_HELD					8
#define ACD_PHONESTATUS_RESERVED			17	/* reserved for routing */
#define ACD_PHONESTATUS_IDLE					25	/* idle/released/onhook */
#define ACD_PHONESTATUS_DIRTY					99	/* device status uncertain and need to be checked */				

#define ACD_ALG_FIRSTAVAIL					1
#define ACD_ALG_ROUNDROBIN					2
#define ACD_ALG_LONGESTIDLE					3
#define ACD_ALG_FEWESTCALL					4
#define ACD_ALG_LI_FEWESTCALL				5

int acd_Load();

int acd_LoadAgent(int agentId);
int acd_AgentLogin(int groupId, int agentId, char *device, char *location);
int acd_AgentLogout(int groupId, int agentId);
int acd_AgentReady(int groupId, int agentId);
int acd_AgentNotReady(int groupId, int agentId, int reason);
int acd_AgentACW(int groupId, int agentId, int reason);
int acd_AgentBusy(int groupId, int agentId, int reason);

int acd_AgentChangeExtStatus(int groupId, int agentId, int statusId,char *session_key);

int acd_PrintAgents(char *buf, int buflen);
int acd_PrintAgent(char *buf, int buflen, int agentId);

int acd_ReserveAgentExt(int agentGroup, int algorithm, t_SkillList *reqskills, char *ext, char *ch);
int acd_GetAgentForMM(int agentGroup, int algorithm, t_SkillList *reqskills, char *media);

/* setting */
int acd_ChangeAutoACWSetting(int groupId, int autoacw, int acwtime, int defreason);

#endif//_AcdH_

