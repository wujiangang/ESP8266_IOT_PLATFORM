/******************************************************************************
 * Copyright (C) 2014 -2016  Espressif System
 *
 * FileName: user_upgrade.c
 *
 * Description: downlaod upgrade userbin file from upgrade server
 *
 * Modification history:
 * 2015/7/3, v1.0 create this file.
*******************************************************************************/
//#include "version.h"
#include "user_config.h"

#include "esp_common.h"
#include "lwip/mem.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "upgrade.h"
#include "ssl/ssl_ssl.h"


/*the size cannot be bigger than below*/
#define UPGRADE_DATA_SEG_LEN 1460
#define UPGRADE_RETRY_TIMES 30

LOCAL os_timer_t upgrade_10s;
LOCAL uint32 totallength = 0;
LOCAL uint32 sumlength = 0;
LOCAL BOOL flash_erased=0;

char *precv_buf=NULL;
os_timer_t upgrade_timer;
xTaskHandle *pxCreatedTask=NULL;

#ifdef UPGRADE_SSL_ENABLE
//#include "ssl/cert.h"
//#include "ssl/private_key.h"
unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;
#endif
/******************************************************************************
 * FunctionName : upgrade_deinit
 * Description  :
 * Parameters   :
 * Returns      : none
*******************************************************************************/
void  
LOCAL upgrade_deinit(void)
{
    if (system_upgrade_flag_check() != UPGRADE_FLAG_START) {
        system_upgrade_deinit();
        //system_upgrade_reboot();
    }
}

/******************************************************************************
 * FunctionName : upgrade_data_load
 * Description  : parse the data from server,send fw data to system interface 
 * Parameters   : pusrdata--data from server,
 *              : length--length of the pusrdata
 * Returns      : none
 *  
 * first data from server:
 * HTTP/1.1 200 OK
 * Server: nginx/1.6.2
 * Date: Tue, 14 Jul 2015 09:15:51 GMT
 * Content-Type: application/octet-stream
 * Content-Length: 282448
 * Connection: keep-alive
 * Content-Disposition: attachment;filename=user2.bin
 * Vary: Cookie
 * X-RateLimit-Remaining: 3599
 * X-RateLimit-Limit: 3600
 * X-RateLimit-Reset: 1436866251
*******************************************************************************/
BOOL upgrade_data_load(char *pusrdata, unsigned short length)
{
    char *ptr = NULL;
    char *ptmp2 = NULL;
    char lengthbuffer[32];

    
    if (totallength == 0 && (ptr = (char *)strstr(pusrdata, "\r\n\r\n")) != NULL &&
            (ptr = (char *)strstr(pusrdata, "Content-Length")) != NULL) {

        os_printf("\n pusrdata %s\n",pusrdata);

        ptr = (char *)strstr(pusrdata, "Content-Length: ");
        if (ptr != NULL) {
            ptr += 16;
            ptmp2 = (char *)strstr(ptr, "\r\n");

            if (ptmp2 != NULL) {
                memset(lengthbuffer, 0, sizeof(lengthbuffer));
                memcpy(lengthbuffer, ptr, ptmp2 - ptr);
                sumlength = atoi(lengthbuffer);
                os_printf("userbin sumlength:%d \n",sumlength);
                
                ptr = (char *)strstr(pusrdata, "\r\n\r\n");
                length -= ptr - pusrdata;
                length -= 4;
                totallength += length;

                /*at the begining of the upgrade,we get the sumlength 
                 *and erase all the target flash sectors,return false
                 *to close the connection, and start upgrade again.  
                 */
                if(FALSE==flash_erased){
                    flash_erased=system_upgrade(ptr + 4, sumlength);
                    return flash_erased;
                }else{
                    system_upgrade(ptr + 4, length);
                }
            } else {
                os_printf("ERROR:Get sumlength failed\n");
                return false;
            }
        } else {
            os_printf("ERROR:Get Content-Length failed\n");
            return false;
        }
        
    } 
    else {
        if(totallength != 0){
            totallength += length;
            
            if(totallength > sumlength){
                os_printf("strip the 400 error mesg\n");
                length =length -(totallength- sumlength);
            }
            
            os_printf(">>>recv %dB, %dB left\n",totallength,sumlength-totallength);
            system_upgrade(pusrdata, length);
            
        } else {
            os_printf("server response with something else,check it!\n");
            return false;
        }
    }

    return true;
}
#ifdef UPGRADE_SSL_ENABLE
/**
 * Display what session id we have.
 */
static void   display_session_id(SSL *ssl)
{
    int i;
    const uint8_t *session_id = ssl_get_session_id(ssl);
    int sess_id_size = ssl_get_session_id_size(ssl);

    if (sess_id_size > 0) {
        printf("-----BEGIN SSL SESSION PARAMETERS-----\n");

        for (i = 0; i < sess_id_size; i++) {
            printf("%02x", session_id[i]);
        }

        printf("\n-----END SSL SESSION PARAMETERS-----\n");
    }
}

/**
 * Display what cipher we are using
 */
static void   display_cipher(SSL *ssl)
{
    printf("CIPHER is ");

    switch (ssl_get_cipher_id(ssl)) {
        case SSL_AES128_SHA:
            printf("AES128-SHA");
            break;

        case SSL_AES256_SHA:
            printf("AES256-SHA");
            break;

        case SSL_RC4_128_SHA:
            printf("RC4-SHA");
            break;

        case SSL_RC4_128_MD5:
            printf("RC4-MD5");
            break;

        default:
            printf("Unknown - %d", ssl_get_cipher_id(ssl));
            break;
    }

    printf("\n");
}

/******************************************************************************
 * FunctionName : upgrade_task
 * Description  : task to connect with target server and get firmware data 
 * Parameters   : pvParameters--save the server address\port\request frame for
 *              : the upgrade server\call back functions to tell the userapp
 *              : the result of this upgrade task
 * Returns      : none
*******************************************************************************/
void upgrade_ssl_task(void *pvParameters)
{
    int recbytes;
    int sta_socket;
    int retry_count = 0;
    struct ip_info ipconfig;
    
    struct upgrade_server_info *server = pvParameters;

    flash_erased=FALSE;
    precv_buf = (char*)malloc(UPGRADE_DATA_SEG_LEN);//the max data length
    
    while (retry_count++ < UPGRADE_RETRY_TIMES) {
        
        wifi_get_ip_info(STATION_IF, &ipconfig);

        /* check the ip address or net connection state*/
        while (ipconfig.ip.addr == 0) {
            vTaskDelay(1000 / portTICK_RATE_MS);
            wifi_get_ip_info(STATION_IF, &ipconfig);
        }
        
        sta_socket = socket(PF_INET,SOCK_STREAM,0);
        if (-1 == sta_socket) {
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            os_printf("socket fail !\r\n");
            continue;
        }

        /*for upgrade connection debug*/
        //server->sockaddrin.sin_addr.s_addr= inet_addr("192.168.1.170");
        if(0 != connect(sta_socket,(struct sockaddr *)(&server->sockaddrin),sizeof(struct sockaddr))) {
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            os_printf("connect fail!\r\n");
            continue;
        }

        uint32_t options = SSL_SERVER_VERIFY_LATER | SSL_DISPLAY_CERTS | SSL_NO_DEFAULT_KEY;
        int i=0;
        int quiet = 0;
        int cert_index = 0, ca_cert_index = 0;
        int cert_size, ca_cert_size;
        char **ca_cert, **cert;
        SSL *ssl;
        SSL_CTX *ssl_ctx;
        uint8_t *read_buf = NULL;

        cert_size = ssl_get_config(SSL_MAX_CERT_CFG_OFFSET);
        ca_cert_size = ssl_get_config(SSL_MAX_CA_CERT_CFG_OFFSET);
        ca_cert = (char **)calloc(1, sizeof(char *)*ca_cert_size);
        cert = (char **)calloc(1, sizeof(char *)*cert_size);

        if ((ssl_ctx= ssl_ctx_new(options, SSL_DEFAULT_CLNT_SESS)) == NULL) {
            printf("Error: Client context is invalid\n");
            close(sta_socket);
            continue;
        }

        for (i = 0; i < cert_index; i++) {
            if (ssl_obj_load(ssl_ctx, SSL_OBJ_X509_CERT, cert[i], NULL)){
                printf("Certificate '%s' is undefined.\n", cert[i]);
            }
        }
        
        for (i = 0; i < ca_cert_index; i++) {
            if (ssl_obj_load(ssl_ctx, SSL_OBJ_X509_CACERT, ca_cert[i], NULL)){
                printf("Certificate '%s' is undefined.\n", ca_cert[i]);
            }
        }

        free(cert);
        free(ca_cert);

        ssl= ssl_client_new(ssl_ctx, sta_socket, NULL, 0);
        if (ssl == NULL){
            ssl_ctx_free(ssl_ctx);
            close(sta_socket);
            continue;
        }
        
        if(ssl_handshake_status(ssl) != SSL_OK){
            printf("client handshake fail.\n");
            ssl_free(ssl);
            ssl_ctx_free(ssl_ctx);
            close(sta_socket);
            continue;
        }
        
        //handshake sucesses,show cert and free x509_ctx here
        if (!quiet) {
            const char *common_name = ssl_get_cert_dn(ssl,SSL_X509_CERT_COMMON_NAME);
            if (common_name) {
                printf("Common Name:\t\t\t%s\n", common_name);
            }
            display_session_id(ssl);
            display_cipher(ssl);
            quiet = true;

            x509_free(ssl->x509_ctx);
            ssl->x509_ctx=NULL;
        }

        system_upgrade_init();
        system_upgrade_flag_set(UPGRADE_FLAG_START);

        if(ssl_write(ssl, server->url, strlen(server->url)+1) < 0) {
            ssl_free(ssl);
            ssl_ctx_free(ssl_ctx);
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            os_printf("send fail\n");
            continue;
        }
        os_printf("Request send success\n");

        while((recbytes = ssl_read(ssl, &read_buf)) >= 0) {

            if(recbytes == 0){
                vTaskDelay(500 / portTICK_RATE_MS);
                continue;
            }
            
            if(recbytes > UPGRADE_DATA_SEG_LEN) {
                ssl_free(ssl);
                ssl_ctx_free(ssl_ctx);
                close(sta_socket);
                vTaskDelay(2000 / portTICK_RATE_MS);
                printf("bigger than UPGRADE_DATA_SEG_LEN\n");
            }
            memcpy(precv_buf,read_buf,recbytes);

            if(FALSE==flash_erased){
                ssl_free(ssl);
                ssl_ctx_free(ssl_ctx);
                close(sta_socket);
                os_printf("pre erase flash!\n");
                upgrade_data_load(precv_buf,recbytes);
                break;
            }
            
            if(false == upgrade_data_load(read_buf,recbytes)) {
                os_printf("upgrade data error!\n");
                ssl_free(ssl);
                ssl_ctx_free(ssl_ctx);
                close(sta_socket);
                flash_erased=FALSE;
                vTaskDelay(1000 / portTICK_RATE_MS);
                break;
            }
            /*this two length data should be equal, if totallength is bigger, 
             *maybe data wrong or server send extra info, drop it anyway*/
            if(totallength >= sumlength) {
                os_printf("upgrade data load finish.\n");
                ssl_free(ssl);
                ssl_ctx_free(ssl_ctx);
                close(sta_socket);
                goto finish;
            }
            os_printf("upgrade_task %d word left\n",uxTaskGetStackHighWaterMark(NULL));
            
        }
        
        if(recbytes < 0) {
            os_printf("ERROR:read data fail! recbytes %d\r\n",recbytes);
            ssl_free(ssl);
            ssl_ctx_free(ssl_ctx);
            close(sta_socket);
            flash_erased=FALSE;
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
        
        os_printf("upgrade_task %d word left\n",uxTaskGetStackHighWaterMark(NULL));
        
        totallength =0;
        sumlength = 0;
    }
    
finish:

    os_timer_disarm(&upgrade_timer);

    totallength = 0;
    sumlength = 0;
    flash_erased=FALSE;
    free(precv_buf);
    
    if(retry_count == UPGRADE_RETRY_TIMES){
        /*retry too many times, fail*/
        server->upgrade_flag = false;
        system_upgrade_flag_set(UPGRADE_FLAG_IDLE);

    }else{
        server->upgrade_flag = true;
        system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
    }
    
    upgrade_deinit();
    
    os_printf("\n Exit upgrade task.\n");
    if (server->check_cb != NULL) {
        server->check_cb(server);
    }
    vTaskDelay(100 / portTICK_RATE_MS);
    vTaskDelete(NULL);
}
#else
/******************************************************************************
 * FunctionName : upgrade_task
 * Description  : task to connect with target server and get firmware data 
 * Parameters   : pvParameters--save the server address\port\request frame for
 *              : the upgrade server\call back functions to tell the userapp
 *              : the result of this upgrade task
 * Returns      : none
*******************************************************************************/
void upgrade_task(void *pvParameters)
{
    int recbytes;
    int sta_socket;
    int retry_count = 0;
    struct ip_info ipconfig;
    
    struct upgrade_server_info *server = pvParameters;

    flash_erased=FALSE;
    precv_buf = (char*)malloc(UPGRADE_DATA_SEG_LEN);
    if(NULL == precv_buf){
        os_printf("upgrade_task,memory exhausted, check it\n");
    }
    
    while (retry_count++ < UPGRADE_RETRY_TIMES) {
        
        wifi_get_ip_info(STATION_IF, &ipconfig);

        /* check the ip address or net connection state*/
        while (ipconfig.ip.addr == 0) {
            vTaskDelay(1000 / portTICK_RATE_MS);
            wifi_get_ip_info(STATION_IF, &ipconfig);
        }
        
        sta_socket = socket(PF_INET,SOCK_STREAM,0);
        if (-1 == sta_socket) {
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            os_printf("socket fail !\r\n");
            continue;
        }

        /*for upgrade connection debug*/
        //server->sockaddrin.sin_addr.s_addr= inet_addr("192.168.1.170");

        if(0 != connect(sta_socket,(struct sockaddr *)(&server->sockaddrin),sizeof(struct sockaddr))) {
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            os_printf("connect fail!\r\n");
            continue;
        }
        os_printf("Connect ok!\r\n");

        system_upgrade_init();
        system_upgrade_flag_set(UPGRADE_FLAG_START);

        if(write(sta_socket,server->url,strlen(server->url)+1) < 0) {
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            os_printf("send fail\n");
            continue;
        }
        os_printf("Request send success\n");

        while((recbytes = read(sta_socket, precv_buf, UPGRADE_DATA_SEG_LEN)) > 0) {

            if(FALSE==flash_erased){
                close(sta_socket);
                os_printf("pre erase flash!\n");
                upgrade_data_load(precv_buf,recbytes);
                break;                    
            }
            
            if(false == upgrade_data_load(precv_buf,recbytes)) {
                os_printf("upgrade data error!\n");
                close(sta_socket);
                flash_erased=FALSE;
                vTaskDelay(1000 / portTICK_RATE_MS);
                break;
            }
            /*this two length data should be equal, if totallength is bigger, 
             *maybe data wrong or server send extra info, drop it anyway*/
            if(totallength >= sumlength) {
                os_printf("upgrade data load finish.\n");
                close(sta_socket);
                goto finish;
            }

            os_printf("upgrade_task %d word left\n",uxTaskGetStackHighWaterMark(NULL));
            
        }
        
        if(recbytes <= 0) {
            close(sta_socket);
            flash_erased=FALSE;
            vTaskDelay(1000 / portTICK_RATE_MS);
            os_printf("ERROR:read data fail!\r\n");
        }

        totallength =0;
        sumlength = 0;
    }
    
finish:

    os_timer_disarm(&upgrade_timer);

    if(NULL != precv_buf) {
        free(precv_buf);
    }
    
    totallength = 0;
    sumlength = 0;
    flash_erased=FALSE;

    if(retry_count == UPGRADE_RETRY_TIMES){
        /*retry too many times, fail*/
        server->upgrade_flag = false;
        system_upgrade_flag_set(UPGRADE_FLAG_IDLE);

    }else{
        server->upgrade_flag = true;
        system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
    }
    
    upgrade_deinit();
    
    os_printf("\n Exit upgrade task.\n");
    if (server->check_cb != NULL) {
        server->check_cb(server);
    }
    vTaskDelay(100 / portTICK_RATE_MS);
    vTaskDelete(NULL);
}
#endif
/******************************************************************************
 * FunctionName : upgrade_check
 * Description  : check the upgrade process, if not finished in 300S,exit
 * Parameters   : pvParameters--save the server address\port\request frame for
 * Returns      : none
*******************************************************************************/
LOCAL void  
upgrade_check(struct upgrade_server_info *server)
{
    /*network not stable, upgrade data lost, this may be called*/
    vTaskDelete(pxCreatedTask);
    os_timer_disarm(&upgrade_timer);
    
    if(NULL != precv_buf) {
        free(precv_buf);
    }
    
    totallength = 0;
    sumlength = 0;
    flash_erased=FALSE;

    /*take too long to finish,fail*/
    server->upgrade_flag = false;
    system_upgrade_flag_set(UPGRADE_FLAG_IDLE);
    
    upgrade_deinit();
    
    os_printf("\n upgrade fail,exit.\n");
    if (server->check_cb != NULL) {
        server->check_cb(server);
    }

}

#ifdef UPGRADE_SSL_ENABLE
/******************************************************************************
 * FunctionName : system_upgrade_start_ssl
 * Description  : task to connect with target server and get firmware data 
 * Parameters   : pvParameters--save the server address\port\request frame for
 *              : the upgrade server\call back functions to tell the userapp
 *              : the result of this upgrade task
 * Returns      : true if task created successfully, false failed.
*******************************************************************************/
/******************************************************************************
 * NOTE:THE SYSTEM_UPGRADE_START_SSL TASK NEEDS 20K+ RAM SPACE, IT IS REALLY
 *      A BIG NUMBER. IF WANT TO RUN THIS TASK, YOU SHOULD MODIFY THE USER_CONFIG.H
 *      TO USE THE LIGHT WEB_SERVICE INSTEAD OF THE HTTPD SERVER TO SAVE MEMORY.
 *******************************************************************************/

BOOL  
system_upgrade_start_ssl(struct upgrade_server_info *server)
{
    portBASE_TYPE ret = 0;
    
    if(NULL == pxCreatedTask){
        ret = xTaskCreate(upgrade_ssl_task, "upgrade_task", 800, server, 5, pxCreatedTask);//1024, 890 left

        if(pdPASS == ret){
            os_timer_disarm(&upgrade_timer);
            os_timer_setfn(&upgrade_timer, (os_timer_func_t *)upgrade_check, server);
            os_timer_arm(&upgrade_timer, 1200000, 0);
        }
    }
 
    return(pdPASS == ret);
}
#else
/******************************************************************************
 * FunctionName : system_upgrade_start
 * Description  : task to connect with target server and get firmware data 
 * Parameters   : pvParameters--save the server address\port\request frame for
 *              : the upgrade server\call back functions to tell the userapp
 *              : the result of this upgrade task
 * Returns      : true if task created successfully, false failed.
*******************************************************************************/

BOOL  
system_upgrade_start(struct upgrade_server_info *server)
{
    portBASE_TYPE ret = 0;
    
    if(NULL == pxCreatedTask){
        ret = xTaskCreate(upgrade_task, "upgrade_task", 224, server, 5, pxCreatedTask);//1024, 890 left

        if(pdPASS == ret){
            os_timer_disarm(&upgrade_timer);
            os_timer_setfn(&upgrade_timer, (os_timer_func_t *)upgrade_check, server);
            os_timer_arm(&upgrade_timer, 1200000, 0);
        }
    }
 
    return(pdPASS == ret);
}
#endif

