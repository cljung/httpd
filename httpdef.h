#ifndef _INC_HTTPDEF_FILE___
#define _INC_HTTPDEF_FILE___

typedef enum tagHTTPCMD {
		  cmdGET			= 1
		, cmdPOST			= 1
} HTTPCMD;

typedef struct tagHTTPCMDINFO {
	HTTPCMD		cmd;
	char		keyword[4+1];
	int			length;
	int			supported;
} HTTPCMDINFO;

#endif // _INC_HTTPDEF_FILE___
