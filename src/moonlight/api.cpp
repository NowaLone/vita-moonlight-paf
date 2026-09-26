#include <stddef.h>
#include "moonlight/api.h"
#include "moonlight/config.h"
namespace {
static MoonlightEventCallback cb=NULL; static void *ud=NULL;
static void emit(MoonlightEventType t,int r,int id,const char*a){if(!cb)return;MoonlightEvent e={t,r,id,a};cb(&e,ud);}
}
extern "C" {
int moonlight_api_init(void){return moonlight_config_init();}
void moonlight_api_shutdown(void){moonlight_config_shutdown();cb=NULL;ud=NULL;}
const char* moonlight_api_config_path(void){return moonlight_config_path();}
int moonlight_api_get_settings(MoonlightSettings*out){return moonlight_config_get(out);}
int moonlight_api_get_setting_value(MoonlightSettingKey k,int*out){return moonlight_config_get_value(k,out);}
int moonlight_api_set_setting_value(MoonlightSettingKey k,int v){return moonlight_config_set_value(k,v);}
int moonlight_api_set_motion_scalar_x(float v){return moonlight_config_set_motion_scalar_x(v);}
int moonlight_api_set_motion_scalar_y(float v){return moonlight_config_set_motion_scalar_y(v);}
int moonlight_api_save_settings(void){return moonlight_config_save();}
int moonlight_api_apply_settings(void){return moonlight_config_save();}
int moonlight_api_set_event_callback(MoonlightEventCallback f,void*u){cb=f;ud=u;return 0;}
int moonlight_api_get_hosts(MoonlightHost*out,int cap){(void)out;(void)cap;return 0;}
int moonlight_api_search_hosts(void){emit(MOONLIGHT_EVENT_HOST_SCAN_STARTED,0,-1,NULL);emit(MOONLIGHT_EVENT_HOST_SCAN_FINISHED,0,-1,NULL);return 0;}
int moonlight_api_add_host(const char*a,uint16_t p,const char*n){(void)a;(void)p;(void)n;return 0;}
int moonlight_api_pair_host(const char*a){(void)a;return 0;}
int moonlight_api_start_stream(const char*a){(void)a;return 0;}
int moonlight_api_stop_stream(void){return 0;}
}
