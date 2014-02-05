#ifndef __CtbLibMessageH__
#define __CtbLibMessageH__

#ifdef __cplusplus
extern "C" {
#endif

//Message Sender

#define MSG_SRC_CC_CTI 							0x01				// CTI
#define MSG_SRC_CC_CCD 							0x02				// ACD
#define MSG_SRC_CC_DBLOG 						0x03				// Database Logger
#define MSG_SRC_CC_HBEAT 						0x04				// Heartbeat Source

#define MSG_SRC_CC_IVR 							0x06				// IVR
#define MSG_SRC_CC_QUEUE 						0x07				// Voice Logger
#define MSG_SRC_CC_VOICELOG						0x08				// Voice Logger

#define MSG_SRC_CC_AGENT 						0x0A				// Agent
#define MSG_SRC_CC_ADMIN 						0x0B				// Admin

#define MSG_SRC_CTI 							0x01				// CTI
#define MSG_SRC_AGENTHANDLER					0x1A
#define MSG_SRC_MANAGER							0x1B


//Message Type

/* agent handler */
#define MSGTYPE_USE_EXT							0x0A01				/* use an extension */			
#define MSGTYPE_REL_EXT							0x0A02				/* release an extension */
#define MSGTYPE_ANSWER_CALL						0x0A07				/* answer call */
#define MSGTYPE_HOLD_CALL						0x0A08				/* hold a call */
#define MSGTYPE_RETR_CALL						0x0A09				/* retrieve call */
#define MSGTYPE_DROP_CALL						0x0A0A				/* drop call */
#define MSGTYPE_AUTOXFER_CALL					0x0A0B				/* auto transfer call */
#define MSGTYPE_MAKE_CALL						0x0A0E				/* make call */
#define MSGTYPE_SET_ASSIGNID					0x0A11

#define MSGTYPE_CONFERENCE_CALL					0x0A35				/* auto conference call */
#define MSGTYPE_TRANS_COMPLETE					0x0A36				/* transfer complete */
#define MSGTYPE_CONF_COMPLETE					0x0A37				/* conference complete */
#define MSGTYPE_SWAP_HOLD						0x0A38				/* swap hold */
#define MSGTYPE_RETRVBACK						0x0A39				/* retrieve back */

#define MSGTYPE_ADD_STATION						0x0B01				/* add station */
#define MSGTYPE_REM_STATION						0x0B02				/* delete station */
#define MSGTYPE_CHA_STATION						0x0B03				/* change station */

/* client */
#define MSGTYPE_AGENTREQ_DIAL					0x000E

#define MSGTYPE_REQ_AGENTLOGIN         			0x02
#define MSGTYPE_ACK_AGENTLOGIN         			0x02
#define MSGTYPE_REQ_AGENTLOGOUT        			0x03
#define MSGTYPE_ACK_AGENTLOGOUT        			0x03
#define MSGTYPE_REQ_AGENTACW  `       			0x04
#define MSGTYPE_ACK_AGENTACW					0x04
#define MSGTYPE_REQ_AGENTNOTREADY      			0x05
#define MSGTYPE_ACK_AGENTNOTREADY      			0x05
#define MSGTYPE_REQ_AGENTREADY         			0x06
#define MSGTYPE_ACK_AGENTREADY         			0x06
#define MSGTYPE_REQ_AGENTOUTBOUND      			0x11
#define MSGTYPE_ACK_AGENTOUTBOUND      			0x11

#define MSGTYPE_EVENT_AGENTBUSY					0x12

#define MSGTYPE_EVENT_CALLOFFERED				0x21
#define MSGTYPE_EVENT_CALLALERTING				0x22
#define MSGTYPE_EVENT_CALLCONNECTED				0x23
#define MSGTYPE_EVENT_CALLDISCONNECTED			0x24
#define MSGTYPE_EVENT_QUEUED					0x25
#define MSGTYPE_EVENT_DISCONNECTED_QUEUE		0x26
#define MSGTYPE_EVENT_CALLINITIATED				0x2D
#define MSGTYPE_EVENT_CALLHELD					0x27
#define MSGTYPE_EVENT_CALLRECONNECTED			0x28
#define MSGTYPE_EVENT_CALLORIGINATED			0x29
#define MSGTYPE_EVENT_CALLTRUNKSEIZED			0x2a

#define MSGTYPE_EVENT_HOLDPENDTRANSFER			0x30
#define MSGTYPE_EVENT_HOLDPENDCONF				0x31

#define MSGTYPE_EVENT_EMAILALERT				0x40

#define MSGTYPE_DATA_GROUPINFO					0x6001
#define MSGTYPE_DATA_AGENTLOGIN					0x6002
#define MSGTYPE_DATA_AGENTLOGOUT				0x6003
#define MSGTYPE_DATA_AGENTSTATUS				0x6004
#define MSGTYPE_DATA_EXTSTATUS					0x6005

#define MSGTYPE_NOTIF_AUTOACW					0x5001

// link and app message
#define MSGTYPE_STATUS_REQ						0x000A
#define MSGTYPE_STATUS_RES						0x000B

#define MSGTYPE_DBPING							0x1999

// acd
#define MSGTYPE_AGENTAVAIL_REQ					0x0081
#define MSGTYPE_AGENTAVAIL_RES					0x0082
#define MSGTYPE_AGENTLOGIN_REQ 					0x0083
#define MSGTYPE_AGENTLOGIN_RES					0x0084
#define MSGTYPE_AGENTLOGOUT_REQ					0x0085
#define MSGTYPE_AGENTLOGOUT_RES					0x0086
#define MSGTYPE_AGENTSETSTATUS_REQ				0x0087
#define MSGTYPE_AGENTSETSTATUS_RES				0x0088


#define MAX_CTBMSG								25

#define CTBMESSAGE_VERSION						0x01   // versi 1

#define CTBMSG_NUMERIC							0x0000
#define CTBMSG_STRING							0x0001
#define CTBMSG_RAW								0x0002

typedef struct tCtbMessageField{
		UINT16	DataType;
		UINT16	DataLen;
		int		  UsingMem;
		union	{
			UINT32	iVal;
			char*   szVal;
			char*   bufVal;
		}a;		
}tCtbMessageField;

typedef struct tCtbMessage{
	UINT8						Version;
	UINT16						Sender;
	UINT16						Type;
	UINT16						Count;	
	tCtbMessageField 			Fields[MAX_CTBMSG];
}tCtbMessage;


int ctbMsgInit(tCtbMessage *m);
int ctbMsgReset(tCtbMessage *m);
int ctbMsgFree(tCtbMessage *m);

int ctbMsgEncode(tCtbMessage *m, unsigned char *buf, int len);
int ctbMsgDecode(tCtbMessage *m, unsigned char *buf, int len);

#define ctbMsgInsertNumeric(m,i,v) \
	(m)->Fields[i].DataType = CTBMSG_NUMERIC;\
	(m)->Fields[i].DataLen  = 4;\
  (m)->Fields[i].a.iVal     = v;
  
#define ctbMsgInsertString(m,i,v) \
	(m)->Fields[i].DataType = CTBMSG_STRING;\
	(m)->Fields[i].DataLen  = 0;\
  (m)->Fields[i].a.szVal  = v;
  
#define ctbMsgInsertRaw(m,i,v,l) \
	(m)->Fields[i].DataType = CTBMSG_RAW;\
	(m)->Fields[i].DataLen  = l;\
  (m)->Fields[i].a.bufVal = v;
  
#ifdef __cplusplus
}
#endif

#endif//__CtbLibMessageH__
