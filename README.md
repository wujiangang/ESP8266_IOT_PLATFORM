# ESP8266 IOT Platform #

----------

ESP8266 SDK provides users with a simple, fast and efficient development platform for Internet of Things products. The ESP8266 IOT Platform is based on the FreeRTOS ESP8266 SDK (https://github.com/espressif/esp_iot_rtos_sdk) and adds on to it some commonly used functionalities, in an example application of a smart plug. This application uses the ESP-TOUCH protocol to realise smart configuration of the device. The communication protocols used are JSON and HTTP REST. An Android mobile APK (https://github.com/EspressifApp/IOT-Espressif-Android) is also included as a basic template for the users.
   
## Code Structure ##

## usr directory ##

user_main.c: The entry point for the main program. 

user_webserver.c: Creates the TCP webserver, using JSON packets and REST architecture.

user_devicefind.c: Creates a UDP service, which recieves special finder message on port 1025 and allows the user to discover devices on the network. 

user_esp_platform.c: provides the Espressif Smart Configuration API (ESP-TOUCH) example; communicates with the Espressif Cloud servers (customize this to connect to your own servers); maintains the network status and data transmission to server. 

user_plug.c: implements the functionality of a smart plug in this example. 

user_esp_platform_timer.c: implements the timer functionalities. 

user_light.c: could be used to output PWM signals that can be used for smart lighting. 

user_cgi.c: implents an adapter between the HTTP webserver and the SDK. 

## upgrade directory ##

upgrade.c: firmware upgrade example. 

upgrade_lib.c: operations on FLASH devices pertaining the upgrade of firmware. 

## include directory ##

The include directory includes the relevant headers needed for the project. Of interest, is "user_config.h", which can be used to configure or select the examples. By setting the MACROs, we can enable the relevant functionality, e.g. PLUG_DEVICE and LIGHT_DEVICE. 

Please note that you have to adjust these parameters based on your flash map. For more details, please refer to "2A-ESP8266 __IOT_SDK_User_Manuel" 

user_esp_platform.h: #define ESP_PARAM_START_SEC 0x7D

user_light.h: #define PRIV_PARAM_START_SEC 0x7C

user_plug.h: #define PRIV_PARAM_START_SEC 0x7C

## Driver Directory ##

This contains the GPIO interface. 

## libesphttpd Directory ##

This directory implements a small HTTP server. It is compatible with most web browsers. Core contains the parser implementing the HTTP protocol and a simple file system. ESPFS is a file system with simple compression capabilites built in.  util contains the interface with WiFi and DNS related codes. 

## html_light and html_plug Directories ##

These directories contain the JavaScript and HTML pages and user interface resources. 

## Usage ##

## Configuration ##

Target device can be configured through defining user_config.h macro. This application default configuration is a smart power plug (or smart power socket) (#define PLUG_DEVICE 1), and supports the HTTP server function (#define HTTPD_SERVER 1).

## Compiling the Code ##

First export the two parameters specifying the paths of  esp8266 RTOS SDK and compiler generated firmware.

export SDK_PATH=~/esp_iot_sdk_freertos
(esp8266 RTOS SDK path)

export BIN_PATH=~/esp8266_bin
(the folder to save target binary)

Run the compilation script ./gen_misc.sh; you will prompted for some configuration parameters. User the firmware download tool to flash the device with the bins generated. For my version of FreeRTOS ESP8266 SDK 1.2.0.3, I have used the following parameters in the upload:

boot_v1.4(b1).bin, downloads to flash 0x00000

user1.2048.new.3.bin, downloads to flash 0x10000

esp_init_data_default.bin, downloads to 0x1fc000

blank.bin, downloads to flash 0x1fe000