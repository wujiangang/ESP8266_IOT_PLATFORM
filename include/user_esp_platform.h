#ifndef __USER_DEVICE_H__
#define __USER_DEVICE_H__

#ifdef CLIENT_SSL_ENABLE
#include "ssl/ssl_ssl.h"
#endif


/* NOTICE---this is for 512KB spi flash.
 * you can change to other sector if you use other size spi flash. */
#define ESP_PARAM_START_SEC     0x7D

#define packet_size   (2 * 1024)

#define token_size 41

struct esp_platform_saved_param {
    uint8 devkey[40];
    uint8 token[40];
    uint8 activeflag;
    uint8 tokenrdy;
    uint8 pad[2];
};

enum {
    DEVICE_GOT_IP=39,
    DEVICE_CONNECTING,
    DEVICE_ACTIVE_DONE,
    DEVICE_ACTIVE_FAIL,
    DEVICE_CONNECT_SERVER_FAIL
};

struct dhcp_client_info {
    ip_addr_t ip_addr;
    ip_addr_t netmask;
    ip_addr_t gw;
    uint8 flag;
    uint8 pad[3];
};

enum{
    AP_DISCONNECTED = 0,
    AP_CONNECTED,
    DNS_SUCESSES,
    DNS_FAIL,
};

struct client_conn_param {
    int32 sock_fd;
#ifdef CLIENT_SSL_ENABLE
    SSL *ssl;
    SSL_CTX *ssl_ctx;
#endif
};

void user_esp_platform_init(void);
sint8   user_esp_platform_deinit(void);

#endif
