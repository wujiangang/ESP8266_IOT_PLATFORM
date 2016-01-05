/******************************************************************************
 * Copyright (C) 2014 -2016  Espressif System
 *
 * FileName: upgrade_lib.c
 *
 * Description: entry file of user application
 *
 * Modification history:
 * 2015/7/1, v1.0 create this file.
*******************************************************************************/
#include "esp_common.h"
#include "lwip/mem.h"
#include "upgrade.h"

struct upgrade_param {
    uint32 fw_bin_addr;
    uint8 fw_bin_sec;
    uint8 fw_bin_sec_num;
    uint8 fw_bin_sec_earse;
    uint8 extra;
    uint8 save[4];
    uint8 *buffer;
};

LOCAL struct upgrade_param *upgrade;

//extern SpiFlashChip *flashchip;

/******************************************************************************
 * FunctionName : user_upgrade_internal
 * Description  : a
 * Parameters   :
 * Returns      :
*******************************************************************************/
LOCAL bool  
system_upgrade_internal(struct upgrade_param *upgrade, uint8 *data, u32 len)
{
    bool ret = false;
    u8 secnm=0;
    if(data == NULL || len == 0)
    {
        return true;
    }

    /*got the sumlngth,erase all upgrade sector*/
    if(len > SPI_FLASH_SEC_SIZE ) {
        upgrade->fw_bin_sec_earse=upgrade->fw_bin_sec;

        secnm=((upgrade->fw_bin_addr + len)>>12) + (len&0xfff?1:0);
        while(upgrade->fw_bin_sec_earse != secnm) {
            spi_flash_erase_sector(upgrade->fw_bin_sec_earse);
            upgrade->fw_bin_sec_earse++;
            vTaskDelay(10 / portTICK_RATE_MS);
        }
        os_printf("flash erase over\n");
        return true;
    }
    
    upgrade->buffer = (uint8 *)zalloc(len + upgrade->extra);

    memcpy(upgrade->buffer, upgrade->save, upgrade->extra);
    memcpy(upgrade->buffer + upgrade->extra, data, len);

    len += upgrade->extra;
    upgrade->extra = len & 0x03;
    len -= upgrade->extra;

    if(upgrade->extra<=4)
        memcpy(upgrade->save, upgrade->buffer + len, upgrade->extra);
    else
        os_printf("ERR3:arr_overflow,%u,%d\n",__LINE__,upgrade->extra);

    do {
        if (upgrade->fw_bin_addr + len >= (upgrade->fw_bin_sec + upgrade->fw_bin_sec_num) * SPI_FLASH_SEC_SIZE) {
            break;
        }

        if (spi_flash_write(upgrade->fw_bin_addr, (uint32 *)upgrade->buffer, len) != SPI_FLASH_RESULT_OK) {
            break;
        }
        
        ret = true;
        upgrade->fw_bin_addr += len;
    } while (0);

    free(upgrade->buffer);
    upgrade->buffer = NULL;
    return ret;
}

/******************************************************************************
 * FunctionName : user_upgrade
 * Description  : a
 * Parameters   :
 * Returns      :
*******************************************************************************/
bool system_upgrade(uint8 *data, uint32 len)
{
    bool ret;

/* for connect data debug
    if(len < 1460){
        char *precv_buf = (char*)malloc(1480);
        memcpy(precv_buf, data,len);
        memcpy(precv_buf+len,"\0\r\n",3);
        printf("%s\n",precv_buf);
        free(precv_buf);
    }
*/
    ret = system_upgrade_internal(upgrade, data, len);

    return ret;
}

/******************************************************************************
 * FunctionName : system_upgrade_init
 * Description  : a
 * Parameters   :
 * Returns      :
*******************************************************************************/
void  
system_upgrade_init(void)
{
    uint32 user_bin2_start;
    uint8 flash_buf[4];
    uint8 high_half;

    spi_flash_read(0, (uint32 *)flash_buf, 4);
    high_half = (flash_buf[3] & 0xF0) >> 4;
    
    os_printf("high_half %d\n",high_half);

    if (upgrade == NULL) {
        upgrade = (struct upgrade_param *)zalloc(sizeof(struct upgrade_param));
    }

    system_upgrade_flag_set(UPGRADE_FLAG_IDLE);

    if (high_half == 2 || high_half == 3 || high_half == 4) {
        user_bin2_start = 129;  // 128 + 1
        upgrade->fw_bin_sec_num = 123;  // 128 - 1 - 4
    } else {
        user_bin2_start = 65;   // 64 + 1
        upgrade->fw_bin_sec_num = 59;   // 64 - 1 - 4
    }

    upgrade->fw_bin_sec = (system_upgrade_userbin_check() == USER_BIN1) ? user_bin2_start : 1;

    upgrade->fw_bin_addr = upgrade->fw_bin_sec * SPI_FLASH_SEC_SIZE;
    
    upgrade->fw_bin_sec_earse = upgrade->fw_bin_sec;
}

/******************************************************************************
 * FunctionName : system_upgrade_deinit
 * Description  : a
 * Parameters   :
 * Returns      :
*******************************************************************************/
void  
system_upgrade_deinit(void)
{
	if (upgrade != NULL) {
		free(upgrade);
		upgrade = NULL;
	}else {
		return;
	}
}
