#ifndef HTTPDESPFS_H
#define HTTPDESPFS_H

#include "httpd.h"

int cgiEspFsHook(HttpdConnData *connData);
int   cgiEspFsTemplate(HttpdConnData *connData);

#endif