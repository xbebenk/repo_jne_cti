#ifndef _CtiH_
#define _CtiH_

/* call session status constants */
#define CALLSTATUS_OFFERED				0001		/* when call first come to VDN/access number */
#define CALLSTATUS_ABANDONED			0002		/* call dropped right after offered */
#define CALLSTATUS_IVR_RINGING			1001		/* ringing on IVR device */
#define CALLSTATUS_IVR_ABANDON			1002		/* FIN: terminated after ringing on IVR device */	
#define CALLSTATUS_IVR_CONNECTED		1003		/* connected on IVR device */
#define CALLSTATUS_IVR_TERMINATED		1004		/* FIN: terminated on IVR device */
#define CALLSTATUS_QUEUED				2001		/* on queue */
#define CALLSTATUS_QUEUE_TERMINATED		2002		/* FIN: terminated while on queue */
#define CALLSTATUS_AGENT_ROUTED			3001		/* call being routed to agent */
#define CALLSTATUS_AGENT_RINGING		3002		/**/
#define CALLSTATUS_AGENT_ABANDON		3003		/* FIN: terminated after ringing or initiated on agent */
#define CALLSTATUS_AGENT_CONNECTED		3004		/**/
#define CALLSTATUS_AGENT_TERMINATED		3005		/* FIN: terminated on agent */
#define CALLSTATUS_AGENT_HELD			3006
#define CALLSTATUS_AGENT_ORIGINATED		3007
#define CALLSTATUS_AGENT_TRUNKSEIZED	3008
#define CALLSTATUS_AGENT_INITIATED		3009
#define CALLSTATUS_AGENT_FAILED			3010

/* Phone Status */
#define PHONESTATUS_NULL				0
#define PHONESTATUS_IDLE				1
#define PHONESTATUS_RINGING			2
#define PHONESTATUS_CONNECTED		3
#define PHONESTATUS_HELP				4
#define PHONESTATUS_ORIGINATED	5
#define PHONESTATUS_TRUNKSEIZED	6

/*
 * Call Direction Definition
 * INCOMING: call come on akses number like VDN/Hunting/etc
 * OUTGOING: call via trunk
 * INTERNAL: call not going anywhere
 */
#define CALLDIR_INCOMING  1
#define CALLDIR_OUTGOING  2
#define CALLDIR_INTERNAL  3

/* CTI Private structure */
typedef struct TCTIPvt{
	

	/* TAPI protocol data */
	HANDLE   hTapiEvent;		/* TAPI event notification */
	HANDLE	 hDataEvent;		/* data event notification */
	
	// pipe handle	
	pthread_t mainThreadId;
	HANDLE hReader;		// reader pipe handle
	HANDLE hWriter;		// writer pipe handle
	pthread_mutex_t hPipeLock;
}TCTIPvt;

int cti_Load();
int cti_ProcessMsg(TCTIPvt *cti_pvt, unsigned char *message, int len);
int cti_Write(char *msg, int len);
int cti_CheckStationIdleStatus(char* ext_no);
int cti_Setcallqueue(char *ext_no, int group_id);

#endif//_CtiH_
