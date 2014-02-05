#ifndef _AppsvrListH_
#define _AppsvrListH_

#define listNewItem(type) \
  (type*)calloc(1, sizeof(type))

#define listInit(list) do{ 	\
	(list)->size = 0;					\
	(list)->first = NULL;			\
	(list)->last = NULL;			\
}while(0)

#define listInitWithLock(list) do{ 	\
	(list)->size = 0;					\
	(list)->first = NULL;			\
	(list)->last = NULL;			\
	pthread_mutex_init(&(list)->lock,  NULL); \
}while(0)

#define listInsertFirst(list, elmt) do{	\
	if (!(list)->first){										\
		(list)->first = (elmt);								\
		(list)->last  = (elmt);								\
		(list)->size  = 0;										\
	}else{																	\
		item->next    = (list)->first         \
		(list)->first->prev = (elmt);					\
		(list)->first = (elmt);								\
	}																				\
	++(list)->size;													\
}while(0)

#define listInsertLast(list, elmt) do{	\
	if (!(list)->first){									\
		(list)->first = (elmt);							\
		(list)->last  = (elmt);							\
		(list)->size  = 0;									\
	}else{																\
		(elmt)->prev    = (list)->last;	    \
		(list)->last->next = (elmt);				\
		(list)->last = (elmt);							\
	}																			\
	++(list)->size;												\
}while(0)

#define listInsertAfter(list, listelmt, elmt) do{ \
	(elmt)->next = (listelmt)->next;		\
	(elmt)->prev = (listelmt);					\
	if ((listelmt)->next)								\
		(listelmt)->next->prev = (elmt);	\
	else                                \
		(list)->last = (elmt);            \
	(listelmt)->next = (elmt);					\
	++(list)->size;											\
}while(0)

#define listInsertBefore(list, listelmt, elmt) do{ \
	(elmt)->next = (listelmt);					\
	(elmt)->prev = (listelmt)->prev;		\
	if ((listelmt)->prev)								\
		(listelmt)->prev->next = (elmt);	\
	else																\
		(list)->first = (elmt);           \
	(listelmt)->prev = (elmt);					\
	++(list)->size;											\
}while(0)

#define listRemove(list, elmt) do{ \
  if (!(elmt)->prev && !(elmt)->next){ \
    (list)->first = (list)->last = NULL; \
	}else if (!(elmt)->prev){ \
	  (elmt)->next->prev = NULL; \
	  (list)->first = (elmt)->next; \
	}else if (!(elmt)->next){ \
	  (elmt)->prev->next = NULL; \
	  (list)->last = (elmt)->prev; \
	}else{ \
	  (elmt)->prev->next = (elmt)->next; \
	  (elmt)->next->prev = (elmt)->prev; \
	} \
	--(list)->size;\
	elmt->next = NULL;\
	elmt->prev = NULL;\
}while(0)

#define listSize(list)	(list)->size
#define listFirst(list) (list)->first
#define listLast(list) (list)->last
#define listIsFirst(list, elmt) ((elmt) == (list)->first)
#define listIsLast(list, elmt) ((elmt) == (list)->last)
#define listNext(elmt) (elmt)->next
#define listPrev(elmt) (elmt)->prev

#define listLock(list) pthread_mutex_lock(&(list)->lock)
#define listUnlock(list) pthread_mutex_unlock(&(list)->lock)

#define listDestroy

#endif//_AppsvrListH_
