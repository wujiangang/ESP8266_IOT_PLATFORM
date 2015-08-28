#include "esp_common.h"

#if LIGHT_DEVICE

#ifndef __USER_LIGHT_H__
#define __USER_LIGHT_H__
/*pwm.h: function and macro definition of PWM API , driver level */
/*user_light.h: user interface for light API, user level*/
/*user_light_adj: API for color changing and lighting effects, user level*/
#include "driver/key.h"
#include "pwm.h"


/* NOTICE !!! ---this is for 512KB spi flash.*/
/* You can change to other sector if you use other size spi flash. */
/* Refer to the documentation about OTA support and flash mapping*/
#define PRIV_PARAM_START_SEC		0x7C
#define PRIV_PARAM_SAVE     0

/*note this pin may be used by PWM_3_OUT */
#define PLUG_KEY_NUM            1
#define PLUG_KEY_0_IO_MUX     PERIPHS_IO_MUX_MTCK_U
#define PLUG_KEY_0_IO_NUM     13
#define PLUG_KEY_0_IO_FUNC    FUNC_GPIO13

#define PWM_CHANNEL 3

/*max duty value sent from APK*/
#define APP_MAX_PWM 22222


enum {
    LIGHT_RED = 0,
    LIGHT_GREEN,
    LIGHT_BLUE,
    LIGHT_COLD_WHITE,
    LIGHT_WARM_WHITE,
};

enum {
    LED_OFF = 0,
    LED_ON  = 1,
    LED_1HZ,
    LED_5HZ,
    LED_20HZ,
};

/*Definition of GPIO PIN params, for GPIO initialization*/
#define PWM_0_OUT_IO_MUX PERIPHS_IO_MUX_MTDI_U
#define PWM_0_OUT_IO_NUM 12
#define PWM_0_OUT_IO_FUNC  FUNC_GPIO12

#define PWM_1_OUT_IO_MUX PERIPHS_IO_MUX_MTDO_U
#define PWM_1_OUT_IO_NUM 15
#define PWM_1_OUT_IO_FUNC  FUNC_GPIO15

#define PWM_2_OUT_IO_MUX PERIPHS_IO_MUX_MTMS_U
#define PWM_2_OUT_IO_NUM 14
#define PWM_2_OUT_IO_FUNC  FUNC_GPIO14

#define PWM_3_OUT_IO_MUX PERIPHS_IO_MUX_MTCK_U
#define PWM_3_OUT_IO_NUM 13
#define PWM_3_OUT_IO_FUNC  FUNC_GPIO13

#define PWM_4_OUT_IO_MUX PERIPHS_IO_MUX_GPIO4_U
#define PWM_4_OUT_IO_NUM 4
#define PWM_4_OUT_IO_FUNC  FUNC_GPIO4

struct light_saved_param {
    uint32  pwm_period;
    uint32  pwm_duty[PWM_CHANNEL];
};

void user_light_init(void);
uint32 user_light_get_duty(uint8 channel);
void user_light_set_duty(uint32 duty, uint8 channel);
uint32 user_light_get_period(void);
void user_light_set_period(uint32 period);

#endif

#endif
