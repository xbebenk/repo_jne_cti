#ifndef __SmctIniH__
#define __SmctIniH__

#define	INIFILE_PARSINGERROR		-2
#define	INIFILE_ERROR						-1
#define	INIFILE_PAIR						1
#define	INIFILE_SECTION					2
#define	INIFILE_COMMENT					3
#define	INIFILE_BLANK						4

typedef struct IniSectionItem{
	char									*KeyName;
	char									*KeyValue;
	struct IniSectionItem	*Next;
	struct IniSectionItem	*Prev;
}tIniSectionItem;

typedef struct IniSection{
	char 								*Name;
	int									ItemCount;
	tIniSectionItem			*FirstItem;
	tIniSectionItem			*LastItem;
	struct IniSection		*Next;
	struct IniSection		*Prev;
}tIniSection;

typedef struct{
	tIniSection	*FirstSection;
	tIniSection	*LastSection;
	int					SectionCount;
	char				Filename[1024];
}tSmctIni;

DllExport tSmctIni* smctIniLoad(tSmctIni *pIni, const char* Filename);
DllExport int 	smctIniFree(tSmctIni *pIni);
DllExport int 	smctIniSave(tSmctIni *pIni);
DllExport int 	smctIniPrint(tSmctIni *pIni);
DllExport char* smctIniReadString(tSmctIni *pIni, const char* Section, const char *Key, char *defval);
DllExport int 	smctIniReadInt(tSmctIni *pIni, const char* Section, const char *Key, int defval);
DllExport int 	smctIniReadBool(tSmctIni *pIni, const char* Section, const char *Key);
DllExport int		smctIniWriteString(tSmctIni *pIni, const char* Section, const char *Key, const char *value);
DllExport int 	smctIniWriteInt(tSmctIni *pIni, const char* Section, const char *Key, int value);
DllExport int 	smctIniDeleteSection(tSmctIni *pIni, const char* Section);
DllExport int 	smctIniDeleteKey(tSmctIni *pIni, const char* Section, const char *Key);
DllExport int 	smctIniSectionExist(tSmctIni *pIni, const char* Section);

#endif// __SmctIniH__
