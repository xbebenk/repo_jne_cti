#ifndef _CtiTapiH_
#define _CtiTapiH_

#include <Tapi.h>

#define INVOKETYPE_DROPCALL						0x0001
#define INVOKETYPE_MONITORDEVICE				0x0002
#define INVOKETYPE_DEVICESTATUSQUERY			0x0003
#define INVOKETYPE_HOLDCALL						0x0004
#define INVOKETYPE_RETRIEVECALL					0x0005
#define INVOKETYPE_AUTODIAL						0x0006
#define INVOKETYPE_RELINGUISHCONTROL			0x0007

typedef struct _TapiDevice{
	int				type;						/* 1 = VDN/HG IVR; 2 = EXT of IVR; 3 = EXT of AGENT; 4 = EXT of QUEUE; 5 = ACD SPLIT */
  												/* 6 = Predictive Device																														 */
	#define TAPIDEVICE_VDN						1
	#define TAPIDEVICE_IVR						2
	#define TAPIDEVICE_AGENT					3
	#define TAPIDEVICE_QUEUE					4
	#define TAPIDEVICE_ACDHUNTING				5
	#define TAPIDEVICEMODEL_ANALOG				0
	#define TAPIDEVICEMODEL_DIGITAL				1
	#define TAPIDEVICEMODEL_SOFTPHONE			2
  
	#define TAPIDEVICESTATUS_BUSY				7
	#define TAPIDEVICESTATUS_RESERVED			17
	#define TAPIDEVICESTATUS_IDLE				25
	#define TAPIDEVICEACTION_TRANSFERRING		1

	unsigned char crv[2];						/* unsigned char[2] */
	int				tapiId;						/* TAPI device Id */
	int				call_id;
	int				have_callsession;
	HLINE			hLine;
	HPHONE			hPhone;
	char 			number[8];					/* char[8], dialed number */
	char 			dialNumber[8];
	char			ipAddress[20];
	char			ivr_port[5];
  
	/* for VDN type device data */
	char			huntNumber[8]; 				/* IVR hunting number untuk VDN, ACD Split untuk Agent Ext*/
	int				isDirect; 					/* apakah ke IVR atau langsung ke agent, jika direct maka cti akan request ext ke ACD, jika tidak akan dilempar ke hunting */                               

	t_SkillList	skills;                               
	int				routingAlgorithm;			/* algoritma yang akan digunakan untuk distribusi call ke VDN ini */
	char			trashTarget[16];			/* nomer tujuan jika tidak ada agent yang available */
  
	/* for agent ext device data */  
	int				agentId;					
	char			agentLoginId[64];
	int				groupId;
	int				status;						/* device status */
	int				model;						/* device-model: 0-analog,1-digital,2-softphone */
  
	int				action;

	/* call data */
	char			session_key[34];			/* char[16] */
	HCALL			activeCall;					/* current/active call ID */
	HCALL			secondCall;					/* current/active call ID */
	HCALL			heldCall;
    HCALL			consultCall;
	HCALL			confCall;
	HCALL			dummyCall;
	int				swaptype;

	int				call_mode;					/**
  												0 - none
  												1 - normal call
  												2 - single step conf
  												3 - coaching/whispering
												*/
	char			connected_ext[8]; 			/* Untuk crv_type VDN, artinya EXT */
	int				assignment_id;    			/* untuk kebutuhan aplikasi outbound call */
	int				occupancy;
	int				queue_port; 		  		/* voice board port resource */
	int				queue_board; 		  		/* voice board port resource */
	int				bit_skill; 					/* skill agent dlm format bit */	
	int				work_status;  
	int				is_reached; 				/* indikator call sampai ke agent, di-set pada saat alerting dan drop */
	int				onqueue;
	int				queue_group;

	//for predictive device
	int				assigned_group;				//call dari device ini di-assign ke group mana
	char			assigned_ext[16];			//call dari device ini di-assign ke ext mana
	int				assigned_to;				//assign type 0:group 1:ext
	int				device_status;				/*  0x0000:idle
  													0x0001:inuse
  													0x0002:requesting_agent
  													0x0008:transfering_to_ext
  												*/

	struct _TapiDevice *next;					/* pointer to next asai_domain */
	struct _TapiDevice *prev;					/* pointer to next asai_domain */
}t_TapiDevice;

typedef struct {
	int size;
	t_TapiDevice *first;
	t_TapiDevice *last;
}t_TapiDeviceList;


typedef struct _CallSession{
	HLINE					line;						/* handle line yang pertamakali terima call */
	char					session_key[34];
	int						call_id;
	HCALL					origCall;					/* handle to original call */
	HCALL					currCall;					/* handle to current call */
	int						trunk_number;
	int						trunk_member;
	char					calling_number[64];
	char					called_number[64];
	char					connected_number[64];
	char					vdn_number[64];				/* vdn number of this call */	
	int						direction;					/* 1 = in, 2 = out, 4 = internal */
	int						assignment_id;				/* kebutuhan aplikasi */
	int						langId;						/* language ID */
	int						skill;						/* skill required for this call */
	unsigned char			call_crv[2];
	
	/* IVR data */
	unsigned char			ivrCRV[2];
	char					ivrExt[9];		    	    /* ivr extension */
	
	
	
	/* Queue data */
	unsigned char			queCRV[2];
	char					queExt[9];		    	    /* que extension */
	int						queBoard;
	int						quePort;	
	
	int						status;						/* current status of this call session */
	int						prevStatus;					/* previous status of this call session */
	
	/* Agent Data */	
	int						agentId;
	int						oldAgentId;					/* previous agent id */
	int 					coaching_party_id;		
	int						is_connected_to_agent;
	int						is_to_be_queued;
	int						group_id;
	
	int						eventTime;					/* timestamp event terjadi */
	
	/* statistic data */	
	int						startTime;
	int						ringTime;
	int						answeredTime;
	int						holdTime;
	
	char					hunt_number[8];				/* IVR hunting number untuk VDN, ACD Split untuk Agent Ext*/
	
	int						is_direct;					/* apakah ke IVR atau langsung ke agent */  
	int						is_onhold;
	int						is_reached;					// indikator call sampai ke agent, di-set pada saat alerting dan drop
  
	struct _CallSession	*prev;  						/* pointer ke call_session, linked list implementation */
	struct _CallSession	*next;							/* pointer ke call_session, linked list implementation */
}t_CallSession;

typedef struct {
	int size;
	t_CallSession *first;
	t_CallSession *last;
}t_CallSessionList;

int tapi_Init(TCTIPvt *cti_pvt);
int tapi_MainLoop(TCTIPvt *cti_pvt);
int tapi_LoadDevices(TCTIPvt *cti_pvt);

t_TapiDevice* tapi_FindAllDeviceByNumber(char* number);
int tapi_CheckStationIdleStatus(char *ext_no);
int tapi_StationIdleStatus(char *ext_no);
int tapi_setcallqueue(char *ext_no, int group_id);
int tapi_UseExt(int agentID,int groupID,char *extNo);
int tapi_ReleaseExt(int agentId, char *extNo);
#endif//_CtiTapiH_