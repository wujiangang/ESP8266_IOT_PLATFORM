/******************************************************************************
 * Copyright 2015-2017 Espressif Systems
 *
 * FileName: user_cgi.c
 *
 * Description: Specialized functions that provide an API into the 
 * functionality this ESP provides.
 *
 *******************************************************************************/
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/mem.h"
#include "lwip/sockets.h"
#include "json/cJSON.h"

#include "user_iot_version.h"

#if ESP_PLATFORM
#include "user_esp_platform.h"
#endif

#if PLUG_DEVICE
#include "user_plug.h"
#endif
#if LIGHT_DEVICE
#include "user_light.h"
#endif

#include "upgrade.h"

#include "user_cgi.h"

/******************************************************************************/
typedef struct _scaninfo {
    STAILQ_HEAD(, bss_info) *pbss;
    struct single_conn_param *psingle_conn_param;
    uint8 totalpage;
    uint8 pagenum;
    uint8 page_sn;
    uint8 data_cnt;
} scaninfo;
LOCAL scaninfo *pscaninfo;
extern u16 scannum;

LOCAL os_timer_t *restart_xms;
LOCAL rst_parm *rstparm;
LOCAL struct station_config *sta_conf;
LOCAL struct softap_config *ap_conf;


/******************************************************************************
 * FunctionName : user_binfo_get
 * Description  : get the user bin paramer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
* { "status":"200", "user_bin":"userx.bin" }
*******************************************************************************/
LOCAL int  
user_binfo_get(cJSON *pcjson, const char* pname)
{
    char buff[12];

    cJSON_AddStringToObject(pcjson, "status", "200");
    if(system_upgrade_userbin_check() == 0x00) {
         sprintf(buff, "user1.bin");
    } else if (system_upgrade_userbin_check() == 0x01) {
         sprintf(buff, "user2.bin");
    } else{
        printf("system_upgrade_userbin_check fail\n");
        return -1;
    }
    cJSON_AddStringToObject(pcjson, "user_bin", buff);

    return 0;
}
/******************************************************************************
 * FunctionName : system_info_get
 * Description  : get the user bin paramer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
{"Version":{"hardware":"0.1","sdk_version":"1.1.2","iot_version":"v1.0.5t23701(a)"},
"Device":{"product":"Plug","manufacturer":"Espressif Systems"}}
*******************************************************************************/
LOCAL int  
system_info_get(cJSON *pcjson, const char* pname )
{
    char buff[16]={0};

    cJSON * pSubJson_Version = cJSON_CreateObject();
    if(NULL == pSubJson_Version){
        printf("pSubJson_Version creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Version", pSubJson_Version);
    
    cJSON * pSubJson_Device = cJSON_CreateObject();
    if(NULL == pSubJson_Device){
        printf("pSubJson_Device creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Device", pSubJson_Device);

#if SENSOR_DEVICE
    cJSON_AddStringToObject(pSubJson_Version,"hardware","0.3");
#else
    cJSON_AddStringToObject(pSubJson_Version,"hardware","0.1");
#endif
    cJSON_AddStringToObject(pSubJson_Version,"sdk_version",system_get_sdk_version());
    sprintf(buff,"%s%d.%d.%dt%d(%s)",VERSION_TYPE,IOT_VERSION_MAJOR,\
    IOT_VERSION_MINOR,IOT_VERSION_REVISION,device_type,UPGRADE_FALG);
    cJSON_AddStringToObject(pSubJson_Version,"iot_version",buff);
    

    cJSON_AddStringToObject(pSubJson_Device,"manufacture","Espressif Systems");
#if SENSOR_DEVICE
#if HUMITURE_SUB_DEVICE
    cJSON_AddStringToObject(pSubJson_Device,"product", "Humiture");
#elif FLAMMABLE_GAS_SUB_DEVICE
    cJSON_AddStringToObject(pSubJson_Device,"product", "Flammable Gas");
#endif
#endif
#if PLUG_DEVICE
    cJSON_AddStringToObject(pSubJson_Device,"product", "Plug");
#endif
#if LIGHT_DEVICE
    cJSON_AddStringToObject(pSubJson_Device,"product", "Light");
#endif

    //char * p = cJSON_Print(pcjson);
    //printf("@.@ system_info_get exit with  %s len:%d \n", p, strlen(p));

    return 0;
}

#if PLUG_DEVICE
/******************************************************************************
 * FunctionName : status_get
 * Description  : set up the device status as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
{"Response":{
"status":0}} 
*******************************************************************************/
LOCAL int  
switch_status_get(cJSON *pcjson, const char* pname )
{

    cJSON * pSubJson_response = cJSON_CreateObject();
    if(NULL == pSubJson_response){
        printf("pSubJson_response creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "response", pSubJson_response);
    
    cJSON_AddNumberToObject(pSubJson_response, "status", user_plug_get_status());

    return 0;
}
/******************************************************************************
 * FunctionName : status_set
 * Description  : parse the device status parmer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON formatted string
 * Returns      : result
 {"Response":
 {"status":1 }}
*******************************************************************************/
LOCAL int  
switch_status_set(const char *pValue)
{
    cJSON * pJsonSub=NULL;
    cJSON * pJsonSub_status=NULL;
    
    cJSON * pJson =  cJSON_Parse(pValue);
    if(NULL != pJson){
        pJsonSub = cJSON_GetObjectItem(pJson, "response");
    }
    
    if(NULL != pJsonSub){
        pJsonSub_status = cJSON_GetObjectItem(pJsonSub, "status");
    }
    
    if(NULL != pJsonSub_status){
        if(pJsonSub_status->type == cJSON_Number){
            user_plug_set_status(pJsonSub_status->valueint);
            if(NULL != pJson)cJSON_Delete(pJson);
            return 0;
        }
    }
    
    if(NULL != pJson)cJSON_Delete(pJson);
    printf("switch_status_set fail\n");
    return -1;
}

#endif

#if LIGHT_DEVICE
/******************************************************************************
 * FunctionName : light_status_get
 * Description  : set up the device status as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
PwmTree {
"period":1000,
"rgb":{
"red":62152,
"green":65530,
"blue":62152
}
} 
*******************************************************************************/

LOCAL int  
light_status_get(cJSON *pcjson, const char* pname )
{

    cJSON_AddNumberToObject(pcjson, "period", user_light_get_period());

    cJSON * pSubJson_rgb= cJSON_CreateObject();
    if(NULL == pSubJson_rgb){
        printf("pSubJson_rgb creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "rgb", pSubJson_rgb);

    cJSON_AddNumberToObject(pSubJson_rgb, "red", user_light_get_duty(LIGHT_RED));
    cJSON_AddNumberToObject(pSubJson_rgb, "green", user_light_get_duty(LIGHT_GREEN));
    cJSON_AddNumberToObject(pSubJson_rgb, "blue", user_light_get_duty(LIGHT_GREEN));
    cJSON_AddNumberToObject(pSubJson_rgb, "cwhite", (PWM_CHANNEL>LIGHT_COLD_WHITE?user_light_get_duty(LIGHT_COLD_WHITE):0));
    cJSON_AddNumberToObject(pSubJson_rgb, "wwhite", (PWM_CHANNEL>LIGHT_WARM_WHITE?user_light_get_duty(LIGHT_WARM_WHITE):0));

    return 0;
}

/******************************************************************************
 * FunctionName : light_status_get
 * Description  : set up the device status as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON formatted string
 * Returns      : result
*******************************************************************************/

LOCAL int  
light_status_set(const char *pValue)
{
    static uint32 r,g,b,cw,ww,period;
    period = 1000;
    cw=0;
    ww=0;
    extern uint8 light_sleep_flg;
    u8 flag = 0;

    cJSON * pJson;
    cJSON * pJsonSub;
    cJSON * pJsonSub_freq;
    cJSON * pJsonSub_rgb;

    pJson =  cJSON_Parse(pValue);
    if(NULL == pJson){
        printf("light_status_set cJSON_Parse fail\n");
        return -1;
    }
    
    pJsonSub_rgb = cJSON_GetObjectItem(pJson, "rgb");
    if(NULL != pJsonSub_rgb){
        if(pJsonSub_rgb->type == cJSON_Object){

            pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "red");
            if(NULL != pJsonSub){
                if(pJsonSub->type == cJSON_Number){
                    r = pJsonSub->valueint;
                }else{
                    flag = 1;
                    printf("light_req_parse red type error!\n");
                }
            }else{
                flag = 1;
                printf("GetObjectItem red failed!\n");
            }

            pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "green");
            if(NULL != pJsonSub){
                if(pJsonSub->type == cJSON_Number){
                    g = pJsonSub->valueint;
                }else{
                    flag = 1;
                    printf("light_req_parse green type error!\n");
                }
            }else{
                flag = 1;
                printf("GetObjectItem green failed!\n");
            }
            
            pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "blue");
            if(NULL != pJsonSub){
                if(pJsonSub->type == cJSON_Number){
                    b = pJsonSub->valueint;
                }else{
                    flag = 1;
                    printf("light_req_parse blue type error!\n");
                }
            }else{
                flag = 1;
                printf("GetObjectItem blue failed!\n");
            }

            pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "cwhite");
            if(NULL != pJsonSub){
                if(pJsonSub->type == cJSON_Number){
                    cw = pJsonSub->valueint;
                }else{
                    flag = 1;
                    printf("light_req_parse cwhite type error!\n");
                }
            }else{
                //printf("GetObjectItem no cwhite!\n");
            }
            
            pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "wwhite");
            if(NULL != pJsonSub){
                if(pJsonSub->type == cJSON_Number){
                    ww = pJsonSub->valueint;
                }else{
                    flag = 1;
                    printf("light_req_parse wwhite type error!\n");
                }
            }else{
                //printf("GetObjectItem no wwhite !\n");
            }

        }
    }
    
    pJsonSub_freq = cJSON_GetObjectItem(pJson, "period");
    if(NULL != pJsonSub_freq){
        if(pJsonSub_freq->type == cJSON_Number){
            period = pJsonSub_freq->valueint;
        }else{
            flag = 1;
            printf("light_req_parse period type error!\n");
        }
    }else{
        flag = 1;
        printf("GetObjectItem period failed!\n");
    }

    /*this item is optional*/
    pJsonSub = cJSON_GetObjectItem(pJson, "response");
    if(NULL != pJsonSub){
        if(pJsonSub->type == cJSON_Number){
            //PostCmdNeeRsp = pJsonSub->valueint;
            //printf("LIGHT response:%u\n",PostCmdNeeRsp);
        }else{
            flag = 1;
            printf("ERROR:light_req_parse cwhite type error!\n");
        }
    }

    if(0 == flag){
        if((r|g|b|ww|cw) == 0){
            if(light_sleep_flg==0){
                /*entry sleep mode?*/
            }
            
        }else{
            if(light_sleep_flg==1){
                printf("modem sleep en\r\n");
                //wifi_set_sleep_type(MODEM_SLEEP_T);
                light_sleep_flg =0;
            }
        }
        
        light_set_aim(r,g,b,cw,ww,period);
    }
    
    cJSON_Delete(pJson);
    return 0;
}

#endif


#if SENSOR_DEVICE
LOCAL int  
user_set_sleep(const char *pValue)
{
    printf("user_set_sleep %s \n", pValue);
    
    return 0;
}
#else
LOCAL int  
user_set_reboot(const char *pValue)
{
    printf("user_set_reboot %s \n", pValue);
    
    return 0;
}
#endif


LOCAL int  
system_status_reset(const char *pValue)
{
    printf("system_status_reset %s \n", pValue);
    
    return 0;
}

LOCAL int  
user_upgrade_start(const char *pValue)
{
    printf("user_upgrade_start %s \n", pValue);
    
    return 0;
}

LOCAL int  
user_upgrade_reset(const char *pValue)
{
    printf("user_upgrade_reset %s \n", pValue);
    
    return 0;
}
/******************************************************************************
 * FunctionName : restart_10ms_cb
 * Description  : system restart or wifi reconnected after a certain time.
 * Parameters   : arg -- Additional argument to pass to the function
 * Returns      : none
*******************************************************************************/
LOCAL void  
restart_xms_cb(void *arg)
{
    if (rstparm != NULL) {
        switch (rstparm->parmtype) {
            case WIFI:
                if (sta_conf->ssid[0] != 0x00) {
                    printf("restart_xms_cb set sta_conf noreboot\n");
                    wifi_station_set_config(sta_conf);
                    wifi_station_disconnect();
                    wifi_station_connect();
                }

                if (ap_conf->ssid[0] != 0x00) {
                    wifi_softap_set_config(ap_conf);
                    printf("restart_xms_cb set ap_conf sys restart\n");
                    system_restart();
                }

                free(ap_conf);
                ap_conf = NULL;
                free(sta_conf);
                sta_conf = NULL;
                free(rstparm);
                rstparm = NULL;
                free(restart_xms);
                restart_xms = NULL;

                break;

            case DEEP_SLEEP:
            case REBOOT:
#if SENSOR_DEVICE
                wifi_set_opmode(STATION_MODE);
                if (rstparm->parmtype == DEEP_SLEEP) {
                    system_deep_sleep(SENSOR_DEEP_SLEEP_TIME);
                }
#endif
                break;

            default:
                break;
        }
    }
}

/******************************************************************************
 * FunctionName : wifi_station_get
 * Description  : set up the station paramer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
*******************************************************************************/
LOCAL int  
wifi_station_get(cJSON *pcjson)
{
    struct ip_info ipconfig;
    uint8 buff[20];
    bzero(buff, sizeof(buff));

    cJSON * pSubJson_Connect_Station= cJSON_CreateObject();
    if(NULL == pSubJson_Connect_Station){
        printf("pSubJson_Connect_Station creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Connect_Station", pSubJson_Connect_Station);

    cJSON * pSubJson_Ipinfo_Station= cJSON_CreateObject();
    if(NULL == pSubJson_Ipinfo_Station){
        printf("pSubJson_Ipinfo_Station creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Ipinfo_Station", pSubJson_Ipinfo_Station);

    wifi_station_get_config(sta_conf);
    wifi_get_ip_info(STATION_IF, &ipconfig);

    sprintf(buff, IPSTR, IP2STR(&ipconfig.ip));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Station,"ip",buff);
    sprintf(buff, IPSTR, IP2STR(&ipconfig.netmask));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Station,"mask",buff);
    sprintf(buff, IPSTR, IP2STR(&ipconfig.gw));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Station,"gw",buff);
    cJSON_AddStringToObject(pSubJson_Connect_Station, "ssid", sta_conf->ssid);
    cJSON_AddStringToObject(pSubJson_Connect_Station, "password", sta_conf->password);

    return 0;
}

/******************************************************************************
 * FunctionName : wifi_softap_get
 * Description  : set up the softap paramer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
*******************************************************************************/
LOCAL int  
wifi_softap_get(cJSON *pcjson)
{
    struct ip_info ipconfig;
    uint8 buff[20];
    bzero(buff, sizeof(buff));
    
    cJSON * pSubJson_Connect_Softap= cJSON_CreateObject();
    if(NULL == pSubJson_Connect_Softap){
        printf("pSubJson_Connect_Softap creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Connect_Softap", pSubJson_Connect_Softap);

    cJSON * pSubJson_Ipinfo_Softap= cJSON_CreateObject();
    if(NULL == pSubJson_Ipinfo_Softap){
        printf("pSubJson_Ipinfo_Softap creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Ipinfo_Softap", pSubJson_Ipinfo_Softap);
    wifi_softap_get_config(ap_conf);
    wifi_get_ip_info(SOFTAP_IF, &ipconfig);

    sprintf(buff, IPSTR, IP2STR(&ipconfig.ip));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Softap,"ip",buff);
    sprintf(buff, IPSTR, IP2STR(&ipconfig.netmask));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Softap,"mask",buff);
    sprintf(buff, IPSTR, IP2STR(&ipconfig.gw));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Softap,"gw",buff);
    cJSON_AddNumberToObject(pSubJson_Connect_Softap, "channel", ap_conf->channel);
    cJSON_AddStringToObject(pSubJson_Connect_Softap, "ssid", ap_conf->ssid);
    cJSON_AddStringToObject(pSubJson_Connect_Softap, "password", ap_conf->password);
    
    switch (ap_conf->authmode) {
        case AUTH_OPEN:
            cJSON_AddStringToObject(pSubJson_Connect_Softap, "authmode", "OPEN");
            break;
        case AUTH_WEP:
            cJSON_AddStringToObject(pSubJson_Connect_Softap, "authmode", "WEP");
            break;
        case AUTH_WPA_PSK:
            cJSON_AddStringToObject(pSubJson_Connect_Softap, "authmode", "WPAPSK");
            break;
        case AUTH_WPA2_PSK:
            cJSON_AddStringToObject(pSubJson_Connect_Softap, "authmode", "WPA2PSK");
            break;
        case AUTH_WPA_WPA2_PSK:
            cJSON_AddStringToObject(pSubJson_Connect_Softap, "authmode", "WPAPSK/WPA2PSK");
            break;
        default :
            cJSON_AddNumberToObject(pSubJson_Connect_Softap, "authmode",  ap_conf->authmode);
            break;
    }
    return 0;
}

/******************************************************************************
 * FunctionName : wifi_softap_get
 * Description  : set up the softap paramer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
{
"Response":{"Station":
{"Connect_Station":{"ssid":"","password":""},
"Ipinfo_Station":{"ip":"0.0.0.0","mask":"0.0.0.0","gw":"0.0.0.0"}},
"Softap":
{"Connect_Softap":{"authmode":"OPEN","channel":1,"ssid":"ESP_A132F0","password":""},
"Ipinfo_Softap":{"ip":"192.168.4.1","mask":"255.255.255.0","gw":"192.168.4.1"}}
}}
*******************************************************************************/
LOCAL int  
wifi_info_get(cJSON *pcjson,const char* pname)
{

    ap_conf = (struct softap_config *)zalloc(sizeof(struct softap_config));
    sta_conf = (struct station_config *)zalloc(sizeof(struct station_config));

    cJSON * pSubJson_Response= cJSON_CreateObject();
    if(NULL == pSubJson_Response){
        printf("pSubJson_Response creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Response", pSubJson_Response);

    cJSON * pSubJson_Station= cJSON_CreateObject();
    if(NULL == pSubJson_Station){
        printf("pSubJson_Station creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pSubJson_Response, "Station", pSubJson_Station);
    
    cJSON * pSubJson_Softap= cJSON_CreateObject();
    if(NULL == pSubJson_Softap){
        printf("pSubJson_Softap creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pSubJson_Response, "Softap", pSubJson_Softap);

    if((wifi_station_get(pSubJson_Station) == -1)||(wifi_softap_get(pSubJson_Softap) == -1)){

        free(sta_conf);
        free(ap_conf);
        sta_conf = NULL;
        ap_conf = NULL;
        return -1;
    } else{
        free(sta_conf);
        free(ap_conf);
        sta_conf = NULL;
        ap_conf = NULL;
        return 0;
    }

}

/******************************************************************************
 * FunctionName : wifi_station_set
 * Description  : parse the station parmer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON formated string
 * Returns      : result
 * {"Request":{"Station":{"Connect_Station":{"ssid":"","password":"","token":""}}}}
 * {"Request":{"Softap":{"Connect_Softap":
   {"authmode":"OPEN", "channel":6, "ssid":"IOT_SOFTAP","password":""}}}}
 * {"Request":{"Station":{"Connect_Station":{"token":"u6juyl9t6k4qdplgl7dg7m90x96264xrzse6mx1i"}}}}
*******************************************************************************/
LOCAL int  
wifi_info_set(const char* pValue)
{
    cJSON * pJson;
    cJSON * pJsonSub;
    cJSON * pJsonSub_request;
    cJSON * pJsonSub_Connect_Station;
    cJSON * pJsonSub_Connect_Softap;
    cJSON * pJsonSub_Sub;

//    user_esp_platform_set_connect_status(DEVICE_CONNECTING);
    
    if (restart_xms != NULL) {
        os_timer_disarm(restart_xms);
    }
    
    if (ap_conf == NULL) {
        ap_conf = (struct softap_config *)zalloc(sizeof(struct softap_config));
    }
    
    if (sta_conf == NULL) {
        sta_conf = (struct station_config *)zalloc(sizeof(struct station_config));
    }

    pJson = cJSON_Parse(pValue);
    if(NULL == pJson){
        printf("wifi_info_set cJSON_Parse fail\n");
        return -1;
    }
    
    pJsonSub_request = cJSON_GetObjectItem(pJson, "Request");
    if(pJsonSub_request == NULL) {
        printf("cJSON_GetObjectItem pJsonSub_request fail\n");
        return -1;
    }

    if((pJsonSub = cJSON_GetObjectItem(pJsonSub_request, "Station")) != NULL){

        pJsonSub_Connect_Station= cJSON_GetObjectItem(pJsonSub,"Connect_Station");
        if(NULL == pJsonSub_Connect_Station){
            printf("cJSON_GetObjectItem Connect_Station fail\n");
            return -1;
        }

        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Station,"ssid");
        if(NULL != pJsonSub_Sub){       
            if( strlen(pJsonSub_Sub->valuestring)<=32 )
                memcpy(sta_conf->ssid, pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            else
                 os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,strlen(pJsonSub_Sub->valuestring));
        }

        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Station,"password");
        if(NULL != pJsonSub_Sub){
            if( strlen(pJsonSub_Sub->valuestring)<=64 )
                memcpy(sta_conf->password, pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            else
                 os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,strlen(pJsonSub_Sub->valuestring));
        }

#if ESP_PLATFORM
        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Station,"token");
        if(NULL != pJsonSub_Sub){       
            user_esp_platform_set_token(pJsonSub_Sub->valuestring);
        }
#endif
    }

    if((pJsonSub = cJSON_GetObjectItem(pJsonSub_request, "Softap")) != NULL){
        pJsonSub_Connect_Softap= cJSON_GetObjectItem(pJsonSub,"Connect_Softap");
        if(NULL == pJsonSub_Connect_Softap){
            printf("cJSON_GetObjectItem Connect_Softap fail!\n");
            return -1;
        }

        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Softap,"ssid");
        if(NULL != pJsonSub_Sub){
            /*
                printf("pJsonSub_Connect_Softap pJsonSub_Sub->ssid %s  len%d\n",
                pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            */
            if( strlen(pJsonSub_Sub->valuestring)<=32 )
                memcpy(ap_conf->ssid, pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            else
                 os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,strlen(pJsonSub_Sub->valuestring));
        }

        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Softap,"password");
        if(NULL != pJsonSub_Sub){
        /*
            printf("pJsonSub_Connect_Softap pJsonSub_Sub->password %s  len%d\n",
                pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
          */          
            if( strlen(pJsonSub_Sub->valuestring)<=64 )
                memcpy(ap_conf->password, pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            else
                 os_printf("ERR:arr_overflow,%u,%d\n",__LINE__,strlen(pJsonSub_Sub->valuestring));
        }

        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Softap,"channel");
        if(NULL != pJsonSub_Sub){
            /*
            printf("pJsonSub_Connect_Softap channel %d\n",pJsonSub_Sub->valueint);
            */
            ap_conf->channel = pJsonSub_Sub->valueint;
        }

        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Softap,"authmode");
        if(NULL != pJsonSub_Sub){
            if (strcmp(pJsonSub_Sub->valuestring, "OPEN") == 0) {
                ap_conf->authmode = AUTH_OPEN;
            } else if (strcmp(pJsonSub_Sub->valuestring, "WPAPSK") == 0) {
                ap_conf->authmode = AUTH_WPA_PSK;
            } else if (strcmp(pJsonSub_Sub->valuestring, "WPA2PSK") == 0) {
                ap_conf->authmode = AUTH_WPA2_PSK;
            } else if (strcmp(pJsonSub_Sub->valuestring, "WPAPSK/WPA2PSK") == 0) {
                ap_conf->authmode = AUTH_WPA_WPA2_PSK;
            } else {
                ap_conf->authmode = AUTH_OPEN;
            }
            /*
            printf("pJsonSub_Connect_Softap ap_conf->authmode %d\n",ap_conf->authmode);
            */
        }

    }

    cJSON_Delete(pJson);

    if (rstparm == NULL) {
        rstparm = (rst_parm *)zalloc(sizeof(rst_parm));
    }
    rstparm->parmtype = WIFI;
    
    if (sta_conf->ssid[0] != 0x00 || ap_conf->ssid[0] != 0x00) {
        ap_conf->ssid_hidden = 0;
        ap_conf->max_connection = 4;
    
        if (restart_xms == NULL) {
            restart_xms = (os_timer_t *)malloc(sizeof(os_timer_t));
            if(NULL == restart_xms){
                printf("ERROR:wifi_info_set,memory exhausted, check it\n");
            }
        }

        os_timer_disarm(restart_xms);
        os_timer_setfn(restart_xms, (os_timer_func_t *)restart_xms_cb, NULL);
        os_timer_arm(restart_xms, 20, 0);  // delay 10ms, then do
    } else {
        free(ap_conf);
        free(sta_conf);
        free(rstparm);
        sta_conf = NULL;
        ap_conf = NULL;
        rstparm =NULL;
    }
    
    return 0;
}

/******************************************************************************
 * FunctionName : scan_result_output
 * Description  : set up the scan data as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
                : total -- flag that send the total scanpage or not
 * Returns      : result
*******************************************************************************/
LOCAL int  
scan_result_output(cJSON *pcjson, bool total)
{
    int count=2;//default no more than 8 
    u8 buff[20];
    LOCAL struct bss_info *bss;
    cJSON * pSubJson_page;
    char *pchar;
    
    while((bss = STAILQ_FIRST(pscaninfo->pbss)) != NULL && count-- ){

        pSubJson_page= cJSON_CreateObject();
        if(NULL == pSubJson_page){
            printf("pSubJson_page creat fail\n");
            return -1;
        }
        cJSON_AddItemToArray(pcjson, pSubJson_page);//pcjson

        memset(buff, 0, sizeof(buff));
        sprintf(buff, MACSTR, MAC2STR(bss->bssid));
        cJSON_AddStringToObject(pSubJson_page, "bssid", buff);
        cJSON_AddStringToObject(pSubJson_page, "ssid", bss->ssid);
        cJSON_AddNumberToObject(pSubJson_page, "rssi", -(bss->rssi));
        cJSON_AddNumberToObject(pSubJson_page, "channel", bss->channel);
        switch (bss->authmode) {
            case AUTH_OPEN:
                cJSON_AddStringToObject(pSubJson_page, "authmode", "OPEN");
                break;
            case AUTH_WEP:
                cJSON_AddStringToObject(pSubJson_page, "authmode", "WEP");
                break;
            case AUTH_WPA_PSK:
                cJSON_AddStringToObject(pSubJson_page, "authmode", "WPAPSK");
                break;
            case AUTH_WPA2_PSK:
                cJSON_AddStringToObject(pSubJson_page, "authmode", "WPA2PSK");
                break;
            case AUTH_WPA_WPA2_PSK:
                cJSON_AddStringToObject(pSubJson_page, "authmode", "WPAPSK/WPA2PSK");
                break;
            default :
                cJSON_AddNumberToObject(pSubJson_page, "authmode", bss->authmode);
                break;
        }
        STAILQ_REMOVE_HEAD(pscaninfo->pbss, next);
        free(bss);
    }

    return 0;
}

/******************************************************************************
 * FunctionName : scan_info_get
 * Description  : set up the scan data as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
*******************************************************************************/
LOCAL int  
scan_info_get(cJSON *pcjson, const char* pname)
{

    //printf("scan_info_get pscaninfo->totalpage %d\n",pscaninfo->totalpage);
    cJSON * pSubJson_Response= cJSON_CreateObject();
    if(NULL == pSubJson_Response){
        printf("pSubJson_Response creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Response", pSubJson_Response);

    cJSON_AddNumberToObject(pSubJson_Response, "TotalPage", pscaninfo->totalpage);


    cJSON_AddNumberToObject(pSubJson_Response, "PageNum", pscaninfo->pagenum);

    cJSON * pSubJson_ScanResult= cJSON_CreateArray();
    if(NULL == pSubJson_ScanResult){
        printf("pSubJson_ScanResult creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pSubJson_Response, "ScanResult", pSubJson_ScanResult);

    if(0 != scan_result_output(pSubJson_ScanResult,0)){
        printf("scan_result_print fail\n");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * FunctionName : device_get
 * Description  : set up the device information parmer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
{"Status":{
"status":3}}
*******************************************************************************/
LOCAL int  
connect_status_get(cJSON *pcjson, const char* pname )
{

    cJSON * pSubJson_Status = cJSON_CreateObject();
    if(NULL == pSubJson_Status){
        printf("pSubJson_Status creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Status", pSubJson_Status); 

//    cJSON_AddNumberToObject(pSubJson_Status, "status", user_esp_platform_get_connect_status());

    return 0;
}


typedef int (* cgigetCallback)(cJSON *pcjson, const char* pchar);
typedef int (* cgisetCallback)(const char* pchar);

typedef struct {
    const char *file;
    const char *cmd;
    cgigetCallback  get;
    cgisetCallback  set;
} EspCgiApiEnt;

const EspCgiApiEnt espCgiApiNodes[]={
#if PLUG_DEVICE
    {"config", "switch", switch_status_get,switch_status_set},
#elif LIGHT_DEVICE
    {"config", "light", light_status_get,light_status_set},
#endif
    
#if SENSOR_DEVICE
    {"config", "sleep", NULL,user_set_sleep},
#else
    {"config", "reboot", NULL,user_set_reboot},
#endif
    {"config", "wifi", wifi_info_get,wifi_info_set},
//    {"client", "scan",  scan_info_get, NULL},
    {"client", "status", connect_status_get, NULL},
    {"config", "reset", NULL,system_status_reset},
    {"client", "info",  system_info_get, NULL},
    {"upgrade", "getuser", user_binfo_get, NULL},
//    {"upgrade", "start", NULL, user_upgrade_start},
//    {"upgrade", "reset", NULL, user_upgrade_reset},
    {NULL, NULL, NULL}
};

int   cgiEspApi(HttpdConnData *connData) {
    char *file=&connData->url[1]; //skip initial slash
    int len, i;
    int ret=0;
    
    char *pchar = NULL;
    cJSON *pcjson = NULL;
    char *pbuf=(char*)zalloc(48);
    
    httpdStartResponse(connData, 200);
//  httpdHeader(connData, "Content-Type", "text/json");
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdEndHeaders(connData);
    httpdFindArg(connData->getArgs, "command", pbuf, 48);

//    printf("File %s Command %s\n", file, pbuf);

    //Find the command/file combo in the espCgiApiNodes table
    i=0;
    while (espCgiApiNodes[i].cmd!=NULL) {
        if (strcmp(espCgiApiNodes[i].file, file)==0 && strcmp(espCgiApiNodes[i].cmd, pbuf)==0) break;
        i++;
    }

    if (espCgiApiNodes[i].cmd==NULL) {
        //Not found
        len=sprintf(pbuf, "{\n \"status\": \"404 Not Found\"\n }\n");
        //printf("Resp %s\n", pbuf);
        httpdSend(connData, pbuf, len);
    } else {
        if (connData->requestType==HTTPD_METHOD_POST) {
            //Found, req is using POST
            //printf("post cmd found %s",espCgiApiNodes[i].cmd);
            if(NULL != espCgiApiNodes[i].set){
                espCgiApiNodes[i].set(connData->post->buff);
            }
            
            //ToDo: Use result of json parsing code somehow
            len=sprintf(pbuf, "{\n \"status\": \"ok\"\n }\n");
            httpdSend(connData, pbuf, len);
            
        } else {
            //Found, req is using GET
        
            //printf("get cmd found %s\n",espCgiApiNodes[i].cmd);
            pcjson=cJSON_CreateObject();
            if(NULL == pcjson) {
                printf(" ERROR! cJSON_CreateObject fail!\n");
                return HTTPD_CGI_DONE;
            }
            ret=espCgiApiNodes[i].get(pcjson, espCgiApiNodes[i].cmd);
            if(ret == 0){
                pchar = cJSON_Print(pcjson);
                len = strlen(pchar);
                //printf("Resp %s\n", pchar);
                httpdSend(connData, pchar, len);
            }
            
            if(pcjson){
                cJSON_Delete(pcjson);
            }
            if(pchar){
                free(pchar);
                pchar=NULL;
            }
        }
    }

    if(pbuf){
        free(pbuf);
        pbuf=NULL;
    }
    return HTTPD_CGI_DONE;
}

