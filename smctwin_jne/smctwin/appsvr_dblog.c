/*
 DBLog module: Event and Applicaton activity Logger
 Module untuk logging ke database, hanya satu instance yang jalan dalam satu program
 digunakan oleh modul-modul yang mesti dijalankan secara realtime untuk menghindari 
 overhead koneksi dan query ke database
 
 assume thread lock, malloc and memory are cheap
 
 BE CAREFUL WITH MALLOC!!!
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <process.h>

#include "include/smct.h"
#include "ctblib_message.h"
#include "include/appsvr.h"
#include "include/appsvr_thread.h"
#include "include/appsvr_dblog.h"
#include "acd/acd.h"
#include "cti/cti.h"
//#include "cti/cti_asai.h"

#pragma warning(disable : 4996)  // deprecated CRT function

#define LOGCALLSTAT_FILENAME "c:\\smartcenter\\log\\call_info.txt"

int offered_on_ivr=0;
int answered_on_ivr=0;
int abandon_on_ivr=0;
int terminate_on_ivr=0;
int call_queued=0;
int terminate_on_queue=0;


typedef struct _t_CallStat{
	int  callOffered;
	int  callRejected;
	int  callAnswered;
	int  callAbandoned;
	
	int  currentDay;
}t_CallStat;

typedef struct _t_DBLogPvt{
	tDbConn dbConn;	

	HANDLE hReader;		// reader pipe handle
	HANDLE hWriter;		// writer pipe handle
	
	pthread_t mainThreadId;
}t_DBLogPvt;

static t_DBLogPvt *DBLogPvt;
static t_CallStat CallStat={0};

static void dblog_CallStat(int stattype){
	time_t t;
	struct tm tm;
	FILE *log = NULL;
	char tmp[256];
	
	time(&t);
	localtime_s(&tm, &t);
	if (tm.tm_mday != CallStat.currentDay){
		/* the day has changed */
		CallStat.currentDay    = tm.tm_mday;
		CallStat.callOffered   = 0;  
		CallStat.callRejected  = 0; 
		CallStat.callAnswered  = 0; 
		CallStat.callAbandoned = 0;
	}
			
	switch(stattype){
		case STATTYPE_CALL_OFFERED:
			++CallStat.callOffered;
			break;
		case STATTYPE_CALL_REJECTED:
			++CallStat.callRejected;
			break;
		case STATTYPE_CALL_ANSWERED:
			++CallStat.callAnswered;
			break;
		case STATTYPE_CALL_ABANDONED:
			++CallStat.callAbandoned;
			break;
	}
	
	/* log to file */
	_snprintf(tmp, sizeof(tmp), "%s", LOGCALLSTAT_FILENAME);	
	log = fopen((char *)tmp, "w+");
	if (log){	
		fprintf(log, "[call_statistic]\r\n");
		fprintf(log, "offered=%d\r\n", CallStat.callOffered);
		fprintf(log, "rejected=%d\r\n", CallStat.callRejected);
		fprintf(log, "answered=%d\r\n", CallStat.callAnswered);
		fprintf(log, "abandoned=%d\r\n", CallStat.callAbandoned);
		fflush(log);
		
		fclose(log);
	}	
}

/**
 Reset DB on application start up
 */
static void dblog_Reset_system_stat(t_DBLogPvt *pvt){
	char sql[1024];
	int  sqlLen=0,ret;
	sqlLen = sprintf(sql, "update system_statistic set offered_on_ivr=0, abandon_on_ivr=0, answered_on_ivr=0, "
		  "terminate_on_ivr=0, queued_to_agent=0, terminate_on_queue=0,offered_to_agent=0, abandon_on_agent=0, "
		  "answered_on_agent=0, terminate_on_agent=0 where group_id in(1,2,3);");
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
}

static void dblog_Reset(t_DBLogPvt *pvt){
	char sql[1024];
	int  sqlLen=0,ret;
	sqlLen = sprintf(sql, "UPDATE agent_activity SET status=0, status_reason=0, ext_status=0, "
						" ext_number='', location=''");
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
}

static t_DBLogPvt *dblog_Init(void){
	t_DBLogPvt *pvt;
	pvt = (t_DBLogPvt*)malloc(sizeof(t_DBLogPvt));
	if (!pvt)
		return NULL;
	memset(pvt, 0, sizeof(t_DBLogPvt));	
	if (CreatePipe(&pvt->hReader, &pvt->hWriter, NULL, 10000) == 0) {
		logger_Print(4,5,"DB>> Unable to create control socket: %s\n", strerror(errno));
		free(pvt);
		return NULL;
	}	
	/* connect to DB */
	pvt->dbConn = dbLib->openConnection(appContext->dbHost, appContext->dbUser, appContext->dbPasswd, appContext->dbName, 0);
	if (!pvt->dbConn){
		logger_Print(4,5,"DB>> DB Connection failed\n");
		return NULL;
	}
	dblog_Reset(pvt);
	return pvt;
}

static int dblog_LoadConfig(t_DBLogPvt *pvt){
	
	return 0;
}

static int dblog_DataCallOffered(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	dblog_CallStat(STATTYPE_CALL_OFFERED);
	sqlLen = sprintf(sql, "INSERT INTO call_session (session_id, direction, status, start_time, a_number, b_number, d_number, trunk_number, trunk_member, ivr_data) "
				  "VALUES ('%s', %ld, %d, now(), '%s', '%s', '%s', %ld, %ld, '%s') ", 
										msg->Fields[1].a.szVal, msg->Fields[7].a.iVal, CALLSTATUS_OFFERED, msg->Fields[4].a.szVal,
										msg->Fields[5].a.szVal, msg->Fields[6].a.szVal, msg->Fields[2].a.iVal, msg->Fields[3].a.iVal, msg->Fields[8].a.szVal);
	
	if(sqlLen){
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
		return ret;
	}
	return -1;
}

/**
 Call Initiated Data
 0 - CallId
 1 - SessionKey
 2 - CallStatus
 3 - CallDirection
 4 - DeviceNumber
 5 - AgentId
 6 - GroupId
 7 - AssignmentId
 */
static int dblog_DataCallInitiated(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	sqlLen = sprintf(sql, "INSERT INTO call_session (session_id, direction, status, start_time, a_number, agent_id, agent_group, agent_time, agent_ext, assign_id) "
					  "VALUES ('%s', %ld, %ld, now(), '%s', %ld, %ld, now(), '%s', %ld ) ", 
											msg->Fields[1].a.szVal, 
											msg->Fields[3].a.iVal, 
											msg->Fields[2].a.iVal, 
											msg->Fields[4].a.szVal,
											msg->Fields[5].a.iVal, 
											msg->Fields[6].a.iVal,
											msg->Fields[4].a.szVal,
											msg->Fields[7].a.iVal);

		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
		return ret;
}

static int dblog_DataCallAlerting(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	switch(msg->Fields[2].a.iVal){
		case CALLSTATUS_IVR_RINGING:
			sqlLen = sprintf(sql, "UPDATE call_session SET status=%ld, b_number='%s', ivr_ext='%s' WHERE session_id = '%s'",	                       
							msg->Fields[2].a.iVal,  msg->Fields[3].a.szVal, msg->Fields[3].a.szVal, msg->Fields[1].a.szVal);
			break;
		case CALLSTATUS_AGENT_RINGING:
			sqlLen = sprintf(sql, "UPDATE call_session SET status=%ld, agent_id = %ld, agent_group=%ld, agent_ring_time=now(), "
							"agent_ext='%s', ivr_ext='%s' WHERE session_id = '%s'",	                       
							msg->Fields[2].a.iVal, msg->Fields[5].a.iVal, msg->Fields[6].a.iVal, 
							msg->Fields[3].a.szVal, msg->Fields[7].a.szVal, msg->Fields[1].a.szVal);	
			break;
		default:
			break;
	}

	if (sqlLen){
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);  
		return ret;
	}
	return -1;
}

static int dblog_stat_alerting(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	switch(msg->Fields[2].a.iVal){
	case CALLSTATUS_IVR_RINGING:
		++offered_on_ivr;
		sqlLen = sprintf(sql, "UPDATE system_statistic set offered_on_ivr = offered_on_ivr + 1");
		break;
	case CALLSTATUS_AGENT_RINGING:
		sqlLen = sprintf(sql, "UPDATE system_statistic set offered_to_agent = offered_to_agent + 1 WHERE group_id=%d",
			msg->Fields[6].a.iVal);	
		break;
	default:
		break;
	}
	if (sqlLen){  
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);

		
		logger_Print(4,5,"offered_ivr=%d, answered_ivr=%d, terminate_on_ivr=%d,abandon_on_ivr=%d\n",
			offered_on_ivr,answered_on_ivr,terminate_on_ivr,abandon_on_ivr);
		return ret;
	}
	return -1;
}

/*
	Input: - session_id
	       - status
	       - ivr_ext or agent_ext
	       - agent id
	       - agent group
 */
static int dblog_DataCallConnected(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	switch(msg->Fields[2].a.iVal){
		case CALLSTATUS_IVR_CONNECTED:
			sqlLen = sprintf(sql, "UPDATE call_session SET status=%ld WHERE session_id = '%s'",	                       
					msg->Fields[2].a.iVal, msg->Fields[1].a.szVal);
		  break;
		case CALLSTATUS_AGENT_CONNECTED:
			sqlLen = sprintf(sql, "UPDATE call_session SET status=%ld,direction=%d, agent_id = %ld, agent_group=%ld, agent_time=now(),"
						  " a_number='%s', agent_ext='%s', agent_ring=now() - agent_ring_time WHERE session_id = '%s'",
						  msg->Fields[2].a.iVal,msg->Fields[4].a.iVal, msg->Fields[5].a.iVal, 
						  msg->Fields[6].a.iVal, msg->Fields[3].a.szVal, msg->Fields[7].a.szVal, msg->Fields[1].a.szVal);
		  break;
		case CALLSTATUS_AGENT_ORIGINATED:
			sqlLen = sprintf(sql, "UPDATE call_session SET status=%ld,direction=%d, agent_id = %ld, agent_group=%ld, agent_time=now(),"
						  " a_number='%s', agent_ext='%s', b_number='%s', d_number='%s' WHERE session_id = '%s'",
						  msg->Fields[2].a.iVal,msg->Fields[4].a.iVal, msg->Fields[5].a.iVal, msg->Fields[6].a.iVal,
						  msg->Fields[7].a.szVal, msg->Fields[7].a.szVal, msg->Fields[3].a.szVal,msg->Fields[3].a.szVal,msg->Fields[1].a.szVal);
			break;
		default:
			break;
	}
	if (sqlLen){ 
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
		return ret;
	}
	return -1;
}

static int dblog_stat_connected(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	switch(msg->Fields[2].a.iVal){
	  case CALLSTATUS_IVR_CONNECTED:
		  ++answered_on_ivr;
		  sqlLen = sprintf(sql,"UPDATE system_statistic set answered_on_ivr = answered_on_ivr +1");
		  break;
	  case CALLSTATUS_AGENT_CONNECTED:
		  sqlLen = sprintf(sql,"UPDATE system_statistic set answered_on_agent = answered_on_agent +1 WHERE group_id=%d",msg->Fields[6].a.iVal);
		  break;
	  default:
		  break;
	}
	if(sqlLen){
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
		logger_Print(4,5,"offered_ivr=%d, answered_ivr=%d, terminate_on_ivr=%d,abandon_on_ivr=%d\n",
			offered_on_ivr,answered_on_ivr,terminate_on_ivr,abandon_on_ivr);
		return ret;
	}
	return -1;
}

/* 
	call session finished 
	hanya melakukan update status
*/
static int dblog_DataCallDisconnected(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	switch(msg->Fields[2].a.iVal){
		case CALLSTATUS_IVR_TERMINATED:
			sqlLen = sprintf(sql, "UPDATE call_session SET end_time=now(), status=%ld, ivr_ext='%s', ivr_duration=now() - start_time, "
								"ivr_port='%s' WHERE session_id = '%s'",	                       
								msg->Fields[2].a.iVal, msg->Fields[3].a.szVal, msg->Fields[7].a.szVal, msg->Fields[1].a.szVal);
			break;		
		case CALLSTATUS_IVR_ABANDON:
			sqlLen = sprintf(sql, "UPDATE call_session SET end_time=now(), status=%ld, ivr_ext='%s' "
								"WHERE session_id = '%s'",	                       
								msg->Fields[2].a.iVal, msg->Fields[3].a.szVal, msg->Fields[1].a.szVal);
			break;
		case CALLSTATUS_AGENT_TERMINATED:
			sqlLen = sprintf(sql, "UPDATE call_session SET end_time=now(), status=%ld, agent_end_time=now(), "
								"agent_talk=now()-agent_time WHERE session_id = '%s'",	                       
								msg->Fields[2].a.iVal, msg->Fields[1].a.szVal);	
			break;
		case CALLSTATUS_AGENT_ABANDON:
			sqlLen = sprintf(sql, "UPDATE call_session SET end_time=now(), status=%ld, agent_end_time=now(),a_number='%s', "
								"agent_talk=now()-agent_time WHERE session_id = '%s'",	                       
								msg->Fields[2].a.iVal, msg->Fields[7].a.szVal,msg->Fields[1].a.szVal);	
			break;
		case CALLSTATUS_ABANDONED:  		
			sqlLen = sprintf(sql, "UPDATE call_session SET end_time=now(), status=%ld, agent_end_time=now() "
								"WHERE session_id = '%s'",	                       
								msg->Fields[2].a.iVal, msg->Fields[1].a.szVal);
			break;
		case CALLSTATUS_QUEUE_TERMINATED:
			sqlLen = sprintf(sql, "UPDATE call_session SET end_time=now(),que_end=now(), status=%ld, ivr_ext='%s', "
								" ivr_duration=now() - start_time WHERE session_id = '%s'",	                       
								msg->Fields[2].a.iVal, msg->Fields[3].a.szVal, msg->Fields[1].a.szVal);
			break;
		case CALLSTATUS_AGENT_FAILED:
			sqlLen = sprintf(sql, "UPDATE call_session SET end_time=now(), status=%ld, agent_end_time=now(), "
								"agent_talk=now()-agent_time WHERE session_id = '%s'",	                       
								msg->Fields[2].a.iVal, msg->Fields[1].a.szVal);	
			break;
		default:
			break;
	}

	/* utk sementara log data hanya utk yang inbound */
	if (msg->Fields[4].a.iVal == CALLDIR_INCOMING){
		switch(msg->Fields[2].a.iVal){
			case CALLSTATUS_IVR_TERMINATED:
			case CALLSTATUS_IVR_ABANDON:  	
				break;
			case CALLSTATUS_AGENT_TERMINATED:
				dblog_CallStat(STATTYPE_CALL_ANSWERED);
				break;
			case CALLSTATUS_AGENT_ABANDON:
				dblog_CallStat(STATTYPE_CALL_ABANDONED);
				break;
			case CALLSTATUS_ABANDONED:
				dblog_CallStat(STATTYPE_CALL_REJECTED);
				break;
		}
	}

	if (sqlLen){
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
		return ret;
	}
	return -1;
}

static int dblog_DataCallQueued(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;
	
	sqlLen = sprintf(sql,"UPDATE call_session set status = %d, ivr_port='%s', que_ext='%s', que_port='%s', que_start=now(), agent_group=%d where session_id='%s'",
		CALLSTATUS_QUEUED, msg->Fields[2].a.szVal,msg->Fields[3].a.szVal,msg->Fields[2].a.szVal,msg->Fields[4].a.iVal,msg->Fields[1].a.szVal);
	
	if (sqlLen){
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
		return ret;
	}
	return -1;
}

static int dblog_stat_queued(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;
	
	++call_queued;
	sqlLen = sprintf(sql,"UPDATE system_statistic set call_queued = %d where group_id = %d",call_queued,msg->Fields[4].a.iVal);
	
	if (sqlLen){
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
		return ret;
	}
	return -1;
}

static int dblog_stat_terminateOnQueue(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	--call_queued;
	sqlLen = sprintf(sql,"UPDATE system_statistic SET terminate_on_queue = terminate_on_queue +1 WHERE group_id =%d", msg->Fields[3].a.iVal);
	if (sqlLen){
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);  
		return ret;
	}
	return -1;
}
static int dblog_stat_disconnected(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	switch(msg->Fields[2].a.iVal){
		case CALLSTATUS_IVR_TERMINATED:
			sqlLen = sprintf(sql,"UPDATE system_statistic SET terminate_on_ivr = terminate_on_ivr + 1");
			++terminate_on_ivr;
			break;
		case CALLSTATUS_IVR_ABANDON:
			sqlLen = sprintf(sql,"UPDATE system_statistic SET abandon_on_ivr = abandon_on_ivr + 1");
			++abandon_on_ivr;
			break;
		case CALLSTATUS_AGENT_TERMINATED:
			sqlLen = sprintf(sql,"UPDATE system_statistic SET terminate_on_agent = terminate_on_agent + 1 WHERE group_id=%d",msg->Fields[6].a.iVal);
			break;
		case CALLSTATUS_AGENT_ABANDON:
			sqlLen = sprintf(sql,"UPDATE system_statistic SET abandon_on_agent = abandon_on_agent + 1 WHERE group_id=%d",msg->Fields[6].a.iVal);
			break;
		//case CALLSTATUS_QUEUE_TERMINATED:
		//	sqlLen = 0;
		//	break;
		default:
			break;
	}

	/* utk sementara log data hanya utk yang inbound */
	if (msg->Fields[4].a.iVal == CALLDIR_INCOMING){
		switch(msg->Fields[2].a.iVal){
			case CALLSTATUS_IVR_TERMINATED:
			case CALLSTATUS_IVR_ABANDON:  	
				break;
			case CALLSTATUS_AGENT_TERMINATED:
				dblog_CallStat(STATTYPE_CALL_ANSWERED);
				break;
			case CALLSTATUS_AGENT_ABANDON:
				dblog_CallStat(STATTYPE_CALL_ABANDONED);
				break;
			case CALLSTATUS_ABANDONED:
				dblog_CallStat(STATTYPE_CALL_REJECTED);
				break;
		}
	}

	if (sqlLen){
		ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
		logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);  
		logger_Print(4,5,"offered_ivr=%d, answered_ivr=%d, terminate_on_ivr=%d,abandon_on_ivr=%d\n",
						offered_on_ivr,answered_on_ivr,terminate_on_ivr,abandon_on_ivr);
		return ret;
	}
	return -1;
}

/**
 Call Originated Data
 0 - CallId
 1 - SessionKey
 2 - CallStatus
 3 - CallDirection
 4 - callingNumber
 5 - calledNumber
 6 - AgentId
 7 - GroupId
 8 - AssignmentId
 */
static int dblog_DataCallOriginated(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	sqlLen = sprintf(sql, "UPDATE call_session SET status=%ld, a_number='%s',b_number='%s', d_number='%s' "
												"WHERE session_id = '%s'",	                       
											msg->Fields[2].a.iVal, msg->Fields[4].a.szVal,
											msg->Fields[5].a.szVal, msg->Fields[5].a.szVal,									
											msg->Fields[1].a.szVal);	
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	return ret;
}

static int dblog_stat_Originated(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	sqlLen = sprintf(sql, "UPDATE system_statistic SET agent_outgoing = agent_outgoing + 1 where group_id = '%d'",msg->Fields[7].a.iVal);	
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	return ret;
}
/**
 Call Trunk Seized Data
 0 - CallId
 1 - SessionKey
 2 - CallStatus
 3 - CallDirection
 4 - trunkNumber
 5 - trunkMember
 6 - AgentId
 7 - GroupId
 8 - AssignmentId
 */
static int dblog_DataCallTrunkSeized(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	sqlLen = sprintf(sql, "UPDATE call_session SET status=%ld, trunk_number='%ld', trunk_member='%ld'"
													"WHERE session_id = '%s'",	                       
												msg->Fields[2].a.iVal,
												msg->Fields[4].a.iVal,
												msg->Fields[5].a.iVal,
												msg->Fields[1].a.szVal);

	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	return ret;
}

static int dblog_DataGroupInfo(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char sql[1024];
	int  sqlLen=0,ret;

	sqlLen = sprintf(sql, "INSERT INTO agent_trend (agent_group, trend_time, agent_reg, agent_active, agent_ready, agent_acw, agent_notready, agent_busy, agent_outbound) "
						  "VALUES (%ld, now(), %ld, %ld, %ld, %ld, %ld, %ld, %ld) ", 
												msg->Fields[0].a.iVal, msg->Fields[1].a.iVal, msg->Fields[2].a.iVal, msg->Fields[3].a.iVal,
												msg->Fields[4].a.iVal, msg->Fields[5].a.iVal, msg->Fields[6].a.iVal, msg->Fields[7].a.iVal);

	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	return ret;
}

/**
 AGENT DATA
 */
 
static int dblog_LogAgentHistory(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char 		sql[1024];
	int  		sqlLen,ret;

	/* update status sebelumnya */
	sqlLen = sprintf(sql, "UPDATE agent_activity_log"
							" SET end_time=from_unixtime(%ld), next_status=%ld, next_reason=%ld"		                      
							" WHERE agent = %ld and end_time is NULL",
							msg->Fields[4].a.iVal,
							msg->Fields[2].a.iVal,
							msg->Fields[3].a.iVal,
							msg->Fields[0].a.iVal);	
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);

	/* insert new record ke DB */
	sqlLen = sprintf(sql, "INSERT INTO agent_activity_log (agent, agent_group, ext_number, location, start_time, status, reason) "
						" VALUES (%ld, %ld, '%s', '%s', from_unixtime(%ld), %ld, %ld)", 
						  msg->Fields[0].a.iVal,
						  msg->Fields[1].a.iVal,
						  msg->Fields[5].a.szVal,
						  msg->Fields[6].a.szVal,
						  msg->Fields[4].a.iVal,
						  msg->Fields[2].a.iVal,
						  msg->Fields[3].a.iVal);
	


	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);

	return 0;
}

/**
 Agent Extension/Phone History
 	0 - agent_id
 	1 - group
	2 - agent->statusTime
	3 - agent->extNumber
	4 - agent->extIpAddress
	5 - agent->extStatus
 */
static int dblog_LogAgentExtHistory(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char 		sql[1024];
	int  		sqlLen,ret;
  
	/* Update agent ext status di table agent_activity */
	sqlLen = sprintf(sql, "UPDATE agent_activity SET ext_status=%ld WHERE agent = %ld",
		                    msg->Fields[5].a.iVal,
		                    msg->Fields[0].a.iVal);		
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);

	/* update status sebelumnya */
	sqlLen = sprintf(sql, "UPDATE phone_status_log SET end_time=from_unixtime(%ld), next_status=%ld"		                      
		                    " WHERE agent = %ld and end_time is NULL",
		                    msg->Fields[2].a.iVal,
		                    msg->Fields[5].a.iVal,
		                    msg->Fields[0].a.iVal);
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);	

	/* insert new record ke DB */
	sqlLen = sprintf(sql, "INSERT INTO phone_status_log (agent, agent_group, ext_number, location, start_time, status,session_id) "
  	                    " VALUES (%ld, %ld, '%s', '%s', from_unixtime(%ld), %ld,'%s')", 
	                      msg->Fields[0].a.iVal,
	                      msg->Fields[1].a.iVal,
	                      msg->Fields[3].a.szVal,
	                      msg->Fields[4].a.szVal,
	                      msg->Fields[2].a.iVal,
	                      msg->Fields[5].a.iVal,msg->Fields[6].a.szVal);
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	
	switch(msg->Fields[5].a.iVal){
		case ACD_PHONESTATUS_TALKING:
			sqlLen = sprintf(sql, "UPDATE call_session SET agent_ring = (now() - agent_ring_time) WHERE session_id = '%s'",
				msg->Fields[6].a.szVal);
			break;
		case ACD_PHONESTATUS_IDLE:	
			sqlLen = sprintf(sql, "UPDATE call_session SET agent_talk = (now() - agent_time) WHERE session_id = '%s'",
				msg->Fields[6].a.szVal);
			break;
	}
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	
  return 0;
}

/*
 AgentLogin
*/
static int dblog_DataAgentLogin(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char 		sql[1024];
	int  		sqlLen,ret;
	tDbSet  dbSet;

	/* periksa apakah data agent sudah ada di tabel agent_activity */  
	sqlLen = sprintf(sql, "SELECT id FROM agent_activity WHERE agent = %ld", msg->Fields[0].a.iVal);
	dbSet = dbLib->openQuery(pvt->dbConn, NULL, sql, sqlLen);
	if(dbLib->nextRow(dbSet)){  	
		/* sudah ada, update data */
		sqlLen = sprintf(sql, "UPDATE agent_activity SET agent_group=%ld, status=%ld, "
						  "status_time=from_unixtime(%ld), login_time=from_unixtime(%ld), "
						  "logout_time=NULL, ext_number='%s', location='%s', last_logout_time=login_time, "
						  "tot_ready_time=0, tot_notready_time=0, tot_acw_time=0 "
						  "WHERE agent = %ld", 
						  msg->Fields[1].a.iVal,
						  msg->Fields[2].a.iVal,
						  msg->Fields[4].a.iVal,
						  msg->Fields[4].a.iVal,
						  msg->Fields[5].a.szVal,
						  msg->Fields[6].a.szVal,
						  msg->Fields[0].a.iVal);
	}else{
	/* belum ada, insert data */
		sqlLen = sprintf(sql, "INSERT INTO agent_activity (agent, agent_group, status, status_time, login_time, ext_number, location) "
						  "VALUES (%ld, %ld, %ld, from_unixtime(%ld), from_unixtime(%ld), '%s', '%s')", 
						  msg->Fields[0].a.iVal,
						  msg->Fields[1].a.iVal,
						  msg->Fields[2].a.iVal,
						  msg->Fields[4].a.iVal,
						  msg->Fields[4].a.iVal,  	                      
						  msg->Fields[5].a.szVal,
						  msg->Fields[6].a.szVal
						  );
	}
	dbLib->closeQuery(dbSet);
	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	dblog_LogAgentHistory(pvt, msg);
	return ret;
}

/**
  Preq. : Record agent sudah ada di database
          Kalo cuma ganti status agent tidak mungkin berpindah tempat
 */
static int dblog_DataAgentStatus(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char 		sql[1024];
	char		subSql[256];
	int  		sqlLen,ret;

	/* prev status time */
	subSql[0] = '\0';
	switch(msg->Fields[7].a.iVal){
	case ACD_AGENTSTATUS_READY:
		sprintf(subSql,", tot_ready_time=tot_ready_time+%ld ", msg->Fields[4].a.iVal - msg->Fields[9].a.iVal);
		break;
	case ACD_AGENTSTATUS_NOTREADY:
		sprintf(subSql,", tot_notready_time=tot_notready_time+%ld ", msg->Fields[4].a.iVal - msg->Fields[9].a.iVal);
		break;
	case ACD_AGENTSTATUS_ACW:
		sprintf(subSql,", tot_acw_time=tot_acw_time+%ld ", msg->Fields[4].a.iVal - msg->Fields[9].a.iVal);
		break;
	default:
		sprintf(subSql,", tot_ready_time=tot_ready_time+%ld ", msg->Fields[4].a.iVal - msg->Fields[9].a.iVal);
	}

	sqlLen = sprintf(sql, "UPDATE agent_activity "
							" SET agent_group=%ld, status=%ld, status_time=from_unixtime(%ld), status_reason=%ld, tot_acd_call=%ld "
							" %s"
							" WHERE agent = %ld", 
							  msg->Fields[1].a.iVal,
						  msg->Fields[2].a.iVal,
						  msg->Fields[4].a.iVal,
						  msg->Fields[3].a.iVal,
						  msg->Fields[10].a.iVal,
						  subSql,
						  msg->Fields[0].a.iVal);

	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	dblog_LogAgentHistory(pvt, msg);
	return ret;
}

/* agent logout */
static int dblog_DataAgentLogout(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char 		sql[1024];
	int  		sqlLen,ret;
	

	sqlLen = sprintf(sql, "UPDATE agent_activity"
							" SET agent_group=0, status=0, status_time=from_unixtime(%ld), last_login_time=login_time, login_time=NULL,"
							"     ext_number='', location='0', logout_time=from_unixtime(%ld), "
							"     tot_ready_time=0, tot_notready_time=0, tot_acw_time=0 "
							" WHERE agent = %ld",
							msg->Fields[4].a.iVal,
							msg->Fields[4].a.iVal,
						msg->Fields[0].a.iVal);

	ret = dbLib->execQuery(pvt->dbConn, sql, sqlLen);	
	logger_Print(4,5,"ret=%d, DB>>%s\n",ret,sql);
	dblog_LogAgentHistory(pvt, msg);
	return ret;
}

static int dblog_DBPing(t_DBLogPvt *pvt, tCtbMessage	*msg){
	char 		sql[1024];
	int  		sqlLen;
	tDbSet  dbSet;

	sqlLen = sprintf(sql, "SELECT now()");
	dbSet = dbLib->openQuery(pvt->dbConn, NULL, sql, sqlLen);	
	dbLib->closeQuery(dbSet);

	return 0;
}

static int dblog_ProcessMessage(t_DBLogPvt *pvt, char *buf, int len){
	tCtbMessage	msg;

	memset(&msg, 0, sizeof(tCtbMessage));

	/* parse message */
	ctbMsgInit(&msg);
	ctbMsgDecode(&msg, buf, len);
	/* process message */
	switch (msg.Sender){
		case MSG_SRC_CTI:		// message from agent handler
			switch(msg.Type){
				case MSGTYPE_EVENT_CALLOFFERED:
					dblog_DataCallOffered(pvt, &msg);
					break;
				case MSGTYPE_EVENT_CALLALERTING:
					dblog_DataCallAlerting(pvt, &msg);
					dblog_stat_alerting(pvt, &msg);
					break;
				case MSGTYPE_EVENT_CALLCONNECTED:
					dblog_DataCallConnected(pvt, &msg);
					dblog_stat_connected(pvt, &msg);
					break;
				case MSGTYPE_EVENT_CALLDISCONNECTED:
					dblog_DataCallDisconnected(pvt, &msg);
					dblog_stat_disconnected(pvt, &msg);
					break;
				case MSGTYPE_EVENT_DISCONNECTED_QUEUE:
					dblog_stat_terminateOnQueue(pvt, &msg);
					break;
				case MSGTYPE_EVENT_QUEUED:
					dblog_DataCallQueued(pvt, &msg);
					dblog_stat_queued(pvt, &msg);
					break;
				case MSGTYPE_EVENT_CALLHELD:
					break;
				case MSGTYPE_EVENT_CALLRECONNECTED:
					break;
				case MSGTYPE_EVENT_CALLORIGINATED:
					dblog_DataCallOriginated(pvt, &msg);
					//dblog_stat_Originated(pvt, &msg);
					break;
				case MSGTYPE_EVENT_CALLINITIATED:
					dblog_DataCallInitiated(pvt, &msg);
					break;
				case MSGTYPE_EVENT_CALLTRUNKSEIZED:
					//dblog_DataCallTrunkSeized(pvt, &msg);
					break;
				case MSGTYPE_DBPING:
					logger_Print(4,2,"DB>>DBPING\n");
					dblog_DBPing(pvt, &msg);
					break;
			}
			break;
		case MSG_SRC_AGENTHANDLER:		// message from agent handler
			switch(msg.Type){
				case MSGTYPE_ACK_AGENTLOGIN:
					logger_Print(4,5,"DB>> Agent Login\n");
					dblog_DataAgentLogin(pvt, &msg);
					break;
				case MSGTYPE_REL_EXT:
					
					break;
				case MSGTYPE_DROP_CALL:
					
					break;
			}
			break;
		case MSG_SRC_CC_CCD:
			switch(msg.Type){
				case MSGTYPE_DATA_GROUPINFO:
					//printf("msgtype=%d\n",msg.Type);
					logger_Print(4,5,"DB>> Group Info data\n");
					dblog_DataGroupInfo(pvt, &msg);
					break;
				case MSGTYPE_DATA_AGENTLOGIN:
					logger_Print(4,5,"DB>> Agent Status Login\n");
					dblog_DataAgentLogin(pvt, &msg);
					break;
				case MSGTYPE_DATA_AGENTLOGOUT:
					logger_Print(4,5,"DB>> Agent Status Logout\n");
					dblog_DataAgentLogout(pvt, &msg);
					break;
				case MSGTYPE_DATA_AGENTSTATUS:
					logger_Print(4,5,"DB>> Agent Status Change\n");
					dblog_DataAgentStatus(pvt, &msg);
					break;
				case MSGTYPE_DATA_EXTSTATUS:
					logger_Print(4,5,"DB>> Agent Ext Status Change\n");
					dblog_LogAgentExtHistory(pvt, &msg);					
					
			}
			break;
		default:
			break;
	}	

	ctbMsgFree(&msg);
	return 0;
}

static void dblog_MainLoop(void *param){
	t_DBLogPvt *pvt;	
	char 	buf[1024];	
	int		len, msgLen;
	int   ret, nread, nreadsofar;
	//int dbmsg=0;
	
	pvt = (t_DBLogPvt*)param;
	logger_Print(4,5,"DB>> Thread started\n");	

	for(;;){
		// read header contain data len
		ret = ReadFile(pvt->hReader,  buf, 2, &nread, NULL);
		//read all data
		msgLen = (buf[0] << 8) + buf[1];
		nread  = 0;
		nreadsofar = 0;
		len    = msgLen;
		while(len > 0){
			ret = ReadFile(pvt->hReader,  buf+nreadsofar, len, &nread, NULL);
			nreadsofar += nread;
			len -= nread;
			nread = 0;
			//++dbmsg;
		}
			
    dblog_ProcessMessage(pvt, buf, msgLen);
	}
	logger_Print(4,5,"DB>> Thread Stopped\n");
}

int dblog_Load(){
	logger_Print(4,5,"DB>> Initilizing\n");
	DBLogPvt = dblog_Init();
	if(!DBLogPvt){
		logger_Print(4,5,"DB>> Initilizing Failed\n");
		return 0;
	}
	
	dblog_LoadConfig(DBLogPvt);
	
	/* create server thread */
	smctPthreadCreateDetached(&DBLogPvt->mainThreadId, NULL, dblog_MainLoop, (void*)DBLogPvt);
	logger_Print(4,5,"DB>> Creating main thread\n");
	return 0;
}

int dblog_PutMessage(char *msg, int len){
	int nWritten;
	char buflen[2];

	if (!DBLogPvt)
	return -1;

	// write header
	buflen[0] = (char)(len >> 8);
	buflen[1] = (char)(len & 0x000000ff);
	WriteFile(DBLogPvt->hWriter, buflen, 2, &nWritten, NULL);

	WriteFile(DBLogPvt->hWriter, msg, len, &nWritten, NULL);
	return 0;
}
