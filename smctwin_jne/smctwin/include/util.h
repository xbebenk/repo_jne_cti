/*
 Contain utility define and macro
 */

#ifndef __UtilH__
#define __UtilH__

/* Macro */
#define NewItem(x,type) \
  x = (type*)malloc(sizeof(type));\
       memset(x, 0, sizeof(type));
       
/* List */
#define ListDLInsertLast(first,last,item)\
	if (!first){\
		first = item;\
		last  = item;\
	}else{\
		last->Next = item;\
		item->Prev = last;\
		last       = item;\
	}


#endif//__UtilH__
