#ifndef _SmctMessageH_
#define _SmctMessageH_

#include "smctcommon.h"

#define SMCTMSG_MAXPARAM	200

#define SMCTMSG_MINLEN	12

#define	SMCTMSG_VER_2_2	0x22
#define	SMCTMSG_VER_2_0	0x02
#define	SMCTMSG_VER_1_0	0x01

#define	SMCTMSG_VER_MAYOR(ver) (((ver)>>4)&0x000F)
#define	SMCTMSG_VER_MINOR(ver) ( (ver)    &0x000F)

#define	SMCTMSG_TYPE_INT32			0x0000
#define	SMCTMSG_TYPE_STRING			0x0001
#define	SMCTMSG_TYPE_BYTEARR		0x0002
#define	SMCTMSG_TYPE_UNDEFINED	0xFFFF

typedef struct _SmctMessageParam{
		UINT16	type;
		UINT16	length;
		int			needToFreed;
		union	{
			UINT32	iVal;			/* Integer 32 bit value */
			char*   szVal;		/* Null terminated string value */
			char*   bufVal;		/* array of byte value */
			void*		unkVal;		/* unknown type value */
		}a;
}SmctMessageParam;

typedef struct _SmctMessage{
	UINT16 	versionMajor;
	UINT16 	versionMinor;
	UINT16 	originatorId;
	UINT16	serviceId;
	UINT16	serviceType;
	UINT16	invokeId;
	UINT16	paramCount;
	SmctMessageParam params[SMCTMSG_MAXPARAM];		/* we should think better implementation */
}SmctMessage;


#define smctMsgReset 	smctMsgResetV2_2
#define smctMsgEncode smctMsgEncodeV2_2
#define smctMsgDecode smctMsgDecodeV2_2
#define smctMsgFree   smctMsgFreeV2_2

DllExport void smctMsgResetV2_2(SmctMessage *m);
DllExport void smctMsgFreeV2_2 (SmctMessage *m);
DllExport int smctMsgDecodeV2_2(SmctMessage *m, unsigned char *b, int len);
DllExport int smctMsgEncodeV2_2(SmctMessage *m, unsigned char *b, int len);

#endif//_SmctMessageH_

