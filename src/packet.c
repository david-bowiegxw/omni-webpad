#include "packet.h"
#include <string.h>
static uint16_t clamp_motion(uint16_t value){return value>1023?1023:value;}
static void put_be16(uint8_t *out,uint16_t value){out[0]=(uint8_t)(value>>8);out[1]=(uint8_t)value;}
void ds3_neutral_input(ds3_input_t *input){
 memset(input,0,sizeof(*input));
 input->lx=input->ly=input->rx=input->ry=128;
 input->accel_x=input->accel_y=input->accel_z=input->gyro_z=512;
}
void ds3_build_packet(const ds3_input_t *input,uint8_t out[DS3_REPORT_SIZE]) {
    memset(out,0,DS3_REPORT_SIZE);
    uint32_t mask=input->buttons&DS3_MASK_ALL;
    out[0]=1;
    out[2]=mask; out[3]=mask>>8; out[4]=(mask>>16)&1;
    out[6]=input->lx;out[7]=input->ly;out[8]=input->rx;out[9]=input->ry;
    // Pressure order: dpad U/R/D/L, L2/R2/L1/R1, triangle/circle/cross/square.
    for(unsigned i=0;i<12;i++) out[14+i]=(mask&(1u<<(i+4)))?255:0;
    out[29]=2; out[30]=5; out[31]=0x10;
    put_be16(out+41,clamp_motion(input->accel_x));
    put_be16(out+43,clamp_motion(input->accel_y));
    put_be16(out+45,clamp_motion(input->accel_z));
    put_be16(out+47,clamp_motion(input->gyro_z));
}
