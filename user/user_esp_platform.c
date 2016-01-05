/******************************************************************************
 * Copyright (C) 2014 -2016  Espressif System
 *
 * FileName: user_esp_platform.c
 *
 * Description: The client mode configration.
 *              Check your hardware connection with the host while use this mode.
 *
 * Modification history:
 * 2015/7/1, v1.0 create this file.
*******************************************************************************/
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/lwip/sys.h"
#include "lwip/lwip/ip_addr.h"
#include "lwip/lwip/netdb.h"
#include "lwip/lwip/sockets.h"
#include "lwip/lwip/err.h"


#include "user_iot_version.h"
#include "smartconfig.h"

#include "upgrade.h"

#include "user_esp_platform.h"

#if ESP_PLATFORM

//#define DEMO_TEST /*for test*/
#define DEBUG

#ifdef DEBUG
#define ESP_DBG os_printf
#else
#define ESP_DBG
#endif

#if PLUG_DEVICE
#include "user_plug.h"

#define RESPONSE_FRAME  "{\"status\": 200, \"datapoint\": {\"x\": %d}, \"nonce\": %d, \"deliver_to_device\": true}\n"
#define FIRST_FRAME     "{\"nonce\": %d, \"path\": \"/v1/device/identify\", \"method\": \"GET\",\"meta\": {\"Authorization\": \"token %s\"}}\n"
#endif

#if LIGHT_DEVICE
#include "user_light.h"

#define RESPONSE_FRAME  "{\"status\": 200,\"nonce\": %d, \"datapoint\": {\"x\": %d,\"y\": %d,\"z\": %d,\"k\": %d,\"l\": %d},\"deliver_to_device\":true}\n"
#define FIRST_FRAME     "{\"nonce\": %d, \"path\": \"/v1/device/identify\", \"method\": \"GET\",\"meta\": {\"Authorization\": \"token %s\"}}\n"
#endif

#if PLUG_DEVICE || LIGHT_DEVICE
#define BEACON_FRAME    "{\"path\": \"/v1/ping/\", \"method\": \"POST\",\"meta\": {\"Authorization\": \"token %s\"}}\n"
#define RPC_RESPONSE_FRAME  "{\"status\": 200, \"nonce\": %d, \"deliver_to_device\": true}\n"
#define TIMER_FRAME     "{\"body\": {}, \"get\":{\"is_humanize_format_simple\":\"true\"},\"meta\": {\"Authorization\": \"Token %s\"},\"path\": \"/v1/device/timers/\",\"post\":{},\"method\": \"GET\"}\n"
#define pheadbuffer "Connection: close\r\n\
Cache-Control: no-cache\r\n\
User-Agent: Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.1599.101 Safari/537.36 \r\n\
Accept: */*\r\n\
Authorization: token %s\r\n\
Accept-Encoding: gzip,deflate,sdch\r\n\
Accept-Language: zh-CN,zh;q=0.8\r\n\r\n"

LOCAL uint8 ping_status=TRUE;
#endif

#if SENSOR_DEVICE
#include "user_sensor.h"

#if HUMITURE_SUB_DEVICE
#define UPLOAD_FRAME  "{\"nonce\": %d, \"path\": \"/v1/datastreams/tem_hum/datapoint/\", \"method\": \"POST\", \
\"body\": {\"datapoint\": {\"x\": %s%d.%02d,\"y\": %d.%02d}}, \"meta\": {\"Authorization\": \"token %s\"}}\n"
#elif FLAMMABLE_GAS_SUB_DEVICE
#define UPLOAD_FRAME  "{\"nonce\": %d, \"path\": \"/v1/datastreams/flammable_gas/datapoint/\", \"method\": \"POST\", \
\"body\": {\"datapoint\": {\"x\": %d.%03d}}, \"meta\": {\"Authorization\": \"token %s\"}}\n"
#endif
LOCAL uint32 count = 0;
#endif

#define ACTIVE_FRAME    "{\"nonce\": %d,\"path\": \"/v1/device/activate/\", \"method\": \"POST\", \"body\": {\"encrypt_method\": \"PLAIN\", \"token\": \"%s\", \"bssid\": \""MACSTR"\",\"rom_version\":\"%s\"}, \"meta\": {\"Authorization\": \"token %s\"}}\n"
#define UPGRADE_FRAME  "{\"path\": \"/v1/messages/\", \"method\": \"POST\", \"meta\": {\"Authorization\": \"token %s\"},\
\"get\":{\"action\":\"%s\"},\"body\":{\"pre_rom_version\":\"%s\",\"rom_version\":\"%s\"}}\n"

#define REBOOT_MAGIC  (12345)

LOCAL int  user_boot_flag;

struct esp_platform_saved_param esp_param;

LOCAL uint8 device_status;

LOCAL uint32 active_nonce;
LOCAL struct rst_info rtc_info;

LOCAL uint8 iot_version[20];
LOCAL uint8 esp_domain_ip[4]= {115,29,202,58};

LOCAL ip_addr_t esp_server_ip;
LOCAL struct sockaddr_in remote_addr;
LOCAL struct client_conn_param client_param;
LOCAL char pusrdata[2048];

LOCAL xQueueHandle QueueStop = NULL;
LOCAL os_timer_t client_timer;

/******************************************************************************
 * FunctionName : smartconfig_done
 * Description  : callback function which be called during the samrtconfig process
 * Parameters   : status -- the samrtconfig status
 *                pdata --
 * Returns      : none
*******************************************************************************/
#if (PLUG_DEVICE || SENSOR_DEVICE)

void  
smartconfig_done(sc_status status, void *pdata)
{
    switch(status) {
        case SC_STATUS_WAIT:
            printf("SC_STATUS_WAIT\n");
            user_link_led_output(LED_1HZ);
            break;
        case SC_STATUS_FIND_CHANNEL:
            printf("SC_STATUS_FIND_CHANNEL\n");
            user_link_led_output(LED_1HZ);
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            printf("SC_STATUS_GETTING_SSID_PSWD\n");
            user_link_led_output(LED_20HZ);
            
            sc_type *type = pdata;
            if (*type == SC_TYPE_ESPTOUCH) {
                printf("SC_TYPE:SC_TYPE_ESPTOUCH\n");
            } else {
                printf("SC_TYPE:SC_TYPE_AIRKISS\n");
            }
            break;
        case SC_STATUS_LINK:
            printf("SC_STATUS_LINK\n");
            struct station_config *sta_conf = pdata;
    
            wifi_station_set_config(sta_conf);
            wifi_station_disconnect();
            wifi_station_connect();
            break;
        case SC_STATUS_LINK_OVER:
            printf("SC_STATUS_LINK_OVER\n");
            if (pdata != NULL) {
                uint8 phone_ip[4] = {0};
                memcpy(phone_ip, (uint8*)pdata, 4);
                printf("Phone ip: %d.%d.%d.%d\n",phone_ip[0],phone_ip[1],phone_ip[2],phone_ip[3]);
            }
            smartconfig_stop();
            
            user_link_led_output(LED_OFF);
            device_status = DEVICE_GOT_IP;
            break;
    }

}

#elif LIGHT_DEVICE

void  
smartconfig_done(sc_status status, void *pdata)
{
    switch(status) {
        case SC_STATUS_WAIT:
            printf("SC_STATUS_WAIT\n");
            break;
        case SC_STATUS_FIND_CHANNEL:
            printf("SC_STATUS_FIND_CHANNEL\n");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            printf("SC_STATUS_GETTING_SSID_PSWD\n");
            
            sc_type *type = pdata;
            if (*type == SC_TYPE_ESPTOUCH) {
                printf("SC_TYPE:SC_TYPE_ESPTOUCH\n");
            } else {
                printf("SC_TYPE:SC_TYPE_AIRKISS\n");
            }
            break;
        case SC_STATUS_LINK:
            printf("SC_STATUS_LINK\n");
            struct station_config *sta_conf = pdata;
    
            wifi_station_set_config(sta_conf);
            wifi_station_disconnect();
            wifi_station_connect();
            break;
        case SC_STATUS_LINK_OVER:
            printf("SC_STATUS_LINK_OVER\n");
            if (pdata != NULL) {
                uint8 phone_ip[4] = {0};
                memcpy(phone_ip, (uint8*)pdata, 4);
                printf("Phone ip: %d.%d.%d.%d\n",phone_ip[0],phone_ip[1],phone_ip[2],phone_ip[3]);
            }
            smartconfig_stop();
            
            device_status = DEVICE_GOT_IP;
            break;
    }

}
#endif

/******************************************************************************
 * FunctionName : smartconfig_task
 * Description  : start the samrtconfig proces and call back 
 * Parameters   : pvParameters
 * Returns      : none
*******************************************************************************/
void  
smartconfig_task(void *pvParameters)
{
    printf("smartconfig_task start\n");
    smartconfig_start(smartconfig_done);

    vTaskDelete(NULL);
}
/******************************************************************************
 * FunctionName : user_esp_platform_get_token
 * Description  : get the espressif's device token
 * Parameters   : token -- the parame point which write the flash
 * Returns      : none
*******************************************************************************/
void  
user_esp_platform_get_token(uint8_t *token)
{
    if (token == NULL) {
        return;
    }

    memcpy(token, esp_param.token, sizeof(esp_param.token));
}
/******************************************************************************
 * FunctionName : user_esp_platform_set_token
 * Description  : save the token for the espressif's device
 * Parameters   : token -- the parame point which write the flash
 * Returns      : none
*******************************************************************************/
void  
user_esp_platform_set_token(uint8_t *token)
{
    if (token == NULL) {
        return;
    }

    esp_param.activeflag = 0;
    esp_param.tokenrdy=1;
    
    if(strlen(token)<=40)
        memcpy(esp_param.token, token, strlen(token));
    else
        os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,strlen(token));

    system_param_save_with_protect(ESP_PARAM_START_SEC, &esp_param, sizeof(esp_param));
}

/******************************************************************************
 * FunctionName : user_esp_platform_set_active
 * Description  : set active flag
 * Parameters   : activeflag -- 0 or 1
 * Returns      : none
*******************************************************************************/
void  
user_esp_platform_set_active(uint8 activeflag)
{
    esp_param.activeflag = activeflag;
    if(0 == activeflag){esp_param.tokenrdy=0;}

    system_param_save_with_protect(ESP_PARAM_START_SEC, &esp_param, sizeof(esp_param));
}
/******************************************************************************
 * FunctionName : user_esp_platform_set_connect_status
 * Description  : set each connection step's status
 * Parameters   : none
 * Returns      : status
*******************************************************************************/

void  
user_esp_platform_set_connect_status(uint8 status)
{
    device_status = status;
}

/******************************************************************************
 * FunctionName : user_esp_platform_get_connect_status
 * Description  : get each connection step's status
 * Parameters   : none
 * Returns      : status
*******************************************************************************/
uint8  
user_esp_platform_get_connect_status(void)
{
    uint8 status = wifi_station_get_connect_status();

    if (status == STATION_GOT_IP) {
        status = (device_status == 0) ? DEVICE_CONNECTING : device_status;
    }

    ESP_DBG("status %d\n", status);
    return status;
}

/******************************************************************************
 * FunctionName : user_esp_platform_parse_nonce
 * Description  : parse the device nonce
 * Parameters   : pbuffer -- the recivce data point
 * Returns      : the nonce
*******************************************************************************/
int  
user_esp_platform_parse_nonce(char *pbuffer)
{
    char *pstr = NULL;
    char *pparse = NULL;
    char noncestr[11] = {0};
    int nonce = 0;
    pstr = (char *)strstr(pbuffer, "\"nonce\": ");

    if (pstr != NULL) {
        pstr += 9;
        pparse = (char *)strstr(pstr, ",");

        if (pparse != NULL) {

            if( (pparse - pstr)<=11)
                memcpy(noncestr, pstr, pparse - pstr);
            else
                os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,(pparse - pstr));
            
        } else {
            pparse = (char *)strstr(pstr, "}");

            if (pparse != NULL) {
                if( (pparse - pstr)<=11)
                    memcpy(noncestr, pstr, pparse - pstr);
                else
                    os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,(pparse - pstr));
            } else {
                pparse = (char *)strstr(pstr, "]");

                if (pparse != NULL) {
                    if( (pparse - pstr)<=11)
                        memcpy(noncestr, pstr, pparse - pstr);
                    else
                        os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,(pparse - pstr));
                } else {
                    return 0;
                }
            }
        }

        nonce = atoi(noncestr);
    }

    return nonce;
}

/******************************************************************************
 * FunctionName : user_esp_platform_get_info
 * Description  : get and update the espressif's device status
 * Parameters   : pespconn -- the espconn used to connect with host
 *                pbuffer -- prossing the data point
 * Returns      : none
*******************************************************************************/
void  
user_esp_platform_get_info( struct client_conn_param *pclient_param, uint8 *pbuffer)
{
    char *pbuf = NULL;
    int nonce = 0;

    pbuf = (char *)zalloc(packet_size);

    nonce = user_esp_platform_parse_nonce(pbuffer);

    if (pbuf != NULL) {
#if PLUG_DEVICE
        sprintf(pbuf, RESPONSE_FRAME, user_plug_get_status(), nonce);
#elif LIGHT_DEVICE
        uint32 white_val;
        white_val = (PWM_CHANNEL>LIGHT_COLD_WHITE?user_light_get_duty(LIGHT_COLD_WHITE):0);
        sprintf(pbuf, RESPONSE_FRAME, nonce, user_light_get_period(),
                   user_light_get_duty(LIGHT_RED), user_light_get_duty(LIGHT_GREEN),
                   user_light_get_duty(LIGHT_BLUE),white_val );
#endif

        ESP_DBG("%s\n", pbuf);
#ifdef CLIENT_SSL_ENABLE
        ssl_write(pclient_param->ssl, pbuf, strlen(pbuf));
#else
        write(pclient_param->sock_fd, pbuf, strlen(pbuf));
#endif
        free(pbuf);
        pbuf = NULL;
    }
}

/******************************************************************************
 * FunctionName : user_esp_platform_set_info
 * Description  : prossing the data and controling the espressif's device
 * Parameters   : sockfd -- the socket handle used to connect with host
 *                pbuffer -- prossing the data point
 * Returns      : none
*******************************************************************************/
void  
user_esp_platform_set_info( struct client_conn_param *pclient_param, uint8 *pbuffer)
{

#if PLUG_DEVICE
    char *pstr = NULL;
    pstr = (char *)strstr(pbuffer, "plug-status");

    if (pstr != NULL) {
        pstr = (char *)strstr(pbuffer, "body");

        if (pstr != NULL) {

            if (strncmp(pstr + 27, "1", 1) == 0) {
                user_plug_set_status(0x01);
            } else if (strncmp(pstr + 27, "0", 1) == 0) {
                user_plug_set_status(0x00);
            }
        }
    }

#elif LIGHT_DEVICE
    char *pstr = NULL;
    char *pdata = NULL;
    char *pbuf = NULL;
    char recvbuf[10];
    uint16 length = 0;
    uint32 data = 0;
    static uint32 rr,gg,bb,cw,ww,period;
    ww=0;
    cw=0;
    extern uint8 light_sleep_flg;
    pstr = (char *)strstr(pbuffer, "\"path\": \"/v1/datastreams/light/datapoint/\"");

    if (pstr != NULL) {
        pstr = (char *)strstr(pbuffer, "{\"datapoint\": ");

        if (pstr != NULL) {
            pbuf = (char *)strstr(pbuffer, "}}");
            length = pbuf - pstr;
            length += 2;
            pdata = (char *)zalloc(length + 1);
            memcpy(pdata, pstr, length);

            pstr = (char *)strchr(pdata, 'x');

            if (pstr != NULL) {
                pstr += 4;
                pbuf = (char *)strchr(pstr, ',');

                if (pbuf != NULL) {
                    length = pbuf - pstr;
                    memset(recvbuf, 0, 10);
                    memcpy(recvbuf, pstr, length);
                    data = atoi(recvbuf);
                    period = data;
                    //user_light_set_period(data);
                }
            }

            pstr = (char *)strchr(pdata, 'y');

            if (pstr != NULL) {
                pstr += 4;
                pbuf = (char *)strchr(pstr, ',');

                if (pbuf != NULL) {
                    length = pbuf - pstr;
                    memset(recvbuf, 0, 10);
                    
                    if( length<=10)
                        memcpy(recvbuf, pstr, length);
                    else
                        os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,length);
                    
                    data = atoi(recvbuf);
                    rr=data;
                    printf("r: %d\r\n",rr);
                    //user_light_set_duty(data, 0);
                }
            }

            pstr = (char *)strchr(pdata, 'z');

            if (pstr != NULL) {
                pstr += 4;
                pbuf = (char *)strchr(pstr, ',');

                if (pbuf != NULL) {
                    length = pbuf - pstr;
                    memset(recvbuf, 0, 10);
                    
                    if( length<=10)
                        memcpy(recvbuf, pstr, length);
                    else
                        os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,length);

                    data = atoi(recvbuf);
                    gg=data;
                    printf("g: %d\r\n",gg);
                    //user_light_set_duty(data, 1);
                }
            }

            pstr = (char *)strchr(pdata, 'k');

            if (pstr != NULL) {
                pstr += 4;;
                pbuf = (char *)strchr(pstr, ',');

                if (pbuf != NULL) {
                    length = pbuf - pstr;
                    memset(recvbuf, 0, 10);
                    if( length<=10)
                        memcpy(recvbuf, pstr, length);
                    else
                        os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,length);
                    data = atoi(recvbuf);
                    bb=data;
                    printf("b: %d\r\n",bb);
                    //user_light_set_duty(data, 2);
                }
            }

            pstr = (char *)strchr(pdata, 'l');

            if (pstr != NULL) {
                pstr += 4;;
                pbuf = (char *)strchr(pstr, ',');

                if (pbuf != NULL) {
                    length = pbuf - pstr;
                    memset(recvbuf, 0, 10);
                    if( length<=10)
                        memcpy(recvbuf, pstr, length);
                    else
                        os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,length);
                    data = atoi(recvbuf);
                    cw=data;
                    ww=data;
                    printf("cw: %d\r\n",cw);
                    printf("ww:%d\r\n",ww);   //chg
                    //user_light_set_duty(data, 2);
                }
            }

            free(pdata);
        }
    }
    
    if((rr|gg|bb|cw|ww) == 0){
        if(light_sleep_flg==0){

        }
        
    }else{
        if(light_sleep_flg==1){
            printf("modem sleep en\r\n");
            //wifi_set_sleep_type(MODEM_SLEEP_T); 
            light_sleep_flg =0;
        }
    }

    light_set_aim(rr,gg,bb,cw,ww,period);
//    user_light_restart();

#endif

    user_esp_platform_get_info(pclient_param, pbuffer);
}

/******************************************************************************
 * FunctionName : user_esp_platform_discon
 * Description  : A new incoming connection has been disconnected.
 * Parameters   : espconn -- the espconn used to disconnect with host
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_esp_platform_discon(struct client_conn_param* pclient_param)
{
    ESP_DBG("user_esp_platform_discon\n");

#if (PLUG_DEVICE || SENSOR_DEVICE)
    user_link_led_output(LED_OFF);
#endif

#ifdef CLIENT_SSL_ENABLE
    ssl_free(pclient_param->ssl);
    ssl_ctx_free(pclient_param->ssl_ctx);
    close(pclient_param->sock_fd);
#else
    close(pclient_param->sock_fd);
#endif
}

/******************************************************************************
 * FunctionName : user_esp_platform_sent
 * Description  : Processing the application data and sending it to the host
 * Parameters   : pespconn -- the espconn used to connetion with the host
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_esp_platform_sent(struct client_conn_param *pclient_param)
{
    uint8 devkey[token_size] = {0};
    uint32 nonce;
    char *pbuf = (char *)zalloc(packet_size);

    memcpy(devkey, esp_param.devkey, 40);

    if (esp_param.activeflag == 0xFF) {
        esp_param.activeflag = 0;
    }

    if (pbuf != NULL) {
        if (esp_param.activeflag == 0) {
            uint8 token[token_size] = {0};
            uint8 bssid[6];
            active_nonce = rand() & 0x7fffffff;

            memcpy(token, esp_param.token, 40);

            wifi_get_macaddr(STATION_IF, bssid);
            //Jeremy, set token directly for cloud test,
            sprintf(pbuf, ACTIVE_FRAME, active_nonce, token, MAC2STR(bssid),iot_version, devkey);// "451870eb31a466f16876606a6d3f250b429faf97"
        }

#if SENSOR_DEVICE
#if HUMITURE_SUB_DEVICE
        else {
#if 0
            uint16 tp, rh;
            uint8 data[4];

            if (user_mvh3004_read_th(data)) {
                rh = data[0] << 8 | data[1];
                tp = data[2] << 8 | data[3];
            }

#else
            uint16 tp, rh;
            uint8 *data;
            uint32 tp_t, rh_t;
            data = (uint8 *)user_mvh3004_get_poweron_th();

            rh = data[0] << 8 | data[1];
            tp = data[2] << 8 | data[3];
#endif
            tp_t = (tp >> 2) * 165 * 100 / (16384 - 1);
            rh_t = (rh & 0x3fff) * 100 * 100 / (16384 - 1);

            if (tp_t >= 4000) {
                sprintf(pbuf, UPLOAD_FRAME, count, "", tp_t / 100 - 40, tp_t % 100, rh_t / 100, rh_t % 100, devkey);
            } else {
                tp_t = 4000 - tp_t;
                sprintf(pbuf, UPLOAD_FRAME, count, "-", tp_t / 100, tp_t % 100, rh_t / 100, rh_t % 100, devkey);
            }
        }

#elif FLAMMABLE_GAS_SUB_DEVICE
        else {
            uint32 adc_value = system_adc_read();

            sprintf(pbuf, UPLOAD_FRAME, count, adc_value / 1024, adc_value * 1000 / 1024, devkey);
        }

#endif
#else
        else {
            nonce = rand() & 0x7fffffff;
            sprintf(pbuf, FIRST_FRAME, nonce , devkey);
        }

#endif
        ESP_DBG("%s\n", pbuf);

#ifdef CLIENT_SSL_ENABLE
        ssl_write(pclient_param->ssl, pbuf, strlen(pbuf));
#else
        write(pclient_param->sock_fd, pbuf, strlen(pbuf));
#endif

        free(pbuf);
    }
}

#if PLUG_DEVICE || LIGHT_DEVICE
/******************************************************************************
 * FunctionName : user_esp_platform_sent_beacon
 * Description  : sent beacon frame for connection with the host is activate
 * Parameters   : pespconn -- the espconn used to connetion with the host
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_esp_platform_sent_beacon(struct client_conn_param *pclient_param)
{

    if (esp_param.activeflag == 0) {
        ESP_DBG("please check device is activated.\n");
        user_esp_platform_sent(pclient_param);
    } else {
        uint8 devkey[token_size] = {0};
        memcpy(devkey, esp_param.devkey, 40);

        printf("sent_beacon %u\n", system_get_time());

        char *pbuf = (char *)zalloc(packet_size);

        if (pbuf != NULL) {
            sprintf(pbuf, BEACON_FRAME, devkey);

#ifdef CLIENT_SSL_ENABLE
            ssl_write(pclient_param->ssl, pbuf, strlen(pbuf));
#else
            write(pclient_param->sock_fd, pbuf, strlen(pbuf));
#endif
            free(pbuf);
        }
    }
    
}

/******************************************************************************
 * FunctionName : user_platform_rpc_set_rsp
 * Description  : response the message to server to show setting info is received
 * Parameters   : pespconn -- the espconn used to connetion with the host
 *                nonce -- mark the message received from server
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_platform_rpc_set_rsp(struct client_conn_param *pclient_param, int nonce)
{
    char *pbuf = (char *)zalloc(packet_size);

    if (pclient_param->sock_fd< 0) {
        return;
    }

    sprintf(pbuf, RPC_RESPONSE_FRAME, nonce);
    ESP_DBG("%s\n", pbuf);
#ifdef CLIENT_SSL_ENABLE
    ssl_write(pclient_param->ssl, pbuf, strlen(pbuf));
#else
    write(pclient_param->sock_fd, pbuf, strlen(pbuf));
#endif
    free(pbuf);
}

/******************************************************************************
 * FunctionName : user_platform_timer_get
 * Description  : get the timers from server
 * Parameters   : pespconn -- the espconn used to connetion with the host
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_platform_timer_get(struct client_conn_param *pclient_param)
{
    uint8 devkey[token_size] = {0};
    char *pbuf = (char *)zalloc(packet_size);
    memcpy(devkey, esp_param.devkey, 40);

    sprintf(pbuf, TIMER_FRAME, devkey);
    ESP_DBG("%s\n", pbuf);
#ifdef CLIENT_SSL_ENABLE
    ssl_write(pclient_param->ssl, pbuf, strlen(pbuf));
#else
    write(pclient_param->sock_fd, pbuf, strlen(pbuf));
#endif
    free(pbuf);
}

/******************************************************************************
 * FunctionName : user_esp_platform_upgrade_cb
 * Description  : Processing the downloaded data from the server
 * Parameters   : pespconn -- the espconn used to connetion with the host
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_esp_platform_upgrade_rsp(void *arg)
{
    uint8 devkey[41] = {0};
    uint8 *pbuf = NULL;
    char *action = NULL;
    struct upgrade_server_info *server = arg;
    struct client_conn_param *pclient=(struct client_conn_param*)server->pclient_param;

    memcpy(devkey, esp_param.devkey, 40);
    pbuf = (char *)zalloc(packet_size);

    if (server->upgrade_flag == true) {
        printf("upgarde_successfully\n");
        action = "device_upgrade_success";
        sprintf(pbuf, UPGRADE_FRAME, devkey, action, server->pre_version, server->upgrade_version);
        ESP_DBG("%s\n",pbuf);

#ifdef CLIENT_SSL_ENABLE
        ssl_write(pclient->ssl, pbuf, strlen(pbuf));
#else
        write(pclient->sock_fd, pbuf, strlen(pbuf));
#endif

        if (pbuf != NULL) {
            free(pbuf);
            pbuf = NULL;
        }
    } else {
        printf("upgrade_failed\n");
        action = "device_upgrade_failed";
        sprintf(pbuf, UPGRADE_FRAME, devkey, action,server->pre_version, server->upgrade_version);
        ESP_DBG("%s\n",pbuf);

#ifdef CLIENT_SSL_ENABLE
        ssl_write(pclient->ssl, pbuf, strlen(pbuf));
#else
        write(pclient->sock_fd, pbuf, strlen(pbuf));
#endif

        if (pbuf != NULL) {
            free(pbuf);
            pbuf = NULL;
        }
    }

    if(server != NULL){
        free(server->url);
        server->url = NULL;
        free(server);
        server = NULL;
    }
}

/******************************************************************************
 * FunctionName : user_esp_platform_upgrade_begin
 * Description  : Processing the received data from the server
 * Parameters   : pespconn -- the espconn used to connetion with the host
 *                server -- upgrade param
 * Returns      : none
*******************************************************************************/

LOCAL void  
user_esp_platform_upgrade_begin(struct client_conn_param *pclient_param, struct upgrade_server_info *server)
{
    uint8 user_bin[10] = {0};
    uint8 devkey[41] = {0};

    server->pclient_param=(void*)pclient_param;

    memcpy(devkey, esp_param.devkey, 40);

    struct sockaddr iname;
    struct sockaddr_in *piname= (struct sockaddr_in *)&iname;
    int len = sizeof(iname);
    getpeername(pclient_param->sock_fd, &iname, (socklen_t *)&len);
    
    bzero(&server->sockaddrin,sizeof(struct sockaddr_in));
    
    server->sockaddrin.sin_family = AF_INET;
    server->sockaddrin.sin_addr= piname->sin_addr;
#ifdef UPGRADE_SSL_ENABLE
    server->sockaddrin.sin_port = htons(443);
#else
    server->sockaddrin.sin_port = htons(80);
#endif

    server->check_cb = user_esp_platform_upgrade_rsp;
    //server->check_times = 120000;/*rsp once finished*/

    if (server->url == NULL) {
        server->url = (uint8 *)zalloc(512);
    }

    if (system_upgrade_userbin_check() == UPGRADE_FW_BIN1) {
        memcpy(user_bin, "user2.bin", 10);
    } else if (system_upgrade_userbin_check() == UPGRADE_FW_BIN2) {
        memcpy(user_bin, "user1.bin", 10);
    }

    sprintf(server->url, "GET /v1/device/rom/?action=download_rom&version=%s&filename=%s HTTP/1.0\r\nHost: "ESP_DOMAIN":%d\r\n"pheadbuffer"",
               server->upgrade_version, user_bin,ntohs(server->sockaddrin.sin_port), devkey);//  IPSTR  IP2STR(server->sockaddrin.sin_addr.s_addr)
    ESP_DBG("%s\n",server->url);

#ifdef UPGRADE_SSL_ENABLE
    if (system_upgrade_start_ssl(server) == true) 
#else

    if (system_upgrade_start(server) == true) 
#endif
    {
        ESP_DBG("upgrade is already started\n");
    }
}

#endif

/******************************************************************************
 * FunctionName : user_esp_platform_data_process
 * Description  : Processing the received data from the server
 * Parameters   : arg -- Additional argument to pass to the callback function
 *                pusrdata -- The received data (or NULL when the connection has been closed!)
 *                length -- The length of received data
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_esp_platform_data_process(struct client_conn_param *pclient_param, char *pusrdata, unsigned short length)
{
    char *pstr = NULL;
    LOCAL char pbuffer[1024 * 2] = {0};//use heap jeremy

    ESP_DBG("user_esp_platform_data_process %s, %d\n", pusrdata,length);

    if (length == 1460) {
        /*save the first segment of a big json data*/
        memcpy(pbuffer, pusrdata, length);
        
    } else {
        /*put this part after the pirst segment, 
         *to deal with the complete data(1460<len<2048) this way */

        if( length<=(2048 - strlen(pbuffer)) )
            memcpy(pbuffer + strlen(pbuffer), pusrdata, length);
        else
            os_printf("ERR:arr_overflow,%u,%d,%d\n",__LINE__,length, 2048 - strlen(pbuffer) );

        if ((pstr = (char *)strstr(pbuffer, "\"status\":")) != NULL) {
            if (strncmp(pstr + 10, "400", 3) == 0) {
                printf("ERROR! invalid json string.\n");
                if(device_status = DEVICE_CONNECTING){
                    device_status = DEVICE_ACTIVE_FAIL;
                }
                
                /*data is handled, zero the buffer bef exit*/
                memset(pbuffer, 0, sizeof(pbuffer));
                return;
            }
        }

        if ((pstr = (char *)strstr(pbuffer, "\"activate_status\": ")) != NULL &&
                user_esp_platform_parse_nonce(pbuffer) == active_nonce) {
                
            if (strncmp(pstr + 19, "1", 1) == 0) {
                printf("device activates successful.\n");
                device_status = DEVICE_ACTIVE_DONE;
                esp_param.activeflag = 1;
                system_param_save_with_protect(ESP_PARAM_START_SEC, &esp_param, sizeof(esp_param));
                user_esp_platform_sent(pclient_param);
                if(LIGHT_DEVICE){
                    //system_restart(); //Jeremy.L why restart?
                }
            } else {
                printf("device activates failed.\n");
                device_status = DEVICE_ACTIVE_FAIL;
            }
        }
#if (PLUG_DEVICE || LIGHT_DEVICE)
        else if ((pstr = (char *)strstr(pbuffer, "\"action\": \"sys_upgrade\"")) != NULL) {
            if ((pstr = (char *)strstr(pbuffer, "\"version\":")) != NULL) {
                
                struct upgrade_server_info *server = NULL;
                int nonce = user_esp_platform_parse_nonce(pbuffer);
                user_platform_rpc_set_rsp(pclient_param, nonce);
                server = (struct upgrade_server_info *)zalloc(sizeof(struct upgrade_server_info));
                memcpy(server->upgrade_version, pstr + 12, 16);
                server->upgrade_version[15] = '\0';
                sprintf(server->pre_version,"%s%d.%d.%dt%d(%s)",VERSION_TYPE,IOT_VERSION_MAJOR,\
                        IOT_VERSION_MINOR,IOT_VERSION_REVISION,device_type,UPGRADE_FALG);
                user_esp_platform_upgrade_begin(pclient_param, server);
            }
        } 
        else if ((pstr = (char *)strstr(pbuffer, "\"action\": \"sys_reboot\"")) != NULL) {
            os_timer_disarm(&client_timer);
            os_timer_setfn(&client_timer, (os_timer_func_t *)system_upgrade_reboot, NULL);
            os_timer_arm(&client_timer, 1000, 0);
        } 
        
        else if ((pstr = (char *)strstr(pbuffer, "/v1/device/timers/")) != NULL) {
            int nonce = user_esp_platform_parse_nonce(pbuffer);
            user_platform_rpc_set_rsp(pclient_param, nonce);
            //printf("pclient_param sockfd %d\n",pclient_param->sock_fd);
            //os_timer_disarm(&client_timer);
            //os_timer_setfn(&client_timer, (os_timer_func_t *)user_platform_timer_get, pclient_param);
            //os_timer_arm(&client_timer, 2000, 0);
            user_platform_timer_get(pclient_param);
            
        } else if ((pstr = (char *)strstr(pbuffer, "\"method\": ")) != NULL) {
            if (strncmp(pstr + 11, "GET", 3) == 0) {
                user_esp_platform_get_info(pclient_param, pbuffer);
            } else if (strncmp(pstr + 11, "POST", 4) == 0) {
                user_esp_platform_set_info(pclient_param, pbuffer);
            }
            
        } else if ((pstr = (char *)strstr(pbuffer, "ping success")) != NULL) {
            printf("ping success\n");
            ping_status = TRUE;
            
        } else if ((pstr = (char *)strstr(pbuffer, "send message success")) != NULL) {
        } else if ((pstr = (char *)strstr(pbuffer, "timers")) != NULL) {
            user_platform_timer_start(pusrdata);
        }
        
#elif SENSOR_DEVICE
        else if ((pstr = (char *)strstr(pbuffer, "\"status\":")) != NULL) {
            
            if (strncmp(pstr + 10, "200", 3) != 0) {
                printf("message upload failed.\n");
            } else {
                count++;
                ESP_DBG("message upload sucessful.\n");
            }
            os_timer_disarm(&client_timer);
            os_timer_setfn(&client_timer, (os_timer_func_t *)user_esp_platform_discon, &client_param);
            os_timer_arm(&client_timer, 10, 0);
        }
#endif
        else if ((pstr = (char *)strstr(pbuffer, "device")) != NULL) {
#if PLUG_DEVICE || LIGHT_DEVICE
            user_platform_timer_get(pclient_param);
#elif SENSOR_DEVICE

#endif
        }

        /*data is handled, zero the buffer bef exit*/
        memset(pbuffer, 0, sizeof(pbuffer));
    }

}

#ifdef AP_CACHE
/******************************************************************************
 * FunctionName : user_esp_platform_ap_change
 * Description  : add the user interface for changing to next ap ID.
 * Parameters   :
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_esp_platform_ap_change(void)
{
    uint8 current_id;
    uint8 i = 0;
    
    current_id = wifi_station_get_current_ap_id();
    ESP_DBG("current ap id =%d\n", current_id);

    if (current_id == AP_CACHE_NUMBER - 1) {
       i = 0;
    } else {
       i = current_id + 1;
    }
    while (wifi_station_ap_change(i) != true) {
    //try it out until universe collapses
    ESP_DBG("try ap id =%d\n", i);
    vTaskDelay(3000 / portTICK_RATE_MS);
       i++;
       if (i == AP_CACHE_NUMBER - 1) {
           i = 0;
       }
    }

}
#endif

LOCAL bool  
user_esp_platform_reset_mode(void)
{
    if (wifi_get_opmode() == STATION_MODE) {
        wifi_set_opmode(STATIONAP_MODE);
    }

#ifdef AP_CACHE
    /* delay 5s to change AP */
    vTaskDelay(5000 / portTICK_RATE_MS);
    user_esp_platform_ap_change();
    
    return true;
#endif

    return false;
}

/******************************************************************************
 * FunctionName : user_esp_platform_connected
 * Description  : A new incoming connection has been connected.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_esp_platform_connected(struct client_conn_param *pclient_param)
{
    ESP_DBG("user_esp_platform_connect suceed\n");
    if (wifi_get_opmode() ==  STATIONAP_MODE ) {
#ifndef DEMO_TEST
        wifi_set_opmode(STATION_MODE);
#endif
    }

#if (PLUG_DEVICE || SENSOR_DEVICE)
    user_link_led_output(LED_ON);
#endif
}

/******************************************************************************
 * FunctionName : user_mdns_conf
 * Description  : mdns init
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
struct mdns_info {
	char *host_name;
	char *server_name;
	uint16 server_port;
	unsigned long ipAddr;
	char *txt_data[10];
};

/******************************************************************************
 * FunctionName : espconn_mdns_init
 * Description  : register a device with mdns, the low level API
 * Parameters   : ipAddr -- the ip address of device
 * 				  hostname -- the hostname of device
 * Returns      : none
*******************************************************************************/
void espconn_mdns_init(struct mdns_info *info);

#if LIGHT_DEVICE
void user_mdns_conf()
{

struct ip_info ipconfig;
wifi_get_ip_info(STATION_IF, &ipconfig);

struct mdns_info *info = (struct mdns_info *)zalloc(sizeof(struct mdns_info));
info->host_name = "espressif_light_demo";
info->ipAddr= ipconfig.ip.addr; //sation ip
info->server_name = "espLight";
info->server_port = 80;
info->txt_data[0] = "version = 1.0.1";
//espconn_mdns_init(info);
}
#endif

/******************************************************************************
 * FunctionName : user_esp_platform_check_conection
 * Description  : check and try to rebuild connection, parse ESP dns domain.
 * Parameters   : void
 * Returns      : connection stat as blow
 *enum{
 *  AP_DISCONNECTED = 0,
 *  AP_CONNECTED,
 *  DNS_SUCESSES,
 *};
*******************************************************************************/
LOCAL BOOL  
user_esp_platform_check_conection(void)
{
    u8 single_ap_retry_count = 0;
    u8 dns_retry_count = 0;
    u8 dns_ap_retry_count= 0;
    
    struct ip_info ipconfig;
    struct hostent* phostent = NULL;
    
    memset(&ipconfig, 0, sizeof(ipconfig));
    wifi_get_ip_info(STATION_IF, &ipconfig);

    
    struct station_config *sta_config = (struct station_config *)zalloc(sizeof(struct station_config)*5);
    int ap_num = wifi_station_get_ap_info(sta_config);
    free(sta_config);

#define MAX_DNS_RETRY_CNT 20
#define MAX_AP_RETRY_CNT 200

#ifdef USE_DNS
    
    do{
        //what if the ap connect well but no ip offer, wait and wait again, time could be more longer 
        if(dns_retry_count == MAX_DNS_RETRY_CNT){
            if(ap_num >= 2) user_esp_platform_ap_change();
        }
        dns_retry_count = 0;
        
        while( (ipconfig.ip.addr == 0 || wifi_station_get_connect_status() != STATION_GOT_IP)){
            
            /* if there are wrong while connecting to some AP, change to next and reset counter */
            if (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD ||
                    wifi_station_get_connect_status() == STATION_NO_AP_FOUND ||
                    wifi_station_get_connect_status() == STATION_CONNECT_FAIL ||
                    (single_ap_retry_count++ > MAX_AP_RETRY_CNT)) {
                if(ap_num >= 2){
                    ESP_DBG("try other APs ...\n");
                    user_esp_platform_ap_change();
                }
                ESP_DBG("connecting...\n");
                single_ap_retry_count = 0;
            }
    
            vTaskDelay(3000/portTICK_RATE_MS);
            wifi_get_ip_info(STATION_IF, &ipconfig);
        }

        //***************************
#if LIGHT_DEVICE
            user_mdns_conf();
#endif
        //***************************
        
        do{
            ESP_DBG("STA trying to parse esp domain name!\n");
            phostent = (struct hostent *)gethostbyname(ESP_DOMAIN);

            if(phostent == NULL){
                vTaskDelay(500/portTICK_RATE_MS);
            }else{
                printf("Get DNS OK!\n");
                break;
            }

        }while(dns_retry_count++ < MAX_DNS_RETRY_CNT);

    }while(NULL == phostent && dns_ap_retry_count++ < AP_CACHE_NUMBER);

    if(phostent!=NULL){

        if( phostent->h_length <= 4 )
            memcpy(&esp_server_ip,(char*)(phostent->h_addr_list[0]),phostent->h_length);
        else
            os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, phostent->h_length );
        
        
        printf("ESP_DOMAIN IP address: %s\n", inet_ntoa(esp_server_ip));

        ping_status = TRUE;
        free(phostent);
        phostent == NULL;
        
        return DNS_SUCESSES;
    } else {
        return DNS_FAIL;
    }

#else

    while( (ipconfig.ip.addr == 0 || wifi_station_get_connect_status() != STATION_GOT_IP)){
        
        ESP_DBG("STA trying to connect AP!\n");
        /* if there are wrong while connecting to some AP, change to next and reset counter */
        if (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD ||
                wifi_station_get_connect_status() == STATION_NO_AP_FOUND ||
                wifi_station_get_connect_status() == STATION_CONNECT_FAIL ||
                (single_ap_retry_count++ > MAX_AP_RETRY_CNT)) {
                
            user_esp_platform_ap_change();
            single_ap_retry_count = 0;
        }
    
        vTaskDelay(300/portTICK_RATE_MS);
        wifi_get_ip_info(STATION_IF, &ipconfig);
    }

    memcpy(&esp_server_ip, esp_domain_ip, 4);
    ESP_DBG("ESP_DOMAIN IP address: %s\n", inet_ntoa(esp_server_ip));
    
#endif

    if(ipconfig.ip.addr != 0){ 
        return AP_CONNECTED;
    }else{
        return AP_DISCONNECTED;
    }
}


/******************************************************************************
 * FunctionName : user_esp_platform_param_recover
 * Description  : espconn struct parame init when get ip addr
 * Parameters   : none
 * Returns      : none
*******************************************************************************/

LOCAL void  
user_esp_platform_param_recover(void)
{
    struct ip_info sta_info;
    struct dhcp_client_info dhcp_info;
    
    sprintf(iot_version,"%s%d.%d.%dt%d(%s)",VERSION_TYPE,IOT_VERSION_MAJOR,\
    IOT_VERSION_MINOR,IOT_VERSION_REVISION,device_type,UPGRADE_FALG);
    printf("IOT VERSION:%s\n", iot_version);

    
    system_rtc_mem_read(0,&rtc_info,sizeof(struct rst_info));
     if(rtc_info.reason == 1 || rtc_info.reason == 2) {
         ESP_DBG("flag = %d,epc1 = 0x%08x,epc2=0x%08x,epc3=0x%08x,excvaddr=0x%08x,depc=0x%08x,\nFatal \
exception (%d): \n",rtc_info.reason,rtc_info.epc1,rtc_info.epc2,rtc_info.epc3,rtc_info.excvaddr,rtc_info.depc,rtc_info.exccause);
     }
    struct rst_info info = {0};
    system_rtc_mem_write(0,&info,sizeof(struct rst_info));

    system_param_load(ESP_PARAM_START_SEC, 0, &esp_param, sizeof(esp_param));
    
    /***add by tzx for saving ip_info to avoid dhcp_client start****/
    /*
    system_rtc_mem_read(64,&dhcp_info,sizeof(struct dhcp_client_info));
    if(dhcp_info.flag == 0x01 ) {
        printf("set default ip ??\n");
        sta_info.ip = dhcp_info.ip_addr;
        sta_info.gw = dhcp_info.gw;
        sta_info.netmask = dhcp_info.netmask;
        if ( true != wifi_set_ip_info(STATION_IF,&sta_info)) {
            printf("set default ip wrong\n");
        }
    }
    memset(&dhcp_info,0,sizeof(struct dhcp_client_info));
    system_rtc_mem_write(64,&dhcp_info,sizeof(struct rst_info));
*/
    system_rtc_mem_read(70, &user_boot_flag, sizeof(user_boot_flag));
    
    int boot_flag = 0xffffffff;
    system_rtc_mem_write(70,&boot_flag,sizeof(boot_flag));

    /*restore system timer after reboot, note here not for power off */
    user_platform_timer_restore();
}

/******************************************************************************
 * FunctionName : user_esp_platform_param_recover
 * Description  : espconn struct parame init when get ip addr
 * Parameters   : none
 * Returns      : none
*******************************************************************************/

LOCAL void  
user_platform_stationap_enable(void)
{
    
#ifdef SOFTAP_ENCRYPT //should debug here
    char macaddr[6];
    char password[33];
    struct softap_config config;

    wifi_softap_get_config(&config);
    wifi_get_macaddr(SOFTAP_IF, macaddr);

    memset(config.password, 0, sizeof(config.password));
    //sprintf(password, MACSTR "_%s", MAC2STR(macaddr), PASSWORD);
    sprintf(password, "0123456789");
    memcpy(config.password, password, strlen(password));
    config.authmode = AUTH_WPA_WPA2_PSK;
    wifi_softap_set_config(&config);
    
#endif

    wifi_set_opmode(STATIONAP_MODE);
}

/******************************************************************************
 * FunctionName : user_esp_platform_init
 * Description  : device parame init based on espressif platform
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
#ifdef CLIENT_SSL_ENABLE

	static const char default_certificate[] ICACHE_RODATA_ATTR STORE_ATTR = {
	  0x30, 0x82, 0x03, 0x54, 0x30, 0x82, 0x02, 0x3c, 0xa0, 0x03, 0x02, 0x01,
	  0x02, 0x02, 0x03, 0x02, 0x34, 0x56, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86,
	  0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x42, 0x31,
	  0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53,
	  0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x0d, 0x47,
	  0x65, 0x6f, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20, 0x49, 0x6e, 0x63, 0x2e,
	  0x31, 0x1b, 0x30, 0x19, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x12, 0x47,
	  0x65, 0x6f, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20, 0x47, 0x6c, 0x6f, 0x62,
	  0x61, 0x6c, 0x20, 0x43, 0x41, 0x30, 0x1e, 0x17, 0x0d, 0x30, 0x32, 0x30,
	  0x35, 0x32, 0x31, 0x30, 0x34, 0x30, 0x30, 0x30, 0x30, 0x5a, 0x17, 0x0d,
	  0x32, 0x32, 0x30, 0x35, 0x32, 0x31, 0x30, 0x34, 0x30, 0x30, 0x30, 0x30,
	  0x5a, 0x30, 0x42, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
	  0x13, 0x02, 0x55, 0x53, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04,
	  0x0a, 0x13, 0x0d, 0x47, 0x65, 0x6f, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20,
	  0x49, 0x6e, 0x63, 0x2e, 0x31, 0x1b, 0x30, 0x19, 0x06, 0x03, 0x55, 0x04,
	  0x03, 0x13, 0x12, 0x47, 0x65, 0x6f, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20,
	  0x47, 0x6c, 0x6f, 0x62, 0x61, 0x6c, 0x20, 0x43, 0x41, 0x30, 0x82, 0x01,
	  0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
	  0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30, 0x82, 0x01,
	  0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xda, 0xcc, 0x18, 0x63, 0x30, 0xfd,
	  0xf4, 0x17, 0x23, 0x1a, 0x56, 0x7e, 0x5b, 0xdf, 0x3c, 0x6c, 0x38, 0xe4,
	  0x71, 0xb7, 0x78, 0x91, 0xd4, 0xbc, 0xa1, 0xd8, 0x4c, 0xf8, 0xa8, 0x43,
	  0xb6, 0x03, 0xe9, 0x4d, 0x21, 0x07, 0x08, 0x88, 0xda, 0x58, 0x2f, 0x66,
	  0x39, 0x29, 0xbd, 0x05, 0x78, 0x8b, 0x9d, 0x38, 0xe8, 0x05, 0xb7, 0x6a,
	  0x7e, 0x71, 0xa4, 0xe6, 0xc4, 0x60, 0xa6, 0xb0, 0xef, 0x80, 0xe4, 0x89,
	  0x28, 0x0f, 0x9e, 0x25, 0xd6, 0xed, 0x83, 0xf3, 0xad, 0xa6, 0x91, 0xc7,
	  0x98, 0xc9, 0x42, 0x18, 0x35, 0x14, 0x9d, 0xad, 0x98, 0x46, 0x92, 0x2e,
	  0x4f, 0xca, 0xf1, 0x87, 0x43, 0xc1, 0x16, 0x95, 0x57, 0x2d, 0x50, 0xef,
	  0x89, 0x2d, 0x80, 0x7a, 0x57, 0xad, 0xf2, 0xee, 0x5f, 0x6b, 0xd2, 0x00,
	  0x8d, 0xb9, 0x14, 0xf8, 0x14, 0x15, 0x35, 0xd9, 0xc0, 0x46, 0xa3, 0x7b,
	  0x72, 0xc8, 0x91, 0xbf, 0xc9, 0x55, 0x2b, 0xcd, 0xd0, 0x97, 0x3e, 0x9c,
	  0x26, 0x64, 0xcc, 0xdf, 0xce, 0x83, 0x19, 0x71, 0xca, 0x4e, 0xe6, 0xd4,
	  0xd5, 0x7b, 0xa9, 0x19, 0xcd, 0x55, 0xde, 0xc8, 0xec, 0xd2, 0x5e, 0x38,
	  0x53, 0xe5, 0x5c, 0x4f, 0x8c, 0x2d, 0xfe, 0x50, 0x23, 0x36, 0xfc, 0x66,
	  0xe6, 0xcb, 0x8e, 0xa4, 0x39, 0x19, 0x00, 0xb7, 0x95, 0x02, 0x39, 0x91,
	  0x0b, 0x0e, 0xfe, 0x38, 0x2e, 0xd1, 0x1d, 0x05, 0x9a, 0xf6, 0x4d, 0x3e,
	  0x6f, 0x0f, 0x07, 0x1d, 0xaf, 0x2c, 0x1e, 0x8f, 0x60, 0x39, 0xe2, 0xfa,
	  0x36, 0x53, 0x13, 0x39, 0xd4, 0x5e, 0x26, 0x2b, 0xdb, 0x3d, 0xa8, 0x14,
	  0xbd, 0x32, 0xeb, 0x18, 0x03, 0x28, 0x52, 0x04, 0x71, 0xe5, 0xab, 0x33,
	  0x3d, 0xe1, 0x38, 0xbb, 0x07, 0x36, 0x84, 0x62, 0x9c, 0x79, 0xea, 0x16,
	  0x30, 0xf4, 0x5f, 0xc0, 0x2b, 0xe8, 0x71, 0x6b, 0xe4, 0xf9, 0x02, 0x03,
	  0x01, 0x00, 0x01, 0xa3, 0x53, 0x30, 0x51, 0x30, 0x0f, 0x06, 0x03, 0x55,
	  0x1d, 0x13, 0x01, 0x01, 0xff, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xff,
	  0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0xc0,
	  0x7a, 0x98, 0x68, 0x8d, 0x89, 0xfb, 0xab, 0x05, 0x64, 0x0c, 0x11, 0x7d,
	  0xaa, 0x7d, 0x65, 0xb8, 0xca, 0xcc, 0x4e, 0x30, 0x1f, 0x06, 0x03, 0x55,
	  0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0xc0, 0x7a, 0x98, 0x68,
	  0x8d, 0x89, 0xfb, 0xab, 0x05, 0x64, 0x0c, 0x11, 0x7d, 0xaa, 0x7d, 0x65,
	  0xb8, 0xca, 0xcc, 0x4e, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	  0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00,
	  0x35, 0xe3, 0x29, 0x6a, 0xe5, 0x2f, 0x5d, 0x54, 0x8e, 0x29, 0x50, 0x94,
	  0x9f, 0x99, 0x1a, 0x14, 0xe4, 0x8f, 0x78, 0x2a, 0x62, 0x94, 0xa2, 0x27,
	  0x67, 0x9e, 0xd0, 0xcf, 0x1a, 0x5e, 0x47, 0xe9, 0xc1, 0xb2, 0xa4, 0xcf,
	  0xdd, 0x41, 0x1a, 0x05, 0x4e, 0x9b, 0x4b, 0xee, 0x4a, 0x6f, 0x55, 0x52,
	  0xb3, 0x24, 0xa1, 0x37, 0x0a, 0xeb, 0x64, 0x76, 0x2a, 0x2e, 0x2c, 0xf3,
	  0xfd, 0x3b, 0x75, 0x90, 0xbf, 0xfa, 0x71, 0xd8, 0xc7, 0x3d, 0x37, 0xd2,
	  0xb5, 0x05, 0x95, 0x62, 0xb9, 0xa6, 0xde, 0x89, 0x3d, 0x36, 0x7b, 0x38,
	  0x77, 0x48, 0x97, 0xac, 0xa6, 0x20, 0x8f, 0x2e, 0xa6, 0xc9, 0x0c, 0xc2,
	  0xb2, 0x99, 0x45, 0x00, 0xc7, 0xce, 0x11, 0x51, 0x22, 0x22, 0xe0, 0xa5,
	  0xea, 0xb6, 0x15, 0x48, 0x09, 0x64, 0xea, 0x5e, 0x4f, 0x74, 0xf7, 0x05,
	  0x3e, 0xc7, 0x8a, 0x52, 0x0c, 0xdb, 0x15, 0xb4, 0xbd, 0x6d, 0x9b, 0xe5,
	  0xc6, 0xb1, 0x54, 0x68, 0xa9, 0xe3, 0x69, 0x90, 0xb6, 0x9a, 0xa5, 0x0f,
	  0xb8, 0xb9, 0x3f, 0x20, 0x7d, 0xae, 0x4a, 0xb5, 0xb8, 0x9c, 0xe4, 0x1d,
	  0xb6, 0xab, 0xe6, 0x94, 0xa5, 0xc1, 0xc7, 0x83, 0xad, 0xdb, 0xf5, 0x27,
	  0x87, 0x0e, 0x04, 0x6c, 0xd5, 0xff, 0xdd, 0xa0, 0x5d, 0xed, 0x87, 0x52,
	  0xb7, 0x2b, 0x15, 0x02, 0xae, 0x39, 0xa6, 0x6a, 0x74, 0xe9, 0xda, 0xc4,
	  0xe7, 0xbc, 0x4d, 0x34, 0x1e, 0xa9, 0x5c, 0x4d, 0x33, 0x5f, 0x92, 0x09,
	  0x2f, 0x88, 0x66, 0x5d, 0x77, 0x97, 0xc7, 0x1d, 0x76, 0x13, 0xa9, 0xd5,
	  0xe5, 0xf1, 0x16, 0x09, 0x11, 0x35, 0xd5, 0xac, 0xdb, 0x24, 0x71, 0x70,
	  0x2c, 0x98, 0x56, 0x0b, 0xd9, 0x17, 0xb4, 0xd1, 0xe3, 0x51, 0x2b, 0x5e,
	  0x75, 0xe8, 0xd5, 0xd0, 0xdc, 0x4f, 0x34, 0xed, 0xc2, 0x05, 0x66, 0x80,
	  0xa1, 0xcb, 0xe6, 0x33
	};
	static unsigned int default_certificate_len = 856;

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
#endif


#ifdef SPIFFS_ENABLE
#define FS1_FLASH_SIZE      (128*1024)
#define FS2_FLASH_SIZE      (128*1024)

#define FS1_FLASH_ADDR      (1024*1024)
#define FS2_FLASH_ADDR      (1280*1024)

#define SECTOR_SIZE         (4*1024) 
#define LOG_BLOCK           (SECTOR_SIZE)
#define LOG_PAGE            (128)

#define FD_BUF_SIZE         32*4
#define CACHE_BUF_SIZE      (LOG_PAGE + 32)*8

static void upgrade_spiffs_config(void)
{
	struct esp_spiffs_config spiffs_config;
	bzero(&spiffs_config,sizeof(struct esp_spiffs_config));
	
	spiffs_config.phys_size = FS1_FLASH_SIZE;
	spiffs_config.phys_addr = FS1_FLASH_ADDR;
	spiffs_config.phys_erase_block = SECTOR_SIZE;
	spiffs_config.log_block_size = LOG_BLOCK;
	spiffs_config.log_page_size = LOG_PAGE;
	spiffs_config.fd_buf_size = FD_BUF_SIZE * 2;
	spiffs_config.cache_buf_size = CACHE_BUF_SIZE;
	esp_spiffs_init(&spiffs_config);
}
#endif


void  
user_esp_platform_maintainer(void *pvParameters)
{
    int ret;
    u8 timeout_count = 0;
    int32 nNetTimeout=1000;// 1 Sec

    u8 connect_retry_c;
    u8 total_connect_retry_c;

    struct ip_info sta_ipconfig;
    
    bool ValueFromReceive = false;
    portBASE_TYPE xStatus;

    int stack_counter=0;
    int quiet=0;
    
    client_param.sock_fd=-1;
    
    //vTaskDelay(10000 / portTICK_RATE_MS);//wait pc serial ready

    user_esp_platform_param_recover();

#if PLUG_DEVICE
    user_plug_init();
#elif LIGHT_DEVICE
	user_light_init();
#elif SENSOR_DEVICE
    user_sensor_init(esp_param.activeflag);
#endif

#ifdef AP_CACHE
    wifi_station_ap_number_set(AP_CACHE_NUMBER);
#endif

    //printf("rtc_info.reason 000-%d, 000-%d reboot, flag %d\n",rtc_info.reason,(REBOOT_MAGIC == user_boot_flag),user_boot_flag);

    /*if not reboot back, power on with key pressed, enter stationap mode*/
    if( REBOOT_MAGIC != user_boot_flag && 0 == user_get_key_status()){
        /*device power on with stationap mode defaultly, neednt config again*/
        //user_platform_stationap_enable();
        printf("enter softap+station mode\n");
#if (PLUG_DEVICE || SENSOR_DEVICE)
        user_link_led_output(LED_ON);//gpio 12
#endif
        //for cloud test only
        if(0){/*for test*/
            struct station_config *sta_config = (struct station_config *)zalloc(sizeof(struct station_config));
            wifi_station_get_ap_info(sta_config);
            memset(sta_config, 0, sizeof(struct station_config));
            sprintf(sta_config->ssid, "IOT_DEMO_TEST");
            sprintf(sta_config->password, "0000");
            wifi_station_set_config(sta_config);
            free(sta_config);
        }
        
        if(TRUE == esp_param.tokenrdy) goto Local_mode;

    }else {
        struct station_config *sta_config5 = (struct station_config *)zalloc(sizeof(struct station_config)*5);
        int ret = wifi_station_get_ap_info(sta_config5);
        free(sta_config5);
        printf("wifi_station_get_ap num %d\n",ret);
        if(0 == ret) {
            /*should be true here, Zero just for debug usage*/
            if(1){
                /*AP_num == 0, no ap cached,start smartcfg*/
                wifi_set_opmode(STATION_MODE);
                xTaskCreate(smartconfig_task, "smartconfig_task", 256, NULL, 2, NULL);

                while(device_status != DEVICE_GOT_IP){
                    ESP_DBG("configing...\n");
                    vTaskDelay(2000 / portTICK_RATE_MS);
                }
            }
            
        } else {
        
#ifndef DEMO_TEST
            /* entry station mode and connect to ap cached */
            printf("entry station mode to connect server \n");
            wifi_set_opmode(STATION_MODE);
#endif
        }
    }

#ifdef DEMO_TEST
    wifi_set_opmode(STATIONAP_MODE);
#endif

    /*if token not ready, wait here*/
    while(TRUE != esp_param.tokenrdy) {
        //ESP_DBG("token invalid...\n");
        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    while(1){
        
        xStatus = xQueueReceive(QueueStop,&ValueFromReceive,0);
        if ( pdPASS == xStatus && TRUE == ValueFromReceive){
            ESP_DBG("platform_maintainer rcv exit signal!\n");
            break;
        }

#if (PLUG_DEVICE || SENSOR_DEVICE)
        //chenck ip or DNS, led start blinking
        if(wifi_get_opmode()==STATION_MODE)user_link_led_output(LED_5HZ);
#endif

        do{
            ret = user_esp_platform_check_conection();
        } while( AP_DISCONNECTED == ret || DNS_FAIL == ret);

        client_param.sock_fd= socket(PF_INET, SOCK_STREAM, 0);
        if (-1 == client_param.sock_fd) {
            close(client_param.sock_fd);
            ESP_DBG("socket fail!\n");
            continue;
        }

        bzero(&remote_addr, sizeof(struct sockaddr_in));
        memcpy(&remote_addr.sin_addr.s_addr, &esp_server_ip, 4);
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_len = sizeof(remote_addr);
#ifdef CLIENT_SSL_ENABLE
        remote_addr.sin_port = htons(8443);
#else
        remote_addr.sin_port = htons(8000);
#endif

        device_status = DEVICE_CONNECTING;
        connect_retry_c= 20;//connect retry times per AP
        do{
            ret = connect(client_param.sock_fd,(struct sockaddr*)&remote_addr,sizeof(struct sockaddr));
            if(0 != ret){
                ESP_DBG("connect fail!\n");
                device_status = DEVICE_CONNECT_SERVER_FAIL;
                vTaskDelay(1000 / portTICK_RATE_MS);
           }
            else{
                ESP_DBG("connect sucess!\n");
#ifdef CLIENT_SSL_ENABLE
                int i=0;
                int cert_index = 0, ca_cert_index = 0;
                int cert_size, ca_cert_size;
                char **ca_cert, **cert;

                uint32_t options = SSL_DISPLAY_CERTS | SSL_NO_DEFAULT_KEY;

                cert_size = ssl_get_config(SSL_MAX_CERT_CFG_OFFSET);
                ca_cert_size = ssl_get_config(SSL_MAX_CA_CERT_CFG_OFFSET);
                ca_cert = (char **)calloc(1, sizeof(char *)*ca_cert_size);
                cert = (char **)calloc(1, sizeof(char *)*cert_size);

                if ((client_param.ssl_ctx= ssl_ctx_new(options, SSL_DEFAULT_CLNT_SESS)) == NULL) {
                    printf("Error: Client context is invalid\n");
                    close(client_param.sock_fd);
                    continue;
                }

				ssl_obj_memory_load(ssl_ctx, SSL_OBJ_X509_CACERT, default_certificate, default_certificate_len, NULL);
                
#ifdef SPIFFS_ENABLE

                upgrade_spiffs_config();
                //        for (i = 0; i < cert_index; i++) {
                //        if (ssl_obj_load(ssl_ctx, SSL_OBJ_X509_CERT, "TLS.example.cer", NULL)){
                //           printf("Certificate '%s' is undefined.\n", "TLS.example.cer");
                //        }else
                //          printf("Certificate '%s' is defined.\n", "TLS.example.cer");
                //        }
                
                //      if (ssl_obj_load(ssl_ctx, SSL_OBJ_RSA_KEY, "TLS.example.key", NULL)){
                //           printf("key '%s' is undefined.\n", "TLS.example.key");
                //        }else
                //          printf("key '%s' is defined.\n", "TLS.example.key");
                
                //        for (i = 0; i < ca_cert_index; i++) {
                //        if (ssl_obj_load(ssl_ctx, SSL_OBJ_X509_CACERT, "TLS.ca-chain.cer", NULL)){
                //           printf("Certificate auth '%s' is undefined.\n", "TLS.ca-chain.cer");
                //        }else
                //          printf("Certificate auth '%s' is defined.\n", "TLS.ca-chain.cer");
                //        }
#elif 0
                //      int ret = 0;
                //      if (ret = ssl_obj_option_load(ssl_ctx, SSL_OBJ_X509_CACERT, "TLS.ca-chain.cer", NULL, 0x7c))
                //          printf("Certificate auth %s load fail\n",  "TLS.ca-chain.cer");
                //      else
                //          printf("Certificate auth load ok\n");
                
                //      if (ret = ssl_obj_option_load(ssl_ctx, SSL_OBJ_RSA_KEY, "TLS.example.key", NULL, 0x7c))
                //          printf("key %s load fail, %d\n",  "TLS.example.key", ret);
                //      else
                //          printf("key load ok\n");
                
                //      if (ret = ssl_obj_option_load(ssl_ctx, SSL_OBJ_X509_CERT, "TLS.example.cer", NULL, 0x7c))
                //          printf("Certificate %s load fail, %d\n",  "TLS.example.cer", ret);
                //      else
                //          printf("Certificate load ok\n");
                
#endif

                free(cert);
                free(ca_cert);

                /* Try session resumption? */
                printf("client handshake start! ssl_ctx 0x%x sockfd %d\n",client_param.ssl_ctx,client_param.sock_fd);
                client_param.ssl= ssl_client_new(client_param.ssl_ctx, client_param.sock_fd, NULL, 0);
                if (client_param.ssl == NULL){
                    ssl_ctx_free(client_param.ssl_ctx);
                    close(client_param.sock_fd);
                    continue;
                }
                
                if(ssl_handshake_status(client_param.ssl) != SSL_OK){
                    printf("client handshake fail.\n");
                    ssl_free(client_param.ssl);
                    ssl_ctx_free(client_param.ssl_ctx);
                    close(client_param.sock_fd);
                    continue;
                }
                
                //handshake sucesses,show cert here for debug only
                if (!quiet) {
                    const char *common_name = ssl_get_cert_dn(client_param.ssl,SSL_X509_CERT_COMMON_NAME);
                    if (common_name) {
                        printf("Common Name:\t\t\t%s\n", common_name);
                    }
                    display_session_id(client_param.ssl);
                    display_cipher(client_param.ssl);
                    quiet = true;
                }
#endif      
                user_esp_platform_connected(&client_param);
                break;
            }

        }while( connect_retry_c-- != 0 && ret);

        if(connect_retry_c == 0 && ret){
            close(client_param.sock_fd);
            //connect fail,go big loop, try another AP
            continue;
        }
        
        user_esp_platform_sent(&client_param);
        
        fd_set read_set,write_set;  
        struct timeval timeout;
        while(1){
            
            xStatus = xQueueReceive(QueueStop,&ValueFromReceive,0);
            if ( pdPASS == xStatus && TRUE == ValueFromReceive){
                ESP_DBG("esp_platform_maintainer rcv exit signal!\n");
                break;
            }

            /*clear fdset, and set the selct function wait time*/
            FD_ZERO(&read_set);
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            /* allow parallel reading of server and standard input */
            FD_SET(client_param.sock_fd, &read_set);
            
            ret = select(client_param.sock_fd+1, &read_set, NULL, NULL, &timeout);
            if ((ret) > 0){
                /* read standard input? */
                if (FD_ISSET(client_param.sock_fd, &read_set)){
                    printf("read application data\n");
                    //setsockopt(client_param.sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&nNetTimeout, sizeof(int));
#ifdef CLIENT_SSL_ENABLE
                    uint8_t *read_buf = NULL;
                    ret = ssl_read(client_param.ssl, &read_buf);
                    if (ret > 0) {
                        user_esp_platform_data_process(&client_param,read_buf,ret);
                        timeout_count = 0;
                    }
                    else if (ret < 0){
                        //disconnect,exit the recv loop,to connect again
                        ESP_DBG("recv error %d,disconnect with server!\n", ret);
                        user_esp_platform_discon(&client_param);
                        timeout_count = 0;
                        break;
                    }
#else
                    memset(pusrdata, 0, sizeof(pusrdata));
                    ret = recv(client_param.sock_fd, (u8 *)pusrdata, sizeof(pusrdata), 0);
                    if (ret > 0){
                        user_esp_platform_data_process(&client_param,pusrdata,ret);
                        timeout_count = 0;
                    }
                    else if ((ret == 0)||(ret == -1 && errno != EAGAIN)){
                        //ret == 0 connection is closed by server
                        //ret == -1 && ERRNO != AGAIN, not timeout, smth wrong
                        //disconnect,exit the recv loop,to connect again
                        ESP_DBG("recv error %d,disconnect with server!\n", ret);
                        user_esp_platform_discon(&client_param);
                        timeout_count = 0;
                        break;
                    }
#endif
                }
            }
#if (PLUG_DEVICE || LIGHT_DEVICE)
            else{
                //start the tmeout counter,once it reach the beacon time,send the beacon and wait response,
                wifi_get_ip_info(STATION_IF, &sta_ipconfig);
                if((sta_ipconfig.ip.addr == 0 || wifi_station_get_connect_status() != STATION_GOT_IP)){
                    user_esp_platform_discon(&client_param);
                    timeout_count = 0;
                    break;
                }

                if(timeout_count++ > BEACON_TIME/nNetTimeout){
                    if (ping_status == FALSE) {        //disconnect,exit the recv loop,to connect again
                        ESP_DBG("user_esp_platform_sent_beacon,server noresponse, and beacon time comes again!\n");
                        user_esp_platform_discon(&client_param);
                        break;
                    }
                    user_esp_platform_sent_beacon(&client_param);
                    ping_status == FALSE;
                    timeout_count = 0;
                }
            }
#endif
            //jeremy, what about the stack, the fit value?
            ESP_DBG("platform_maintainer stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());
            }
        }
Local_mode:
    wifi_set_opmode(STATIONAP_MODE);

#ifdef CLIENT_SSL_ENABLE
    ssl_free(client_param.ssl);
    ssl_ctx_free(client_param.ssl_ctx);
#endif

    if(client_param.sock_fd >= 0)close(client_param.sock_fd);
    vQueueDelete(QueueStop);
    QueueStop = NULL;
    vTaskDelete(NULL);

}


void   user_esp_platform_init(void)
{
    if (QueueStop == NULL)
        QueueStop = xQueueCreate(1,1);

    if (QueueStop != NULL){
#ifdef CLIENT_SSL_ENABLE
        xTaskCreate(user_esp_platform_maintainer, "platform_maintainer", 640, NULL, 5, NULL);//ssl need much more stack
#else
        xTaskCreate(user_esp_platform_maintainer, "platform_maintainer", 384, NULL, 5, NULL);//512, 274 left,384
#endif
    }
        
}

sint8   user_esp_platform_deinit(void)
{
    bool ValueToSend = true;
    portBASE_TYPE xStatus;
    if (QueueStop == NULL)
        return -1;

    xStatus = xQueueSend(QueueStop,&ValueToSend,0);
    if (xStatus != pdPASS){
        ESP_DBG("WEB SEVER Could not send to the queue!\n");
        return -1;
    } else {
        taskYIELD();
        return pdPASS;
    }
}

#endif
