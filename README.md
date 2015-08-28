ESP8266_iot_platform

================


esp8266 iot platform based on esp8266 RTOS SDK1.2 OR later.


COMPILE

first,

export SDK_PATH=~/esp_iot_sdk_freertos(your esp8266 RTOS SDK path)

export BIN_PATH=~/esp8266_bin(the folder to save target binary)


second,

using command ./gen_misc.sh,select the parameter step by step,for example,y,1,2,0,3


DOWLOAD

take 1,2,0,3 configuration for example,


boot_v1.4(b1).bin, downloads to flash 0x00000

user1.2048.new.3.bin, downloads to flash 0x10000

esp_init_data_default.bin, downloads to 0x1fc000

lank.bin, downloads to flash 0x1fe000
