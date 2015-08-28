#ifndef __USER_DEVICEFIND_H__
#define __USER_DEVICEFIND_H__

#define TIMER_NUMBER 10

struct esp_platform_wait_timer_param {
    int wait_time_second;
    uint8 wait_time_param[12];
    uint8 wait_action[16];
};

struct wait_param {
    uint32 min_time_backup;
    uint16 action_number;
    uint16 count;
    uint8 action[TIMER_NUMBER][15];
};

struct timer_bkup_param{
    u32 timer_recoup;
    u32 timer_start_time;
    u32 buffer_size;
    int timestamp;
    u16 magic;
    char *split_buffer;
    char pad;
};


void user_platform_timer_start(char* pbuffer);
void user_platform_timer_restore(void);
void user_platform_timer_bkup(void);

#endif
