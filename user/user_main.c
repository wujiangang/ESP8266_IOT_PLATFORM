/******************************************************************************
 * Copyright (C) 2014 -2016  Espressif System
 *
 * FileName: user_main.c
 *
 * Description: get and config the device timer
 *
 * Modification history:
 * 2015/7/1, v1.0 create this file.
*******************************************************************************/

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "user_config.h"
#include "user_devicefind.h"
#include "user_webserver.h"
#ifdef ESP_PLATFORM
#include "user_esp_platform.h"
#endif

#if HTTPD_SERVER
#include "espfsformat.h"
#include "espfs.h"
#include "captdns.h"
#include "httpd.h"
#include "cgiwifi.h"
#include "httpdespfs.h"
#include "user_cgi.h"
#include "webpages-espfs.h"

#ifdef ESPFS_HEATSHRINK
#include "heatshrink_config_custom.h"
#include "heatshrink_decoder.h"
#endif

#endif

#ifdef SERVER_SSL_ENABLE
#include "ssl/cert.h"
#include "ssl/private_key.h"
#else
    
#ifdef CLIENT_SSL_ENABLE
unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;
#endif
#endif

#if HTTPD_SERVER
HttpdBuiltInUrl builtInUrls[]={
	{"*", cgiRedirectApClientToHostname, "esp.nonet"},
	{"/", cgiRedirect, "/index.html"},
	{"/wifi", cgiRedirect, "/wifi/wifi.tpl"},
	{"/wifi/wifiscan.cgi", cgiWiFiScan, NULL},
	{"/wifi/wifi.tpl", cgiEspFsTemplate, tplWlan},
	{"/wifi/connect.cgi", cgiWiFiConnect, NULL},
	{"/wifi/connstatus.cgi", cgiWiFiConnStatus, NULL},
	{"/wifi/setmode.cgi", cgiWiFiSetMode, NULL},

	{"/config", cgiEspApi, NULL},
	{"/client", cgiEspApi, NULL},
	{"/upgrade", cgiEspApi, NULL},

	{"*", cgiEspFsHook, NULL}, //Catch-all cgi function for the filesystem
	{NULL, NULL, NULL} //end marker
};
#endif

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
    printf("SDK version:%s,%u\n", system_get_sdk_version(),__LINE__ );
    wifi_set_opmode(STATIONAP_MODE);
    
#if ESP_PLATFORM
    /*Initialization of the peripheral drivers*/
    /*For light demo , it is user_light_init();*/
    /* Also check whether assigned ip addr by the router.If so, connect to ESP-server  */
    user_esp_platform_init();
#endif

    /*Establish a udp socket to receive local device detect info.*/
    /*Listen to the port 1025, as well as udp broadcast.
    /*If receive a string of device_find_request, it rely its IP address and MAC.*/
    user_devicefind_start();
    
#if WEB_SERVICE
    /*Establish a TCP server for http(with JSON) POST or GET command to communicate with the device.*/
    /*You can find the command in "2B-SDK-Espressif IoT Demo.pdf" to see the details.*/
    user_webserver_start();

#elif HTTPD_SERVER
	/*Initialize DNS server for captive portal*/
	captdnsInit();

	/*Initialize espfs containing static webpages*/
    espFsInit((void*)(webpages_espfs_start));

	/*Initialize webserver*/
	httpdInit(builtInUrls, 80);
#endif

}


