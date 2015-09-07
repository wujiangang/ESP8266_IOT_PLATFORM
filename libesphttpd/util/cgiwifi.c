/*
Cgi/template routines for the /wifi url.
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */


#include <esp8266.h>
#include "cgiwifi.h"

//Enable this to disallow any changes in AP settings
//#define DEMO_MODE

//WiFi access point data
typedef struct {
	char ssid[32];
	char rssi;
	char enc;
} ApData;

//Scan result
typedef struct {
	char scanInProgress; //if 1, don't access the underlying stuff from the webpage.
	ApData **apData;
	int noAps;
} ScanResultData;

//Static scan status storage.
static ScanResultData cgiWifiAps;

#define CONNTRY_IDLE 0
#define CONNTRY_WORKING 1
#define CONNTRY_SUCCESS 2
#define CONNTRY_FAIL 3
//Connection result var
static int connTryStatus=CONNTRY_IDLE;
static os_timer_t resetTimer;

//Callback the code calls when a wlan ap scan is done. Basically stores the result in
//the cgiWifiAps struct.
void   wifiScanDoneCb(void *arg, STATUS status) {
	int n;
	struct bss_info *bss_link = (struct bss_info *)arg;

	if (status!=OK) {
		cgiWifiAps.scanInProgress=0;
		return;
	}

	//Clear prev ap data if needed.
	if (cgiWifiAps.apData!=NULL) {
		for (n=0; n<cgiWifiAps.noAps; n++) free(cgiWifiAps.apData[n]);
		free(cgiWifiAps.apData);
	}

	//Count amount of access points found.
	n=0;
	while (bss_link != NULL) {
		bss_link = bss_link->next.stqe_next;
		n++;
	}
	//Allocate memory for access point data
	cgiWifiAps.apData=(ApData **)malloc(sizeof(ApData *)*n);
	cgiWifiAps.noAps=n;

	//Copy access point data to the static struct
	n=0;
	bss_link = (struct bss_info *)arg;
	while (bss_link != NULL) {
		if (n>=cgiWifiAps.noAps) {
			//This means the bss_link changed under our nose. Shouldn't happen!
			//Break because otherwise we will write in unallocated memory.
			printf("Huh? I have more than the allocated %d aps!\n", cgiWifiAps.noAps);
			break;
		}
		//Save the ap data.
		cgiWifiAps.apData[n]=(ApData *)malloc(sizeof(ApData));
		cgiWifiAps.apData[n]->rssi=bss_link->rssi;
		cgiWifiAps.apData[n]->enc=bss_link->authmode;
		strncpy(cgiWifiAps.apData[n]->ssid, (char*)bss_link->ssid, 32);

		bss_link = bss_link->next.stqe_next;
		n++;
	}
	//We're done.
	cgiWifiAps.scanInProgress=0;
}


//Routine to start a WiFi access point scan.
static void   wifiStartScan() {
//	int x;
	if (cgiWifiAps.scanInProgress) return;
	cgiWifiAps.scanInProgress=1;
	wifi_station_scan(NULL, wifiScanDoneCb);
}

//This CGI is called from the bit of AJAX-code in wifi.tpl. It will initiate a
//scan for access points and if available will return the result of an earlier scan.
//The result is embedded in a bit of JSON parsed by the javascript in wifi.tpl.
int   cgiWiFiScan(HttpdConnData *connData) {
	int pos=(int)connData->cgiData;
	int len;
    char*pbuf=(char*)zalloc(256);

	if (!cgiWifiAps.scanInProgress && pos!=0) {
		//Fill in json code for an access point
		if (pos-1<cgiWifiAps.noAps) {
			len=sprintf(pbuf, "{\"essid\": \"%s\", \"rssi\": \"%d\", \"enc\": \"%d\"}%s\n", 
					cgiWifiAps.apData[pos-1]->ssid, cgiWifiAps.apData[pos-1]->rssi, 
					cgiWifiAps.apData[pos-1]->enc, (pos-1==cgiWifiAps.noAps-1)?"":",");
			httpdSend(connData, pbuf, len);
		}
		pos++;
		if ((pos-1)>=cgiWifiAps.noAps) {
			len=sprintf(pbuf, "]\n}\n}\n");
			httpdSend(connData, pbuf, len);
			//Also start a new scan.
			wifiStartScan();
            if(pbuf){
                free(pbuf);
                pbuf=NULL;
            }
            printf("scan data send over\n");
			return HTTPD_CGI_DONE;
		} else {
			connData->cgiData=(void*)pos;
            if(pbuf){
                free(pbuf);
                pbuf=NULL;
            }
			return HTTPD_CGI_MORE;
		}
	}

	httpdStartResponse(connData, 200);
	httpdHeader(connData, "Content-Type", "text/json");
	httpdEndHeaders(connData);

	if (cgiWifiAps.scanInProgress==1) {
		//We're still scanning. Tell Javascript code that.
		len=sprintf(pbuf, "{\n \"result\": { \n\"inProgress\": \"1\"\n }\n}\n");
		httpdSend(connData, pbuf, len);
        if(pbuf){
            free(pbuf);
            pbuf=NULL;
        }
		return HTTPD_CGI_DONE;
	} else {
		//We have a scan result. Pass it on.
		len=sprintf(pbuf, "{\n \"result\": { \n\"inProgress\": \"0\", \n\"APs\": [\n");
		httpdSend(connData, pbuf, len);
		if (cgiWifiAps.apData==NULL) cgiWifiAps.noAps=0;
		connData->cgiData=(void *)1;
        if(pbuf){
            free(pbuf);
            pbuf=NULL;
        }
		return HTTPD_CGI_MORE;
	}
}

//Temp store for new ap info.
static struct station_config stconf;

//This routine is ran some time after a connection attempt to an access point. If
//the connect succeeds, this gets the module in STA-only mode.
static void   resetTimerCb(void *arg) {
	int x=wifi_station_get_connect_status();
	if (x==STATION_GOT_IP) {
		//Go to STA mode. This needs a reset, so do that.
		printf("Got IP. Going into STA mode..\n");
		wifi_set_opmode(1);
		//system_restart(); //needn't restart anymore
	} else {
		connTryStatus=CONNTRY_FAIL;
		printf("Connect fail. Not going into STA-only mode.\n");
		//Maybe also pass this through on the webpage?
	}
}



//Actually connect to a station. This routine is timed because I had problems
//with immediate connections earlier. It probably was something else that caused it,
//but I can't be arsed to put the code back :P
static void   reassTimerCb(void *arg) {
	int x;
	printf("Try to connect to AP....\n");
	wifi_station_disconnect();
	wifi_station_set_config(&stconf);
	wifi_station_connect();
	x=wifi_get_opmode();
	connTryStatus=CONNTRY_WORKING;
	if (x!=1) {
		//Schedule disconnect/connect
		os_timer_disarm(&resetTimer);
		os_timer_setfn(&resetTimer, resetTimerCb, NULL);
		os_timer_arm(&resetTimer, 15000, 0); //time out after 15 secs of trying to connect
	}
}


//This cgi uses the routines above to connect to a specific access point with the
//given ESSID using the given password.
int   cgiWiFiConnect(HttpdConnData *connData) {
	static os_timer_t reassTimer;
	if (connData->conn==NULL) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}
    
	char *pbuf = (char*)zalloc(64);
	httpdFindArg(connData->post->buff, "essid", pbuf, 32);
	strncpy((char*)stconf.ssid, pbuf, 32);
	httpdFindArg(connData->post->buff, "passwd", pbuf, 64);
	strncpy((char*)stconf.password, pbuf, 64);
	printf("Try to connect to AP %s pw %s\n", (char*)stconf.ssid, (char*)stconf.password);

	//Schedule disconnect/connect
	os_timer_disarm(&reassTimer);
	os_timer_setfn(&reassTimer, reassTimerCb, NULL);
//Set to 0 if you want to disable the actual reconnecting bit
#ifdef DEMO_MODE
	httpdRedirect(connData, "/wifi");
#else
	os_timer_arm(&reassTimer, 500, 0);
	httpdRedirect(connData, "connecting.html");
#endif

    if(pbuf){
        free(pbuf);
        pbuf=NULL;
    }
	return HTTPD_CGI_DONE;
}

//This cgi uses the routines above to connect to a specific access point with the
//given ESSID using the given password.
int   cgiWiFiSetMode(HttpdConnData *connData) {
	int len;
	char pbuf[8];
	
	if (connData->conn==NULL) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

	len=httpdFindArg(connData->getArgs, "mode", pbuf, sizeof(pbuf));
	if (len!=0) {
		printf("cgiWifiSetMode: %s\n", pbuf);
		wifi_set_opmode(atoi(pbuf));
        cgiWifiAps.scanInProgress=0;
	}
	httpdRedirect(connData, "/wifi");
	return HTTPD_CGI_DONE;
}

int   cgiWiFiConnStatus(HttpdConnData *connData) {
	int len;
	struct ip_info info;
	char *pbuf = (char*)zalloc(128);//the max len of warmning msg
	int st=wifi_station_get_connect_status();
	httpdStartResponse(connData, 200);
	httpdHeader(connData, "Content-Type", "text/json");
	httpdEndHeaders(connData);
	if (connTryStatus==CONNTRY_IDLE) {
		len=sprintf(pbuf, "{\n \"status\": \"idle\"\n }\n");
	} else if (connTryStatus==CONNTRY_WORKING || connTryStatus==CONNTRY_SUCCESS) {
		if (st==STATION_GOT_IP) {
			wifi_get_ip_info(0, &info);
			len=sprintf(pbuf, "{\n \"status\": \"success\",\n \"ip\": \"%d.%d.%d.%d\" }\n", 
				(int)(info.ip.addr>>0)&0xff, (int)(info.ip.addr>>8)&0xff, 
				(int)(info.ip.addr>>16)&0xff, (int)(info.ip.addr>>24)&0xff);
			//Reset into AP-only mode sooner.
			os_timer_disarm(&resetTimer);
			os_timer_setfn(&resetTimer, resetTimerCb, NULL);
			os_timer_arm(&resetTimer, 1000, 0);
		} else {
			len=sprintf(pbuf, "{\n \"status\": \"working\"\n }\n");
		}
	} else {
		len=sprintf(pbuf, "{\n \"status\": \"fail\"\n }\n");
	}
	httpdSend(connData, pbuf, len);

    if(pbuf){
        free(pbuf);
        pbuf=NULL;
    }
	return HTTPD_CGI_DONE;
}

//Template code for the WLAN page.
int   tplWlan(HttpdConnData *connData, char *token, void **arg) {
	int x;
	static struct station_config stconf;
	if (token==NULL) return HTTPD_CGI_DONE;
	wifi_station_get_config(&stconf);
    
	char *pbuf = (char*)zalloc(128);//the max len of warmning msg

	strcpy(pbuf, "Unknown");
	if (strcmp(token, "WiFiMode")==0) {
		x=wifi_get_opmode();
		if (x==1) strcpy(pbuf, "Client");
		if (x==2) strcpy(pbuf, "SoftAP");
		if (x==3) strcpy(pbuf, "STA+AP");
	} else if (strcmp(token, "currSsid")==0) {
		strcpy(pbuf, (char*)stconf.ssid);
	} else if (strcmp(token, "WiFiPasswd")==0) {
		strcpy(pbuf, (char*)stconf.password);
	} else if (strcmp(token, "WiFiapwarn")==0) {
		x=wifi_get_opmode();
		if (x==2) {
			strcpy(pbuf, "<b>Can't scan in this mode.</b> Click <a href=\"setmode.cgi?mode=3\">here</a> to go to STA+AP mode.");
		} else {
			strcpy(pbuf, "Click <a href=\"setmode.cgi?mode=2\">here</a> to go to standalone AP mode.");
		}
	}
	httpdSend(connData, pbuf, -1);
    
    if(pbuf){
        free(pbuf);
        pbuf=NULL;
    }
	return HTTPD_CGI_DONE;
}


