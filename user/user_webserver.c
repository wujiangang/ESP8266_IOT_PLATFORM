/******************************************************************************
 * Copyright (C) 2014 -2016  Espressif System
 *
 * FileName: user_webserver.c
 *
 * Description: The web server mode configration.
 *              Check your hardware connection with the host while use this mode.
 * Modification history:
 * 2015/7/1, v1.0 create this file.
*******************************************************************************/
#include "user_webserver.h"

#if WEB_SERVICE

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

#define DEBUG

#ifdef DEBUG
#define WS_DEBUG os_printf
#else
#define WS_DEBUG
#endif

typedef struct _scaninfo {
    STAILQ_HEAD(, bss_info) *pbss;
    struct single_conn_param *psingle_conn_param;
//    struct espconn *pespconn;
    uint8 totalpage;
    uint8 pagenum;
    uint8 page_sn;
    uint8 data_cnt;
} scaninfo;

extern u16 scannum;

LOCAL os_timer_t *restart_10ms;
LOCAL rst_parm *rstparm;

LOCAL scaninfo *pscaninfo;

LOCAL struct station_config *sta_conf;
LOCAL struct softap_config *ap_conf;

LOCAL uint8 upgrade_lock;
LOCAL os_timer_t app_upgrade_10s;
LOCAL os_timer_t upgrade_check_timer;

LOCAL uint32 PostCmdNeeRsp = 1;

LOCAL struct conn_param connections;
LOCAL struct single_conn_param *single_conn[MAX_CLIENT_NUMBER];

LOCAL xQueueHandle QueueStop = NULL;
LOCAL xQueueHandle RCVQueueStop = NULL;

LOCAL char *precvbuffer;
LOCAL uint32 dat_sumlength;//could be removed Jeremy
/******************************************************************************
 * FunctionName : user_binfo_get
 * Description  : get up the user bin paramer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
* { "status":"200", "user_bin":"userx.bin" }
*******************************************************************************/
LOCAL int  
user_binfo_get(cJSON *pcjson, const char* pname)
{
    char string[32];

    cJSON_AddStringToObject(pcjson, "status", "200");
    if(system_upgrade_userbin_check() == 0x00) {
         sprintf(string, "user1.bin");
    } else if (system_upgrade_userbin_check() == 0x01) {
         sprintf(string, "user2.bin");
    } else{
        WS_DEBUG("system_upgrade_userbin_check fail\n");
        return -1;
    }
    cJSON_AddStringToObject(pcjson, "user_bin", string);

    return 0;
}
/******************************************************************************
 * FunctionName : system_info_get
 * Description  : get up the user bin paramer as a JSON format
 * Parameters   : pcjson -- A pointer to a JSON object
 * Returns      : result
{"Version":{"hardware":"0.1","sdk_version":"1.1.2","iot_version":"v1.0.5t23701(a)"},
"Device":{"product":"Plug","manufacturer":"Espressif Systems"}}
*******************************************************************************/
LOCAL int  
system_info_get(cJSON *pcjson, const char* pname )
{
    char string[32]={0};

    cJSON * pSubJson_Version = cJSON_CreateObject();
    if(NULL == pSubJson_Version){
        WS_DEBUG("pSubJson_Version creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Version", pSubJson_Version);
    
    cJSON * pSubJson_Device = cJSON_CreateObject();
    if(NULL == pSubJson_Device){
        WS_DEBUG("pSubJson_Device creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Device", pSubJson_Device);

#if SENSOR_DEVICE
    cJSON_AddStringToObject(pSubJson_Version,"hardware","0.3");
#else
    cJSON_AddStringToObject(pSubJson_Version,"hardware","0.1");
#endif
    cJSON_AddStringToObject(pSubJson_Version,"sdk_version",system_get_sdk_version());
    sprintf(string,"%s%d.%d.%dt%d(%s)",VERSION_TYPE,IOT_VERSION_MAJOR,\
    IOT_VERSION_MINOR,IOT_VERSION_REVISION,device_type,UPGRADE_FALG);
    cJSON_AddStringToObject(pSubJson_Version,"iot_version",string);
    

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
    //WS_DEBUG("@.@ system_info_get exit with  %s len:%d \n", p, strlen(p));

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
        WS_DEBUG("pSubJson_Status creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Status", pSubJson_Status); 

    cJSON_AddNumberToObject(pSubJson_Status, "status", user_esp_platform_get_connect_status());

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
        WS_DEBUG("pSubJson_response creat fail\n");
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
switch_req_parse(const char *pValue)
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
            WS_DEBUG("switch_status_set status %d d %d\n",pJsonSub_status->valueint);
            user_plug_set_status(pJsonSub_status->valueint);
            if(NULL != pJson)cJSON_Delete(pJson);
            return 0;
        }
    }
    
    if(NULL != pJson)cJSON_Delete(pJson);
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
"blue":62152,
"bwhitelue":62152,
"wwhite":62152
}
} 
*******************************************************************************/

LOCAL int  
light_status_get(cJSON *pcjson, const char* pname )
{

    cJSON_AddNumberToObject(pcjson, "period", user_light_get_period());

	cJSON * pSubJson_rgb= cJSON_CreateObject();
	if(NULL == pSubJson_rgb){
		WS_DEBUG("pSubJson_rgb creat fail\n");
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
light_req_parse(const char *pValue)
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
        printf("light_req_parse cJSON_Parse fail\n");
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
        			WS_DEBUG("light_req_parse red type error!\n");
        		}
            }else{
                flag = 1;
                WS_DEBUG("GetObjectItem red failed!\n");
            }

    		pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "green");
            if(NULL != pJsonSub){
        		if(pJsonSub->type == cJSON_Number){
        	        g = pJsonSub->valueint;
        	    }else{
            	    flag = 1;
        			WS_DEBUG("light_req_parse green type error!\n");
        		}
            }else{
                flag = 1;
                WS_DEBUG("GetObjectItem green failed!\n");
            }
    		
    		pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "blue");
            if(NULL != pJsonSub){
        		if(pJsonSub->type == cJSON_Number){
        	        b = pJsonSub->valueint;
        	    }else{
            	    flag = 1;
        			WS_DEBUG("light_req_parse blue type error!\n");
        		}
            }else{
                flag = 1;
                WS_DEBUG("GetObjectItem blue failed!\n");
            }

    		pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "cwhite");
            if(NULL != pJsonSub){
        		if(pJsonSub->type == cJSON_Number){
        	        cw = pJsonSub->valueint;
        	    }else{
            	    flag = 1;
        			WS_DEBUG("light_req_parse cwhite type error!\n");
        		}
            }else{
                flag = 1;
                WS_DEBUG("GetObjectItem cwhite failed!\n");
            }
            
    		pJsonSub = cJSON_GetObjectItem(pJsonSub_rgb, "wwhite");
            if(NULL != pJsonSub){
        		if(pJsonSub->type == cJSON_Number){
        	        ww = pJsonSub->valueint;
        	    }else{
            	    flag = 1;
        			WS_DEBUG("light_req_parse wwhite type error!\n");
        		}
            }else{
                flag = 1;
                WS_DEBUG("GetObjectItem wwhite failed!\n");
            }

    	}
    }
    
	pJsonSub_freq = cJSON_GetObjectItem(pJson, "period");
    if(NULL != pJsonSub_freq){
    	if(pJsonSub_freq->type == cJSON_Number){
            period = pJsonSub_freq->valueint;
        }else{
            flag = 1;
    		WS_DEBUG("light_req_parse period type error!\n");
    	}
    }else{
        flag = 1;
		WS_DEBUG("GetObjectItem period  failed!\n");
    }

    /*this item is optional*/
	pJsonSub = cJSON_GetObjectItem(pJson, "response");
    if(NULL != pJsonSub){
    	if(pJsonSub->type == cJSON_Number){
            PostCmdNeeRsp = pJsonSub->valueint;
    		WS_DEBUG("LIGHT response:%u\n",PostCmdNeeRsp);
        }else{
            flag = 1;
    		WS_DEBUG("ERROR:light_req_parse cwhite type error!\n");
    	}
    }

    if(0 == flag){
        if((r|g|b|ww|cw) == 0){
            if(light_sleep_flg==0){
                /*entry sleep mode?*/
            }
            
        }else{
            if(light_sleep_flg==1){
                WS_DEBUG("modem sleep en\r\n");
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
    uint8 pbuf[20];
    bzero(pbuf, sizeof(pbuf));

    cJSON * pSubJson_Connect_Station= cJSON_CreateObject();
    if(NULL == pSubJson_Connect_Station){
        WS_DEBUG("pSubJson_Connect_Station creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Connect_Station", pSubJson_Connect_Station);

    cJSON * pSubJson_Ipinfo_Station= cJSON_CreateObject();
    if(NULL == pSubJson_Ipinfo_Station){
        WS_DEBUG("pSubJson_Ipinfo_Station creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Ipinfo_Station", pSubJson_Ipinfo_Station);

    wifi_station_get_config(sta_conf);
    wifi_get_ip_info(STATION_IF, &ipconfig);

    sprintf(pbuf, IPSTR, IP2STR(&ipconfig.ip));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Station,"ip",pbuf);
    sprintf(pbuf, IPSTR, IP2STR(&ipconfig.netmask));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Station,"mask",pbuf);
    sprintf(pbuf, IPSTR, IP2STR(&ipconfig.gw));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Station,"gw",pbuf);

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
    uint8 pbuf[20];
    bzero(pbuf, sizeof(pbuf));
    
    cJSON * pSubJson_Connect_Softap= cJSON_CreateObject();
    if(NULL == pSubJson_Connect_Softap){
        WS_DEBUG("pSubJson_Connect_Softap creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Connect_Softap", pSubJson_Connect_Softap);

    cJSON * pSubJson_Ipinfo_Softap= cJSON_CreateObject();
    if(NULL == pSubJson_Ipinfo_Softap){
        WS_DEBUG("pSubJson_Ipinfo_Softap creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Ipinfo_Softap", pSubJson_Ipinfo_Softap);

    wifi_softap_get_config(ap_conf);
    wifi_get_ip_info(SOFTAP_IF, &ipconfig);

    sprintf(pbuf, IPSTR, IP2STR(&ipconfig.ip));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Softap,"ip",pbuf);
    sprintf(pbuf, IPSTR, IP2STR(&ipconfig.netmask));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Softap,"mask",pbuf);
    sprintf(pbuf, IPSTR, IP2STR(&ipconfig.gw));
    cJSON_AddStringToObject(pSubJson_Ipinfo_Softap,"gw",pbuf);
    
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

    cJSON * pSubJson_Response= cJSON_CreateObject();
    if(NULL == pSubJson_Response){
        WS_DEBUG("pSubJson_Response creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Response", pSubJson_Response);

    cJSON * pSubJson_Station= cJSON_CreateObject();
    if(NULL == pSubJson_Station){
        WS_DEBUG("pSubJson_Station creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pSubJson_Response, "Station", pSubJson_Station);
    
    cJSON * pSubJson_Softap= cJSON_CreateObject();
    if(NULL == pSubJson_Softap){
        WS_DEBUG("pSubJson_Softap creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pSubJson_Response, "Softap", pSubJson_Softap);

    //Jeremy.L select one?
    if(wifi_station_get(pSubJson_Station) ==-1) return -1;
    if(wifi_softap_get(pSubJson_Softap)==-1) return -1;

    return 0;

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
wifi_req_parse(char *pValue)
{

    cJSON * pJson;
    cJSON * pJsonSub;
    cJSON * pJsonSub_request;
    cJSON * pJsonSub_Connect_Station;
    cJSON * pJsonSub_Connect_Softap;
    cJSON * pJsonSub_Sub;
    
    pJson =  cJSON_Parse(pValue);
    if(NULL == pJson){
        printf("wifi_req_parse cJSON_Parse fail\n");
        return -1;
    }
    
    pJsonSub_request = cJSON_GetObjectItem(pJson, "Request");
    if(pJsonSub_request == NULL) {
        WS_DEBUG("cJSON_GetObjectItem pJsonSub_request fail\n");
        return -1;
    }

    if((pJsonSub = cJSON_GetObjectItem(pJsonSub_request, "Station")) != NULL){

        pJsonSub_Connect_Station= cJSON_GetObjectItem(pJsonSub,"Connect_Station");
        if(NULL == pJsonSub_Connect_Station){
            WS_DEBUG("cJSON_GetObjectItem Connect_Station fail\n");
            return -1;
        }
        
        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Station,"ssid");
        if(NULL != pJsonSub_Sub){       
             if(  strlen(pJsonSub_Sub->valuestring) <= 32 )
                memcpy(sta_conf->ssid, pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            else
                os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, strlen(pJsonSub_Sub->valuestring) );
        }
        
        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Station,"password");
        if(NULL != pJsonSub_Sub){
             if(  strlen(pJsonSub_Sub->valuestring) <= 64 )
                memcpy(sta_conf->password, pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            else
                os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, strlen(pJsonSub_Sub->valuestring) );
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
            WS_DEBUG("cJSON_GetObjectItem Connect_Softap fail!\n");
            return -1;
        }
        
        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Softap,"ssid");
        if(NULL != pJsonSub_Sub){
            WS_DEBUG("pJsonSub_Connect_Softap pJsonSub_Sub->ssid %s  len%d\n",
                pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            
            if(  strlen(pJsonSub_Sub->valuestring) <= 32 )
                memcpy(ap_conf->ssid, pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            else
                os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, strlen(pJsonSub_Sub->valuestring) );
        }
        
        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Softap,"password");
        if(NULL != pJsonSub_Sub){
            WS_DEBUG("pJsonSub_Connect_Softap pJsonSub_Sub->password %s  len%d\n",
                pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            
            if(  strlen(pJsonSub_Sub->valuestring) <= 64 )
                memcpy(ap_conf->password, pJsonSub_Sub->valuestring, strlen(pJsonSub_Sub->valuestring));
            else
                os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, strlen(pJsonSub_Sub->valuestring) );
            
        }
        
        pJsonSub_Sub = cJSON_GetObjectItem(pJsonSub_Connect_Softap,"channel");
        if(NULL != pJsonSub_Sub){
            WS_DEBUG("pJsonSub_Connect_Softap channel %d\n",pJsonSub_Sub->valueint);
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
            WS_DEBUG("pJsonSub_Connect_Softap ap_conf->authmode %d\n",ap_conf->authmode);
        }
        
    }

    cJSON_Delete(pJson);
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
    u8 buffer[32];
    LOCAL struct bss_info *bss;
    cJSON * pSubJson_page;
    char *pchar;
    
    while((bss = STAILQ_FIRST(pscaninfo->pbss)) != NULL && count-- ){
        printf("add page to array %d \n",count);
        pSubJson_page= cJSON_CreateObject();
        if(NULL == pSubJson_page){
            WS_DEBUG("pSubJson_page creat fail\n");
            return -1;
        }
        cJSON_AddItemToArray(pcjson, pSubJson_page);//pcjson

        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, MACSTR, MAC2STR(bss->bssid));
        cJSON_AddStringToObject(pSubJson_page, "bssid", buffer);
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
scan_info_get(cJSON *pcjson,const char* pname)
{

    //printf("scan_info_get pscaninfo->totalpage %d\n",pscaninfo->totalpage);
    cJSON * pSubJson_Response= cJSON_CreateObject();
    if(NULL == pSubJson_Response){
        WS_DEBUG("pSubJson_Response creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pcjson, "Response", pSubJson_Response);

    cJSON_AddNumberToObject(pSubJson_Response, "TotalPage", pscaninfo->totalpage);


    cJSON_AddNumberToObject(pSubJson_Response, "PageNum", pscaninfo->pagenum);

    cJSON * pSubJson_ScanResult= cJSON_CreateArray();
    if(NULL == pSubJson_ScanResult){
        WS_DEBUG("pSubJson_ScanResult creat fail\n");
        return -1;
    }
    cJSON_AddItemToObject(pSubJson_Response, "ScanResult", pSubJson_ScanResult);

    if(0 != scan_result_output(pSubJson_ScanResult,0)){
        WS_DEBUG("scan_result_print fail\n");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * FunctionName : parse_url
 * Description  : parse the received data from the server
 * Parameters   : precv -- the received data
 *                purl_frame -- the result of parsing the url
 * Returns      : none
*******************************************************************************/
LOCAL void  
parse_url(char *precv, URL_Frame *purl_frame)
{
    char *str = NULL;
    uint8 length = 0;
    char *pbuffer = NULL;
    char *pbufer = NULL;

    if (purl_frame == NULL || precv == NULL) {
        return;
    }

    pbuffer = (char *)strstr(precv, "Host:");

    if (pbuffer != NULL) {
        length = pbuffer - precv;
        pbufer = (char *)zalloc(length + 1);
        pbuffer = pbufer;
        memcpy(pbuffer, precv, length);
        memset(purl_frame->pSelect, 0, URLSize);
        memset(purl_frame->pCommand, 0, URLSize);
        memset(purl_frame->pFilename, 0, URLSize);

        if (strncmp(pbuffer, "GET ", 4) == 0) {
            purl_frame->Type = GET;
            pbuffer += 4;
        } else if (strncmp(pbuffer, "POST ", 5) == 0) {
            purl_frame->Type = POST;
            pbuffer += 5;
        }

        pbuffer ++;
        str = (char *)strstr(pbuffer, "?");

        if (str != NULL) {
            length = str - pbuffer;
            
            if(  length <= 12 )
                memcpy(purl_frame->pSelect, pbuffer, length);
            else
                os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, length );
            
            str ++;
            pbuffer = (char *)strstr(str, "=");

            if (pbuffer != NULL) {
                length = pbuffer - str;
                
            if(  length <= 12 )
                memcpy(purl_frame->pCommand, str, length);
            else
                os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, length );
                
                pbuffer ++;
                str = (char *)strstr(pbuffer, "&");

                if (str != NULL) {
                    length = str - pbuffer;
                    
                    if(  length <= 12 )
                        memcpy(purl_frame->pFilename, pbuffer, length);
                    else
                        os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, length );
                    
                } else {
                    str = (char *)strstr(pbuffer, " HTTP");

                    if (str != NULL) {
                        length = str - pbuffer;

                        if(  length <= 12 )
                            memcpy(purl_frame->pFilename, pbuffer, length);
                        else
                            os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, length );
                        
                    }
                }
            }
        }

        free(pbufer);
    } else {
        return;
    }
}

/******************************************************************************
 * FunctionName : save_data
 * Description  : save the packet data
 * Parameters   : precv -- packet data to be checked for
                : length --length of the packet
 * Returns      : none
*******************************************************************************/

LOCAL bool  
save_data(char *precv, uint16 length)
{
    bool flag = false;
    char length_buf[10] = {0};
    char *ptemp = NULL;
    char *pdata = NULL;
    uint16 headlength = 0;
    static uint32 totallength = 0;

    u32 precvlen = length;
    
    ptemp = (char *)strstr(precv, "\r\n\r\n");
    
    if (ptemp != NULL) {
        length -= ptemp - precv;
        length -= 4;
        totallength += length;
        headlength = ptemp - precv + 4;
        pdata = (char *)strstr(precv, "Content-Length: ");
        
        if (pdata != NULL) {
            pdata += 16;
            precvbuffer = (char *)strstr(pdata, "\r\n");

            if (precvbuffer != NULL) {
                
                if(  (precvbuffer - pdata) <= 10 )
                    memcpy(length_buf, pdata, precvbuffer - pdata);
                else
                    os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, (precvbuffer - pdata) );
                
                dat_sumlength = atoi(length_buf);
            }
        } else {
            if (totallength != 0x00){
                totallength = 0;
                dat_sumlength = 0;
                return false;
            }
        }
        
        if ((dat_sumlength + headlength) >= 1024) {
            precvbuffer = (char *)zalloc(headlength + 1);
            memcpy(precvbuffer, precv, headlength + 1);
        } else {
            precvbuffer = (char *)zalloc(dat_sumlength + headlength + 1);
            //printf("precvlen %d precvbuffer 0x%x  datalen%d\n",precvlen,precvbuffer,(dat_sumlength + headlength + 1));
            memcpy(precvbuffer, precv, precvlen);
        }
        
    } else {
        if (precvbuffer != NULL) {
            totallength += length;
            memcpy(precvbuffer + strlen(precvbuffer), precv, length);
        } else {
            totallength = 0;
            dat_sumlength = 0;
            return false;
        }
    }

    if (totallength == dat_sumlength) {
        totallength = 0;
        dat_sumlength = 0;
        return true;
    } else {
        return false;
    }
}
/******************************************************************************
 * FunctionName : check_data
 * Description  : check the packet data is normal or not
 * Parameters   : precv -- packet data to be checked for
                : length --length of the packet
 * Returns      : none
*******************************************************************************/
LOCAL bool  
check_data(char *precv, uint16 length)
{
    char length_buf[10] = {0};
    char *ptemp = NULL;
    char *pdata = NULL;
    char *tmp_precvbuffer;
    uint16 tmp_length = length;
    uint32 tmp_totallength = 0;
    
    ptemp = (char *)strstr(precv, "\r\n\r\n");

    if (ptemp != NULL) {

        tmp_length -= ptemp - precv;
        tmp_length -= 4;
        tmp_totallength += tmp_length;

        pdata = (char *)strstr(precv, "Content-Length: ");
        
        if (pdata != NULL){
            pdata += 16;
            tmp_precvbuffer = (char *)strstr(pdata, "\r\n");
            
            if (tmp_precvbuffer != NULL){
                WS_DEBUG(" tmp_precvbuffer 0x%x pdata 0x%x ",tmp_precvbuffer,pdata);
                
                if(  (tmp_precvbuffer - pdata) <= 10 )
                    memcpy(length_buf, pdata, tmp_precvbuffer - pdata);
                else
                    os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, (tmp_precvbuffer - pdata) );
                
                dat_sumlength = atoi(length_buf);
                WS_DEBUG("A_dat:%u,tot:%u,lenght:%u\n",dat_sumlength,tmp_totallength,tmp_length);
                
                if(dat_sumlength != tmp_totallength){
                    return false;
                }
            }
        }
    }
    return true;
}

/******************************************************************************
 * FunctionName : restart_10ms_cb
 * Description  : system restart or wifi reconnected after a certain time.
 * Parameters   : arg -- Additional argument to pass to the function
 * Returns      : none
*******************************************************************************/
LOCAL void  
restart_10ms_cb(void *arg) // Jeremy.L
{
    if (rstparm != NULL && rstparm->pconnpara != NULL) {
        switch (rstparm->parmtype) {
            case WIFI:
                    if (sta_conf->ssid[0] != 0x00) {
                        
                        printf("restart_10ms_cb set sta_conf noreboot\n");
                        wifi_station_set_config(sta_conf);
                        wifi_station_disconnect();
                        wifi_station_connect();
                        //user_esp_platform_check_ip(1);  //Jeremy.L, TBD
                    }

                    if (ap_conf->ssid[0] != 0x00) {
                        wifi_softap_set_config(ap_conf);
                        printf("restart_10ms_cb set ap_conf sys restart\n");
                        system_restart();
                    }

                    free(ap_conf);
                    ap_conf = NULL;
                    free(sta_conf);
                    sta_conf = NULL;
                    free(rstparm);
                    rstparm = NULL;
                    free(restart_10ms);
                    restart_10ms = NULL;

                break;

            case DEEP_SLEEP:
            case REBOOT:
                // is it possiable for a web server with multi connections to sleep?
                if (rstparm->pconnpara->conn_num == 0) {
                    wifi_set_opmode(STATION_MODE);

                    if (rstparm->parmtype == DEEP_SLEEP) {
#if SENSOR_DEVICE
                        system_deep_sleep(SENSOR_DEEP_SLEEP_TIME);
#endif
                    }
                } else {
                    os_timer_arm(restart_10ms, 10, 0);
                }

                break;

            default:
                break;
        }
    }
}

/******************************************************************************
 * FunctionName : data_send
 * Description  : processing the data as http format and send to the client or server
 * Parameters   : arg -- argument to set for client or server
 *                responseOK -- true or false
 *                psend -- The send data
 * Returns      :
*******************************************************************************/
LOCAL void  
data_send(struct single_conn_param *psingle_conn_param, bool responseOK, char *psend)
{
    uint16 length = 0;
    char *pbuf = NULL;
    char httphead[256]; //jeremy
    memset(httphead, 0, 256);
    
    if (responseOK) {
        sprintf(httphead,
                   "HTTP/1.0 200 OK\r\nContent-Length: %d\r\nServer: lwIP/1.4.0\r\n",
                   psend ? strlen(psend) : 0);

        if (psend) {
            sprintf(httphead + strlen(httphead),
                       "Content-type: application/json\r\nExpires: Fri, 10 Apr 2008 14:00:00 GMT\r\nPragma: no-cache\r\n\r\n");
            length = strlen(httphead) + strlen(psend);
            printf("data_send length %d %d  %d\n",length,strlen(psend),strlen(httphead));
            pbuf = (char *)zalloc(length + 1);
            memcpy(pbuf, httphead, strlen(httphead));
            memcpy(pbuf + strlen(httphead), psend, strlen(psend));
            
        } else {
            sprintf(httphead + strlen(httphead), "\n");
            length = strlen(httphead);
        }
        
    } else {
        sprintf(httphead, "HTTP/1.0 400 BadRequest\r\n\
Content-Length: 0\r\nServer: lwIP/1.4.0\r\n\n");
        length = strlen(httphead);
    }

    if (psend) {
        /* there are user data need be sent */
        printf("socket %d, data to be sent %d bytes\n",psingle_conn_param->sock_fd,length);
#ifdef SERVER_SSL_ENABLE
        ssl_write(psingle_conn_param->ssl, pbuf, length);
#else
        write(psingle_conn_param->sock_fd, pbuf, length);
#endif
    } else {
        /* no user app data need be sent */
        printf("socket %d, data to be send %d bytes\n",psingle_conn_param->sock_fd,length);
#ifdef SERVER_SSL_ENABLE
        ssl_write(psingle_conn_param->ssl, httphead, length);
#else
        write(psingle_conn_param->sock_fd, httphead, length);
#endif
    }
    
    if (pbuf) {
        free(pbuf);
        pbuf = NULL;
    }
    printf("data send over\n");
}

/******************************************************************************
 * FunctionName : json_send
 * Description  : processing the data as json format and send to the client or server
 * Parameters   : arg -- argument to set for client or server
 *                ParmType -- json format type
 * Returns      : none
*******************************************************************************/
LOCAL void  
json_send(struct single_conn_param *psingle_conn_param, ParmType ParmType)
{
    char *pbuf;
    cJSON *pcjson;
    int ret=0;

    pbuf = NULL;;
    pcjson=cJSON_CreateObject();
    if(NULL == pcjson){
        WS_DEBUG(" ERROR! cJSON_CreateObject fail!\n");
        return;
    }
    switch (ParmType) {
#if LIGHT_DEVICE

        case LIGHT_STATUS:
            ret=light_status_get(pcjson, "light");
            break;
#endif

#if PLUG_DEVICE

        case SWITCH_STATUS:
            ret=switch_status_get(pcjson,"switch");
            break;
#endif
        case INFOMATION:
            ret=system_info_get(pcjson,"INFOMATION");
            break;

        case WIFI:
            ret=wifi_info_get(pcjson,"wifi");
            break;

        case CONNECT_STATUS:
            ret=connect_status_get(pcjson, "info");
            break;

        case USER_BIN:
            ret=user_binfo_get(pcjson, "user_info");
            break;
        case SCAN: {
            u8 i = 0;
            u8 scancount = 0;
            struct bss_info *bss = NULL;
            bss = STAILQ_FIRST(pscaninfo->pbss);

            if (bss == NULL) {
                free(pscaninfo);
                pscaninfo = NULL;
                pbuf = (char *)zalloc(100);
                sprintf(pbuf, "{\n\"successful\": false,\n\"data\": null\n}");
            } else if (pscaninfo->page_sn == pscaninfo->pagenum) {
                pbuf = (char *)zalloc(100);
                pscaninfo->page_sn = 0;
                sprintf(pbuf, "{\n\"successful\": false,\n\"meessage\": \"repeated page\"\n}");
                
            } else if (pscaninfo->data_cnt > scannum) {
                pbuf = (char *)zalloc(100);
                pscaninfo->data_cnt -= 8;
                sprintf(pbuf, "{\n\"successful\": false,\n\"meessage\": \"error page\"\n}");
                
            } else {
                pscaninfo->data_cnt += 8;
                pscaninfo->page_sn = pscaninfo->pagenum;
                ret=scan_info_get(pcjson, "scan"); //scan_tree
            }

            break;
        }

        default :
            break;
    }

    if(ret == 0){
        
        if(pbuf == NULL){
        pbuf = cJSON_Print(pcjson);
        }
        WS_DEBUG(">> send buf length is %d  \n",strlen(pbuf));
        data_send(psingle_conn_param, true, pbuf);
    }
    cJSON_Delete(pcjson);
    free(pbuf);
    pbuf=NULL;
}

/******************************************************************************
 * FunctionName : response_send
 * Description  : processing the send result
 * Parameters   : arg -- argument to set for client or server
 *                responseOK --  true or false
 * Returns      : none
*******************************************************************************/
LOCAL void  
response_send(struct single_conn_param *psingle_conn_param, bool responseOK)
{
    data_send(psingle_conn_param, responseOK, NULL);

}

/******************************************************************************
 * FunctionName : json_scan_cb
 * Description  : processing the scan result
 * Parameters   : arg -- Additional argument by the callback function
 *                status -- scan status
 * Returns      : none
*******************************************************************************/
int runed =0;
LOCAL void   json_scan_cb(void *arg, STATUS status)
{
    char *pbuf=NULL;

    pscaninfo->pbss = arg;

    if (scannum % 8 == 0) {
        pscaninfo->totalpage = scannum / 8;
    } else {
        pscaninfo->totalpage = scannum / 8 + 1;
    }

    cJSON * pJson = cJSON_CreateObject();
    if(NULL == pJson){
        WS_DEBUG("pSubJson_con_status creat fail\n");
        goto error_handle;
    }

    cJSON * pSubJson_Response = cJSON_CreateObject();
    if(NULL == pSubJson_Response){
        WS_DEBUG("pSubJson_Response creat fail\n");
        goto error_handle;
    }
    cJSON_AddItemToObject(pJson, "Response", pSubJson_Response);
    
    cJSON_AddNumberToObject(pJson, "TotalPage", pscaninfo->totalpage);
    
/*
    cJSON * pSubJson_Arry = cJSON_CreateArray();
    if(NULL == pSubJson_Arry){
        WS_DEBUG("pSubJson_Arry creat fail\n");
        goto error_handle;
    }
    cJSON_AddItemToObject(pSubJson_Response, "TotalPage", pSubJson_Arry); 

    if(scan_result_output(pSubJson_Arry, 0) != 0){ //get total scan page to arry
        WS_DEBUG("pSubJson_Arry get scan page fail\n");
        goto error_handle;
    }
*/
    
    pbuf = cJSON_Print(pJson);
    printf("\n %s \r\n", pbuf);
    data_send(pscaninfo->psingle_conn_param, true, pbuf);

error_handle:
    cJSON_Delete(pJson);
    if(pbuf != NULL) free(pbuf);
    
}

/******************************************************************************
 * FunctionName : local_upgrade_deinit
 * Description  : check upgrade result
 * Parameters   : sock_fd--socket handler
 * Returns      : none
*******************************************************************************/
void  
upgrade_check_func(struct single_conn_param *psingle_conn_param)
{

    os_timer_disarm(&upgrade_check_timer);
    if(system_upgrade_flag_check() == UPGRADE_FLAG_START) {
        
        WS_DEBUG("local upgrade failed\n");
        response_send(psingle_conn_param, false);
        system_upgrade_deinit();
        system_upgrade_flag_set(UPGRADE_FLAG_IDLE);
        
    } else if( system_upgrade_flag_check() == UPGRADE_FLAG_FINISH ) {
        WS_DEBUG("local upgrade success\n");
        response_send(psingle_conn_param, true);
    }
    upgrade_lock = 0;
}

/******************************************************************************
 * FunctionName : local_upgrade_deinit
 * Description  : disconnect the connection with the host
 * Parameters   : void
 * Returns      : none
*******************************************************************************/
LOCAL void  
local_upgrade_deinit(void)
{
    if (system_upgrade_flag_check() != UPGRADE_FLAG_START) {
        WS_DEBUG("system upgrade deinit\n");
        system_upgrade_deinit();
    }
}
/******************************************************************************
 * FunctionName : local_upgrade_download
 * Description  : Processing the upgrade data from the host
 * Parameters   : sock_fd -- socket handler
 *                pusrdata -- The upgrade data (or NULL when the connection has been closed!)
 *                length -- The length of upgrade data
 * Returns      : none
*******************************************************************************/
LOCAL void  
local_upgrade_download(struct single_conn_param *psingle_conn_param,char *pusrdata, unsigned short length)
{
    char *ptr = NULL;
    char *ptmp2 = NULL;
    char lengthbuffer[32];
    static uint32 totallength = 0;
    static uint32 sumlength = 0;
    char A_buf[2] = {0xE9 ,0x03}; char  B_buf[2] = {0xEA,0x04};

    if (totallength == 0 && (ptr = (char *)strstr(pusrdata, "\r\n\r\n")) != NULL &&
            (ptr = (char *)strstr(pusrdata, "Content-Length")) != NULL) {
        ptr = (char *)strstr(pusrdata, "Content-Length: ");
        if (ptr != NULL) {
            ptr += 16;
            ptmp2 = (char *)strstr(ptr, "\r\n");

            if (ptmp2 != NULL) {
                memset(lengthbuffer, 0, sizeof(lengthbuffer));
                
                if(  (ptmp2 - ptr) <= 32 )
                    memcpy(lengthbuffer, ptr, ptmp2 - ptr);
                else
                    os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, (ptmp2 - ptr) );
                
                sumlength = atoi(lengthbuffer);
                printf("Userbin sumlength %dB\n", sumlength);
            } else {
                WS_DEBUG("sumlength failed\n");
            }
        } else {
            WS_DEBUG("Content-Length: failed\n");
        }

        ptr = (char *)strstr(pusrdata, "\r\n\r\n");
        length -= ptr - pusrdata;
        length -= 4;
        totallength += length;
        WS_DEBUG("Userbin load start.\n");
        system_upgrade(ptr + 4, length);

    } else {
        totallength += length;
        printf("data load %dB\n",length);
        system_upgrade(pusrdata, length);
    }

    if (totallength == sumlength) {
        WS_DEBUG("upgrade file download finished.\n");
        system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
        totallength = 0;
        sumlength = 0;
        upgrade_check_func(psingle_conn_param);
        os_timer_disarm(&app_upgrade_10s);
        os_timer_setfn(&app_upgrade_10s, (os_timer_func_t *)local_upgrade_deinit, NULL);
        os_timer_arm(&app_upgrade_10s, 10, 0);
    }
}

/******************************************************************************
 * FunctionName : webserver_conn_watcher
 * Description  : stop- timer handler
 * Parameters   : index -- webserver connection index;
 * Returns      : none
*******************************************************************************/
LOCAL  
void webserver_conn_watcher(struct single_conn_param * psingle_conn)
{
    os_timer_disarm(&psingle_conn->stop_watch);
    psingle_conn->timeout = 1;
    
    WS_DEBUG("webserver watcher sock_fd %d timeout!\n",psingle_conn->sock_fd);
}

/******************************************************************************
 * FunctionName : webserver_recvdata_process
 * Description  : Processing the received data from the server
 * Parameters   : arg -- Additional argument to pass to the sub function
 *                pusrdata -- The received data (or NULL when the connection has been closed!)
 *                length -- The length of received data
 * Returns      : none
*******************************************************************************/
LOCAL void  
webserver_recvdata_process(struct single_conn_param *psingle_conn_param, char *pusrdata, unsigned short length)
{
    URL_Frame *pURL_Frame = NULL;
    char *pParseBuffer = NULL;
    bool parse_flag = false;

    if(upgrade_lock == 0){

        if(check_data(pusrdata, length) == false)
        {
            WS_DEBUG("invalid data, goto _temp_exit\n");
            goto temp_exit;
        }
        parse_flag = save_data(pusrdata, length);
        if (parse_flag == false) {
            response_send(psingle_conn_param, false);
            goto temp_exit;
        }
        pURL_Frame = (URL_Frame *)zalloc(sizeof(URL_Frame));
        parse_url(precvbuffer, pURL_Frame);
        switch (pURL_Frame->Type) {
            case GET:
                WS_DEBUG("We have a GET request.\n");
                if (strcmp(pURL_Frame->pSelect, "client") == 0 &&
                        strcmp(pURL_Frame->pCommand, "command") == 0) {
                        
                    if (strcmp(pURL_Frame->pFilename, "info") == 0) {
                        json_send(psingle_conn_param, INFOMATION);
                        
                    }else if (strcmp(pURL_Frame->pFilename, "status") == 0) {
                        json_send(psingle_conn_param, CONNECT_STATUS);
                        
                    } else if (strcmp(pURL_Frame->pFilename, "scan") == 0) {
                        char *pstrstr = NULL;
                        pstrstr = (char *)strstr(pusrdata, "&");

                        if (pstrstr == NULL) {
                            if (pscaninfo == NULL) {
                                pscaninfo = (scaninfo *)zalloc(sizeof(scaninfo));
                            }
                            pscaninfo->psingle_conn_param= psingle_conn_param;
                            pscaninfo->pagenum = 0;
                            pscaninfo->page_sn = 0;
                            pscaninfo->data_cnt = 0;
                            wifi_station_scan(NULL, json_scan_cb);
                            
                        } else {
                            pstrstr ++;
                            if (strncmp(pstrstr, "page", 4) == 0) {
                                if (pscaninfo != NULL) {
                                    pscaninfo->pagenum = *(pstrstr + 5);
                                    pscaninfo->pagenum -= 0x30;

                                    if (pscaninfo->pagenum > pscaninfo->totalpage || pscaninfo->pagenum == 0) {
                                        response_send(psingle_conn_param, false);
                                    } else {
                                        //printf("scan & page start\n");
                                        json_send(psingle_conn_param, SCAN);
                                    }
                                } else {
                                    response_send(psingle_conn_param, false);
                                }
                            } else {
                                response_send(psingle_conn_param, false);
                            }
                        }
                    } 
                    else {
                        response_send(psingle_conn_param, false);
                    }
                } 

                else if (strcmp(pURL_Frame->pSelect, "config") == 0 &&
                           strcmp(pURL_Frame->pCommand, "command") == 0) {
                    if (strcmp(pURL_Frame->pFilename, "wifi") == 0) {
                        ap_conf = (struct softap_config *)zalloc(sizeof(struct softap_config));
                        sta_conf = (struct station_config *)zalloc(sizeof(struct station_config));
                        json_send(psingle_conn_param, WIFI);
                        free(sta_conf);
                        free(ap_conf);
                        sta_conf = NULL;
                        ap_conf = NULL;
                    }
#if PLUG_DEVICE
                    else if (strcmp(pURL_Frame->pFilename, "switch") == 0) {
                        json_send(psingle_conn_param, SWITCH_STATUS);
                    }
#endif

#if LIGHT_DEVICE
                    else if (strcmp(pURL_Frame->pFilename, "light") == 0) {
                        json_send(psingle_conn_param, LIGHT_STATUS);
                    }
#endif
                    else if (strcmp(pURL_Frame->pFilename, "reboot") == 0) {
                        json_send(psingle_conn_param, REBOOT);
                    } else {
                        response_send(psingle_conn_param, false);
                    }
                } 

                else if (strcmp(pURL_Frame->pSelect, "upgrade") == 0 &&
                        strcmp(pURL_Frame->pCommand, "command") == 0) {
                        if (strcmp(pURL_Frame->pFilename, "getuser") == 0) {
                            json_send(psingle_conn_param , USER_BIN);
                        }
                } else {
                    response_send(psingle_conn_param, false);
                }

                break;

            case POST:
                WS_DEBUG("We have a POST request.\n");
                pParseBuffer = (char *)strstr(precvbuffer, "\r\n\r\n");

                if (pParseBuffer == NULL) {
                    break;
                }
                pParseBuffer += 4;

                if (strcmp(pURL_Frame->pSelect, "config") == 0 &&
                        strcmp(pURL_Frame->pCommand, "command") == 0) {
#if SENSOR_DEVICE
                    if (strcmp(pURL_Frame->pFilename, "sleep") == 0) 
#else
                    if (strcmp(pURL_Frame->pFilename, "reboot") == 0) 
#endif
                    {
                        if (pParseBuffer != NULL) {
                            if (restart_10ms != NULL) {
                                os_timer_disarm(restart_10ms);
                            }

                            if (rstparm == NULL) {
                                rstparm = (rst_parm *)zalloc(sizeof(rst_parm));
                            }
                            rstparm->pconnpara = (struct conn_param*)&connections;
#if SENSOR_DEVICE
                            rstparm->parmtype = DEEP_SLEEP;
#else
                            rstparm->parmtype = REBOOT;
#endif
                            if (restart_10ms == NULL) {
                                restart_10ms = (os_timer_t *)malloc(sizeof(os_timer_t));
                                if(NULL == restart_10ms){
                                    printf(">>>recvdata_process,memory exhausted, check it\n");
                                }
                            }
                            
                            os_timer_disarm(restart_10ms);
                            os_timer_setfn(restart_10ms, (os_timer_func_t *)restart_10ms_cb, NULL);
                            os_timer_arm(restart_10ms, 10, 0);  // delay 10ms, then do

                            response_send(psingle_conn_param, true);
                        } else {
                            response_send(psingle_conn_param, false);
                        }
                    } 
                    else if (strcmp(pURL_Frame->pFilename, "wifi") == 0) {
                        if (pParseBuffer != NULL) {
                            user_esp_platform_set_connect_status(DEVICE_CONNECTING);

                            if (restart_10ms != NULL) {
                                os_timer_disarm(restart_10ms);
                            }

                            if (ap_conf == NULL) {
                                ap_conf = (struct softap_config *)zalloc(sizeof(struct softap_config));
                            }

                            if (sta_conf == NULL) {
                                sta_conf = (struct station_config *)zalloc(sizeof(struct station_config));
                            }

                            wifi_req_parse(pParseBuffer);

                            if (rstparm == NULL) {
                                rstparm = (rst_parm *)zalloc(sizeof(rst_parm));
                            }

                            rstparm->pconnpara = (struct conn_param*)&connections;
                            rstparm->parmtype = WIFI;

                            if (sta_conf->ssid[0] != 0x00 || ap_conf->ssid[0] != 0x00) {
                                ap_conf->ssid_hidden = 0;
                                ap_conf->max_connection = 4;

                                if (restart_10ms == NULL) {
                                    restart_10ms = (os_timer_t *)malloc(sizeof(os_timer_t));
                                    if(NULL == restart_10ms){
                                        printf(">>>recvdata_process,memory exhausted, check it\n");
                                    }
                                }

                                os_timer_disarm(restart_10ms);
                                os_timer_setfn(restart_10ms, (os_timer_func_t *)restart_10ms_cb, NULL);
                                os_timer_arm(restart_10ms, 10, 0);  // delay 10ms, then do
                            } else {
                                free(ap_conf);
                                free(sta_conf);
                                free(rstparm);
                                sta_conf = NULL;
                                ap_conf = NULL;
                                rstparm =NULL;
                            }
                            response_send(psingle_conn_param, true);
                            
                        } else {
                            response_send(psingle_conn_param, false);
                        }
                    }
#if PLUG_DEVICE
                    else if (strcmp(pURL_Frame->pFilename, "switch") == 0) {
                        if (pParseBuffer != NULL) {
                            switch_req_parse(pParseBuffer);
                            response_send(psingle_conn_param, true);
                        } else {
                            response_send(psingle_conn_param, false);
                        }
                    }
#endif

#if LIGHT_DEVICE
                    else if (strcmp(pURL_Frame->pFilename, "light") == 0) {
                        if (pParseBuffer != NULL) {
							light_req_parse(pParseBuffer);
							
                            if(PostCmdNeeRsp == 1)
                                response_send(psingle_conn_param, true);
                        } else {
                            response_send(psingle_conn_param, false);
                        }
                    }
                    else if (strcmp(pURL_Frame->pFilename, "reset") == 0) {
                            response_send(psingle_conn_param, true);

                            user_esp_platform_set_active(0);
                            system_restore();
                            system_restart();
                    }
#endif
                    else {
                        response_send(psingle_conn_param, false);
                    }
                }
                else if(strcmp(pURL_Frame->pSelect, "upgrade") == 0 &&
                        strcmp(pURL_Frame->pCommand, "command") == 0){
                    if (strcmp(pURL_Frame->pFilename, "start") == 0){
                        response_send(psingle_conn_param, true);
                        WS_DEBUG("local upgrade start\n");
                        upgrade_lock = 1;
                        system_upgrade_init();
                        system_upgrade_flag_set(UPGRADE_FLAG_START);
                        os_timer_disarm(&upgrade_check_timer);
                        os_timer_setfn(&upgrade_check_timer, (os_timer_func_t *)upgrade_check_func, psingle_conn_param);
                        os_timer_arm(&upgrade_check_timer, 120000, 0);
                    } 
                    else if (strcmp(pURL_Frame->pFilename, "reset") == 0) {
                        response_send(psingle_conn_param, true);
                        WS_DEBUG("local upgrade reboot\n");
                        system_upgrade_reboot();
                    } 
                    else {
                        response_send(psingle_conn_param, false);
                    }
                }else {
                    response_send(psingle_conn_param, false);
                }
                 break;
        }
        
        temp_exit:
        if (precvbuffer != NULL){
            free(precvbuffer);
            precvbuffer = NULL;
        }
        
        if(pURL_Frame != NULL){
            free(pURL_Frame);
            pURL_Frame = NULL;
        }

    }
    else if(upgrade_lock == 1){
        local_upgrade_download(psingle_conn_param,pusrdata, length);
        if (precvbuffer != NULL){
            free(precvbuffer);
            precvbuffer = NULL;
        }
        free(pURL_Frame);
        pURL_Frame = NULL;
    }

}

#ifdef SERVER_SSL_ENABLE
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

/******************************************************************************
 * FunctionName : webserver_recv_thread
 * Description  : recieve and process data form client 
 * Parameters   : noe
 * Returns      : none
*******************************************************************************/
#define RECV_BUF_SIZE 2048
LOCAL void   
 webserver_recv_thread(void *pvParameters)
{
    int ret;
    u8  index;
    
    int stack_counter=0;
    bool ValueFromReceive=FALSE;
    portBASE_TYPE xStatus;
    
    u32 maxfdp = 0;
    fd_set readset;
    struct timeval timeout;
    struct conn_param* pconnections=(struct conn_param*) pvParameters;

    //char recvbuf[2048];
    char *precvbuf = (char*)malloc(RECV_BUF_SIZE);
    if(NULL == precvbuf){
        printf(">>>recv_thread, memory exhausted, check it\n");
    }

#ifdef SERVER_SSL_ENABLE
    u8  quiet = FALSE;
    u8 *read_buf = NULL;
    SSL_CTX *ssl_ctx = NULL;
    
    if ((ssl_ctx = ssl_ctx_new(SSL_DISPLAY_CERTS, SSL_DEFAULT_SVR_SESS)) == NULL) {
        printf("Error: Server context is invalid\n");
    }
#endif

    while(1){
        
        xStatus = xQueueReceive(RCVQueueStop,&ValueFromReceive,0);
        if ( pdPASS == xStatus && TRUE == ValueFromReceive){
            WS_DEBUG("webserver_recv_thread rcv exit signal!\n");
            break;
        }

        while(pconnections->conn_num == 0){
            vTaskDelay(1000/portTICK_RATE_MS);
            /*if no client coming in, wait in big loop*/
            continue;
        }

        /*clear fdset, and set the selct function wait time*/
        maxfdp = 0;
        FD_ZERO(&readset);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        for(index=0; index < MAX_CLIENT_NUMBER; index++){
            //find all valid handle 
            if(pconnections->single_conn[index]->sock_fd >= 0){
                FD_SET(pconnections->single_conn[index]->sock_fd, &readset);
                maxfdp =max(pconnections->single_conn[index]->sock_fd, maxfdp);
            }
        }

        //polling all exist client handle
        ret = select(maxfdp+1, &readset, NULL, NULL, &timeout);
        if(ret > 0){
            
#ifdef SERVER_SSL_ENABLE

            for(index=0; index < MAX_CLIENT_NUMBER; index++){
                /* IF this handle there is data/event aviliable, recive it*/
                if (FD_ISSET(pconnections->single_conn[index]->sock_fd, &readset))
                {
                    /*stop the sock handle watchout timer */
                    os_timer_disarm((os_timer_t *)&pconnections->single_conn[index]->stop_watch);
                
                    if(NULL == pconnections->single_conn[index]->ssl){
                        pconnections->single_conn[index]->ssl = ssl_server_new(ssl_ctx, pconnections->single_conn[index]->sock_fd);
                    }
                    
                    if ((ret = ssl_read(pconnections->single_conn[index]->ssl, &read_buf)) == SSL_OK) {
                        /* in the middle of doing a handshake */
                        if (ssl_handshake_status(pconnections->single_conn[index]->ssl) == SSL_OK) {
                            if (!quiet) {
                                display_session_id(pconnections->single_conn[index]->ssl);
                                display_cipher(pconnections->single_conn[index]->ssl);
                                printf("connection handshake ok!\n");
                                quiet = true;
                            }
                        }
                    }
                    
                    if (ret > SSL_OK) {  
                        WS_DEBUG("webserver_recv_thread recv and process sockfd %d!\n",pconnections->single_conn[index]->sock_fd);
                        webserver_recvdata_process(pconnections->single_conn[index],read_buf,ret);

                        /*restart the sock handle watchout timer */
                        os_timer_setfn((os_timer_t *)&pconnections->single_conn[index]->stop_watch, (os_timer_func_t *)webserver_conn_watcher, pconnections->single_conn[index]);
                        os_timer_arm((os_timer_t *)&pconnections->single_conn[index]->stop_watch, STOP_TIMER, 0);
                        
                    } else if (ret == SSL_CLOSE_NOTIFY) {
                        WS_DEBUG("shutting down SSL\n");
                        
                    } else if (ret < SSL_OK) {
                    
                        WS_DEBUG("webserver_recv_thread CONNECTION CLOSED index %d !\n",index);
                        ssl_free(pconnections->single_conn[index]->ssl);
                        close(pconnections->single_conn[index]->sock_fd);
                        pconnections->single_conn[index]->sock_fd = -1;
                        pconnections->single_conn[index]->ssl = NULL;
                        pconnections->conn_num--;
                    }
                }
                /* IF this handle there is no data/event aviliable, check the timeout flag*/
                else if(pconnections->single_conn[index]->timeout == 1){
                
                    WS_DEBUG("webserver_recv_thread index %d timeout,close!\n",index);
                    ssl_free(pconnections->single_conn[index]->ssl);
                    close(pconnections->single_conn[index]->sock_fd);
                    pconnections->single_conn[index]->sock_fd = -1;
                    pconnections->single_conn[index]->ssl = NULL;
                    pconnections->conn_num--;
                }
            }
                
#else
            for(index=0; index < MAX_CLIENT_NUMBER; index++){
                /* IF this handle there is data/event aviliable, recive it*/
                if (FD_ISSET(pconnections->single_conn[index]->sock_fd, &readset))
                {
                    /*stop the sock handle watchout timer */
                    os_timer_disarm((os_timer_t *)&pconnections->single_conn[index]->stop_watch);
                    memset(precvbuf, 0, RECV_BUF_SIZE);

                    ret=recv(pconnections->single_conn[index]->sock_fd,precvbuf,RECV_BUF_SIZE,0);
                    if(ret > 0){
/*
                        struct sockaddr name;
                        struct sockaddr_in *piname;
                        int len = sizeof(name);
                        getpeername(pconnections->single_conn[index]->sock_fd, &name, (socklen_t *)&len);
                        piname  = (struct sockaddr_in *)&name;      
*/
                        WS_DEBUG("webserver recv sockfd %d\n",pconnections->single_conn[index]->sock_fd);
                        webserver_recvdata_process(pconnections->single_conn[index],precvbuf,ret);

                        /*restart the sock handle watchout timer */
                        os_timer_setfn((os_timer_t *)&pconnections->single_conn[index]->stop_watch, (os_timer_func_t *)webserver_conn_watcher, pconnections->single_conn[index]);
                        os_timer_arm((os_timer_t *)&pconnections->single_conn[index]->stop_watch, STOP_TIMER, 0);
//                      printf(">>> heapsize %d\n",system_get_free_heap_size());    
                    }else{
                        //recv error,connection close
                        WS_DEBUG("webserver close sockfd %d !\n",pconnections->single_conn[index]->sock_fd);
                        close(pconnections->single_conn[index]->sock_fd);
                        pconnections->single_conn[index]->sock_fd = -1;
                        pconnections->conn_num--;
                      
                    }
                    
                }
                /* IF this handle there is no data/event aviliable, check the status*/
                else if(pconnections->single_conn[index]->timeout == 1){

                    WS_DEBUG("webserver sockfd %d timeout,close!\n",pconnections->single_conn[index]->sock_fd);
                    close(pconnections->single_conn[index]->sock_fd);
                    pconnections->single_conn[index]->sock_fd = -1;
                    pconnections->conn_num--;
                }
            }
#endif
            
        }else if(ret == -1){
            //select timerout out, wait again. 
            WS_DEBUG("##WS select timeout##\n");
        }
        
        /*for develop test only*/
        if(stack_counter++ ==1){
            stack_counter=0;
            WS_DEBUG("webserver_recv_thread %d word left\n",uxTaskGetStackHighWaterMark(NULL));
        }
        
    }

    for(index=0; index < MAX_CLIENT_NUMBER && pconnections->conn_num; index++){
        //find all valid handle 
        if(pconnections->single_conn[index]->sock_fd >= 0){
            os_timer_disarm((os_timer_t *)&pconnections->single_conn[index]->stop_watch);
#ifdef SERVER_SSL_ENABLE
            ssl_free(pconnections->single_conn[index]->ssl);
#endif
            close(pconnections->single_conn[index]->sock_fd);
            pconnections->conn_num --;
        }
    }
    
#ifdef SERVER_SSL_ENABLE
    ssl_ctx_free(ssl_ctx);
#endif

    if(NULL != precvbuf){
        free(precvbuf);
    }
    vQueueDelete(RCVQueueStop);
    RCVQueueStop = NULL;
    vTaskDelete(NULL);
}

void   webserver_recv_task_start(struct conn_param* pconnections)
{
    if (RCVQueueStop == NULL)
        RCVQueueStop = xQueueCreate(1,1);
    
    if (RCVQueueStop != NULL){
        sys_thread_new("websrecv_thread", webserver_recv_thread, pconnections, 512, 6);//1024, 704 left 320 used
    }
}

sint8   webserver_recv_task_stop(void)
{
    bool ValueToSend = true;
    portBASE_TYPE xStatus;
    if (RCVQueueStop == NULL)
        return -1;

    xStatus = xQueueSend(RCVQueueStop,&ValueToSend,0);
    if (xStatus != pdPASS){
        WS_DEBUG("WEB SEVER Could not send to the rcvqueue!\n");
        return -1;
    } else {
        taskYIELD();
        return pdPASS;
    }
}

/******************************************************************************
 * FunctionName : multi_conn_init
 * Description  : parameter initialize as connections set
 * Parameters   : pconnections
 * Returns      : none
*******************************************************************************/
LOCAL   multi_conn_init(void)
{
    u8 index;
    
    for(index=0; index < MAX_CLIENT_NUMBER; index++){
        single_conn[index] = (struct single_conn_param *)zalloc(sizeof(struct single_conn_param));
        single_conn[index]->sock_fd = -1;
        single_conn[index]->timeout =  0;
    }

    connections.single_conn=single_conn;
    connections.conn_num = 0;/*should init to zero*/

    WS_DEBUG("C > multi_conn_init ok!\n");

}

/******************************************************************************
 * FunctionName : user_webserver_task
 * Description  : parameter initialize as a server
 * Parameters   : null
 * Returns      : none
*******************************************************************************/
LOCAL void   
user_webserver_task(void *pvParameters)
{
    int32 listenfd;
    int32 remotefd;
    int32 len;
    int32 ret;
    u8    index;
    
    struct ip_info ipconfig;
    struct conn_param* pconnections;
    struct sockaddr_in server_addr,remote_addr;

    portBASE_TYPE xStatus;
    bool ValueFromReceive = false;

    int stack_counter=0;
    
    /* Construct local address structure */
    memset(&server_addr, 0, sizeof(server_addr)); /* Zero out structure */
    server_addr.sin_family = AF_INET;            /* Internet address family */
    server_addr.sin_addr.s_addr = INADDR_ANY;   /* Any incoming interface */
    server_addr.sin_len = sizeof(server_addr);  
#ifdef SERVER_SSL_ENABLE
    server_addr.sin_port = htons(WEB_SERVER_SSL_PORT); /* Local SSL port */
#else
    server_addr.sin_port = htons(WEB_SERVER_PORT); /* Local port */
#endif

    /* Create socket for incoming connections */
    do{
        listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd == -1) {
            WS_DEBUG("C > user_webserver_task failed to create sock!\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
    }while(listenfd == -1);

    /* Bind to the local address */
    do{
        ret = bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret != 0) {
            WS_DEBUG("C > user_webserver_task failed to bind sock!\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
    }while(ret != 0);

    do{
        /* Listen to the local connection */
        ret = listen(listenfd, MAX_CLIENT_NUMBER);
        if (ret != 0) {
            WS_DEBUG("C > user_webserver_task failed to set listen queue!\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
        
    }while(ret != 0);
    
    /*initialize as connections set*/
    multi_conn_init();
    pconnections = &connections;
    
    /*start a task to recv data from client*/
    webserver_recv_task_start(pconnections);

    while(1)
    {

      xStatus = xQueueReceive(QueueStop,&ValueFromReceive,0);
      if ( pdPASS == xStatus && TRUE == ValueFromReceive){
          WS_DEBUG("user_webserver_task rcv exit signal!\n");
          break;
      }

      /*block here waiting remote connect request*/
      len = sizeof(struct sockaddr_in);
      remotefd = accept(listenfd, (struct sockaddr *)&remote_addr, (socklen_t *)&len);
      if(remotefd != -1)
      {
        index=0;

        //find the fisrt usable connections param to save the handle.
        for(index=0; index < MAX_CLIENT_NUMBER; index++){
            if(pconnections->single_conn[index]->sock_fd < 0)break;
        }

        if(index < MAX_CLIENT_NUMBER)
        {
            pconnections->conn_num++;
            pconnections->single_conn[index]->sock_fd = remotefd;
            os_timer_disarm(&pconnections->single_conn[index]->stop_watch);
            os_timer_setfn(&pconnections->single_conn[index]->stop_watch, (os_timer_func_t *)webserver_conn_watcher, pconnections->single_conn[index]);
            os_timer_arm(&pconnections->single_conn[index]->stop_watch, STOP_TIMER, 0);
            WS_DEBUG("WEB SERVER acpt index:%d sockfd %d!\n",index,remotefd);
        }else{

            close(remotefd);
            WS_DEBUG("WEB SERVER TOO MUCH CONNECTION, CHECK ITer!\n");
        }

      }else{
          WS_DEBUG("WEB SERVER remote error: %d, WARNING!\n",remotefd);
      }

        if(stack_counter++ ==1){
            stack_counter=0;
            WS_DEBUG("user_webserver_task %d word left\n",uxTaskGetStackHighWaterMark(NULL));
        }

    }
    
    close(listenfd);
    vQueueDelete(QueueStop);
    QueueStop = NULL;
    vTaskDelete(NULL);
}

/******************************************************************************
 * FunctionName : user_webserver_start
 * Description  : start the web server task
 * Parameters   : noe
 * Returns      : none
*******************************************************************************/
void   user_webserver_start(void)
{

    if (QueueStop == NULL)
        QueueStop = xQueueCreate(1,1);

    if (QueueStop != NULL)
        xTaskCreate(user_webserver_task, "user_webserver", 280, NULL, 4, NULL);//512, 376 left,136 used
}

/******************************************************************************
 * FunctionName : user_webserver_stop
 * Description  : stop the task
 * Parameters   : void
 * Returns      : none
*******************************************************************************/
sint8   user_webserver_stop(void)
{
    bool ValueToSend = true;
    portBASE_TYPE xStatus;
    if (QueueStop == NULL)
        return -1;

    xStatus = xQueueSend(QueueStop,&ValueToSend,0);
    if (xStatus != pdPASS){
        WS_DEBUG("WEB SEVER Could not send to the queue!\n");
        return -1;
    } else {
        taskYIELD();
        return pdPASS;
    }
}

#endif
