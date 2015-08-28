/******************************************************************************
 * Copyright (C) 2014 -2016  Espressif System
 *
 * FileName: user_devicefind.c
 *
 * Description: Find your hardware's information while working any mode.
 *
 * Modification history:
 * 2015/7/1, v1.0 create this file.
*******************************************************************************/
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "user_devicefind.h"
#include "user_config.h"

//#define DEBUG

#ifdef DEBUG
#define DF_DEBUG os_printf
#else
#define DF_DEBUG
#endif

LOCAL xQueueHandle QueueStop = NULL;

#define UDF_SERVER_PORT     1025

const char *device_find_request = "Are You Espressif IOT Smart Device?";
#if PLUG_DEVICE
const char *device_find_response_ok = "I'm Plug.";
#elif LIGHT_DEVICE
const char *device_find_response_ok = "I'm Light.";
#elif SENSOR_DEVICE
#if HUMITURE_SUB_DEVICE
const char *device_find_response_ok = "I'm Humiture.";
#elif FLAMMABLE_GAS_SUB_DEVICE
const char *device_find_response_ok = "I'm Flammable Gas.";
#endif
#endif

#define len_udp_msg 70
#define len_mac_msg 70
#define len_ip_rsp  50
/*---------------------------------------------------------------------------*/
LOCAL int32 sock_fd; 

/******************************************************************************
 * FunctionName : user_devicefind_data_process
 * Description  : Processing the received data from the host
 * Parameters   : arg -- Additional argument to pass to the callback function
 *                pusrdata -- The received data (or NULL when the connection has been closed!)
 *                length -- The length of received data
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_devicefind_data_process(char *pusrdata, unsigned short length, struct sockaddr_in *premote_addr)
{
    char *DeviceBuffer;//40
    char *Device_mac_buffer;//60
    char hwaddr[6];
    int   len;
    
    struct ip_info ipconfig;
    struct ip_info ipconfig_r;
    
    if (pusrdata == NULL) {
        return;
    }

    if (wifi_get_opmode() != STATION_MODE) {
        
        wifi_get_ip_info(SOFTAP_IF, &ipconfig);
        wifi_get_macaddr(SOFTAP_IF, hwaddr);
        
        inet_addr_to_ipaddr(&ipconfig_r.ip,&premote_addr->sin_addr);
        if(!ip_addr_netcmp(&ipconfig_r.ip, &ipconfig.ip, &ipconfig.netmask)) {
            //printf("udpclient connect with sta\n");
            wifi_get_ip_info(STATION_IF, &ipconfig);
            wifi_get_macaddr(STATION_IF, hwaddr);
        }
    } else {
        wifi_get_ip_info(STATION_IF, &ipconfig);
        wifi_get_macaddr(STATION_IF, hwaddr);
    }
    
    //DF_DEBUG("%s\n", pusrdata);
    DeviceBuffer = (char*)malloc(len_ip_rsp);
    memset(DeviceBuffer, 0, len_ip_rsp);

    if (length == strlen(device_find_request) &&
        strncmp(pusrdata, device_find_request, strlen(device_find_request)) == 0) {
        sprintf(DeviceBuffer, "%s" MACSTR " " IPSTR, device_find_response_ok,
               MAC2STR(hwaddr), IP2STR(&ipconfig.ip));

        length = strlen(DeviceBuffer);
        DF_DEBUG("%s %d\n", DeviceBuffer,length);

        sendto(sock_fd,DeviceBuffer, length, 0, (struct sockaddr *)premote_addr, sizeof(struct sockaddr_in));
        
    } else if (length == (strlen(device_find_request) + 18)) {
    
        Device_mac_buffer = (char*)malloc(len_mac_msg);
        memset(Device_mac_buffer, 0, len_mac_msg);
        sprintf(Device_mac_buffer, "%s " MACSTR , device_find_request, MAC2STR(hwaddr));
        
        if (strncmp(Device_mac_buffer, pusrdata, strlen(device_find_request) + 18) == 0){

            sprintf(DeviceBuffer, "%s" MACSTR " " IPSTR, device_find_response_ok,
                       MAC2STR(hwaddr), IP2STR(&ipconfig.ip));

            length = strlen(DeviceBuffer);
            DF_DEBUG("%s %d\n", DeviceBuffer,length);
            
            sendto(sock_fd,DeviceBuffer, length, 0, (struct sockaddr *)premote_addr, sizeof(struct sockaddr_in));
        } 

        if(Device_mac_buffer)free(Device_mac_buffer);
    }

    if(DeviceBuffer)free(DeviceBuffer);
    
}

/******************************************************************************
 * FunctionName : user_devicefind_init
 * Description  : the espconn struct parame init
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
LOCAL void  
user_devicefind_task(void *pvParameters)
{
    struct sockaddr_in server_addr;
    int32 ret;
    
    struct sockaddr_in from;
    socklen_t   fromlen;
    struct ip_info ipconfig;
    
    char  *udp_msg;
    bool ValueFromReceive = false;
    portBASE_TYPE xStatus;

    int nNetTimeout=10000;// 1 Sec
    int stack_counter=0;

    memset(&ipconfig, 0, sizeof(ipconfig));
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;       
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(UDF_SERVER_PORT);
    server_addr.sin_len = sizeof(server_addr);

    udp_msg = (char*)malloc(len_udp_msg);

    do{
        sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_fd == -1) {
            os_printf("ERROR:devicefind failed to create sock!\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
    }while(sock_fd == -1);

    do{
        ret = bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret != 0) {
            os_printf("ERROR:devicefind failed to bind sock!\n");
            vTaskDelay(1000/portTICK_RATE_MS);
        }
    }while(ret != 0);


    while(1){
        
        xStatus = xQueueReceive(QueueStop,&ValueFromReceive,0);
        if ( pdPASS == xStatus && TRUE == ValueFromReceive){
            os_printf("user_devicefind_task rcv exit signal!\n");
            break;
        }

        memset(udp_msg, 0, len_udp_msg);
        memset(&from, 0, sizeof(from));
        
        setsockopt(sock_fd,SOL_SOCKET,SO_RCVTIMEO,(char *)&nNetTimeout,sizeof(int));
        fromlen = sizeof(struct sockaddr_in);
        ret = recvfrom(sock_fd, (u8 *)udp_msg, len_udp_msg, 0,(struct sockaddr *)&from,(socklen_t *)&fromlen);
        if (ret > 0) {
            os_printf("recieve from->port %d  %s\n",ntohs(from.sin_port),inet_ntoa(from.sin_addr));
            user_devicefind_data_process(udp_msg,ret,&from);
        }
        
        if(stack_counter++ ==1){
            stack_counter=0;
            DF_DEBUG("user_devicefind_task %d word left\n",uxTaskGetStackHighWaterMark(NULL));
        }
    }

    if(udp_msg)free(udp_msg);

    close(sock_fd);
    vQueueDelete(QueueStop);
    QueueStop = NULL;
    vTaskDelete(NULL);

}

/*
LOCAL void  
user_devicefind_task1(void *pvParameters)
{
    int stack_counter =0;
    while(1){
        vTaskDelay(1000/portTICK_RATE_MS);
        if(stack_counter++ ==2){
            stack_counter=0;
            DF_DEBUG("user_devicefind_task %d word left\n",uxTaskGetStackHighWaterMark(NULL));//256-186=70
        }
    }
}
*/

void   user_devicefind_start(void)
{
    if (QueueStop == NULL)
        QueueStop = xQueueCreate(1,1);

    if (QueueStop != NULL)
        xTaskCreate(user_devicefind_task, "user_devicefind", 256, NULL, 3, NULL);
    
}

sint8   user_devicefind_stop(void)
{
    bool ValueToSend = true;
    portBASE_TYPE xStatus;
    if (QueueStop == NULL)
        return -1;

    xStatus = xQueueSend(QueueStop,&ValueToSend,0);
    if (xStatus != pdPASS){
        os_printf("Could not send to the queue!\n");
        return -1;
    } else {
        taskYIELD();
        return pdPASS;
    }
}

