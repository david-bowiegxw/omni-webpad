#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "tusb.h"
#include "lwip/tcp.h"
#include "dhcpserver.h"
#include "ds3.h"
#include "packet.h"
#include "web_content.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define WIFI_SSID "PS3-PicoW"
#define WIFI_PASSWORD "PicoPad123"
#define LEASE_MS 400
static ds3_input_t input;
static uint32_t last_command;
static unsigned clients;
typedef struct {
 struct tcp_pcb *pcb;
 char input[1024];unsigned used;
 uint8_t *output;unsigned length,queued,acked;
 uint32_t started;
} connection_t;
static uint32_t now_ms(void){return to_ms_since_boot(get_absolute_time());}
static void free_connection(connection_t *c){if(!c)return;free(c->output);free(c);clients--;}
static err_t close_connection(connection_t *c){
 struct tcp_pcb *pcb=c->pcb;
 tcp_arg(pcb,NULL);tcp_sent(pcb,NULL);tcp_recv(pcb,NULL);tcp_poll(pcb,NULL,0);tcp_err(pcb,NULL);
 err_t err=tcp_close(pcb);if(err!=ERR_OK)tcp_abort(pcb);
 free_connection(c);return err==ERR_OK?ERR_OK:ERR_ABRT;
}
static void on_error(void *arg,err_t err){free_connection(arg);}
static void pump(connection_t *c){
 while(c->output && c->queued<c->length){
  unsigned n=c->length-c->queued;
  if(n>tcp_sndbuf(c->pcb))n=tcp_sndbuf(c->pcb);
  if(n>1024)n=1024;
  if(!n)break;
  if(tcp_write(c->pcb,c->output+c->queued,n,TCP_WRITE_FLAG_COPY)!=ERR_OK)break;
  c->queued+=n;
 }
 tcp_output(c->pcb);
}
static err_t on_sent(void *arg,struct tcp_pcb *pcb,u16_t len){
 connection_t *c=arg;c->acked+=len;
 if(c->acked==c->length){
  free(c->output);c->output=NULL;c->length=c->queued=c->acked=c->used=0;
  c->started=now_ms();return ERR_OK;
 }
 pump(c);return ERR_OK;
}
static err_t on_poll(void *arg,struct tcp_pcb *pcb){
 connection_t *c=arg;if(now_ms()-c->started>4000)return close_connection(c);
 pump(c);return ERR_OK;
}
static bool respond(connection_t *c,const void *body,unsigned length,bool gzip,int status){
 char h[256];int n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%sCache-Control: no-store\r\nConnection: keep-alive\r\n\r\n",status,status==200?"OK":"Bad Request",gzip?"text/html; charset=utf-8":"application/json",length,gzip?"Content-Encoding: gzip\r\n":"");
 c->length=n+length;c->output=malloc(c->length);if(!c->output)return false;
 memcpy(c->output,h,n);memcpy(c->output+n,body,length);pump(c);return true;
}
static bool parse_state(const char *path,ds3_input_t *next){
 unsigned long buttons;unsigned lx,ly,rx,ry,ax,ay,az,gz;
 int consumed=0;
 int n=sscanf(path,"/state?m=%lu&lx=%u&ly=%u&rx=%u&ry=%u&ax=%u&ay=%u&az=%u&gz=%u%n",&buttons,&lx,&ly,&rx,&ry,&ax,&ay,&az,&gz,&consumed);
 if(n==9 && path[consumed]==0 && buttons<=DS3_MASK_ALL && lx<=255 && ly<=255 && rx<=255 && ry<=255 && ax<=1023 && ay<=1023 && az<=1023 && gz<=1023){
  next->buttons=(uint32_t)buttons;next->lx=lx;next->ly=ly;next->rx=rx;next->ry=ry;
  next->accel_x=ax;next->accel_y=ay;next->accel_z=az;next->gyro_z=gz;return true;
 }
 if(strncmp(path,"/state?m=",9)==0){
  char *end;errno=0;buttons=strtoul(path+9,&end,10);
  if(!errno && end!=path+9 && *end==0 && buttons<=DS3_MASK_ALL){
   ds3_neutral_input(next);next->buttons=(uint32_t)buttons;return true;
  }
 }
 return false;
}
static err_t on_recv(void *arg,struct tcp_pcb *pcb,struct pbuf *p,err_t err){
 connection_t *c=arg;
 if(!p)return close_connection(c);
 tcp_recved(pcb,p->tot_len);
 if(c->output){pbuf_free(p);return ERR_OK;}
 if(c->used+p->tot_len>=sizeof(c->input)){pbuf_free(p);return close_connection(c);}
 c->used+=pbuf_copy_partial(p,c->input+c->used,p->tot_len,0);pbuf_free(p);c->input[c->used]=0;
 if(!strstr(c->input,"\r\n\r\n"))return ERR_OK;
 char path[240];
 if(sscanf(c->input,"GET %239s",path)!=1)return close_connection(c);
 if(strncmp(path,"/state?m=",9)==0 || strcmp(path,"/status")==0){
  if(strncmp(path,"/state?m=",9)==0){
   ds3_input_t next;
   if(!parse_state(path,&next)){
    const char *bad="{\"error\":\"invalid input\"}";
    if(!respond(c,bad,strlen(bad),false,400))return close_connection(c);
    return ERR_OK;
   }
   input=next;last_command=now_ms();
   uint8_t left,right;uint32_t sequence;ds3_feedback(&left,&right,&sequence);
   char body[160];unsigned n=snprintf(body,sizeof(body),"{\"usb\":%s,\"operational\":%s,\"l\":%u,\"r\":%u,\"q\":%lu}",tud_mounted()?"true":"false",ds3_operational()?"true":"false",left,right,(unsigned long)sequence);
   if(!respond(c,body,n,false,200))return close_connection(c);
  }else{
   char body[1800];unsigned n=snprintf(body,sizeof(body),"{\"version\":\"0.3.0\",\"mask\":%lu,\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"uptime_ms\":%lu,",(unsigned long)input.buttons,input.lx,input.ly,input.rx,input.ry,(unsigned long)now_ms());
   n+=ds3_status(body+n,sizeof(body)-n-2);body[n++]='}';
   if(!respond(c,body,n,false,200))return close_connection(c);
  }
 }else{
  if(!respond(c,page_gz,sizeof(page_gz),true,200))return close_connection(c);
 }
 return ERR_OK;
}
static err_t on_accept(void *arg,struct tcp_pcb *pcb,err_t err){
 if(err!=ERR_OK||clients>=5){tcp_abort(pcb);return ERR_ABRT;}
 connection_t *c=calloc(1,sizeof(*c));if(!c){tcp_abort(pcb);return ERR_ABRT;}
 c->pcb=pcb;c->started=now_ms();clients++;
 tcp_arg(pcb,c);tcp_recv(pcb,on_recv);tcp_sent(pcb,on_sent);tcp_err(pcb,on_error);tcp_poll(pcb,on_poll,2);tcp_nagle_disable(pcb);return ERR_OK;
}
int main(void){
 ds3_neutral_input(&input);ds3_init();tusb_init();
 if(cyw43_arch_init())return 1;
 cyw43_arch_enable_ap_mode(WIFI_SSID,WIFI_PASSWORD,CYW43_AUTH_WPA2_AES_PSK);
 ip_addr_t ip,mask;IP4_ADDR(&ip,192,168,4,1);IP4_ADDR(&mask,255,255,255,0);
 dhcp_server_t dhcp;dhcp_server_init(&dhcp,&ip,&mask);
 struct tcp_pcb *pcb=tcp_new_ip_type(IPADDR_TYPE_V4);
 if(!pcb||tcp_bind(pcb,IP_ANY_TYPE,80)!=ERR_OK)return 2;
 pcb=tcp_listen_with_backlog(pcb,4);if(!pcb)return 3;tcp_accept(pcb,on_accept);
 uint32_t last_led=0;
 for(;;){
  tud_task();cyw43_arch_poll();
  uint32_t now=now_ms();if(now-last_command>LEASE_MS)ds3_neutral_input(&input);
  ds3_task(&input);
  if(now-last_led>250){last_led=now;cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN,tud_mounted()?1:((now/500)&1));}
  sleep_us(100);
 }
}
