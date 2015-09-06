#ifndef HTTPD_H
#define HTTPD_H

#define HTTPDVER "0.3"

#define HTTPD_CGI_MORE 0
#define HTTPD_CGI_DONE 1
#define HTTPD_CGI_NOTFOUND 2
#define HTTPD_CGI_AUTHENTICATED 3

#define HTTPD_METHOD_GET 1
#define HTTPD_METHOD_POST 2


typedef struct HttpdPriv HttpdPriv;
typedef struct HttpdConnData HttpdConnData;
typedef struct HttpdPostData HttpdPostData;
typedef struct single_conn_param single_conn_param;


typedef int (* cgiSendCallback)(HttpdConnData *connData);

struct single_conn_param {
    int32 sockfd;
    int32  timeout;
    os_timer_t stop_watch;
#ifdef CLIENT_SSL_ENABLE
    SSL *ssl;
#endif
};

//A struct describing a http connection. This gets passed to cgi functions.
struct HttpdConnData {
	struct single_conn_param *conn;
	char requestType;
	char *url;
	char *getArgs;
	const void *cgiArg;
	void *cgiData;
	void *cgiPrivData; // Used for streaming handlers storing state between requests
	char *hostName;
	HttpdPriv *priv;
	cgiSendCallback cgi;
	HttpdPostData *post;
	int remote_port;
	u32 remote_ip;
    char  destruct_flg;
};

//A struct describing the POST data sent inside the http connection.  This is used by the CGI functions
struct HttpdPostData {
	int len; // POST Content-Length
	int buffSize; // The maximum length of the post buffer
	int buffLen; // The amount of bytes in the current post buffer
	int received; // The total amount of bytes received so far
	char *buff; // Actual POST data buffer
	char *multipartBoundary;
};

//A struct describing an url. This is the main struct that's used to send different URL requests to
//different routines.
typedef struct {
	const char *url;
	cgiSendCallback cgiCb;
	const void *cgiArg;
} HttpdBuiltInUrl;

int   cgiRedirect(HttpdConnData *connData);
int   cgiRedirectToHostname(HttpdConnData *connData);
int   cgiRedirectApClientToHostname(HttpdConnData *connData);
void   httpdRedirect(HttpdConnData *conn, char *newUrl);
int httpdUrlDecode(char *val, int valLen, char *ret, int retLen);
int   httpdFindArg(char *line, char *arg, char *buff, int buffLen);
void   httpdInit(HttpdBuiltInUrl *fixedUrls, int port);
const char *httpdGetMimetype(char *url);
void   httpdStartResponse(HttpdConnData *conn, int code);
void   httpdHeader(HttpdConnData *conn, const char *field, const char *val);
void   httpdEndHeaders(HttpdConnData *conn);
int   httpdGetHeader(HttpdConnData *conn, char *header, char *ret, int retLen);
int   httpdSend(HttpdConnData *conn, const char *data, int len);

#endif
