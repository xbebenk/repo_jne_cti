
#include "winsock2.h"
#include "include/appsvr.h"
#include "include/appsvr_sock.h"

#pragma warning(disable : 4996)  // deprecated CRT function

static BOOL bSocketInitialize = FALSE;

static void InitSocket(){
	WSADATA wsaData;
	int iResult = WSAStartup( MAKEWORD(2,2), &wsaData );
	if ( iResult != NO_ERROR ){
		printf("Error at WSAStartup()\n");
		return;
	}

	bSocketInitialize = TRUE;
}

SOCKET sockTcpClientCreate(){
	SOCKET sock;
	char optval;

	if (!bSocketInitialize)
		InitSocket();
	
	/* create socket */
  if((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){  	
    return INVALID_SOCKET;
  }
  
  /* set option to reuse address */
  optval = 1;
  if ( setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == SOCKET_ERROR ) {  	
    closesocket(sock);
    return INVALID_SOCKET;
  }
  
	return sock;
}

int sockTcpClientConnect(SOCKET sock, char *host, short port)
{	  
  struct sockaddr_in  clie_addr;
  unsigned long       inaddr;
  struct hostent	    *hp;

	if (!bSocketInitialize)
		InitSocket();
  
  if (port < 0) return -1;

  memset(&clie_addr, 0, sizeof(clie_addr));
  clie_addr.sin_family = AF_INET;
  clie_addr.sin_port   = htons(port);
  
	if ( (inaddr = inet_addr(host)) != INADDR_NONE) {
	  // it's dotted-decimal
	  memcpy((char *) &clie_addr.sin_addr, (char *) &inaddr, sizeof(inaddr));
	}else{
		if ( (hp = gethostbyname(host)) == NULL) {			
			return -1;
		}
		memcpy((char *) &clie_addr.sin_addr, hp->h_addr_list[0], hp->h_length);
	}
	
	if ( connect(sock, (struct sockaddr *)&clie_addr, sizeof(clie_addr)) < 0 ){    
    return -1;
  }
    
	return 0;
}

SOCKET sockTcpServerCreate(int port, int listenqueue){
  SOCKET sock;
  char on;
  struct sockaddr_in  serv_addr;

  if (port < 0) return -1;

  memset(&serv_addr,0,sizeof(serv_addr));
  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port        = htons(port);

  sock = socket(AF_INET, SOCK_STREAM, 0);

  on = 1;
  if ( setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) ) < 0 )
  	return -1;

  /* Bind the sock */
  if ( bind( sock, (struct sockaddr*)&serv_addr, sizeof( struct sockaddr) ) < 0 ){
	  closesocket( sock );
	  return -1;
	}

  if (listen(sock, listenqueue) < 0){
  	closesocket( sock );
    return -1;
  }

  return sock;
}

SOCKET sockTcpServerAccept(SOCKET sock, char *from_ip, unsigned int *from_port){
  struct 	sockaddr_in in_addr;
  int     addr_len;
  SOCKET 		sockClie;

  addr_len = sizeof(in_addr);
  sockClie = accept(sock, (struct sockaddr*)&in_addr, &addr_len);
  if (from_ip)	strcpy(from_ip, inet_ntoa(in_addr.sin_addr));
  if (from_port)*from_port = ntohs( in_addr.sin_port);
  return sockClie;
}

int sockTcpWrite(SOCKET sock, void *buffer, int length){
  int sent = 0;
  int count;
	char *pBuffer = (char*)buffer;

  // return after all data in buffer sent
  while (sent != length) {
		count = send(sock, pBuffer, length - sent, 0);
		if (count < 0) {			
			return -1;
		}

		sent += count;
		pBuffer += count;
	}

  return sent;
}

/* UDP */

SOCKET sockUdpCreate(int listenPort){
  SOCKET sock;
  char optval;
  struct sockaddr_in  clie_addr;

	if (!bSocketInitialize)
		InitSocket();

  if((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1){    
    return -1;
  }
  // set option
  optval = 1;
  if ( setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {    
    return -1;
  }
  optval = 1;
  if ( setsockopt(sock, SOL_SOCKET, SO_BROADCAST,&optval, sizeof(optval)) < 0 ) {     
     return -1;;
  }

  if (listenPort > 0){
          memset(&clie_addr, 0, sizeof(clie_addr));
          clie_addr.sin_family = AF_INET;
          clie_addr.sin_port = htons(listenPort);
          clie_addr.sin_addr.s_addr = INADDR_ANY;
          //Binding port for udp socket
          if (bind(sock, (struct sockaddr *) &clie_addr, sizeof(struct sockaddr)) < 0){            
            return -1;
          }
        }
  return sock;
}

int sockUdpWrite(SOCKET sock, void *buffer, int length, char *ip, int port){
  int sent;
  struct sockaddr_in s;
  int count;
	char *pBuffer = (char*)buffer;

  memset(&s,0, sizeof(s));
  s.sin_family = AF_INET;
  s.sin_port   = htons(port);
  s.sin_addr.s_addr = inet_addr(ip);

  sent = 0;
  // return after all data in buffer sent
  while (sent != length) {
    count = sendto(sock,pBuffer, length - sent, 0, (struct sockaddr*)&s, sizeof(s));
		if (count < 0) {    	
      return -1;
		}
		sent		+= count;
    pBuffer += count;
  }

  return sent;
}

int sockUdpRead(SOCKET sock, void *buffer, int buflen, char *from_ip, unsigned int *from_port)
{
  struct sockaddr_in in_addr;
  int     addr_len;
  int nrecv = 0;

	addr_len = sizeof(in_addr);
  if ((nrecv=recvfrom(sock, buffer, buflen, 0, (struct sockaddr *)&in_addr, &addr_len)) < 0) {    
    return 1;
  }

  if (from_ip)
    strcpy(from_ip, inet_ntoa(in_addr.sin_addr));
  if (from_port)
    *from_port = ntohs( in_addr.sin_port);

	return(nrecv);
}

void sockClose(SOCKET sock){
	closesocket(sock);
}

