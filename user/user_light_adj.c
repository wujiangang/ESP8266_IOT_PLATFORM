
#include "esp_common.h"
#include "user_config.h"

#if LIGHT_DEVICE

#include "user_light.h"
#include "user_light_adj.h"

#define ABS_MINUS(x,y) (x<y?(y-x):(x-y))

uint16  min_ms = 15;
uint8 light_sleep_flg = 0;

bool change_finish=true;
os_timer_t timer_pwm_adj;

uint32  current_duty[PWM_CHANNEL] = {0};
static u32 duty_now[PWM_CHANNEL] = {0};

extern struct light_saved_param light_param;
extern uint32 pwmio_info[8][3];

//-----------------------------------Light para storage---------------------------

static u8 CurFreeLightEvtIdx = 0;
static u8 TotalUsedLightEvtNum = 0;
static u8  CurEvtIdxToBeUse = 0;

#define LIGHT_EVT_QNUM (40) 
static struct pwm_param LightEvtArr[LIGHT_EVT_QNUM];
//------------------------------------------------------------------------------------
static struct pwm_param *LightEvtMalloc(void)
{
    struct pwm_param *tmp = NULL;
    TotalUsedLightEvtNum++;
    if(TotalUsedLightEvtNum > LIGHT_EVT_QNUM ){
        TotalUsedLightEvtNum--;
    }
    else{
        tmp = &(LightEvtArr[CurFreeLightEvtIdx]);
        CurFreeLightEvtIdx++;
        if( CurFreeLightEvtIdx > (LIGHT_EVT_QNUM-1))
        CurFreeLightEvtIdx = 0;
    }
    return tmp;
}

static void   LightEvtFree(void)
{
    TotalUsedLightEvtNum--;
}

//------------------------------------------------------------------------------------

static void   light_pwm_smooth_adj_proc(void);


void  
light_save_target_duty()
{
    printf("light_save_duty\n");
#if SAVE_LIGHT_PARAM
    spi_flash_erase_sector(PRIV_PARAM_START_SEC + PRIV_PARAM_SAVE);
    spi_flash_write((PRIV_PARAM_START_SEC + PRIV_PARAM_SAVE) * SPI_FLASH_SEC_SIZE,
                (uint32 *)&light_param, sizeof(struct light_saved_param));
#endif
}


void  
light_set_aim_r(uint32 r)
{
    current_duty[LIGHT_RED]=r;
    light_pwm_smooth_adj_proc();
}

void  
light_set_aim_g(uint32 g)
{
    current_duty[LIGHT_GREEN]=g;
    light_pwm_smooth_adj_proc();
}

void  
light_set_aim_b(uint32 b)
{
    current_duty[LIGHT_BLUE]=b;
    light_pwm_smooth_adj_proc();
}

void  
light_set_aim_cw(uint32 cw)
{
    current_duty[LIGHT_COLD_WHITE]=cw;
    light_pwm_smooth_adj_proc();
}

void  
light_set_aim_ww(uint32 ww)
{
    current_duty[LIGHT_WARM_WHITE]=ww;
    light_pwm_smooth_adj_proc();
}

LOCAL bool  
check_pwm_current_duty_diff()
{
    int i;
    
    for(i=0;i<PWM_CHANNEL;i++){
        if(light_param.pwm_duty[i] != current_duty[i]){
            return true;
        }
        
    }
    return false;

}

void light_pwm_dh_adj_proc(void *Targ)
{
    uint8 i;
    for(i=0;i<PWM_CHANNEL;i++){ 
        
        duty_now[i] = (duty_now[i]*7 + current_duty[i])>>3;
        
        if( ABS_MINUS(duty_now[i],current_duty[i])<20 ){
            duty_now[i] = current_duty[i];
        }
        user_light_set_duty(duty_now[i],i);
    }
    //printf("duty:%u,%u,%u\r\n", duty_now[0],duty_now[1],duty_now[2] );
//    pwm_init(light_param.pwm_period, light_param.pwm_duty,PWM_CHANNEL,pwmio_info); //lujian
    
    pwm_start();	
    if(check_pwm_current_duty_diff()){
        change_finish = 0;
        os_timer_disarm(&timer_pwm_adj);
        os_timer_setfn(&timer_pwm_adj, (os_timer_func_t *)light_pwm_dh_adj_proc, NULL);
        os_timer_arm(&timer_pwm_adj, min_ms, 0);
    }
    else{
        //printf("finish\n");
        change_finish = 1;  
        //light_save_target_duty();
        os_timer_disarm(&timer_pwm_adj);
        light_pwm_smooth_adj_proc();
    }

}

LOCAL bool  
check_pwm_duty_zero()
{
    int i;
    for(i=0;i<PWM_CHANNEL;i++){
        if(light_param.pwm_duty[i] != 0){
            return false;
        }
    }
    return true;
}


static void 
  light_pwm_smooth_adj_proc(void)
{
    if( TotalUsedLightEvtNum>0 ){

        user_light_set_period( LightEvtArr[CurEvtIdxToBeUse].period );

        memcpy(current_duty,LightEvtArr[CurEvtIdxToBeUse].duty,sizeof(current_duty));
        CurEvtIdxToBeUse++;
        if(CurEvtIdxToBeUse > (LIGHT_EVT_QNUM-1) ){
            CurEvtIdxToBeUse = 0;
        }
        LightEvtFree();
 
        if(change_finish){
            light_pwm_dh_adj_proc(NULL);
        }
    }

    if(change_finish){
           light_save_target_duty();
	    if(check_pwm_duty_zero()){
            if(light_sleep_flg==0){
                //wifi_set_sleep_type(LIGHT_SLEEP_T);
                light_sleep_flg = 1;
            }
        }
    }
}



#if LIGHT_CURRENT_LIMIT
uint32 light_get_cur(uint32 duty , uint8 channel, uint32 period)
{
    uint32 duty_max_limit = (period*1000/45);
    uint32 duty_mapped = duty*22727/duty_max_limit;
    switch(channel){

    case LIGHT_RED : 
        if(duty_mapped>=0 && duty_mapped<23000){
            return (duty_mapped*151000/22727);
        }
    
        break;

    case LIGHT_GREEN:
        if(duty_mapped>=0 && duty_mapped<23000){
            return (duty_mapped*82000/22727);
        }
        break;
    
    case LIGHT_BLUE:
        if(duty_mapped>=0 && duty_mapped<23000){
            return (duty_mapped*70000/22727);
        }
        break;
    
    case LIGHT_COLD_WHITE:
    case LIGHT_WARM_WHITE:
        if(duty_mapped>=0 && duty_mapped<23000){
            return (duty_mapped*115000/22727);
        }
        break;
        
    default:
       printf("CHANNEL ERROR IN GET_CUR\r\n");
       break;

    }

}

#endif

void  
light_set_aim(uint32 r,uint32 g,uint32 b,uint32 cw,uint32 ww,uint32 period)
{
    struct pwm_param *tmp = LightEvtMalloc();    
    if(tmp != NULL){
        tmp->period = (period<1000?period:1000);
        tmp->duty[LIGHT_RED] = r*PWM_DEPTH/APP_MAX_PWM;
        tmp->duty[LIGHT_GREEN] = g*PWM_DEPTH/APP_MAX_PWM;
        tmp->duty[LIGHT_BLUE] = b*PWM_DEPTH/APP_MAX_PWM;
        tmp->duty[LIGHT_COLD_WHITE] = cw*PWM_DEPTH/APP_MAX_PWM;
        tmp->duty[LIGHT_WARM_WHITE] = ww*PWM_DEPTH/APP_MAX_PWM;//chg

#if LIGHT_CURRENT_LIMIT
        uint32 cur_r,cur_g,cur_b,cur_rgb;

        //if(cw>0 || ww>0){
        cur_r = light_get_cur(tmp->duty[LIGHT_RED] , LIGHT_RED, tmp->period);

        cur_g =  light_get_cur(tmp->duty[LIGHT_GREEN] , LIGHT_GREEN, tmp->period);
        cur_b =  light_get_cur(tmp->duty[LIGHT_BLUE] , LIGHT_BLUE, tmp->period);
        cur_rgb = (cur_r+cur_g+cur_b);
        //}
        uint32 cur_cw = light_get_cur( tmp->duty[LIGHT_COLD_WHITE],LIGHT_COLD_WHITE, tmp->period);
        uint32 cur_ww = light_get_cur( tmp->duty[LIGHT_WARM_WHITE],LIGHT_WARM_WHITE, tmp->period);
        uint32 cur_remain,cur_mar;
        cur_remain = (LIGHT_TOTAL_CURRENT_MAX - cur_rgb -LIGHT_CURRENT_MARGIN);
        cur_mar = LIGHT_CURRENT_MARGIN;

 /*
         if((cur_cw < 50000) || (cur_ww < 50000)){
            cur_remain = (LIGHT_TOTAL_CURRENT_MAX - cur_rgb -LIGHT_CURRENT_MARGIN);
            cur_mar = LIGHT_CURRENT_MARGIN;
        }else if((cur_cw < 99000) || (cur_ww < 99000)){
            cur_remain = (LIGHT_TOTAL_CURRENT_MAX - cur_rgb -LIGHT_CURRENT_MARGIN_L2);
            cur_mar = LIGHT_CURRENT_MARGIN_L2;
        }else{
            cur_remain = (LIGHT_TOTAL_CURRENT_MAX - cur_rgb -LIGHT_CURRENT_MARGIN_L3);
            cur_mar = LIGHT_CURRENT_MARGIN_L2;
        }
*/
         
/*
        if((LIGHT_TOTAL_CURRENT_MAX-cur_rgb)>120){
            cur_remain = (LIGHT_TOTAL_CURRENT_MAX - cur_rgb -LIGHT_CURRENT_MARGIN);
            cur_mar = LIGHT_CURRENT_MARGIN;
        }else if((LIGHT_TOTAL_CURRENT_MAX-cur_rgb)>100){
            cur_remain = (LIGHT_TOTAL_CURRENT_MAX - cur_rgb -LIGHT_CURRENT_MARGIN_L2);
            cur_mar = LIGHT_CURRENT_MARGIN_L2;
        }else{
            cur_remain = (LIGHT_TOTAL_CURRENT_MAX - cur_rgb -LIGHT_CURRENT_MARGIN_L3);
            cur_mar = LIGHT_CURRENT_MARGIN_L2;
        }
*/

        while((cur_cw+cur_ww) > cur_remain){
            tmp->duty[LIGHT_COLD_WHITE] =  tmp->duty[LIGHT_COLD_WHITE] * 9 / 10;
            tmp->duty[LIGHT_WARM_WHITE] =  tmp->duty[LIGHT_WARM_WHITE] * 9 / 10;
            cur_cw = light_get_cur( tmp->duty[LIGHT_COLD_WHITE],LIGHT_COLD_WHITE, tmp->period);
            cur_ww = light_get_cur( tmp->duty[LIGHT_WARM_WHITE],LIGHT_WARM_WHITE, tmp->period);
        }       
        printf("debug : %d %d %d %d %d\r\n",cur_r/1000,cur_g/1000,cur_b/1000,cur_cw/1000,cur_ww/1000);
        printf("debug:total current after adj : %d + %d mA \r\n",(cur_cw+cur_ww+cur_r+cur_g+cur_b)/1000,cur_mar/1000);
#endif
        printf("LIGHT p:%u  r : %u  g: %u  b: %u  cw: %u  ww: %u \r\n",period,
                     tmp->duty[0],tmp->duty[1],tmp->duty[2],tmp->duty[3],tmp->duty[4]);
        light_pwm_smooth_adj_proc();
    }
    else{
        printf("light para full\n");
    }
}

#endif

