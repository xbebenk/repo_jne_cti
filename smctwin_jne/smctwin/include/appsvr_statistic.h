#ifndef _AppsvrStatisticH_
#define _AppsvrStatisticH_

#define STATTYPE_CALL_OFFERED			1
#define STATTYPE_CALL_REJECTED		2
#define STATTYPE_CALL_ANSWERED		3
#define STATTYPE_CALL_ABANDONED		4

int statCallData(char *vdn, int stattype);

#define statCallOffered(vdn) statCallData(vdn, STATTYPE_CALL_OFFERED)
#define statCallRejected(vdn) statCallData(vdn, STATTYPE_CALL_REJECTED)
#define statCallAnswered(vdn) statCallData(vdn, STATTYPE_CALL_ANSWERED)
#define statCallAbandoned(vdn) statCallData(vdn, STATTYPE_CALL_ABANDONED)


void stat_Load();


#endif//_AppsvrStatisticH_
