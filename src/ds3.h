#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "packet.h"
void ds3_init(void);
void ds3_task(const ds3_input_t *input);
void ds3_feedback(uint8_t *left,uint8_t *right,uint32_t *sequence);
unsigned ds3_status(char *out,unsigned cap);

bool ds3_operational(void);
