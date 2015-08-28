#ifndef __KEY_H__
#define __KEY_H__

#include "driver/gpio.h"

typedef void (* key_function)(void);

struct single_key_param {
    uint32 gpio_name;
    os_timer_t key_5s;
    os_timer_t key_50ms;
    key_function short_press;
    key_function long_press;
    
    uint8 key_level;
    uint8 gpio_id;
    uint8 gpio_func;
};

struct keys_param {
    struct single_key_param **single_key;
    uint8 key_num;
};

struct single_key_param *key_init_single(uint8 gpio_id, uint32 gpio_name, uint8 gpio_func, key_function long_press, key_function short_press);
BOOL get_key_status(struct single_key_param *single_key);
void key_init(struct keys_param *key);

#endif
