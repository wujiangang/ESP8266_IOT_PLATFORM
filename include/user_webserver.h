#ifndef __USER_WEBSERVER_H__
#define __USER_WEBSERVER_H__

#include "user_config.h"

#if WEB_SERVICE
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "json/cJSON.h"

#include "user_iot_version.h"

#ifdef SERVER_SSL_ENABLE
#include "ssl/ssl_ssl.h"
#endif

#define WEB_SERVER_PORT 80
#define WEB_SERVER_SSL_PORT 443

#define URLSize 12

/*amx client number*/
#define MAX_CLIENT_NUMBER 2

#define STOP_TIMER 120000

typedef enum Result_Resp {
    RespFail = 0,
    RespSuc,
} Result_Resp;

typedef enum ProtocolType {
    GET = 0,
    POST,
} ProtocolType;

typedef enum _ParmType {
    SWITCH_STATUS = 0,
    INFOMATION,
    WIFI,
    SCAN,
    REBOOT,
    DEEP_SLEEP,
    LIGHT_STATUS,
    CONNECT_STATUS,
    USER_BIN
} ParmType;

typedef struct URL_Frame {
    enum ProtocolType Type;
    char pSelect[URLSize];
    char pCommand[URLSize];
    char pFilename[URLSize];
} URL_Frame;

typedef struct _rst_parm {
    struct conn_param *pconnpara;
    ParmType parmtype;
} rst_parm;


struct single_conn_param {
    int32 sock_fd;
    int32  timeout;
    os_timer_t stop_watch;
#ifdef SERVER_SSL_ENABLE
    SSL *ssl;
#endif
};

struct conn_param {
    int32 conn_num;
    struct single_conn_param **single_conn;
};

#define max(a,b) ((a)>(b)?(a):(b))  /**< Find the maximum of 2 numbers. */

void user_webserver_start(void);
sint8 user_webserver_stop(void);

#endif

#endif

