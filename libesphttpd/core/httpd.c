/*
Esp8266 http server - core routines
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */
//#include <esp8266.h>

#include "user_config.h"
#include "esp_common.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "lwip/sockets.h"
#include "cJSON.h"
#include "httpd.h"


//Max length of request head
#define MAX_HEAD_LEN 1024
//Max amount of connections
#define MAX_CONN 3
//Max post buffer len
#define MAX_POST 1024
//Max send buffer len
#define MAX_SENDBUFF_LEN 2048

#define STOP_TIMER 180000


#define max(a,b) ((a)>(b)?(a):(b))  /**< Find the maximum of 2 numbers. */

typedef void * xQueueHandle;

//LOCAL xQueueHandle QueueStop = NULL;
//LOCAL xQueueHandle RCVQueueStop = NULL;
LOCAL int httpd_server_port;

//This gets set at init time.
static HttpdBuiltInUrl *builtInUrls;

//Private data for http connection
struct HttpdPriv {
    char head[MAX_HEAD_LEN];
    int headPos;
    char *sendBuff;
    int sendBuffLen;
};

//Connection pool
static HttpdPriv connPrivData[MAX_CONN];
static HttpdConnData connData[MAX_CONN];
static HttpdPostData connPostData[MAX_CONN];
static single_conn_param sockData[MAX_CONN];

//Struct to keep extension->mime data in
typedef struct {
    const char *ext;
    const char *mimetype;
} MimeMap;

//The mappings from file extensions to mime types. If you need an extra mime type,
//add it here.
static const MimeMap mimeTypes[]={
    {"htm", "text/htm"},
    {"html", "text/html"},
    {"css", "text/css"},
    {"js", "text/javascript"},
    {"txt", "text/plain"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {NULL, "text/html"}, //default value
};

//Returns a static char* to a mime type for a given url to a file.
const char   *httpdGetMimetype(char *url) {
    int i=0;
    //Go find the extension
    char *ext=url+(strlen(url)-1);
    while (ext!=url && *ext!='.') ext--;
    if (*ext=='.') ext++;
    
    //ToDo: strcmp is case sensitive; we may want to do case-intensive matching here...
    while (mimeTypes[i].ext!=NULL && strcmp(ext, mimeTypes[i].ext)!=0) i++;
    return mimeTypes[i].mimetype;
}

//Looks up the connData info for a specific esp connection
static HttpdConnData   *httpdFindConnData(void *arg) {
    int i;
    for (i=0; i<MAX_CONN; i++) {
        if (connData[i].conn==(struct single_conn_param *)arg) return &connData[i];
    }
    //Shouldn't happen.
    printf("FindConnData: Huh? Couldn't find connection for %p\n", arg);
    return NULL;
}

/* for reference
static HttpdConnData ICACHE_FLASH_ATTR *httpdFindConnData(void *arg) {
	struct espconn *espconn = arg;
	for (int i=0; i<MAX_CONN; i++) {
		if (connData[i].remote_port == espconn->proto.tcp->remote_port &&
						memcmp(connData[i].remote_ip, espconn->proto.tcp->remote_ip, 4) == 0) {
			if (arg != connData[i].conn) connData[i].conn = arg; // yes, this happens!?
			return &connData[i];
		}
	}
	//Shouldn't happen.
	os_printf("*** Unknown connection 0x%p\n", arg);
	return NULL;
}
*/

//Retires a connection for re-use
static void   httpdRetireConn(HttpdConnData *conn) {
    if (conn->post->buff!=NULL) free(conn->post->buff);
    conn->post->buff=NULL;
    conn->cgi=NULL;
    conn->conn=NULL;
	conn->remote_port=0;
	conn->remote_ip=0;
}

//Stupid li'l helper function that returns the value of a hex char.
static int httpdHexVal(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='A' && c<='F') return c-'A'+10;
    if (c>='a' && c<='f') return c-'a'+10;
    return 0;
}

//Decode a percent-encoded value.
//Takes the valLen bytes stored in val, and converts it into at most retLen bytes that
//are stored in the ret buffer. Returns the actual amount of bytes used in ret. Also
//zero-terminates the ret buffer.
int httpdUrlDecode(char *val, int valLen, char *ret, int retLen) {
    int s=0, d=0;
    int esced=0, escVal=0;
    while (s<valLen && d<retLen) {
        if (esced==1)  {
            escVal=httpdHexVal(val[s])<<4;
            esced=2;
        } else if (esced==2) {
            escVal+=httpdHexVal(val[s]);
            ret[d++]=escVal;
            esced=0;
        } else if (val[s]=='%') {
            esced=1;
        } else if (val[s]=='+') {
            ret[d++]=' ';
        } else {
            ret[d++]=val[s];
        }
        s++;
    }
    if (d<retLen) ret[d]=0;
    return d;
}

//Find a specific arg in a string of get- or post-data.
//Line is the string of post/get-data, arg is the name of the value to find. The
//zero-terminated result is written in buff, with at most buffLen bytes used. The
//function returns the length of the result, or -1 if the value wasn't found. The 
//returned string will be urldecoded already.
int   httpdFindArg(char *line, char *arg, char *buff, int buffLen) {
    char *p, *e;
    if (line==NULL) return 0;
    p=line;
    while(p!=NULL && *p!='\n' && *p!='\r' && *p!=0) {
        printf("findArg: %s\n", p);
        if (strncmp(p, arg, strlen(arg))==0 && p[strlen(arg)]=='=') {
            p+=strlen(arg)+1; //move p to start of value
            e=(char*)strstr(p, "&");
            if (e==NULL) e=p+strlen(p);
            printf("findArg: val %s len %d\n", p, (e-p));
            return httpdUrlDecode(p, (e-p), buff, buffLen);
        }
        p=(char*)strstr(p, "&");
        if (p!=NULL) p+=1;
    }
    printf("Finding %s in %s: Not found :/\n", arg, line);
    return -1; //not found
}

//Get the value of a certain header in the HTTP client head
int   httpdGetHeader(HttpdConnData *conn, char *header, char *ret, int retLen) {
    char *p=conn->priv->head;
    p=p+strlen(p)+1; //skip GET/POST part
    p=p+strlen(p)+1; //skip HTTP part
    while (p<(conn->priv->head+conn->priv->headPos)) {
        while(*p<=32 && *p!=0) p++; //skip crap at start
        //See if this is the header
        if (strncmp(p, header, strlen(header))==0 && p[strlen(header)]==':') {
            //Skip 'key:' bit of header line
            p=p+strlen(header)+1;
            //Skip past spaces after the colon
            while(*p==' ') p++;
            //Copy from p to end
            while (*p!=0 && *p!='\r' && *p!='\n' && retLen>1) {
                *ret++=*p++;
                retLen--;
            }
            //Zero-terminate string
            *ret=0;
            //All done :)
            return 1;
        }
        p+=strlen(p)+1; //Skip past end of string and \0 terminator
    }
    return 0;
}

//Start the response headers.
void   httpdStartResponse(HttpdConnData *conn, int code) {
    char buff[96];
    int l;
    l=sprintf(buff, "HTTP/1.0 %d OK\r\nServer: esp8266-httpd/"HTTPDVER"\r\nConnection: close\r\n", code);
    httpdSend(conn, buff, l);
}

//Send a http header.
void   httpdHeader(HttpdConnData *conn, const char *field, const char *val) {
    char buff[96];
    int l;

    l=sprintf(buff, "%s: %s\r\n", field, val);
    httpdSend(conn, buff, l);
}

//Finish the headers.
void   httpdEndHeaders(HttpdConnData *conn) {
    httpdSend(conn, "\r\n", -1);
}

//ToDo: sprintf->snprintf everywhere... esp doesn't have snprintf tho' :/
//Redirect to the given URL.
void   httpdRedirect(HttpdConnData *conn, char *newUrl) {
    char *pbuf=(char*)zalloc(128+2*strlen(newUrl));
    int l;
    l=sprintf(pbuf, "HTTP/1.1 302 Found\r\nServer: esp8266-httpd/"HTTPDVER"\r\nConnection: close\r\nLocation: %s\r\n\r\nMoved to %s\r\n", newUrl, newUrl);
    httpdSend(conn, pbuf, l);
    if(pbuf){
        free(pbuf);
        pbuf=NULL;
    }
}

//Use this as a cgi function to redirect one url to another.
int   cgiRedirect(HttpdConnData *connData) {
    if (connData->conn==NULL) {
        //Connection aborted. Clean up.
        return HTTPD_CGI_DONE;
    }
    httpdRedirect(connData, (char*)connData->cgiArg);
    return HTTPD_CGI_DONE;
}

//This CGI function redirects to a fixed url of http://[hostname]/ if hostname field of request isn't
//already that hostname. Use this in combination with a DNS server that redirects everything to the
//ESP in order to load a HTML page as soon as a phone, tablet etc connects to the ESP. Watch out:
//this will also redirect connections when the ESP is in STA mode, potentially to a hostname that is not
//in the 'official' DNS and so will fail.
int   cgiRedirectToHostname(HttpdConnData *connData) {
	int isIP=0;
	int x;
	if (connData->conn==NULL) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}
	if (connData->hostName==NULL) {
		os_printf("Huh? No hostname.\n");
		return HTTPD_CGI_NOTFOUND;
	}

	//Quick and dirty code to see if host is an IP
	if (strlen(connData->hostName)>8) {
		isIP=1;
		for (x=0; x<strlen(connData->hostName); x++) {
			if (connData->hostName[x]!='.' && (connData->hostName[x]<'0' || connData->hostName[x]>'9')) isIP=0;
		}
	}
	if (isIP) return HTTPD_CGI_NOTFOUND;
	//Check hostname; pass on if the same
	if (strcmp(connData->hostName, (char*)connData->cgiArg)==0) return HTTPD_CGI_NOTFOUND;
    char *pbuf=(char*)zalloc(10+strlen(connData->cgiArg));
    //Not the same. Redirect to real hostname.
    sprintf(pbuf, "http://%s/", (char*)connData->cgiArg);
    httpdRedirect(connData, pbuf);

    if(pbuf){
        free(pbuf);
        pbuf=NULL;

    }
    return HTTPD_CGI_DONE;
}


//Same as above, but will only redirect clients with an IP that is in the range of
//the SoftAP interface. This should preclude clients connected to the STA interface
//to be redirected to nowhere.
int   cgiRedirectApClientToHostname(HttpdConnData *connData) {
//  uint32 *remadr;
    struct ip_info apip;
    int x=wifi_get_opmode();
    //Check if we have an softap interface; bail out if not
    if (x!=2 && x!=3) return HTTPD_CGI_NOTFOUND;


    wifi_get_ip_info(SOFTAP_IF, &apip);
    if ((connData->remote_ip & apip.netmask.addr) == (apip.ip.addr & apip.netmask.addr)) {
        return cgiRedirectToHostname(connData);
    } else {
        printf("cgiRedirectApClientToHostname notfound\n");
        /*
        struct sockaddr name;
        int len = sizeof(name);
        getpeername(connData->conn->sockfd, &name, (socklen_t *)&len);
        struct sockaddr_in *piname=(struct sockaddr_in *)&name;
        if ((piname->sin_addr.s_addr & apip.netmask.addr) == (apip.ip.addr & apip.netmask.addr)) {
            printf("cgiRedirectApClientToHostname LOGIC ERROR!\n");
        }
        */
        return HTTPD_CGI_NOTFOUND;
    }
}


//Add data to the send buffer. len is the length of the data. If len is -1
//the data is seen as a C-string.
//Returns 1 for success, 0 for out-of-memory.
int   httpdSend(HttpdConnData *conn, const char *data, int len) {
    if (len<0) len=strlen(data);
    if (conn->priv->sendBuffLen+len>MAX_SENDBUFF_LEN) return 0;
    memcpy(conn->priv->sendBuff+conn->priv->sendBuffLen, data, len);
    conn->priv->sendBuffLen+=len;
    return 1;
}

//Helper function to send any data in conn->priv->sendBuff
static void   xmitSendBuff(HttpdConnData *conn) {
    if (conn->priv->sendBuffLen!=0) {
        write(conn->conn->sockfd,(uint8_t*)conn->priv->sendBuff, conn->priv->sendBuffLen);
//        printf("xmit %dB\n",conn->priv->sendBuffLen);
        conn->priv->sendBuffLen=0;
    }
}

//Callback called when the data on a socket has been successfully  Jeremy
//sent.
static const char *httpNotFoundHeader="HTTP/1.0 404 Not Found\r\nServer: esp8266-httpd/"HTTPDVER"\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: 12\r\n\r\nNot Found.\r\n";

//This is called when the headers have been received and the connection is ready to send
//the result headers and data.
//We need to find the CGI function to call, call it, and dependent on what it returns either
//find the next cgi function, wait till the cgi data is sent or close up the connection.
static void   httpdProcessRequest(HttpdConnData *conn) {
    int r;
    int i=0;
    if (conn->url==NULL) {
        printf("WtF? url = NULL\n");
        
        conn->cgi=NULL; //mark for destruction
        conn->destruct_flg=TRUE;
        return; //Shouldn't happen
    }
    //See if we can find a CGI that's happy to handle the request.
    while (1) {
        //Look up URL in the built-in URL table.
        while (builtInUrls[i].url!=NULL) {
            int match=0;
            //See if there's a literal match
            if (strcmp(builtInUrls[i].url, conn->url)==0) match=1;
            //See if there's a wildcard match
            if (builtInUrls[i].url[strlen(builtInUrls[i].url)-1]=='*' &&
                    strncmp(builtInUrls[i].url, conn->url, strlen(builtInUrls[i].url)-1)==0) match=1;
            if (match) {
                //printf("Is url index %d, %s\n", i, builtInUrls[i].url);
                conn->cgiData=NULL;
                conn->cgi=builtInUrls[i].cgiCb;
                conn->cgiArg=builtInUrls[i].cgiArg;
                break;
            }
            i++;
        }
        if (builtInUrls[i].url==NULL) {
            //Drat, we're at the end of the URL table. This usually shouldn't happen. Well, just
            //generate a built-in 404 to handle this.
            printf("%s not found. 404!\n", conn->url);
            httpdSend(conn, httpNotFoundHeader, -1);
            xmitSendBuff(conn);
            conn->cgi=NULL; //mark for destruction
            conn->destruct_flg=TRUE;
            return;
        }
        
        //Okay, we have a CGI function that matches the URL. See if it wants to handle the
        //particular URL we're supposed to handle.
        r=conn->cgi(conn);
        if (r==HTTPD_CGI_MORE) {
            //Yep, it's happy to do so and has more data to send.
            xmitSendBuff(conn);
            //printf("more data to be sent\n");
            return;
        } else if (r==HTTPD_CGI_DONE) {
            //Yep, it's happy to do so and already is done sending data.
            xmitSendBuff(conn);
            conn->cgi=NULL; //mark conn for destruction
            conn->destruct_flg=TRUE;
            //printf("process over, mark for desttruction\n");
            return;
        } else if (r==HTTPD_CGI_NOTFOUND || r==HTTPD_CGI_AUTHENTICATED) {
            //URL doesn't want to handle the request: either the data isn't found or there's no
            //need to generate a login screen.
            i++; //look at next url the next iteration of the loop.
        }
    }
}

//Parse a line of header data and modify the connection data accordingly.
static void   httpdParseHeader(char *h, HttpdConnData *conn) {
    int i;
    char first_line = false;
    
    if (strncmp(h, "GET ", 4)==0) {
        conn->requestType = HTTPD_METHOD_GET;
        first_line = true;
    } else if (strncmp(h, "Host:", 5)==0) {
        i=5;
        while (h[i]==' ') i++;
        conn->hostName=&h[i];
    } else if (strncmp(h, "POST ", 5)==0) {
        conn->requestType = HTTPD_METHOD_POST;
        first_line = true;
    }

    if (first_line) {
        char *e;
        
        //Skip past the space after POST/GET
        i=0;
        while (h[i]!=' ') i++;
        conn->url=h+i+1;

        //Figure out end of url.
        e=(char*)strstr(conn->url, " ");
        if (e==NULL) return; //wtf?
        *e=0; //terminate url part

        //printf("URL = %s\n", conn->url);
        //Parse out the URL part before the GET parameters.
        conn->getArgs=(char*)strstr(conn->url, "?");
        if (conn->getArgs!=0) {
            *conn->getArgs=0;
            conn->getArgs++;
            printf("args = %s\n", conn->getArgs);
        } else {
            conn->getArgs=NULL;
        }

    } else if (strncmp(h, "Content-Length:", 15)==0) {
        i=15;
        //Skip trailing spaces
        while (h[i]==' ') i++;
        //Get POST data length
        conn->post->len=atoi(h+i);
        //printf("conn->post->len %d\n",conn->post->len);
        // Allocate the buffer
        if (conn->post->len > MAX_POST) {
            // we'll stream this in in chunks
            conn->post->buffSize = MAX_POST;
        } else {
            conn->post->buffSize = conn->post->len;
        }
        //printf("Mallocced buffer for %d + 1 bytes of post data.\n", conn->post->buffSize);
        conn->post->buff=(char*)malloc(conn->post->buffSize + 1);
        conn->post->buffLen=0;
    } else if (strncmp(h, "Content-Type: ", 14)==0) {
        if (strstr(h, "multipart/form-data")) {
            // It's multipart form data so let's pull out the boundary for future use
            char *b;
            if ((b = strstr(h, "boundary=")) != NULL) {
                conn->post->multipartBoundary = b + 7; // move the pointer 2 chars before boundary then fill them with dashes
                conn->post->multipartBoundary[0] = '-';
                conn->post->multipartBoundary[1] = '-';
            }
        }
    }
}


//Callback called when there's data available on a socket.
static void   httpdRecv(void *arg, char *data, unsigned short len) {
    int x;
    char *p, *e;
    char sendBuff[MAX_SENDBUFF_LEN];
    HttpdConnData *conn=httpdFindConnData(arg);
    if (conn==NULL) return;
    conn->priv->sendBuff=sendBuff;
    conn->priv->sendBuffLen=0;

    //This is slightly evil/dirty: we abuse conn->post->len as a state variable for where in the http communications we are:
    //<0 (-1): Post len unknown because we're still receiving headers
    //==0: No post data
    //>0: Need to receive post data
    //ToDo: See if we can use something more elegant for this.

    for (x=0; x<len; x++) {
        if (conn->post->len<0) {
            //This byte is a header byte.
            if (conn->priv->headPos!=MAX_HEAD_LEN) conn->priv->head[conn->priv->headPos++]=data[x];
            conn->priv->head[conn->priv->headPos]=0;
            //Scan for /r/n/r/n. Receiving this indicate the headers end.
            if (data[x]=='\n' && (char *)strstr(conn->priv->head, "\r\n\r\n")!=NULL) {
                //Indicate we're done with the headers.
                conn->post->len=0;
                //Reset url data
                conn->url=NULL;
                //Iterate over all received headers and parse them.
                p=conn->priv->head;
                while(p<(&conn->priv->head[conn->priv->headPos-4])) {
                    e=(char *)strstr(p, "\r\n"); //Find end of header line
                    if (e==NULL) break;         //Shouldn't happen.
                    e[0]=0;                     //Zero-terminate header
                    httpdParseHeader(p, conn);  //and parse it.
                    p=e+2;                      //Skip /r/n (now /0/n)
                }
                //If we don't need to receive post data, we can send the response now.
                if (conn->post->len==0) {
                    httpdProcessRequest(conn);
                }
            }
        } else if (conn->post->len!=0) {
            //This byte is a POST byte.
            conn->post->buff[conn->post->buffLen++]=data[x];
            conn->post->received++;
            conn->hostName=NULL;
            if (conn->post->buffLen >= conn->post->buffSize || conn->post->received == conn->post->len) {
                //Received a chunk of post data
                conn->post->buff[conn->post->buffLen]=0; //zero-terminate, in case the cgi handler knows it can use strings
                //Send the response.
                httpdProcessRequest(conn);
                conn->post->buffLen = 0;
            }
        }
    }
}

/******************************************************************************
 * FunctionName : webserver_conn_watcher
 * Description  : stop- timer handler;if keepalive usable,should change
 * Parameters   : index -- webserver connection index;
 * Returns      : none
*******************************************************************************/
LOCAL void httpserver_conn_watcher(struct single_conn_param * psingle_conn)
{
    os_timer_disarm(&psingle_conn->stop_watch);
    psingle_conn->timeout = 1;
    
    printf("httpdserver sock_fd %d timeout!\n",psingle_conn->sockfd);
}

/******************************************************************************
 * FunctionName : httpserver_task
 * Description  : parameter initialize as a server
 * Parameters   : null
 * Returns      : none
*******************************************************************************/
#define RECV_BUF_SIZE 2048
LOCAL void httpserver_task(void *pvParameters)
{
    int32 listenfd;
    int32 remotefd;
    int32 len;
    int32 ret;
    u8    index;
    
    u32 maxfdp = 0;
    fd_set readset,writeset;
    //struct timeval timeout;
    struct sockaddr_in server_addr;
    struct sockaddr_in remote_addr;

//    portBASE_TYPE xStatus;
//    bool ValueFromReceive = false;

    char *precvbuf = (char*)malloc(RECV_BUF_SIZE);
    if(NULL == precvbuf){
        printf("httpserver_task memory exhausted!!!\n");
    }
    
    
    /* Construct local address structure */
    memset(&server_addr, 0, sizeof(server_addr)); /* Zero out structure */
    server_addr.sin_family = AF_INET;            /* Internet address family */
    server_addr.sin_addr.s_addr = INADDR_ANY;   /* Any incoming interface */
    server_addr.sin_len = sizeof(server_addr);  
    server_addr.sin_port = htons(httpd_server_port); /* Local port */

    /* Create socket for incoming connections */
    do{
        listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd == -1) {
            printf("httpserver_task failed to create sock!\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
    }while(listenfd == -1);

    /* Bind to the local port */
    do{
        ret = bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret != 0) {
            printf("httpserver_task failed to bind!\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
    }while(ret != 0);

    do{
        /* Listen to the local connection */
        ret = listen(listenfd, MAX_CONN);
        if (ret != 0) {
            printf("httpserver_task failed to listen!\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
        
    }while(ret != 0);
    
    while(1){
        /*clear fdset, and set the selct function wait time*/
        maxfdp = 0;
        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        //timeout.tv_sec = 2;
        //timeout.tv_usec = 0;
        
        for(index=0; index < MAX_CONN; index++){
            //find all valid handle
            if (connData[index].conn!=NULL) {
                if(connData[index].conn->sockfd >= 0){
                    FD_SET(connData[index].conn->sockfd, &readset);
                    if(connData[index].cgi != NULL || connData[index].destruct_flg==TRUE) {
                        FD_SET(connData[index].conn->sockfd, &writeset);
                        //printf("index %d, sockfd %d to wselect\n",index, connData[index].conn->sockfd);
                    }else{
                        printf("index %d, sockfd %d, dummy?\n",index, connData[index].conn->sockfd);
                    }
                    maxfdp = max(connData[index].conn->sockfd, maxfdp);
        }}}

        /*add listenfd to readset*/
        FD_SET(listenfd, &readset);
        maxfdp = max(listenfd, maxfdp);

        //polling all exist client handle,wait until readable/writable
        ret = select(maxfdp+1, &readset, &writeset, NULL, NULL);//&timeout
        if(ret > 0){

            if (FD_ISSET(listenfd, &readset)){

                len = sizeof(struct sockaddr_in);
                remotefd = accept(listenfd, (struct sockaddr *)&remote_addr, (socklen_t *)&len);
                if(remotefd != -1) {
                    index=0;
                    //find the fisrt usable connection to save the handle.
                    for(index=0; index < MAX_CONN; index++){
                        if(connData[index].conn == NULL)break;
                    }
            
                    if(index < MAX_CONN){

                        int keepAlive = 1; //enable keepalive
                        int keepIdle = 60; //60s
                        int keepInterval = 5; //5s
                        int keepCount = 3; //retry times
                        
                        setsockopt(remotefd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepAlive, sizeof(keepAlive));//
                        setsockopt(remotefd, IPPROTO_TCP, TCP_KEEPIDLE, (void*)&keepIdle, sizeof(keepIdle));
                        setsockopt(remotefd, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&keepInterval, sizeof(keepInterval));
                        setsockopt(remotefd, IPPROTO_TCP, TCP_KEEPCNT, (void *)&keepCount, sizeof(keepCount));
                        
                        connData[index].priv=&connPrivData[index];
                        connData[index].priv->headPos=0;
                        connData[index].post=&connPostData[index];
                        connData[index].post->buff=NULL;
                        connData[index].post->buffLen=0;
                        connData[index].post->received=0;
                        connData[index].post->len=-1;
                        connData[index].hostName=NULL;
                        connData[index].conn =&sockData[index];
                        connData[index].conn->sockfd = remotefd;
                        connData[index].conn->timeout = 0;
                        connData[index].destruct_flg=FALSE;
                
                        struct sockaddr name;
                        int len = sizeof(name);
                        getpeername(connData->conn->sockfd, &name, (socklen_t *)&len);
                        struct sockaddr_in *piname=(struct sockaddr_in *)&name;
                        connData[index].remote_port = piname->sin_port;
                        connData[index].remote_ip=piname->sin_addr.s_addr;
                
                        os_timer_disarm(&connData[index].conn->stop_watch);
                        os_timer_setfn(&connData[index].conn->stop_watch, (os_timer_func_t *)httpserver_conn_watcher, connData[index].conn);
                        os_timer_arm(&connData[index].conn->stop_watch, STOP_TIMER, 0);
                        printf("httpserver acpt index:%d sockfd %d!\n",index,remotefd);
                    }else{
                        close(remotefd);
                        printf("httpserver overflow close %d!\n",remotefd);
                    }
        
                }else{
                    printf("http client error: %d, WARNING!\n",remotefd);
                }
            }
            
            for(index=0; index < MAX_CONN; index++){
               
                /* IF this handle there is data/event aviliable, recive it*/
                if (connData[index].conn == NULL) continue;
                if (FD_ISSET(connData[index].conn->sockfd, &readset)){

                    /*stop the sock handle watchout timer */
                    os_timer_disarm((os_timer_t *)&connData[index].conn->stop_watch);
                    memset(precvbuf, 0, RECV_BUF_SIZE);

                    ret=recv(connData[index].conn->sockfd,precvbuf,RECV_BUF_SIZE,0);
                    if(ret > 0){
/*
                        struct sockaddr name;
                        struct sockaddr_in *piname;
                        int len = sizeof(name);
                        getpeername(pconnections->single_conn[index]->sock_fd, &name, (socklen_t *)&len);
                        piname  = (struct sockaddr_in *)&name;
*/
                        printf("readable recv sockfd %d len=%d \n",connData[index].conn->sockfd,ret);
                        httpdRecv(connData[index].conn,precvbuf,ret);

                        /*restart the sock handle watchout timer */
                        os_timer_setfn((os_timer_t *)&connData[index].conn->stop_watch, (os_timer_func_t *)httpserver_conn_watcher, connData[index].conn);
                        os_timer_arm((os_timer_t *)&connData[index].conn->stop_watch, STOP_TIMER, 0);
                    }else{
                        //recv error,connection close
                        printf("readable recv sockfd %d ret=%d, close\n",connData[index].conn->sockfd,ret);
                        close(connData[index].conn->sockfd);
                        connData[index].conn->sockfd = -1;
                        connData[index].conn = NULL;// flag for cgi flush data
                        if (connData[index].cgi!=NULL) connData[index].cgi(&connData[index]); //flush cgi data
                        httpdRetireConn(&connData[index]);
                    }
                    
                }
                
                if (connData[index].conn == NULL) continue;
                if(FD_ISSET(connData[index].conn->sockfd, &writeset)){
                    /*stop the sock handle watchout timer */
                    os_timer_disarm((os_timer_t *)&connData[index].conn->stop_watch);
                    memset(precvbuf, 0, RECV_BUF_SIZE);

                    connData[index].priv->sendBuff=precvbuf;
                    connData[index].priv->sendBuffLen=0;

                    if (connData[index].destruct_flg==TRUE) { //Marked for destruction
                        printf("httpserver destruction close sockfd %d\n", connData[index].conn->sockfd);
                        close(connData[index].conn->sockfd);
                        connData[index].conn->sockfd = -1;
                        httpdRetireConn(&connData[index]);
                        
                    } else if(connData[index].cgi != NULL){
                        ret=connData[index].cgi(&connData[index]); //Execute cgi fn.
                        if (ret==HTTPD_CGI_DONE) {
                            connData[index].cgi=NULL; //mark for destruction.
                            connData[index].destruct_flg=TRUE;
                        }
                        if (ret==HTTPD_CGI_NOTFOUND || ret==HTTPD_CGI_AUTHENTICATED) {
                            printf("ERROR! CGI fn returns code %d after sending data! Bad CGI!\n", ret);
                            connData[index].cgi=NULL; //mark for destruction.
                            connData[index].destruct_flg=TRUE;
                        }
                        xmitSendBuff(&connData[index]);
                        
                        /*restart the sock handle watchout timer */
                        os_timer_setfn((os_timer_t *)&connData[index].conn->stop_watch, (os_timer_func_t *)httpserver_conn_watcher, connData[index].conn);
                        os_timer_arm((os_timer_t *)&connData[index].conn->stop_watch, STOP_TIMER, 0);
                    }
                }
                /* IF this handle there is no data/event aviliable, check the status*/
                if(connData[index].conn == NULL) continue;
                if(connData[index].conn->timeout == 1){
                    printf("httpserver close sockfd %d 4timeout!\n",connData[index].conn->sockfd);
                    close(connData[index].conn->sockfd);
                    connData[index].conn->sockfd = -1;
                    connData[index].conn = NULL; //mark for destruction.
                    if (connData[index].cgi!=NULL) {
                        connData[index].cgi(&connData[index]); //flush cgi data
                        printf("cgi!=null,should never happen\n");
                    }
                    httpdRetireConn(&connData[index]);
                }
            }

        }

#if 1        
        /*for develop test only*/
        //printf("httpserver_task %d words, heap %d bytes\n",(int)uxTaskGetStackHighWaterMark(NULL), system_get_free_heap_size());
#endif

    }

    /*release data connection*/
    for(index=0; index < MAX_CONN; index++){
        //find all valid handle 
        if(connData[index].conn == NULL) continue;
        if(connData[index].conn->sockfd >= 0){
            os_timer_disarm((os_timer_t *)&connData[index].conn->stop_watch);
            close(connData[index].conn->sockfd);
            connData[index].conn->sockfd = -1;
            connData[index].conn = NULL;
            if(connData[index].cgi!=NULL) connData[index].cgi(&connData[index]); //flush cgi data
            httpdRetireConn(&connData[index]);
        }
    }
    /*release listen socket*/
    close(listenfd);

    if(NULL != precvbuf){
        free(precvbuf);
    }
    vTaskDelete(NULL);
}

/******************************************************************************
 * FunctionName : user_webserver_start
 * Description  : Httpd initialization routine. Call this to kick off webserver functionality.
 * Parameters   : noe
 * Returns      : none
*******************************************************************************/
void   httpdInit(HttpdBuiltInUrl *fixedUrls, int port)
{
    int i;

    for (i=0; i<MAX_CONN; i++) {
        connData[i].conn=NULL;
    }
    
    builtInUrls = fixedUrls;
    httpd_server_port = port;
    
    xTaskCreate(httpserver_task, (const signed char *)"httpdserver", 1120, NULL, 4, NULL);
}

