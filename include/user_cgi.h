#ifndef __USER_CGI_H__
#define __USER_CGI_H__

#include "httpd.h"

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


typedef struct _rst_parm {
    ParmType parmtype;
} rst_parm;




int   cgiEspApi(HttpdConnData *connData);

#endif
