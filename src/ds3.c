// USB protocol tables adapted from dualsense-to-dualshock3-sixaxis (MIT).
#include "ds3.h"
#include "packet.h"
#include "ds3_tables.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include <stdio.h>
#include <string.h>
static uint8_t device_addr[6],host_addr[6],ef_byte,leds;
static uint8_t rumble_left,rumble_right;
static uint32_t rumble_sequence;
static bool operational;
static uint32_t get_count,set_count,send_count,last_send;
static uint8_t last_get,last_set;
static char events[12][64];
static unsigned event_index;
static uint8_t packet[DS3_REPORT_SIZE];
static const tusb_desc_device_t device = {
 .bLength=18,.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
 .bDeviceClass=0,.bDeviceSubClass=0,.bDeviceProtocol=0,.bMaxPacketSize0=64,
 .idVendor=0x054c,.idProduct=0x0268,.bcdDevice=0x0100,
 .iManufacturer=1,.iProduct=2,.iSerialNumber=0,.bNumConfigurations=1
};
static const uint8_t config[]={
 TUD_CONFIG_DESCRIPTOR(1,1,0,TUD_CONFIG_DESC_LEN+TUD_HID_INOUT_DESC_LEN,TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,500),
 TUD_HID_INOUT_DESCRIPTOR(0,0,HID_ITF_PROTOCOL_NONE,sizeof(hidReportDescriptor),0x02,0x81,64,1)
};
uint8_t const *tud_descriptor_device_cb(void){return (uint8_t const*)&device;}
uint8_t const *tud_descriptor_configuration_cb(uint8_t index){return config;}
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance){return hidReportDescriptor;}
uint16_t const *tud_descriptor_string_cb(uint8_t index,uint16_t langid){
 static uint16_t str[40];
 if(index==0){str[0]=(TUSB_DESC_STRING<<8)|4;str[1]=0x0409;return str;}
 const char *s=index==1?"Sony":index==2?"PLAYSTATION(R)3 Controller":NULL;
 if(!s)return NULL;
 unsigned n=strlen(s);for(unsigned i=0;i<n;i++)str[i+1]=(uint8_t)s[i];
 str[0]=(TUSB_DESC_STRING<<8)|(2*n+2);return str;
}
static void log_event(const char *op,uint8_t id,const uint8_t *data,unsigned n){
 char *s=events[event_index++%12];
 snprintf(s,64,"%lu %s %02X n=%u %02X %02X %02X",(unsigned long)to_ms_since_boot(get_absolute_time()),op,id,n,n?data[0]:0,n>1?data[1]:0,n>2?data[2]:0);
}
void ds3_init(void){
 pico_unique_board_id_t id;pico_get_unique_board_id(&id);
 device_addr[0]=0x02;memcpy(device_addr+1,id.id+3,5);
 memset(host_addr,0xaa,6);ds3_input_t input;ds3_neutral_input(&input);ds3_build_packet(&input,packet);
}
uint16_t tud_hid_get_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t *buffer,uint16_t len){
 const uint8_t *src=NULL;unsigned size=0;uint8_t tmp[64];
 last_get=id;get_count++;
 if(type==HID_REPORT_TYPE_INPUT){src=packet+1;size=48;}
 else if(type==HID_REPORT_TYPE_FEATURE){
  switch(id){
   case 1:src=OUTPUT_0x01;size=sizeof(OUTPUT_0x01);break;
   case 0xef:src=OUTPUT_0xEF;size=sizeof(OUTPUT_0xEF);break;
   case 0xf2:src=BT_INFO;size=sizeof(BT_INFO);break;
   case 0xf5:src=OUTPUT_0xF5;size=sizeof(OUTPUT_0xF5);break;
   case 0xf7:src=OUTPUT_0xF7;size=sizeof(OUTPUT_0xF7);break;
   case 0xf8:src=OUTPUT_0xF8;size=sizeof(OUTPUT_0xF8);break;
   default:break;
  }
 }
 if(!src){log_event("GET?",id,tmp,0);return 0;}
 memcpy(tmp,src,size);
 if(type==HID_REPORT_TYPE_FEATURE){
  if(id==0xef||id==0xf8)tmp[6]=ef_byte;
  // TinyUSB supplies the report ID; F2 device address starts at wire offset 4.
  if(id==0xf2)memcpy(tmp+3,device_addr,6);
  if(id==0xf5){tmp[0]=1;memcpy(tmp+1,host_addr,6);}
 }
 unsigned n=len<size?len:size;memcpy(buffer,tmp,n);log_event("GET",id,tmp,n);return n;
}
void tud_hid_set_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t const *buffer,uint16_t len){
 if(id==0 && len && buffer[0]==1){id=1;buffer++;len--;}
 last_set=id;set_count++;log_event("SET",id,buffer,len);
 if(type==HID_REPORT_TYPE_FEATURE){
  if(id==0xef && len>6)ef_byte=buffer[6];
  if(id==0xf5 && len>=7)memcpy(host_addr,buffer+1,6);
  if(id==0xf4 && len>=2 && buffer[0]==0x42)operational=true;
 }
 if(id==1 && len>=10){
  uint8_t right=buffer[2],left=buffer[4];
  leds=buffer[9];
  if(left!=rumble_left||right!=rumble_right){rumble_left=left;rumble_right=right;rumble_sequence++;}
 }
}
void tud_mount_cb(void){operational=false;log_event("MOUNT",0,NULL,0);}
void tud_umount_cb(void){operational=false;leds=0;rumble_left=rumble_right=0;rumble_sequence++;log_event("UNMOUNT",0,NULL,0);}
void ds3_task(const ds3_input_t *input){
 ds3_build_packet(input,packet);
 uint32_t now=to_ms_since_boot(get_absolute_time());
 if(now-last_send>=8 && tud_hid_ready()){
  if(tud_hid_report(1,packet+1,48)){last_send=now;send_count++;}
 }
}
bool ds3_operational(void){return operational;}
void ds3_feedback(uint8_t *left,uint8_t *right,uint32_t *sequence){
 if(left)*left=rumble_left;
 if(right)*right=rumble_right;
 if(sequence)*sequence=rumble_sequence;
}
unsigned ds3_status(char *out,unsigned cap){
 unsigned n=snprintf(out,cap,"\"usb\":%s,\"ready\":%s,\"operational\":%s,\"reports\":%lu,\"gets\":%lu,\"sets\":%lu,\"last_get\":%u,\"last_set\":%u,\"leds\":%u,\"rumble_left\":%u,\"rumble_right\":%u,\"rumble_sequence\":%lu,\"events\":[",tud_mounted()?"true":"false",tud_ready()?"true":"false",operational?"true":"false",(unsigned long)send_count,(unsigned long)get_count,(unsigned long)set_count,last_get,last_set,leds,rumble_left,rumble_right,(unsigned long)rumble_sequence);
 unsigned total=event_index<12?event_index:12;
 for(unsigned i=0;i<total && n<cap;i++)n+=snprintf(out+n,cap-n,"%s\"%s\"",i?",":"",events[(event_index-total+i)%12]);
 if(n<cap)n+=snprintf(out+n,cap-n,"]");
 return n;
}
