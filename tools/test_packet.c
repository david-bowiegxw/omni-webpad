#include "packet.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 uint8_t r[49];ds3_input_t in;ds3_neutral_input(&in);
 ds3_build_packet(&in,r);assert(r[0]==1 && r[6]==128 && r[41]==2 && r[48]==0);
 in.buttons=65536;ds3_build_packet(&in,r);assert(r[4]==1 && r[2]==0 && r[3]==0);
 in.buttons=9;ds3_build_packet(&in,r);assert(r[2]==9 && r[4]==0);
 for(unsigned i=4;i<16;i++){in.buttons=1u<<i;ds3_build_packet(&in,r);for(unsigned j=0;j<12;j++)assert(r[14+j]==(j==i-4?255:0));}
 in.buttons=0xffffffff;ds3_build_packet(&in,r);assert(r[2]==255&&r[3]==255&&r[4]==1);
 ds3_neutral_input(&in);in.lx=0;in.ly=255;in.rx=17;in.ry=238;in.accel_x=1;in.accel_y=1023;in.accel_z=700;in.gyro_z=512;ds3_build_packet(&in,r);
 assert(r[6]==0&&r[7]==255&&r[8]==17&&r[9]==238);
 assert(r[41]==0&&r[42]==1&&r[43]==3&&r[44]==255&&r[45]==2&&r[46]==188&&r[47]==2&&r[48]==0);
 for(unsigned i=14;i<26;i++)assert(r[i]==0);
 puts("49-byte layout, buttons, analog sticks, motion and release: PASS");
}
