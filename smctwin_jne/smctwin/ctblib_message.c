

#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include "include\smct.h"
#include "ctblib_message.h"

#ifdef __cplusplus
extern "C" {
#endif

int ctbMsgInit(tCtbMessage *m){
	memset(m, 0, sizeof(tCtbMessage));	
	return 0;
}

int ctbMsgReset(tCtbMessage *m){
	ctbMsgInit(m);
	return 0;
}

int ctbMsgFree(tCtbMessage *m){
	int i;

  if (!m)
    return 1;

  //for (i=0; i<(int)m->Count; i++)
  for (i=0; i<MAX_CTBMSG; i++)
    if (((m->Fields[i].DataType == CTBMSG_STRING) || (m->Fields[i].DataType == CTBMSG_RAW))){
    	if (m->Fields[i].UsingMem){
      	free(m->Fields[i].a.szVal);
      	m->Fields[i].UsingMem = 0;
    	}
    	m->Fields[i].a.szVal = NULL;
    }

  return 0;
}

int ctbMsgEncode(tCtbMessage *m, unsigned char *buf, int len)
{
  int i, idx=0;  

  if (!m)
    return 1;

  len = 0;
  //put version
  buf[idx++] = CTBMESSAGE_VERSION;
  buf[idx++] = (unsigned char)(m->Sender >> 8);
  buf[idx++] = (unsigned char)m->Sender;
  buf[idx++] = (unsigned char)(m->Type   >> 8);
  buf[idx++] = (unsigned char)m->Type;
  buf[idx++] = (unsigned char)(m->Count   >> 8);
  buf[idx++] = (unsigned char)m->Count;

  for (i=0; i < (int)m->Count; i++){
  	//put field tipe
  	buf[idx++] = (unsigned char)(m->Fields[i].DataType >> 8);
  	buf[idx++] = (unsigned char)m->Fields[i].DataType;
  	
  	if((m->Fields[i].DataType == CTBMSG_STRING) && (m->Fields[i].a.szVal))  	
  		m->Fields[i].DataLen = (UINT16)strlen(m->Fields[i].a.szVal);
  	
  	//put field length
  	buf[idx++] = (unsigned char)(m->Fields[i].DataLen >> 8);
  	buf[idx++] = (unsigned char)m->Fields[i].DataLen;
  	
  	switch(m->Fields[i].DataType){
  		case CTBMSG_NUMERIC:
  		buf[idx++] = (unsigned char) (m->Fields[i].a.iVal >> 24);
      	buf[idx++] = (unsigned char) (m->Fields[i].a.iVal >> 16);
      	buf[idx++] = (unsigned char) (m->Fields[i].a.iVal >> 8);
      	buf[idx++] = (unsigned char)  m->Fields[i].a.iVal;
  			break;
  		case CTBMSG_STRING:
  			if (m->Fields[i].a.szVal)
  				m->Fields[i].DataLen = (UINT16)strlen(m->Fields[i].a.szVal);
  		case CTBMSG_RAW:
  		  if (m->Fields[i].a.szVal)
  		  	memcpy(buf+idx, m->Fields[i].a.szVal, m->Fields[i].DataLen);
  		  else
  		  	m->Fields[i].DataLen=0;
  		  idx+=m->Fields[i].DataLen;
  			break;
  	}
  }

  return idx;
}

int ctbMsgDecode(tCtbMessage *m, unsigned char *buf, int len)
{
  int i, j, k, idx=0;
  
  if (!m)
    return 1;
	
  m->Version = buf[idx++];
  m->Sender  = (buf[idx]<<8)|buf[idx+1];idx+=2;
  printf("sender = %d\n",m->Sender);
  m->Type    = (buf[idx]<<8)|buf[idx+1];idx+=2;
  printf("Type = %d\n",m->Type);
  m->Count   = (buf[idx]<<8)|buf[idx+1];idx+=2;
  printf("Count = %d\n\n",m->Count);	
	i = 0;
	
  while (idx < len && (i < (int)m->Count)){
		m->Fields[i].DataType = (buf[idx]<<8)|buf[idx+1];idx+=2;
		m->Fields[i].DataLen  = (buf[idx]<<8)|buf[idx+1];idx+=2;
		printf("m->Fields[%d].DataType = %d\n",i,m->Fields[i].DataType);
		printf("m->Fields[%d].DataLen = %d\n\n",i,m->Fields[i].DataLen);
  	switch(m->Fields[i].DataType){
  		case CTBMSG_NUMERIC:
  			m->Fields[i].a.iVal = 0;
 				for (j=m->Fields[i].DataLen-1, k=0; j >=0; j--,k+=8){
  				m->Fields[i].a.iVal += (buf[idx+j] << k);  
  			}  			
  			break;
  		case CTBMSG_STRING:  		  
  		case CTBMSG_RAW:
  			if((m->Fields[i].a.szVal = (char*)malloc(m->Fields[i].DataLen+1))){
	  		  memcpy(m->Fields[i].a.szVal, buf+idx, m->Fields[i].DataLen);
	  		  m->Fields[i].a.szVal[m->Fields[i].DataLen] = 0;
	  		  m->Fields[i].UsingMem = 1;
	  		}
  			break;
  	}
  	idx+=m->Fields[i].DataLen;
  	++i;
  }
  if(m->Type != 1){
	printf("len= %d\n",len);
	printf("field0 = %d\n",m->Fields[0].a.iVal);
  }
  return idx;
}

#ifdef __cplusplus
}
#endif
