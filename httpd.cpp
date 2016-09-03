#if (defined _MSC_VER) /*|| (defined _VMS_WINDOWS_)*/
#pragma warning(disable: 4996)
#define __WSA_CALLS
#define getpid	GetCurrentProcessId
#endif

#include "socketdef.h"
#include "ver.h"
#include "httpdef.h"

#define FREE(p) ( p ? free(p), p = 0 : p )

char			gszParam[64] = {0};
int				gfTrace = 0;
long			gnLogLevel = 1;

static char		gszConfigFile[ 255 ] = {"httpd.conf"};
static char		gszPort[32] = {0};
static char		gszTimestamp[ 32 ];
static bool		gfShutdown = false;
static int		gnExitCode = 0;
static char		gszExitMessage[128] = { "" };
static int		gnPortNbr = 80;
static int		gnFrequency = 1*15;	// browse 15 seconds
static int		gnOsMaj = 0, gnOsMin = 0, gnOsRev = 0, gnOsX1 = 0, gnOsX2 = 0;
static int		gfCtrlC = 0;
static bool		gfGlobalShutdown = false;
static char		gszLogFilename[ 128 ] = {""};
static char		gszHost[64];
static char		gszServer[128];
static long		gcRequests = 0;
static char		gszHttpSiteName[128];
static char		gszHttpRoot[255] = ".";
static char		gszDefaultPage[ 64 ] = { "default.htm" };
static const char	gszNotFound[] = { "<html><body><h1>File Not Found</h1></body></html>" };
static const char	*gszContentType[] = { "text/html", "text/plain", "image/gif", "image/jpeg", "text/css", "image/png" };
static const char	*gszWeekday[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char	*gszMonth[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun"
							, "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

typedef struct tagCLIENTSOCKADDR {
	int					hSocket;
	SOCKADDR_IN			sockaddr;
} CLIENTSOCKADDR;

HTTPCMDINFO	httpcmds[] = {
		   cmdGET,  "GET", sizeof("GET")-1, 1
		 , cmdPOST, "POST", sizeof("POST")-1, 1
};

#define MAX_HTTPCMDS		(int)(sizeof(httpcmds)/sizeof(httpcmds[0]))
////////////////////////////////////////////////////////////////////////////
//
void print_syntax( void )
{
	printf( "\n"
			"syntax: httpd [-port Nbr] [-freq S] [-[no]trace] [-loglevel 1..N]\n"
			"              [-cfg config-file] [-default-page html-page]\n"
			"              [-wwwroot path] [-site-name name]\n"
		    "where\n"
			"\n"
			"port          tcp port to listen to (default is 80)\n"
			"freq          frequency in seconds to poll for Ctrl+C (default is 15sec)\n"
			"trace         if to print trace text to screen\n"
			"cfg           name the config-file (default httpd.conf)\n"
			"default-page  name of default web-page (default is default.htm)\n"
			"wwwroot       path of root to web-files. Do not end with slash/backslash\n"
			"site-name     name of Content-Location, like http://mysite.com\n"
			"\n"
			);
}
#if (defined __vms) || (defined __LINUX__)	/* OpenVMS or Linux*/
////////////////////////////////////////////////////////////////////////////
// OpenVMS and Linux are missing these C-Runtime functions
int vms_stricmp(const char *psz1, const char *psz2)
{
	char	ch1, ch2;
	int		nDiff = 'a' - 'A';

	/* while both not null */
	while (*psz1 && *psz2)
	{
		ch1 = *psz1;
		ch2 = *psz2;
		if (ch1 >= 'a' && ch1 <= 'z')
			ch1 -= nDiff;
		if (ch2 >= 'a' && ch2 <= 'z')
			ch2 -= nDiff;
		if (ch1 != ch2)
			return (int)(ch1 - ch2);
		psz1++;
		psz2++;
	}
	if (!*psz1 && *psz2)
		return (int)-(*psz2); /* string2 is greater */

	if (*psz1 && !*psz2)
		return (int)(*psz1); /* string1 is greater */

	return 0; /* is equal */
}
int vms_strnicmp(const char *psz1, const char *psz2, size_t cMatch)
{
	char	ch1, ch2;
	size_t	n = 0;
	int		nDiff = 'a' - 'A';

	/* while both not null */
	while (n <= cMatch && *psz1 && *psz2)
	{
		ch1 = *psz1;
		ch2 = *psz2;
		if (ch1 >= 'a' && ch1 <= 'z')
			ch1 -= nDiff;
		if (ch2 >= 'a' && ch2 <= 'z')
			ch2 -= nDiff;
		if (ch1 != ch2)
			return (int)(ch1 - ch2);
		n++;
		psz1++;
		psz2++;
	}
	if (n < cMatch)
	{
		if (!*psz1 && *psz2)
			return (int)-(*psz2); /* string2 is greater */

		if (*psz1 && !*psz2)
			return (int)(*psz1); /* string1 is greater */
	}
	return 0; /* is equal */
}
char * vms_strlwr(char *pszBuf)
{
	char	*pch = pszBuf;
	int		nDiff = 'a' - 'A';

	if (!pszBuf)
		return pszBuf;

	while (*pch)
	{
		if (*pch >= 'A' && *pch <= 'Z')
			*pch += nDiff;

		pch++;
	}
	return pszBuf;
}
char *vms_strupr(char *pszBuf)
{
	char	*pch = pszBuf;
	int		nDiff = 'a' - 'A';

	if (!pszBuf)
		return pszBuf;

	while (*pch)
	{
		if (*pch >= 'a' && *pch <= 'z')
			*pch -= nDiff;

		pch++;
	}
	return pszBuf;
}
#endif	// OpenVMS or Linux
/////////////////////////////////////////////////////////////////////
// 
int IsProcessShutdown( void )
{
	//printf( "IsProcessShutdown() = %d\n", (int)gfShutdown );
	if ( gfShutdown )
		 return 1;
	else return 0;
}
/////////////////////////////////////////////////////////////////////
// 
int SetProcessShutdownFlag( int nRc, const char *pszExitMessage )
{
	gfShutdown = true;
	gnExitCode = nRc;
	char *msg = (char*)"process exits";
	if ( pszExitMessage )
		msg = (char*)pszExitMessage;
	sprintf( gszExitMessage, "pid=0x%0X, rc=%d: %s"
			, getpid()
			, nRc, msg );
	return nRc;
}
////////////////////////////////////////////////////////////////////////////
//
void signal_handler( int sig )
{
	printf( "\nCtrl+C pressed - exiting (sig=%d)...\n", sig );
	SetProcessShutdownFlag( 1, (const char*)"Ctrl+C pressed" );
}
////////////////////////////////////////////////////////////////////////////
//
int ReportError( int errcode, int loglevel, const char *format, ... )
{
	va_list argList;
	va_start(argList, format);

	char	*pszCmd = (char*)malloc( 1024 );
	char	*pTail = pszCmd;
	char	chSev[3+1] = "ERR";	// loglevel == 1

	if ( loglevel == 2 )
		strcpy(chSev, "WRN" );
	else
	if ( loglevel >= 3 )
		strcpy(chSev, "INF" );

	pTail += sprintf( pszCmd, "%s-%08d: ", chSev, errcode );
	pTail += vsprintf( pTail, format, argList );

	printf( "%s\n", pszCmd );
	free( pszCmd );
	va_end(argList);

	return errcode;
}
////////////////////////////////////////////////////////////////////////////
//
int check_param( char *pszParam, char *pszValue )
{
	int	nRc = 1;

	if ( *pszParam == '-' || *pszParam == '/' )
		pszParam++;

	if ( !stricmp( pszParam, "trace" ) )
	{
		if ( pszValue )
			 gfTrace = ( atoi(pszValue) ? true : false );
		else gfTrace = true;
	}
	else
	if ( !stricmp( pszParam, "notrace" ) )
	{
		gfTrace = false;
	}
	else
	if ( !stricmp( pszParam, "port" ) && pszValue )
	{
		gnPortNbr = atoi( pszValue );
		sprintf( gszParam, "-port %d", gnPortNbr );
	}
	else
	if ( !stricmp( pszParam, "loglevel" ) && pszValue )
	{
		gnLogLevel = atoi( pszValue );
	}
	else
	if ( !stricmp( pszParam, "freq" ) && pszValue )
		gnFrequency = atoi( pszValue );
	else
	if ( !stricmp( pszParam, "cfg" ) && pszValue )
		strcpy( gszConfigFile, pszValue );
	else
	if ( !stricmp( pszParam, "default-page" ) && pszValue )
		strcpy( gszDefaultPage, pszValue );
	else
	if ( !stricmp( pszParam, "wwwroot" ) && pszValue )
		strcpy( gszHttpRoot, pszValue );
	else
	if ( !stricmp( pszParam, "site-name" ) && pszValue )
		strcpy( gszHttpSiteName, pszValue );

	else nRc = 0;

	return nRc;
}
////////////////////////////////////////////////////////////////////////////
//
int read_config( char *pszConfigFile )
{
	FILE	*fp;
	char	szBuf[256];
	char	*p, *pTmp;

#if defined(__vms)
	fp = fopen( pszConfigFile, "r", "shr=get,put,upd", "rat=cr", "rfm=var", "ctx=rec" );
#else
	fp = fopen( pszConfigFile, "rt");
#endif	
	if ( !fp ) 
		return 0;

	fgets( szBuf, sizeof(szBuf), fp );
	while( !feof(fp) )
	{
		p = strchr( szBuf, '=' );
		if ( !p || *szBuf == '#' ) // # is comment char (like makefiles)
		{
			fgets( szBuf, sizeof(szBuf), fp );
			continue;
		}

		pTmp = p-1;
		while( isspace(*pTmp) )
			*pTmp-- = 0;

		pTmp = p+1;
		while( isspace(*pTmp) )
			pTmp++;

		p = pTmp + strlen( pTmp )-1;
		while( isspace(*p) )
			*p-- = 0;
		check_param( szBuf, pTmp );

		fgets( szBuf, sizeof(szBuf), fp );
	}
	fclose(fp);

	return 1;
}
////////////////////////////////////////////////////////////////////////////
//
int global_init( int argc, char *argv[] )
{
	int		n;
#ifdef _MSC_VER
	char	szType[] = { "Windows" };
#elif (defined __vms) /* OpenVMS */
	char	szType[] = { "OpenVMS" };
#elif (defined __LINUX__) /* Linux/UNIX includes */
	char	szType[] = { "Linux" };
#else
	char	szType[] = { "Other OS" };
#endif

	printf( "%s, %s build, version %s\n"
			"%s\n"
		, VER_PRODUCTNAME, szType, VER_VERSION, VER_COPYRIGHT
		);

	sprintf( gszServer, "%s %s %s" 
			, VER_COMPANY, VER_PRODUCTNAME, VER_VERSION );

	if ( argc >= 2 && ( !strcmp( argv[1], "/?" ) || !strcmp( argv[1], "-?" )) )
	{
		print_syntax();
		exit(0);
	}

	SetProcessShutdownFlag( 0, 0 );
	gfShutdown = false;
	
#ifdef __WSA_CALLS
	// initialize WinSocket under Windows
    WSADATA     wsaData;
    int nRc = WSAStartup( WS_VERSION_REQD,&wsaData );
#endif

	signal( SIGINT, signal_handler );

	for( n = 1; n < argc; n++ )
	{
		if ( !stricmp( argv[n], "-cfg" ) && argc > n+1 )
		{
			check_param( argv[n], argv[n+1] );
			break;
		}
	}

	read_config( gszConfigFile );

	for( n = 1; n < argc; n++ )
	{
		if ( n+1 < argc )
		{
			if ( *argv[n+1] != '-' )
			{
				if ( check_param( argv[n], argv[n+1] ) )
					n++;
			}
			else check_param( argv[n], 0 );
		}
		else check_param( argv[n], 0 );

		if ( IsProcessShutdown() )
			break;
	}

	gethostname( gszHost, sizeof(gszHost)-1 );

	return 0;
}
////////////////////////////////////////////////////////////////////////////
//
int global_exit( void )
{
#ifdef __WSA_CALLS
	WSACleanup(); 
#endif
	return 1;
}
////////////////////////////////////////////////////////////////////////////
//
int socket_send(  SOCKET socket, const char *sendbuf, int sendsize )
{
    int				nRet;
	nRet = send( socket, sendbuf, sendsize, 0 );
	return nRet;
}
////////////////////////////////////////////////////////////////////////////
//
int socket_recv(  SOCKET socket, char *recvbuf, int *recvsize, int timeout )
{
    int				nRet;

	*recvbuf = 0;
	setsockopt( socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(int) );

	nRet = recv( socket, recvbuf, *recvsize, 0 );
	if ( nRet == SOCKET_ERROR )
		 *recvsize = 0;
	else *recvsize = nRet;
	return nRet;
}
////////////////////////////////////////////////////////////////////////////
//
int socket_transact(  SOCKET socket
				 , const char *sendbuf, int sendsize
				 , char *recvbuf, int *recvsize
				 , int timeout
				 )
{
    int				nRet;
	nRet = socket_send( socket, sendbuf, sendsize );
	if ( nRet != SOCKET_ERROR )
		nRet = socket_recv( socket, recvbuf, recvsize, timeout );
	return nRet;
}
/////////////////////////////////////////////////////////////////////
// 
CLIENTSOCKADDR *openClient( int socket, SOCKADDR_IN *psockaddr )
{
	CLIENTSOCKADDR *pClient = 0;
	pClient = (CLIENTSOCKADDR*)malloc( sizeof(CLIENTSOCKADDR) );
	if ( !pClient )
		return 0;

	memset( pClient, 0, sizeof(CLIENTSOCKADDR) );
	pClient->hSocket = socket;
	memcpy( &(pClient->sockaddr), psockaddr, sizeof(SOCKADDR_IN) );
	if ( gfTrace && gnLogLevel >= 3 )
		printf( "client %s connected on socket %d\n", inet_ntoa(psockaddr->sin_addr), socket );
	return pClient;
}
/////////////////////////////////////////////////////////////////////
// 
void closeClient( int socket, CLIENTSOCKADDR *pClient )
{
	if ( pClient )
	{
		pClient->hSocket = 0; // means it's now an empty slot
		if ( gfTrace && gnLogLevel >= 3 )
			printf("client %s disconnected from socket %d\n", inet_ntoa(pClient->sockaddr.sin_addr), socket);
	}
    closesocket( socket );
}
/////////////////////////////////////////////////////////////////////
// 
char HexToChar( const char *buf )
{
	char	ach[2];
	char	ch = ' ';

	ach[0] = *buf;
	ach[1] = *(buf+1);

	if ( ach[0] >= '0' && ach[0] <= '9' )
		ch = (ach[0] - '0') << 4;
	else
	{
		ach[0] = toupper(ach[0]);
		if ( ach[0] >= 'A' && ach[0] <= 'F' )
			ch = (ach[0] - 'A' + 0x10) << 4;
	}
	if ( ach[1] >= '0' && ach[1] <= '9' )
		ch |= (ach[1] - '0');
	else
	{
		ach[1] = toupper(ach[1]);
		if ( ach[1] >= 'A' && ach[1] <= 'F' )
			ch |= (ach[1] - 'A' + 10);
	}
	return ch;
}
/////////////////////////////////////////////////////////////////////
// 
char * WeekdayToString( struct tm *ptm)
{
	return (char*)gszWeekday[ ptm->tm_wday ];
}
char * MonthToString( struct tm *ptm )
{
	return (char*)gszMonth[ ptm->tm_mon ];
}
char *FileToContentType( const char *filename )
{
	char	*p = (char*)strrchr( filename, '.' );
	if ( !p )
		return (char*)gszContentType[0];

	p++;

	if ( !stricmp( p, "htm" ) || !stricmp( p, "html" ) || !stricmp( p, "shtml" ))
		return (char*)gszContentType[0];

	if ( !stricmp( p, "txt" ) )
		return (char*)gszContentType[1];

	if ( !stricmp( p, "gif" ) )
		return (char*)gszContentType[2];

	if ( !stricmp( p, "jpg" ) || !stricmp( p, "jpeg" ) )
		return (char*)gszContentType[3];
	
	if ( !stricmp( p, "css" ) )
		return (char*)gszContentType[4];

	if ( !stricmp( p, "png" ) )
		return (char*)gszContentType[5];

	return (char*)gszContentType[0];
}
/////////////////////////////////////////////////////////////////////
// 
bool SendHttpResponse( CLIENTSOCKADDR *pClient, char *buf
							, int http_status
							, char *filename
							, int nContentLength
							)
{
	int		cch, cchSent;

	time_t	t;
	struct tm *ptm;
	time( &t );
	ptm = localtime( &t );

	cch = sprintf( buf, "HTTP/1.1 %03d OK\n"
				  "Server: %s\n"
				  "Content-Location: %s%s\n"
				  "Date: %s, %02d %s %04d %02d:%02d:%02d GMT\n"
				  "Content-Type: %s\n"
				  "Content-Length: %d\n"
				  "\n"
			, http_status
			, gszServer
			, gszHttpSiteName, filename
			, WeekdayToString( ptm )
			, ptm->tm_mday, MonthToString( ptm ), ptm->tm_year+1900
			, ptm->tm_hour, ptm->tm_min, ptm->tm_sec
			, FileToContentType( filename )
			, nContentLength
			);
#if (defined _MSC_VER)
	char *p = buf;
	while( *p )
	{
		if ( *p == '\\' )
			*p = '/';
		p++;
	}
#endif
	cchSent = send( pClient->hSocket, buf, cch, 0 );

	return true;
}
/////////////////////////////////////////////////////////////////////
// 
bool OnGet( CLIENTSOCKADDR *pClient
			, char  *pRcv, int cbRcv
			, char  *pReply, int *pnReplySize
			, bool	*pfSendResponse
			)
{
	int		cchMaxSize = *pnReplySize;
	int		nRc = 1;
	char	szURI[1024];
	char	*pS, *pD;
	char	*pURI = pRcv+4;
	char	*pTail = pRcv+strlen(pRcv)-1;
	char	ach[2];
	int		cchSent = 1, cch = 0, cchBase;
	int		nContentLength;

	// don't respond since we do it here
	*pfSendResponse = false;

	strcpy( szURI, gszHttpRoot );
	cchBase = strlen(szURI);
	pD = szURI + cchBase;
	pS = pURI;
	if ( *pS != '/' )
	{
#if (defined _MSC_VER)
		*pD++ = '\\';
#else
		*pD++ = '/';
#endif
		cchBase++;
	}

	cch = cchBase;
	while( *pS != '?' && !isspace(*pS) && pS < pTail && cch < (int)sizeof(szURI)-1 )
	{
		if ( *pS == '/' )
#if (defined _MSC_VER)
			*pD++ = '\\';
#else
			*pD++ = '/';
#endif
		else
		if ( *pS == '%' )
		{
			pS++;
			ach[0] = *pS++;
			ach[1] = *pS;
			*pD++ = HexToChar( ach );
		}
		else
		if ( *pS != ':' && *pS > ' ' )
			*pD++ = *pS;

		pS++;
		cch++;
	}

#if (defined _MSC_VER)
	if ( *(pD-1) == '\\' )
#else
	if ( *(pD-1) == '/' )
#endif
	{
		strcpy( pD, gszDefaultPage );
		pD += strlen( gszDefaultPage );
	}
	*pD = 0;											

	FILE *fp = fopen( szURI, "rb" );
	if ( !fp )
	{
		if ( gfTrace && gnLogLevel >= 2 )
			printf( "404 (%s, socket %d) - %s\n", inet_ntoa(pClient->sockaddr.sin_addr), pClient->hSocket, szURI );
		nContentLength = strlen( gszNotFound );
		SendHttpResponse( pClient, pReply, 404, szURI+cchBase, nContentLength );
		cchSent = send( pClient->hSocket, gszNotFound, nContentLength, 0 );
		return true;
	}
	else
	{
		if ( gfTrace && gnLogLevel >= 2 )
			printf( "200 (%s, socket %d) - %s\n", inet_ntoa(pClient->sockaddr.sin_addr), pClient->hSocket, szURI );

		fseek( fp, 0, SEEK_END );
		nContentLength = ftell( fp );
		fseek( fp, 0, SEEK_SET );

		SendHttpResponse( pClient, pReply, 200, szURI+cchBase, nContentLength );
		while( !feof(fp) )
		{
			cch = fread( szURI, 1, sizeof(szURI), fp );
			cchSent = send( pClient->hSocket, szURI, cch, 0 );
		}

		fclose( fp );
	}
	return true;
}
/////////////////////////////////////////////////////////////////////
// 
bool OnReceive(   CLIENTSOCKADDR *pClient
				, char  *pRcv, int cbRcv
				, char  *pReply, int *pnReplySize
				, bool	*pfSendResponse
				)
{
	int			cchMaxSize = *pnReplySize;
	bool		fRc = true;
	int			n, nCmd = -1;

	*pfSendResponse = true;

	for( n = 0; n < MAX_HTTPCMDS; n++ )
	{
		if ( !strncmp( httpcmds[n].keyword, pRcv, httpcmds[n].length ) )
		{
			nCmd = n;
			break;
		}
	}
	if ( n >= MAX_HTTPCMDS || n != 0 )
	{
		*pnReplySize = sprintf( pReply, "HTTP/1.1 500 OK\r\n"  );
		return true;
	}


	// "GET /skut/ HTTP/1.1"
	fRc = OnGet( pClient, pRcv, cbRcv, pReply, pnReplySize, pfSendResponse );

	return fRc;
}
////////////////////////////////////////////////////////////////////////////
//
int getClientSockAddr( int hSocketClient, CLIENTSOCKADDR *pclients[], int count )
{
	int		n;
	for( n = 0; n < count; n++ )
	{
		if ( pclients[n] && pclients[n]->hSocket == hSocketClient )
			return n;
	}
	return -1;
}
////////////////////////////////////////////////////////////////////////////
//
CLIENTSOCKADDR *getClientSockAddrPtr( int hSocketClient, CLIENTSOCKADDR *pclients[], int count )
{
	int		n = getClientSockAddr( hSocketClient, pclients, count );
	if ( n == -1 )
		 return 0;
	else return pclients[n];
}
////////////////////////////////////////////////////////////////////////////
//
int addClientSockAddr( CLIENTSOCKADDR *pClient, CLIENTSOCKADDR *pclients[], int count )
{
	int		n;
	for( n = 0; n < count; n++ )
	{
		if ( !pclients[n] )
		{
			pclients[n] = pClient;
			return n;
		}
	}
	return -1;
}
////////////////////////////////////////////////////////////////////////////
//
bool TcpMonitorMain( int nPortNbr, int nFrequency )
{
	SOCKADDR_IN		sockaddr, sockaddr2;
	CLIENTSOCKADDR	*pasockaddr[ FD_SETSIZE ];
	CLIENTSOCKADDR	*pClient;
	int				nRcv, nLen, nReplySize, hSocketClient;
	int				allow_one_error = 0;
	bool			fOnRcv;
	unsigned long	nread = 1;
#ifdef __vms
	size_t			nLenVMS;
#endif
    fd_set			readfds, testfds;
    int				fd, n, result, fContinue = 1;
	struct timeval	tv;
	int				cchBufSize = 32767;
	unsigned long	*pdwSign;
	char			*pchReq;
	char			*pchResp;
	bool			fSendResponse;
	clock_t			cstart, cfinish;
	double			elapsed_time;

	if ( gfTrace )
		printf( "Initializing http listener on port %d...\n", gnPortNbr );

	pchReq = (char*)malloc(cchBufSize);
	memset( pchReq, 0, cchBufSize );
	pdwSign = (unsigned long*)pchReq;

	pchResp = (char*)malloc(cchBufSize);
	memset( pchResp, 0, cchBufSize );

	// --- tcp ---
    int	hSocketTcp = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr.sin_port = htons(nPortNbr);

    n = bind( hSocketTcp, (const struct sockaddr *)&sockaddr, sizeof(sockaddr));
	if ( n == SOCKET_ERROR )
	{
		closesocket( hSocketTcp );
		hSocketTcp = SOCKET_ERROR;
		printf( "bind() api failed\n" );
		return false;
	}

    listen( hSocketTcp, 5 );
	//
	memset( pasockaddr, 0, sizeof(pasockaddr) );
	memset( &readfds, 0, sizeof(readfds) );
	memset( &testfds, 0, sizeof(testfds) );
    FD_ZERO(&readfds);
    FD_SET( hSocketTcp, &readfds );

	////////////////////////////////////////////////////////////////////////
	//
	while( !IsProcessShutdown() )
	{
#ifdef __vms
		nLenVMS = sizeof(SOCKADDR_IN);
#else
		nLen = sizeof(SOCKADDR_IN);
#endif		
		memset( &sockaddr2, 0, sizeof(sockaddr2) );
        testfds = readfds;
		tv.tv_sec = nFrequency;
		tv.tv_usec = 0;

        result = select( FD_SETSIZE, &testfds, (fd_set *)0, (fd_set *)0, (struct timeval *) &tv);

		// socket error
		if ( result < 0 )
		{
			if (gfTrace && gnLogLevel >= 3)
				printf("select() err %d\n", result);
			if ( allow_one_error )
			{
				allow_one_error = 0;
				continue;
			}
			else break;
		}

		// timeout occured - loop again
		if ( result == 0 )
			continue;

		if ( IsProcessShutdown() )
			break;


#if (defined _MSC_VER) //|| (defined __vms)
        for(n = 0; n < (int)testfds.fd_count; n++)
		{
            if( testfds.fd_array[n] > 0 )
			{
				fd = testfds.fd_array[n];
#else
        for(fd = 0; fd < FD_SETSIZE; fd++)
		{
            if( FD_ISSET(fd,&testfds) )
			{
#endif
				// if server activity, then there is a new client connecting - accept it
                if( fd == hSocketTcp)
				{
#ifdef __vms	/* OpenVMS */
					nLenVMS = sizeof(sockaddr2);
					hSocketClient = accept( fd, (struct sockaddr *)&sockaddr2, (size_t*)&nLenVMS);
#else
					nLen = sizeof(sockaddr2);
					hSocketClient = accept( fd, (SOCKADDR *)&sockaddr2, (socklen_t*)&nLen);
#endif
					pClient = openClient( hSocketClient, &sockaddr2 );
					if ( pClient )
					{
						addClientSockAddr(pClient, pasockaddr, FD_SETSIZE);
					}
                    FD_SET( hSocketClient, &readfds );
				}
				else
				{
					// use ioctl function to see if we have a disconnect or read
                    ioctlsocket( fd, FIONREAD, &nread );
                    if(nread == 0)
					{
						pClient = getClientSockAddrPtr( fd, pasockaddr, FD_SETSIZE );
						closeClient( fd, pClient );
                        FD_CLR( fd, &readfds );
                    }
                    else
					{
						cstart = clock();
						gcRequests++;

						if ( gfTrace && gnLogLevel >= 3 )
							printf( "entering recv(%d)\n", fd );
						*pchReq = 0;
                        nRcv = recv( fd, pchReq, cchBufSize, 0 );
						if ( gfTrace && gnLogLevel >= 3 )
							printf( "recv()=%d\n", nRcv );
						if ( nRcv == SOCKET_ERROR)
							break;

						*(pchReq+nRcv) = 0;
						*(pchReq+cchBufSize-1) = 0;

						pClient = getClientSockAddrPtr( fd, pasockaddr, FD_SETSIZE );
						if ( !pClient )
						{
							if ( gfTrace && gnLogLevel >= 1 )
								printf( "unknown socket %d - closing it\n", fd );
							closesocket( fd );
							FD_CLR( fd, &readfds );
							continue; // skip the rest of it
						}

						if ( gfTrace && gnLogLevel >= 3 )
							printf( "packet from client %s on socket %d, %d bytes\n", inet_ntoa(pClient->sockaddr.sin_addr), pClient->hSocket, nRcv );

						fSendResponse = true;
						nReplySize = cchBufSize;

						if ( gfTrace && gnLogLevel >= 4 )
							printf( ">%*.*s", nRcv, nRcv, pchReq );

						fOnRcv = OnReceive( pClient, pchReq, nRcv, pchResp, &nReplySize, &fSendResponse );

						if ( gfTrace && gnLogLevel >= 4 )
							printf( "<%*.*s", nReplySize, nReplySize, pchResp );

						if ( fSendResponse )
							 nRcv = send( fd, pchResp, nReplySize, 0 );
						else FD_CLR( fd, &readfds );

						closesocket( fd );
						allow_one_error = 1;

						cfinish = clock();
						elapsed_time = (double)(cfinish - cstart) / CLOCKS_PER_SEC;
						if ( gfTrace && gnLogLevel >= 2 )
							printf( "Response time: %6.2lf sec(s)\n", elapsed_time );

					} // nread == 0
				} // if ( fd == m_hSocketTcp )
			} // if FD_ISSET
		} // for
	}	// while

#ifdef __WSA_CALLS
	nLen = WSAGetLastError();
#endif

	if ( hSocketTcp != SOCKET_ERROR )
	{
		shutdown( hSocketTcp, 3 );
		closesocket( hSocketTcp );
		hSocketTcp = SOCKET_ERROR;
	}

	free( pchReq );
	free( pchResp );
	if ( gfTrace && gnLogLevel >= 1 )
		printf( "Requests handled: %ld\n", gcRequests );
	return true;
}
////////////////////////////////////////////////////////////////////////////
//
static u_long GetHostAddress (char *pszHost)
{
	u_long lAddr = INADDR_ANY;
  
	if (*pszHost)
	{
		/* check for a dotted-IP address string */
		lAddr = inet_addr(pszHost);

		/* If not an address, then try to resolve it as a hostname */
		if ((lAddr == INADDR_NONE) && ( strcmp (pszHost, "255.255.255.255")))
		{
#ifndef __LINUX__
			struct hostent	*pHost = 0;

			if ( ( pHost = gethostbyname(pszHost) ) ) // hosts/DNS lookup
				 lAddr = *((u_long*)(pHost->h_addr));
			else lAddr = INADDR_ANY;   /* failure */
#else
			lAddr = INADDR_ANY;
#endif
		}
	}
	return lAddr; 
}
////////////////////////////////////////////////////////////////////////////
//
int main( int argc, char *argv[] )
{
	int		nRc = 0;

	global_init( argc, argv );

	if ( !gfShutdown )
	{

		nRc = TcpMonitorMain( gnPortNbr, gnFrequency );
	}

	global_exit();

	ReportError( gnExitCode, 3, gszExitMessage );
    exit(gnExitCode);
	return nRc;
}
