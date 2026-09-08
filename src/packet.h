#pragma once
#include <stdint.h>
#define DS3_REPORT_SIZE 49
#define DS3_MASK_ALL 0x1ffffu
typedef struct {
 uint32_t buttons;
 uint8_t lx,ly,rx,ry;
 uint16_t accel_x,accel_y,accel_z,gyro_z;
} ds3_input_t;
void ds3_neutral_input(ds3_input_t *input);
void ds3_build_packet(const ds3_input_t *input,uint8_t out[DS3_REPORT_SIZE]);
