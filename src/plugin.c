#include <stdlib.h>
#include <vitasdk.h>
#include <taihen.h>
#include <psp2/display.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <stdint.h>
#include <string.h>
/* DD Touch UI v0.36. Touch gestures + optional native game-controller actions.
   Double tap Cross, portrait touch Square, dungeon map right stick.
   Import hooks only affect game; system and real controller are untouched.
   Front touch native widget pointer handling and rear touch remain supported. */
#define CHORD (SCE_CTRL_START | SCE_CTRL_LTRIGGER)
static SceUID hooks[24];
static tai_hook_ref_t refs[24];
static int mode=1,popup=180,menu_open=0,front_was_pressed=0;
static int ui_fade=0; /* overlay alpha: 0..255 */
static uint8_t enabled[24]={0};
static uint32_t old_buttons=0;
/* Centered Vita overlay. The chord shows/hides UI only; it never toggles mode. */
#define UI_X 310
#define UI_Y 82
#define UI_W 340
#define UI_H 375
static volatile int ui_panel_capture=0,ui_touch_start_x=0,ui_touch_start_y=0;
static volatile unsigned int ui_touch_start_frame=0,ui_mode_hold_rejected=0;
#define UI_MODE_HOLD_FRAMES 45u  /* 750ms at nominal 60fps. A simple tap never switches touch off. */

static volatile unsigned int ui_opens=0,ui_closes=0,ui_toggles=0,ui_consumed=0,ui_passthrough=0;
static volatile unsigned int ui_controls_consumed=0,ui_nav_actions=0,ui_touch_actions=0,ui_pointer_resets=0;
static volatile int ui_nav_row=1,ui_status_dirty=0;
static volatile unsigned int ui_prev_buttons=0;
static volatile unsigned int ui_last_close_frame=0;
static volatile unsigned int ui_last_active_frame=0;
static volatile int ui_touch_last_x=0,ui_touch_last_y=0;
static volatile int ui_down_action_done=0;
static volatile unsigned int ui_last_front_sample_frame=0;
static volatile unsigned int ui_gesture_recovered=0;



static volatile int fx=0,fy=0,front_pressed=0,rear_pressed=0;
static volatile int seen_peek_enter=0,seen_peek_exit=0,seen_frame=0,seen_front=0,seen_back=0;
static int seen_pad=0,seen_chord=0;
/* PCSE00919 v1.16 engine mouse bridge: experimental, isolated Vita3K only. */
static volatile int engine_touch_x=0,engine_touch_y=0,engine_touch_held=0,engine_touch_valid=0;
static volatile unsigned int touch_press_edges=0,touch_release_edges=0,front_reports=0;

static volatile int engine_mouse_down_frames=0,engine_mouse_up_frames=0;
static volatile int engine_target_confirmed=0;
static volatile int engine_strict_rejected=0;
static int engine_compatible=0;
static volatile float *engine_cursor_ptr=0;
static volatile int *engine_button_ptr=0;
/* These are diagnostics only: no writes to unidentified engine globals. */
static volatile const unsigned int *engine_gui_reference_id=0;
static volatile const unsigned int *engine_ui_global_state=0;
static volatile unsigned int captured_widget_idlo=0,captured_widget_idhi=0;
static volatile unsigned int captured_widget_hover_count=0;
static volatile unsigned int gesture_cleanup_count=0;
static volatile unsigned int gesture_start_count=0;
static volatile unsigned int idle_passthrough_frames=0;
/* Performance-first diagnostic scheme. Never write files during touch. */
static volatile unsigned int diag_snapshots=0,diag_skipped_active=0;
static volatile unsigned int diag_next_frame=400,diag_write_step=0;
static int diag_detailed_enabled=0;

static volatile unsigned int controller_active_frames=0;
/* Last usable diagnostic directory: 0=not writable, 1=ux0, 2=ur0. */
static volatile int dd_diag_path=0,dd_diag_last_error=0;
static void trace_once(const char *filename,const char *text);
/* v0.35: read-only native UI pointer hit test plus optional virtual controller
   pulses. All physical pad inputs are preserved and OR'ed into returned data.
   We hook eboot imports only: never modify global system SceCtrl behavior. */
static int dd_enable_doubletap=0,dd_enable_map=1,dd_enable_hero=1;
static int dd_enable_modal_close=1;
static volatile unsigned int dd_modal_close_events=0,dd_inventory_map_suppressed=0;
static volatile unsigned int dd_immediate_events=0,dd_cursor_detaches=0,dd_joystick_handoffs=0;
static volatile unsigned int dd_last_touch_frame=0,dd_touch_watchdog=0;
static volatile int dd_pointer_moved=0;
/* v0.39: default safety/performance path. The engine's own coordinates and
   mouse-button array are enough for the native pointer pipeline. Hooking
   GUI dispatch/focus on every widget caused expensive 1.17 battle frames.
   Cross/Square/right-stick emulation is forbidden in this mode because it
   can activate the focused (wrong) attack instead of the tapped one. */
static int dd_fast_input=1;
static int ui_sounds_enabled=0;
static int diag_runtime_enabled=0;
static volatile unsigned int dd_frame_no=0;
static volatile unsigned int dd_fast_touches=0,dd_fast_up_events=0;
/* v0.41: default to actual spatial position/hover and do not generate the
   engine's global mouse button click. In the console controller mode the
   global click can confirm the previously selected attack ANYWHERE. */
static int dd_spatial_only=1;
/* Selected touch target stays highlighted after finger release. A real pad
   input immediately releases it back to the normal controller navigation. */
static int dd_hover_persist=1;
static volatile unsigned int dd_hover_focus_suppressed=0;
static volatile unsigned int dd_hover_touch_releases=0;
/* Internal GUI router probe: emulator-only opt-in until validated on physical
   hardware. Read native widget output for spatial hit detection, NEVER write
   to engine widget focus or callback data. */
static int dd_widget_probe=1;
static int dd_menu_click=1;
static volatile int dd_menu_authorized=0;
static volatile unsigned int dd_menu_armed=0,dd_menu_held_frames=0,dd_menu_denied=0;
static volatile unsigned int dd_menu_click_ref=0,dd_menu_click_id=0;
/* v0.58 native widget activation, independent from simulated Cross.
   GUI dispatcher returns event[0]=hover and event[1]=confirm; never
   emit confirmation without a positive spatial hover on the touch. */
static int dd_direct_click=1,dd_direct_all_widgets=1;
static volatile unsigned int dd_direct_gesture=0,dd_direct_completed=0;
static volatile unsigned int dd_direct_attempts=0,dd_direct_allowed=0,dd_direct_denied=0;
static volatile unsigned int dd_direct_last_ref=0,dd_direct_last_widget=0;

/* v0.54 spatial double-tap for known frontend GUI widgets only. */
#define DD_REPEAT_FRAMES 108u
#define DD_REPEAT_RADIUS 46
static int dd_repeat_confirm=0;
static volatile unsigned int dd_repeat_first_frame=0,dd_repeat_first_ref=0;
static volatile unsigned int dd_repeat_first_id=0,dd_repeat_first_hi=0;
static volatile int dd_repeat_first_x=-2000,dd_repeat_first_y=-2000;
static volatile int dd_repeat_candidate=0,dd_repeat_observed=0;
static volatile unsigned int dd_repeat_first_hits=0,dd_repeat_success=0,dd_repeat_mismatch=0;

static int dd_is_frontend_ref(void){
 unsigned int ref=engine_gui_reference_id?engine_gui_reference_id[0]:0;
 /* Frontend: 'strt' / save-slot references 'sgsb','sgsc','sgsd'.
    Each save row changes the active reference even before activation. */
 return ref==0x73747274u ||
        (ref>=0x73677362u && ref<=0x73677364u);
}
static volatile unsigned int dd_widget_fast_calls=0,dd_widget_fast_active=0,dd_widget_fast_positive=0,dd_widget_fast_changes=0;
typedef struct {unsigned int frame,idlo,idhi,flags,ref;int x,y;unsigned int arg2,arg3;} dd_widget_evt;
#define DD_WIDGET_SAMPLES 12
static dd_widget_evt dd_widget_events[DD_WIDGET_SAMPLES];
static volatile unsigned int dd_widget_event_count=0;
static volatile unsigned int dd_widget_last_idlo=0,dd_widget_last_idhi=0,dd_widget_last_flags=0;

static volatile int dd_hover_latched=0;
static volatile unsigned int dd_hover_latches=0,dd_hover_pad_handoffs=0;
static volatile unsigned int dd_blocked_global_clicks=0;
#define DD_TOUCH_PROBE_N 8
typedef struct {
 unsigned int frame;
 int x,y,down;
 unsigned int ref,gui160,gui168,gui170,gui178;
} dd_touch_probe_t;
static dd_touch_probe_t dd_probes[DD_TOUCH_PROBE_N];
static volatile unsigned int dd_probe_total=0;
static void dd_probe_sample(int pressed,int x,int y){
 dd_touch_probe_t *t=&dd_probes[dd_probe_total%DD_TOUCH_PROBE_N];
 t->frame=dd_frame_no;t->x=x;t->y=y;t->down=pressed;
 /* Previous build dereferenced unverified game UI globals on every
    finger event. The crash-safe build does not touch them. */
 t->ref=0;t->gui160=0;t->gui168=0;t->gui170=0;t->gui178=0;
 dd_probe_total++;
}
static void dd_probe_report(void){
 /* Static storage: v0.41 allocated 3948 bytes of stack for this function
    on a game display thread and could overflow a 4KB guest stack. */
 static char b[1024];int n=0;
 unsigned int total=dd_probe_total;
 unsigned int first=total>DD_TOUCH_PROBE_N?total-DD_TOUCH_PROBE_N:0;
 n+=sceClibSnprintf(b+n,sizeof(b)-n,
   "v042 touch spatial_only=%d samples=%u hover_latched=%d latched=%u gamepad_handoff=%u blocked_clicks=%u\\n",
   dd_spatial_only,total,dd_hover_latched,dd_hover_latches,
   dd_hover_pad_handoffs,dd_blocked_global_clicks);
 for(unsigned int i=first;i<total && n<(int)sizeof(b)-130;i++){
  const dd_touch_probe_t*t=&dd_probes[i%DD_TOUCH_PROBE_N];
  n+=sceClibSnprintf(b+n,sizeof(b)-n,
    "frame=%u event=%c xy=%d,%d ref=%08x g160=%08x g168=%08x g170=%08x g178=%08x\\n",
    t->frame,t->down?'D':'U',t->x,t->y,t->ref,
    t->gui160,t->gui168,t->gui170,t->gui178);
 }
 /* Vita3K's virtual ux0 filesystem may ignore O_TRUNC and leave tail
    bytes when a shorter snapshot overwrites an older one. Update only
    after the long idle window, never inside the touch callback. */
 sceIoRemove("ux0:data/DDTouchUI/input_targets_v047.txt");
 trace_once("ux0:data/DDTouchUI/input_targets_v047.txt",b);
}

static volatile int dd_fast_snapshot_pending=0;
static volatile unsigned int dd_fast_last_snapshot=0;


/* Keeps the touched menu item's hover after release until a real controller input.
   This avoids the old gamepad focus immediately reselecting the top menu item. */
static int dd_focus_hold_enabled=1;
static volatile int dd_touch_focus_latched=0;
static volatile unsigned int dd_focus_latched_events=0;
static volatile unsigned int dd_focus_unlatched_pad=0;
static volatile unsigned int dd_focus_suppressed_latched=0;
static void dd_detach_mouse_pointer(void){
 dd_touch_focus_latched=0;
 dd_hover_latched=0;
 if(engine_cursor_ptr && engine_button_ptr && dd_pointer_moved){
  /* The engine's mouse getter returns out-of-bounds when coordinates are
     negative. This releases spatial hover so gamepad focus controls the UI. */
  engine_button_ptr[0]=0;
  engine_cursor_ptr[0]=-1024.0f;
  engine_cursor_ptr[1]=-1024.0f;
  dd_cursor_detaches++;dd_pointer_moved=0;
 }
}
static unsigned int dd_current_gui_ref(void){
 if(!engine_gui_reference_id || !engine_compatible)return 0;
 return engine_gui_reference_id[0];
}
static int dd_inventory_modal(void){
 unsigned int ref=dd_current_gui_ref();
 return (ref&0xFFFFFF00u)==0x696E7600u; /* inv[space] / inv! */
}
static int dd_modal_close_spot(int x,int y){
 if(!dd_enable_modal_close || !dd_inventory_modal())return 0;
 /* Restrict to clearly external screen margins, not inventory slots.
    No synthetic Circle on every unrecognized widget. */
 return (y<82 && (x<205 || x>755)) || x<62 || (x>920 && y<165);
}

#define DD_DOUBLE_TAP_FRAMES 21u /* 350ms at 60fps */
#define DD_TAP_MAX_HOLD_FRAMES 32u
#define DD_DOUBLE_TAP_RADIUS 68
#define DD_ACTION_CROSS 1
#define DD_ACTION_HERO 2
#define DD_ACTION_MAP 3
#define DD_ACTION_CIRCLE 4
static volatile unsigned int dd_touch_down_frame=0;
static volatile int dd_touch_start_x=0,dd_touch_start_y=0;
static volatile int dd_last_tap_x=-999,dd_last_tap_y=-999;
static volatile unsigned int dd_last_tap_frame=0,dd_last_tap_id=0;
static volatile int dd_have_first_tap=0,dd_pending_context_action=0;
static volatile unsigned int dd_pending_deadline=0;
static volatile int dd_pending_map_x=0,dd_pending_map_y=0;
static volatile unsigned int dd_action_frames=0,dd_action_type=0;
static volatile uint8_t dd_action_rx=128,dd_action_ry=128;
static volatile unsigned int dd_doubletap_events=0,dd_map_events=0,dd_hero_events=0;
static volatile unsigned int dd_ctrl_peek_events=0,dd_ctrl_read_events=0,dd_ctrl_injected=0;
static volatile unsigned int dd_ctrl_hook_mask=0,dd_taps_ignored=0;

/* Limited regions for game HUD experimentation, not a general per-button map.
   Title menu buttons still use the game's own native widget hit-test.
   Do not emit status-screen or map action for touch outside these HUD zones. */
static int dd_hero_region(int x,int y){
 return x>=35 && x<=285 && y>=347 && y<=540;
}
static int dd_map_region(int x,int y){
 return x>=680 && x<=952 && y>=347 && y<=532;
}
static int dd_action_for_point(int x,int y){
 /* Inventory item selection must never trigger an unrelated hero/map pulse. */
 if(dd_inventory_modal()){
  if((dd_enable_hero && dd_hero_region(x,y)) ||
     (dd_enable_map && dd_map_region(x,y)))dd_inventory_map_suppressed++;
  return 0;
 }
 if(dd_enable_hero && dd_hero_region(x,y))return DD_ACTION_HERO;
 if(dd_enable_map && dd_map_region(x,y))return DD_ACTION_MAP;
 return 0;
}
static void dd_enqueue_action(unsigned int action,int x,int y){
 if(dd_fast_input || !mode || ui_panel_capture || !engine_compatible)return;
 dd_action_type=action;
 dd_action_frames=action==DD_ACTION_MAP?24u:4u;
 if(action==DD_ACTION_MAP){
  /* Vita's right stick selects a neighboring route on the dungeon map.
     Calculate axis from tap's location inside the mini-map. */
  int dx=x-810,dy=y-437;
  int max=dx<0?-dx:dx;
  if((dy<0?-dy:dy)>max)max=dy<0?-dy:dy;
  if(max<18){dd_action_frames=0;return;}
  int vx=128+(dx*102)/(max?max:1);
  int vy=128+(dy*102)/(max?max:1);
  if(vx<0)vx=0;else if(vx>255)vx=255;
  if(vy<0)vy=0;else if(vy>255)vy=255;
  dd_action_rx=(uint8_t)vx;dd_action_ry=(uint8_t)vy;
  dd_map_events++;
 }else if(action==DD_ACTION_HERO)dd_hero_events++;
 else if(action==DD_ACTION_CROSS)dd_doubletap_events++;
 else if(action==DD_ACTION_CIRCLE)dd_modal_close_events++;
}
static void dd_touch_finished_tap(int x,int y,unsigned int current_id){
 if(dd_fast_input || !mode || ui_panel_capture || !engine_compatible)return;
 unsigned int since=dd_frame_no-dd_touch_down_frame;
 if(since>DD_TAP_MAX_HOLD_FRAMES){dd_taps_ignored++;return;}
 /* Inventory/modal close is conservative: only explicit outside margins. */
 if(dd_modal_close_spot(x,y)){
  dd_have_first_tap=0;dd_pending_context_action=0;
  dd_enqueue_action(DD_ACTION_CIRCLE,x,y);
  dd_immediate_events++;
  return;
 }
 int context=dd_action_for_point(x,y);
 if(context){
  /* No 350ms wait for taps on the hero HUD or dungeon map.
     Distinguish inventory by active engine-native GUI reference. */
  dd_have_first_tap=0;dd_pending_context_action=0;
  dd_enqueue_action((unsigned int)context,x,y);
  dd_immediate_events++;
  return;
 }
 int double_match=dd_enable_doubletap && dd_have_first_tap &&
                  (dd_frame_no-dd_last_tap_frame)<=DD_DOUBLE_TAP_FRAMES;
 int dx=x-dd_last_tap_x,dy=y-dd_last_tap_y;
 if(dx*dx+dy*dy>DD_DOUBLE_TAP_RADIUS*DD_DOUBLE_TAP_RADIUS)double_match=0;
 if(current_id && dd_last_tap_id && current_id!=dd_last_tap_id)double_match=0;
 if(double_match){
  dd_have_first_tap=0;dd_pending_context_action=0;
  dd_enqueue_action(DD_ACTION_CROSS,x,y);
 }else{
  dd_last_tap_x=x;dd_last_tap_y=y;
  dd_last_tap_frame=dd_frame_no;dd_last_tap_id=current_id;
  dd_have_first_tap=dd_enable_doubletap;
  dd_pending_context_action=0; /* do not queue unrelated actions */
 }
}
static void dd_inject_pad(SceCtrlData *pads,int ret,int port,int is_read){
 if(ret<=0 || !pads || port!=0)return;
 if(menu_open){
  const unsigned int mask=SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT|
    SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_TRIANGLE|SCE_CTRL_SQUARE|
    SCE_CTRL_SELECT|SCE_CTRL_START|SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER;
  for(int i=0;i<ret;i++){
   pads[i].buttons&=~mask;
   pads[i].lx=128;pads[i].ly=128;
  }
  ui_controls_consumed++;
  return;
 }
 /* Prevent START+L chord from reaching the game as a pause command. */
 for(int i=0;i<ret;i++)if((pads[i].buttons&CHORD)==CHORD)pads[i].buttons&=~CHORD;
 if(dd_fast_input || !mode || ui_panel_capture || !engine_compatible ||
    !dd_action_frames)return;
 for(int i=0;i<ret;i++){
  if(dd_action_type==DD_ACTION_CROSS)
   pads[i].buttons|=SCE_CTRL_CROSS;
  else if(dd_action_type==DD_ACTION_CIRCLE)
   pads[i].buttons|=SCE_CTRL_CIRCLE;
  else if(dd_action_type==DD_ACTION_HERO)
   pads[i].buttons|=SCE_CTRL_SQUARE;
  else if(dd_action_type==DD_ACTION_MAP){
   /* Physical right-stick movement takes priority. */
   int rx=(int)pads[i].rx-128,ry=(int)pads[i].ry-128;
   if(rx>-25 && rx<25 && ry>-25 && ry<25){
    pads[i].rx=dd_action_rx;pads[i].ry=dd_action_ry;
   }
  }
 }
 dd_ctrl_injected++;
 if(is_read)dd_ctrl_read_events++;else dd_ctrl_peek_events++;
}
static int dd_ctrl_peek(int idx,int port,SceCtrlData*pads,int count){
 int ret=0;
 if(idx==16)ret=TAI_CONTINUE(int,refs[16],port,pads,count);
 else if(idx==17)ret=TAI_CONTINUE(int,refs[17],port,pads,count);
 else if(idx==18)ret=TAI_CONTINUE(int,refs[18],port,pads,count);
 else ret=TAI_CONTINUE(int,refs[19],port,pads,count);
 dd_inject_pad(pads,ret,port,0);
 return ret;
}
static int dd_ctrl_read(int idx,int port,SceCtrlData*pads,int count){
 int ret=0;
 if(idx==20)ret=TAI_CONTINUE(int,refs[20],port,pads,count);
 else if(idx==21)ret=TAI_CONTINUE(int,refs[21],port,pads,count);
 else if(idx==22)ret=TAI_CONTINUE(int,refs[22],port,pads,count);
 else ret=TAI_CONTINUE(int,refs[23],port,pads,count);
 dd_inject_pad(pads,ret,port,1);
 return ret;
}
static int dd_hook_peek_0(int p,SceCtrlData*d,int n){return dd_ctrl_peek(16,p,d,n);}
static int dd_hook_peek_1(int p,SceCtrlData*d,int n){return dd_ctrl_peek(17,p,d,n);}
static int dd_hook_peek_2(int p,SceCtrlData*d,int n){return dd_ctrl_peek(18,p,d,n);}
static int dd_hook_peek_3(int p,SceCtrlData*d,int n){return dd_ctrl_peek(19,p,d,n);}
static int dd_hook_read_0(int p,SceCtrlData*d,int n){return dd_ctrl_read(20,p,d,n);}
static int dd_hook_read_1(int p,SceCtrlData*d,int n){return dd_ctrl_read(21,p,d,n);}
static int dd_hook_read_2(int p,SceCtrlData*d,int n){return dd_ctrl_read(22,p,d,n);}
static int dd_hook_read_3(int p,SceCtrlData*d,int n){return dd_ctrl_read(23,p,d,n);}
static void dd_actions_report(void){
 char b[550];
 sceClibSnprintf(b,sizeof(b),
  "doubletap=%u hero=%u map=%u ignored=%u action=%u frames=%u lasttap=(%d,%d) window=%u\\n"
  "ctrl_hookmask=%08x peek_calls=%u read_calls=%u injected_samples=%u prefs=(double:%d hero:%d map:%d close:%d) close_events=%u inv_map_ignored=%u immediate=%u cursor_detach=%u pad_handoff=%u watchdog=%u gui=%08x\\n",
  dd_doubletap_events,dd_hero_events,dd_map_events,dd_taps_ignored,
  dd_action_type,dd_action_frames,dd_last_tap_x,dd_last_tap_y,DD_DOUBLE_TAP_FRAMES,
  dd_ctrl_hook_mask,dd_ctrl_peek_events,dd_ctrl_read_events,dd_ctrl_injected,
  dd_enable_doubletap,dd_enable_hero,dd_enable_map,dd_enable_modal_close,
  dd_modal_close_events,dd_inventory_map_suppressed,dd_immediate_events,
  dd_cursor_detaches,dd_joystick_handoffs,dd_touch_watchdog,dd_current_gui_ref());
 trace_once("ux0:data/DDTouchUI/touch_actions_v047.txt",b);
}

static void engine_mouse_apply(void){
 /* Touch is an ephemeral gesture, not a permanent mouse device.
    Never write engine mouse coordinates/buttons after the touch ends. */
 if(!mode || ui_panel_capture || !engine_compatible || !engine_touch_valid ||
    !engine_cursor_ptr || !engine_button_ptr ||
    (!engine_touch_held && engine_mouse_up_frames<=0))return;
 if(engine_touch_x<0 || engine_touch_x>959 || engine_touch_y<0 || engine_touch_y>543)return;
 volatile float *mouse=engine_cursor_ptr;
 volatile int *mousebuttons=engine_button_ptr;
 mouse[0]=(float)engine_touch_x;mouse[1]=(float)engine_touch_y;
 dd_pointer_moved=1;
 if(dd_fast_input){
  if(dd_spatial_only){
   /* Grant a native click only when a frontend widget under the finger is
      confirmed by GUI hover and the screen context is unchanged. */
   int safe_click=dd_menu_click && dd_menu_authorized &&
    dd_is_frontend_ref() && engine_gui_reference_id[0]==dd_menu_click_ref;
   if(safe_click){
    if(engine_touch_held){
     mousebuttons[0]=engine_mouse_down_frames>0?1:2;
     dd_menu_held_frames++;
    }else if(engine_mouse_up_frames>0)mousebuttons[0]=3;
   }else{
    mousebuttons[0]=0;
    if(dd_menu_click && engine_touch_held)dd_menu_denied++;
   }
  }else{
   /* DEVELOPER ONLY legacy behavior; unsafe during combat targeting. */
   if(engine_touch_held)mousebuttons[0]=(engine_mouse_down_frames>0)?1:2;
   else if(engine_mouse_up_frames>0)mousebuttons[0]=3;
  }
 }else{
  if(engine_target_confirmed && engine_touch_held)
   mousebuttons[0]=engine_mouse_down_frames>0?1:2;
  else if(engine_target_confirmed && engine_mouse_up_frames>0)
   mousebuttons[0]=3;
 }
 /* Outside a gesture, release mouse state to physical gamepad. */
}

static void trace_once(const char *filename,const char *text);
/* Experimental internal engine hooks, title PCSE00919 v1.16/v1.17 only. */
static volatile unsigned int hit_getter=0,hit_down=0,hit_up=0,hit_hold=0,hit_pos=0;
static volatile unsigned int down_by_button[8]={0},up_by_button[8]={0};
static volatile unsigned int down_overrides=0,up_overrides=0,hold_overrides=0,position_overrides=0;
static volatile int last_virtual_x=0,last_virtual_y=0;

/* 0x8104C670 converts the physical mouse coordinates to virtual-widget
   coordinates. Preserve this original conversion, by injecting the pointer
   BEFORE its call; overriding *x and *y after conversion breaks hit-testing. */
/* Noninvasive: record who uses the real engine coordinate getter.
   These callers can reveal the UI dispatcher shared by menu and gameplay. */
static volatile unsigned int xy_caller_pc[64]={0};
static volatile unsigned int xy_caller_hits[64]={0};
static volatile unsigned int xy_caller_count=0;
static void xy_caller_record(unsigned int pc){
 for(unsigned int i=0;i<xy_caller_count;i++){
  if(xy_caller_pc[i]==pc){xy_caller_hits[i]++;return;}
 }
 if(xy_caller_count<64){
  unsigned int i=xy_caller_count++;
  xy_caller_pc[i]=pc;xy_caller_hits[i]=1;
 }
}
static void xy_caller_log(void){
 char s[2700];int k=0;
 k+=sceClibSnprintf(s+k,sizeof(s)-k,"xy_count=%u touch=(%d,%d) active=%d valid=%d\n",
 xy_caller_count,engine_touch_x,engine_touch_y,engine_touch_held,engine_touch_valid);
 for(unsigned int i=0;i<xy_caller_count && k<sizeof(s)-52;i++)
  k+=sceClibSnprintf(s+k,sizeof(s)-k,"0x%08x:%u\n",xy_caller_pc[i],xy_caller_hits[i]);
 trace_once("ux0:data/DDTouchUI/xy_caller_trace.txt",s);
}
static int hook_engine_get_xy(float *x,float *y){
 if(diag_detailed_enabled)
  xy_caller_record(((unsigned int)(uintptr_t)__builtin_return_address(0))&~1u);
 if(mode && engine_compatible && !ui_panel_capture && engine_touch_valid &&
    (engine_touch_held || engine_mouse_up_frames>0)){
  engine_mouse_apply();
  position_overrides++;
 }
 int ret=TAI_CONTINUE(int,refs[9],x,y);
 hit_getter++;
 if(x&&y){last_virtual_x=(int)*x;last_virtual_y=(int)*y;}
 return ret;
}
/* Generic GUI widget input dispatcher, Vita v1.16 offset 0x3710C.
   ABI: void events_output[5], Widget* widget, bool allow_cursor, bool active.
   Logging only: does not override any callback or event. */
static volatile unsigned int widget_calls=0,widget_touch_calls=0,widget_cursor_enabled=0;
static volatile int in_widget_dispatch=0;
static volatile unsigned int native_focus_suppressed=0,native_focus_checked=0;
static volatile unsigned int touch_widget_lastflags=0,touch_widget_hover_samples=0;
static int hook_engine_native_focus(unsigned int idlo,unsigned int idhi){
 /* In v0.46 the old controller focus won immediately after release.
    While a latched pointer remains, suppress only GUI comparator matches,
    never inject Cross or interfere with the gameplay controller path. */
 int finger=mode && engine_touch_valid && engine_touch_held;
 int sticky=mode && dd_hover_persist && dd_fast_input && dd_spatial_only &&
            dd_hover_latched && dd_pointer_moved && !menu_open;
 if(!finger && !sticky)
  return TAI_CONTINUE(int,refs[15],idlo,idhi);
 int result=TAI_CONTINUE(int,refs[15],idlo,idhi);
 native_focus_checked++;
 if(mode && in_widget_dispatch &&
    (finger || sticky || (dd_focus_hold_enabled && dd_touch_focus_latched))){
  if(result){
   native_focus_suppressed++;
   if(dd_touch_focus_latched)dd_focus_suppressed_latched++;
   if(sticky)dd_hover_focus_suppressed++;
  }
  return 0;
 }
 return result;
}

static volatile unsigned int widget_r2ones=0,widget_r3ones=0;
static volatile unsigned int widget_output_counts[5]={0};
static volatile unsigned int widget_last_addr=0,widget_last_parent=0,widget_last_out=0,widget_last_ra=0;
static volatile unsigned int widget_rect_bits[5]={0};
static volatile unsigned int widget_r2last=0,widget_r3last=0;
static volatile unsigned int widget_callers[24]={0},widget_caller_hits[24]={0},widget_callers_n=0;
static volatile unsigned int widget_touch_enabled=0,widget_touch_disabled=0,widget_touch_output_any=0;
static void widget_record_caller(unsigned int addr){
 for(unsigned int i=0;i<widget_callers_n;i++){if(widget_callers[i]==addr){widget_caller_hits[i]++;return;}}
 if(widget_callers_n<24){int k=widget_callers_n++;widget_callers[k]=addr;widget_caller_hits[k]=1;}
}
static void hook_engine_widget_events(void *out,void *widget,unsigned int b2,unsigned int b3){
 unsigned int ra=((unsigned int)(uintptr_t)__builtin_return_address(0))&~1u;
 widget_calls++;
 if(diag_detailed_enabled)widget_record_caller(ra);
 if(b2)widget_r2ones++;if(b3)widget_r3ones++;
 if(engine_touch_held){widget_touch_calls++;if(b3)widget_touch_enabled++;else widget_touch_disabled++;}
 in_widget_dispatch=1;
 TAI_CONTINUE(void,refs[14],out,widget,b2,b3);
 in_widget_dispatch=0;
 if(engine_touch_held && out && widget){
  const volatile uint8_t*flags=(const volatile uint8_t*)out;
  int any=0;
  for(int k=0;k<5;k++){if(flags[k]){widget_output_counts[k]++;any=1;}}
  if(engine_touch_held && b3 && flags[0]){
   /* Capture the native widget identity for trace; never alter engine memory. */
   const volatile unsigned int *obj_id=(const volatile unsigned int *)widget;
   captured_widget_idlo=obj_id[0];
   captured_widget_idhi=obj_id[1];
   captured_widget_hover_count++;
  }
  if(engine_touch_held && b3 && flags[0] && !engine_target_confirmed){
   /* Native GUI spatial hover is authoritative while controller-focus
      comparison is suppressed; capture the full touch gesture once. */
   engine_target_confirmed=1;
  }
  if(engine_touch_held){
   touch_widget_lastflags=flags[0]|(flags[1]<<8)|(flags[2]<<16)|(flags[3]<<24);
   if(flags[0])touch_widget_hover_samples++;
   if(any)widget_touch_output_any++;
  }
  widget_last_addr=(unsigned int)(uintptr_t)widget;
  widget_last_out=flags[0]|(flags[1]<<8)|(flags[2]<<16)|(flags[3]<<24);
  widget_last_ra=ra;widget_r2last=b2;widget_r3last=b3;
  for(int k=0;k<5;k++)widget_rect_bits[k]=((const volatile uint32_t*)widget)[2+k];
 }
}
static void hook_engine_widget_probe(void *out,void *widget,unsigned int b2,unsigned int b3){
 /* Very hot function. Early-return with original when no gesture, without
    caller introspection, file writes or expensive loops. */
 dd_widget_fast_calls++;
 /* CRASH GUARD (v0.46): The v0.44 hook read the GUI reference from game
    data on EVERY widget call even before any front touch / menu appeared.
    Never read any game UI pointer until a real gesture is active.
    While idle this callback is exactly a pass-through. */
 int finger=mode && engine_touch_valid &&
            (engine_touch_held || engine_mouse_up_frames>0u);
 int sticky=mode && dd_hover_persist && dd_fast_input && dd_spatial_only &&
            dd_hover_latched && dd_pointer_moved && !menu_open;
 /* Startup stays a strict pass-through; in particular, no dereference
    of GUI pointers before touch, as required by the v0.44 crash fix. */
 if(!finger && !sticky){
  TAI_CONTINUE(void,refs[14],out,widget,b2,b3);
  return;
 }
 int frontend_active=dd_menu_click && engine_compatible && finger &&
                      (dd_direct_all_widgets || dd_is_frontend_ref());
 if(frontend_active || sticky)in_widget_dispatch=1;
 TAI_CONTINUE(void,refs[14],out,widget,b2,b3);
 if(frontend_active || sticky)in_widget_dispatch=0;
 /* Sticky focus requires only the native widget callback; do not inspect
    or trace widget pointers when the finger is no longer on the screen. */
 if(!finger || !dd_widget_probe || !out || !widget)return;
 dd_widget_fast_active++;
 const volatile uint8_t*event=(const volatile uint8_t*)out;
 if(mode && dd_direct_click && dd_menu_click && engine_touch_held &&
    !menu_open && !ui_panel_capture && engine_compatible &&
    b3 && event[0] && !event[1] && dd_direct_completed!=dd_direct_gesture &&
    engine_gui_reference_id && widget){
   const volatile uint32_t *obj=(const volatile uint32_t*)widget;
   unsigned int id=obj[0],hi=obj[1];
   unsigned int ctx=engine_gui_reference_id[0];
   dd_direct_attempts++;
   if(ctx && !hi && id>0 && id<=128 &&
      (dd_direct_all_widgets || dd_is_frontend_ref()) &&
      dd_frame_no-dd_touch_down_frame<=DD_TAP_MAX_HOLD_FRAMES){
     /* Native one-touch confirmation for the precisely hovered widget.
        event[1] is the engine's 'activated' output, not a global pad button. */
     ((volatile uint8_t*)out)[1]=1;
     dd_direct_completed=dd_direct_gesture;
     dd_direct_allowed++;
     dd_direct_last_ref=ctx;dd_direct_last_widget=id;
   }else dd_direct_denied++;
 }
 unsigned int flags=(unsigned int)event[0]|
                   ((unsigned int)event[1]<<8)|
                   ((unsigned int)event[2]<<16)|
                   ((unsigned int)event[3]<<24);
 if(!flags && !event[4])return;
 dd_widget_fast_positive++;
 const volatile unsigned int *wid=(const volatile unsigned int*)widget;
 unsigned int lo=wid[0],hi=wid[1];
 if(!dd_direct_click && dd_menu_click && dd_fast_input && dd_spatial_only &&
    engine_touch_held && b3 && event[0] && hi==0 &&
    lo>=1 && lo<=16 && dd_is_frontend_ref() &&
    !dd_menu_authorized){
   unsigned int current_ref=engine_gui_reference_id[0];
   int same=dd_repeat_confirm && dd_repeat_candidate &&
     dd_repeat_first_ref==current_ref && dd_repeat_first_id==lo &&
     dd_repeat_first_hi==hi &&
     dd_frame_no-dd_repeat_first_frame<=DD_REPEAT_FRAMES;
   if(same){
    dd_menu_authorized=1;
    dd_menu_click_ref=current_ref;
    dd_menu_click_id=lo;
    engine_mouse_down_frames=4;
    dd_menu_armed++;dd_repeat_success++;
    dd_repeat_candidate=0;dd_repeat_first_id=0;
   }else if(dd_repeat_confirm){
    if(!dd_repeat_observed){
     dd_repeat_first_ref=current_ref;
     dd_repeat_first_id=lo;
     dd_repeat_first_hi=hi;
     dd_repeat_first_x=engine_touch_x;
     dd_repeat_first_y=engine_touch_y;
     dd_repeat_first_frame=dd_frame_no;
     dd_repeat_first_hits++;
     dd_repeat_observed=1;
    }
    if(dd_repeat_candidate)dd_repeat_mismatch++;
   }else{
    dd_menu_authorized=1;
    dd_menu_click_ref=current_ref;
    dd_menu_click_id=lo;
    engine_mouse_down_frames=4;
    dd_menu_armed++;
   }
 }
 if(lo==dd_widget_last_idlo && hi==dd_widget_last_idhi &&
    flags==dd_widget_last_flags)return;
 dd_widget_last_idlo=lo;dd_widget_last_idhi=hi;dd_widget_last_flags=flags;
 unsigned int k=dd_widget_event_count++%DD_WIDGET_SAMPLES;
 dd_widget_events[k].frame=dd_frame_no;
 dd_widget_events[k].idlo=lo;dd_widget_events[k].idhi=hi;
 dd_widget_events[k].flags=flags;
 dd_widget_events[k].ref=engine_gui_reference_id?engine_gui_reference_id[0]:0;
 dd_widget_events[k].x=engine_touch_x;dd_widget_events[k].y=engine_touch_y;
 dd_widget_events[k].arg2=b2;dd_widget_events[k].arg3=b3;
 dd_widget_fast_changes++;
}
static void dd_widget_fast_report(void){
 /* Once after idle; never in a widget or touch callback. */
 static char buffer[1500];
 unsigned int total=dd_widget_event_count;
 unsigned int first=total>DD_WIDGET_SAMPLES?total-DD_WIDGET_SAMPLES:0;
 int n=sceClibSnprintf(buffer,sizeof(buffer),
  "v047 widget probe=%d calls=%u active=%u hits=%u changes=%u lastid=%08x:%08x lastflags=%08x\\n",
  dd_widget_probe,dd_widget_fast_calls,dd_widget_fast_active,
  dd_widget_fast_positive,dd_widget_fast_changes,
  dd_widget_last_idlo,dd_widget_last_idhi,dd_widget_last_flags);
 n+=sceClibSnprintf(buffer+n,sizeof(buffer)-n,
  "menu=%d armed=%u held=%u denied=%u last_id=%u last_ref=%08x\\n",
  dd_menu_click,dd_menu_armed,dd_menu_held_frames,dd_menu_denied,
  dd_menu_click_id,dd_menu_click_ref);
 for(unsigned int j=first;j<total && n<(int)sizeof(buffer)-105;j++){
  const dd_widget_evt *e=&dd_widget_events[j%DD_WIDGET_SAMPLES];
  n+=sceClibSnprintf(buffer+n,sizeof(buffer)-n,
   "f=%u xy=%d,%d id=%08x:%08x flags=%08x ref=%08x b2=%u b3=%u\\n",
   e->frame,e->x,e->y,e->idlo,e->idhi,e->flags,e->ref,e->arg2,e->arg3);
 }
 sceIoRemove("ux0:data/DDTouchUI/native_widget_probe_v047.txt");
 trace_once("ux0:data/DDTouchUI/native_widget_probe_v047.txt",buffer);
}
static void widget_report(void){
 char b[1900];int k=0;
 k+=sceClibSnprintf(b+k,sizeof(b)-k,
 "calls=%u touch=%u r2_enabled=%u r3_enabled=%u touch_r3_yes=%u no=%u output_touch=%u flags=%u,%u,%u,%u,%u\\n",
 widget_calls,widget_touch_calls,widget_r2ones,widget_r3ones,widget_touch_enabled,widget_touch_disabled,widget_touch_output_any,
 widget_output_counts[0],widget_output_counts[1],widget_output_counts[2],widget_output_counts[3],widget_output_counts[4]);
 k+=sceClibSnprintf(b+k,sizeof(b)-k,
 "last_widget=0x%08x caller=0x%08x last_flags=0x%08x opts=%u,%u rect_bits=%08x,%08x,%08x,%08x,%08x\\nfocus_checked=%u suppressed=%u touch_hover_samples=%u touch_lastflags=%08x\\n",
 widget_last_addr,widget_last_ra,widget_last_out,widget_r2last,widget_r3last,
 widget_rect_bits[0],widget_rect_bits[1],widget_rect_bits[2],widget_rect_bits[3],widget_rect_bits[4],
 native_focus_checked,native_focus_suppressed,touch_widget_hover_samples,touch_widget_lastflags);
 for(unsigned int i=0;i<widget_callers_n && k<sizeof(b)-70;i++)
  k+=sceClibSnprintf(b+k,sizeof(b)-k,"pc=0x%08x calls=%u\\n",widget_callers[i],widget_caller_hits[i]);
 trace_once("ux0:data/DDTouchUI/gui_widget_dispatch_probe.txt",b);
}
static int hook_engine_button_down(int button){
 int ret=TAI_CONTINUE(int,refs[10],button);
 hit_down++;
 if(button>=0 && button<8)down_by_button[button]++;
 if(mode && engine_compatible && !ui_panel_capture && engine_touch_valid && engine_target_confirmed && button==1 && engine_mouse_down_frames>0){down_overrides++;return 1;}
 return ret;
}
static int hook_engine_button_up(int button){
 int ret=TAI_CONTINUE(int,refs[11],button);
 hit_up++;
 if(button>=0 && button<8)up_by_button[button]++;
 if(mode && engine_compatible && !ui_panel_capture && engine_touch_valid && engine_target_confirmed && button==1 && engine_mouse_up_frames>0){up_overrides++;return 1;}
 return ret;
}
/* Engine 0x8104C534 checks state 1 (press edge) or 2 (held).
   Widgets that support drag/hold use this, not only the just-pressed query. */
static int hook_engine_button_held(int button){
 int ret=TAI_CONTINUE(int,refs[12],button);
 hit_hold++;
 if(mode && engine_compatible && !ui_panel_capture && engine_touch_valid &&
      button==1 && engine_target_confirmed && engine_touch_held){hold_overrides++;return 1;}
 return ret;
}
/* Instrument original, position-dependent widget hit-test with its own float ABI.
   Does NOT change game behavior: returns the unmodified original result. */
static volatile unsigned int hit_probe_calls=0,hit_probe_positive=0;
static volatile unsigned int hit_probe_last_out=0,hit_probe_last_ret=0;
static volatile unsigned int hit_probe_last_out_ptr=0;
static volatile unsigned int hit_probe_last_ctx=0;
static volatile float hit_probe_last_x=0,hit_probe_last_y=0;
static int hook_engine_hit_test(void *out,void *ctx,float x,float y){
 int ret=TAI_CONTINUE(int,refs[13],out,ctx,x,y);
 if(mode && engine_compatible && engine_touch_valid){
  hit_probe_calls++;
  if(ret){
   hit_probe_positive++;
   if(engine_touch_held)engine_target_confirmed=1;
  }
  hit_probe_last_ret=ret;
  hit_probe_last_x=x;hit_probe_last_y=y;
  hit_probe_last_out_ptr=(unsigned int)(uintptr_t)out;
  hit_probe_last_ctx=(unsigned int)(uintptr_t)ctx;
  if(out)hit_probe_last_out=*(volatile unsigned int *)out;
 }
 return ret;
}
static uint32_t hit_probe_floatbits(float value){
 uint32_t bits=0;
 memcpy(&bits,&value,sizeof(bits));
 return bits;
}
static void hit_probe_report(void){
 char b[330];
 sceClibSnprintf(b,sizeof(b),
   "calls=%u found=%u ret=%u out=0x%08x outptr=0x%08x ctx=0x%08x xbits=0x%08x ybits=0x%08x touch_xy=%d,%d active=%d confirmed=%d rejected=%d",
   hit_probe_calls,hit_probe_positive,hit_probe_last_ret,hit_probe_last_out,
   hit_probe_last_out_ptr,hit_probe_last_ctx,
   hit_probe_floatbits(hit_probe_last_x),hit_probe_floatbits(hit_probe_last_y),
   engine_touch_x,engine_touch_y,engine_touch_held,engine_target_confirmed,engine_strict_rejected);
 trace_once("ux0:data/DDTouchUI/engine_hitprobe_status.txt",b);
}
static void engine_hook_report(void){
 char output[512];
 sceClibSnprintf(output,sizeof(output),
 "getter=%u down=%u up=%u held=%u xy=%d,%d active=%d valid=%d downleft=%d upleft=%d xover=%u dover=%u uover=%u hover=%u edges=%u,%u reports=%u virtual_xy=%d,%d\ndown0_7=%u,%u,%u,%u,%u,%u,%u,%u\nup0_7=%u,%u,%u,%u,%u,%u,%u,%u\nstart=%u cleanup=%u idle=%u\nfocus_latch=%d latch_events=%u pad_handoff=%u focus_suppressed=%u\nfast_input=%d fast_down=%u fast_up=%u diag_runtime=%d spatial_only=%d hover_latched=%d hover_events=%u hover_handoffs=%u blocked_clicks=%u hover_on=%d sticky_suppressed=%u releases=%u\n",
 hit_getter,hit_down,hit_up,hit_hold,engine_touch_x,engine_touch_y,engine_touch_held,engine_touch_valid,engine_mouse_down_frames,engine_mouse_up_frames,
 position_overrides,down_overrides,up_overrides,hold_overrides,touch_press_edges,touch_release_edges,front_reports,last_virtual_x,last_virtual_y,
 down_by_button[0],down_by_button[1],down_by_button[2],down_by_button[3],down_by_button[4],down_by_button[5],down_by_button[6],down_by_button[7],
 up_by_button[0],up_by_button[1],up_by_button[2],up_by_button[3],up_by_button[4],up_by_button[5],up_by_button[6],up_by_button[7],
 gesture_start_count,gesture_cleanup_count,idle_passthrough_frames,
 dd_touch_focus_latched,dd_focus_latched_events,dd_focus_unlatched_pad,
 dd_focus_suppressed_latched,dd_fast_input,dd_fast_touches,
 dd_fast_up_events,diag_runtime_enabled,dd_spatial_only,dd_hover_latched,
 dd_hover_latches,dd_hover_pad_handoffs,dd_blocked_global_clicks,
 dd_hover_persist,dd_hover_focus_suppressed,dd_hover_touch_releases);
 trace_once("ux0:data/DDTouchUI/engine_hook_status.txt",output);
}
static int dd_write_log(const char *filename,const char *text){
 int fd=sceIoOpen(filename,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
 if(fd<0)return fd;
 int size=(int)strlen(text);
 int written=sceIoWrite(fd,text,size);
 sceIoClose(fd);
 if(written!=size)return written<0?written:-1;
 return 0;
}
/* Keep diagnostics available even if ux0:data is temporarily inaccessible.
   Never write to taiHEN config, saves, or game install directories. */
static void trace_once(const char *filename,const char *text){
 int r=dd_write_log(filename,text);
 if(r<0 && !strncmp(filename,"ux0:data/DDTouchUI/",strlen("ux0:data/DDTouchUI/"))){
  /* The directory may not have been present when the game was mounted. */
  sceIoMkdir("ux0:data/DDTouchUI",0777);
  r=dd_write_log(filename,text);
 }
 if(r>=0){
  dd_diag_path=1;dd_diag_last_error=0;return;
 }
 dd_diag_last_error=r;
 const char *prefix="ux0:data/DDTouchUI/";
 const size_t plen=strlen(prefix);
 if(strncmp(filename,prefix,plen))return;
 /* Only logs from the dedicated DDTouchUI folder can use this fallback. */
 char fallback[160];
 sceIoMkdir("ur0:data/DDTouchUI",0777);
 sceClibSnprintf(fallback,sizeof(fallback),"ur0:data/DDTouchUI/%s",filename+plen);
 r=dd_write_log(fallback,text);
 if(r>=0){dd_diag_path=2;dd_diag_last_error=0;}
 else {dd_diag_path=0;dd_diag_last_error=r;}
}

static const char *cfg="ux0:data/DDTouchUI/mode.ini";
/* Config parser v0.60: exact key/value matching, accepts CRLF and UTF-8
   BOM. Missing/invalid configuration retains a safe working default and
   creates a diagnostic file rather than silently disabling all touch. */
static int config_read_state=-1,config_fields=0,config_unknown=0;
static void config_set_flag(const char *k,int v){
 if(!strcmp(k,"mode"))mode=v;
 else if(!strcmp(k,"doubletap"))dd_enable_doubletap=v;
 else if(!strcmp(k,"hero_touch"))dd_enable_hero=v;
 else if(!strcmp(k,"map_touch"))dd_enable_map=v;
 else if(!strcmp(k,"outside_close"))dd_enable_modal_close=v;
 else if(!strcmp(k,"detailed_logs"))diag_detailed_enabled=v;
 else if(!strcmp(k,"focus_hold"))dd_focus_hold_enabled=v;
 else if(!strcmp(k,"fast_input"))dd_fast_input=v;
 else if(!strcmp(k,"runtime_logs"))diag_runtime_enabled=v;
 else if(!strcmp(k,"spatial_only"))dd_spatial_only=v;
 else if(!strcmp(k,"hover_persist"))dd_hover_persist=v;
 else if(!strcmp(k,"widget_probe"))dd_widget_probe=v;
 else if(!strcmp(k,"repeat_confirm"))dd_repeat_confirm=v;
 else if(!strcmp(k,"ui_sounds"))ui_sounds_enabled=v;
 else if(!strcmp(k,"direct_click"))dd_direct_click=v;
 else if(!strcmp(k,"direct_all_widgets"))dd_direct_all_widgets=v;
 else if(!strcmp(k,"menu_click"))dd_menu_click=v;
 else {config_unknown++;return;}
 config_fields++;
}
static char *config_trim(char *p){
 while(*p==' ' || *p=='\t' || *p=='\r')p++;
 char *end=p+strlen(p);
 while(end>p && (end[-1]==' ' || end[-1]=='\t' ||
                  end[-1]=='\r'))*--end=0;
 return p;
}
static void load_config(void){
 int f=sceIoOpen(cfg,SCE_O_RDONLY,0);
 if(f<0){config_read_state=f;return;}
 char b[768]={0};
 int n=sceIoRead(f,b,sizeof(b)-1);
 sceIoClose(f);
 config_read_state=n;
 if(n<=0)return;
 /* Strip BOM on Windows-authored settings. */
 char *p=b;
 if(n>=3 && (unsigned char)p[0]==0xef &&
    (unsigned char)p[1]==0xbb && (unsigned char)p[2]==0xbf)p+=3;
 while(*p){
  char *line=p;
  while(*p && *p!='\n')p++;
  if(*p)*p++=0;
  char *item=config_trim(line);
  if(!*item || *item=='#' || *item==';')continue;
  char *eq=strchr(item,'=');
  if(!eq){config_unknown++;continue;}
  *eq++=0;
  char *key=config_trim(item),*value=config_trim(eq);
  if((value[0]=='0' || value[0]=='1') && !value[1])
   config_set_flag(key,value[0]=='1');
  else config_unknown++;
 }
 if(dd_menu_click)dd_widget_probe=1;
}
static void save_config(void){
 sceIoMkdir("ux0:data/DDTouchUI",0777);
 char b[384];
 sceClibSnprintf(b,sizeof(b),
   "mode=%d\n"
   "doubletap=%d\n"
   "hero_touch=%d\n"
   "map_touch=%d\n"
   "outside_close=%d\n"
   "focus_hold=%d\n"
   "fast_input=%d\n"
   "runtime_logs=%d\n"
   "spatial_only=%d\n"
   "hover_persist=%d\n"
   "menu_click=%d\n"
   "repeat_confirm=%d\n"
   "widget_probe=%d\n"
   "ui_sounds=%d\n"
   "direct_click=%d\n"
   "direct_all_widgets=%d\n",
   mode,dd_enable_doubletap,dd_enable_hero,dd_enable_map,dd_enable_modal_close,
   dd_focus_hold_enabled,dd_fast_input,diag_runtime_enabled,dd_spatial_only,
   dd_hover_persist,dd_menu_click,dd_repeat_confirm,dd_widget_probe,
   ui_sounds_enabled,dd_direct_click,dd_direct_all_widgets);
 const char *temp="ux0:data/DDTouchUI/mode.next";
 int fd=sceIoOpen(temp,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
 if(fd<0)return;
 int n=(int)strlen(b);
 int written=sceIoWrite(fd,b,n);
 sceIoClose(fd);
 if(written!=n){sceIoRemove(temp);return;}
 if(sceIoRename(temp,cfg)<0){
  const char *fallback="ux0:data/DDTouchUI/mode.prev";
  sceIoRemove(fallback);
  if(sceIoRename(cfg,fallback)>=0){
   if(sceIoRename(temp,cfg)<0)
    sceIoRename(fallback,cfg);
   else
    sceIoRemove(fallback);
  }else{
   sceIoRename(temp,cfg);
  }
 }
}
/* Game-owned FMOD UI effects extracted from ui_shared.bank to local raw PCM.
   Worker thread avoids blocking display hook; failure is silent and safe. */
#define UI_SFX_SAMPLES 90000
static int16_t ui_pcm[3][UI_SFX_SAMPLES];
static int ui_pcm_count[3]={0,0,0};
static volatile int ui_sfx_event=0,ui_audio_running=0;
static SceUID ui_audio_thread=-1;
static void ui_sfx_request(int id){
 if(ui_sounds_enabled && id>=1&&id<=3)ui_sfx_event=id;
}
static int ui_audio_worker(SceSize argc,void *args){
 (void)argc;(void)args;
 const char *paths[3]={
  "ux0:data/DDTouchUI/Sounds/open.pcm",
  "ux0:data/DDTouchUI/Sounds/select.pcm",
  "ux0:data/DDTouchUI/Sounds/confirm.pcm"
 };
 /* Trim near-silent tails and smooth the attack and release. */
 const int max_samples[3]={28800,14400,19200};
 for(int j=0;j<3;j++){
  int fd=sceIoOpen(paths[j],SCE_O_RDONLY,0);
  if(fd<0)continue;
  int len=0;
  unsigned char *dst=(unsigned char*)ui_pcm[j];
  while(len<(int)sizeof(ui_pcm[j])){
   int n=sceIoRead(fd,dst+len,sizeof(ui_pcm[j])-len);
   if(n<=0)break;
   len+=n;
  }
  sceIoClose(fd);
  int count=len/2;
  if(count>max_samples[j])count=max_samples[j];
  ui_pcm_count[j]=count;
  for(int i=0;i<count;i++){
   int32_t value=(int32_t)ui_pcm[j][i]*27/100;
   if(i<480)value=value*i/480;
   if(count-i<1920)value=value*(count-i)/1920;
   ui_pcm[j][i]=(int16_t)value;
  }
 }
 /* Reuse the same MAIN audio port during the menu session.
    BGM remains entirely game-owned. No hot-path file operations. */
 int port=-1,last_port=-1;
 int current=-1,pos=0,previous=-1,previous_pos=0,cross_left=0,idle=0;
 int16_t block[512];
 while(ui_audio_running){
  int event=ui_sfx_event;
  if(event){
   ui_sfx_event=0;
   int next=event-1;
   if(next>=0 && next<3 && ui_pcm_count[next]>0){
    if(current>=0 && pos<ui_pcm_count[current]){
     previous=current;previous_pos=pos;cross_left=256;
    }else{previous=-1;cross_left=0;}
    current=next;pos=0;idle=0;
   }
  }
  if(port<0){
   if(current<0){sceKernelDelayThread(9000);continue;}
   port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,512,48000,SCE_AUDIO_OUT_MODE_MONO);
   if(port<0){
    current=-1;previous=-1;cross_left=0;
    sceKernelDelayThread(15000);
    continue;
   }
   last_port=port;
  }
  if(current<0 && previous<0){
   for(int k=0;k<512;k++)block[k]=0;
  }else{
   for(int k=0;k<512;k++){
    int v=0;
    if(current>=0 && pos<ui_pcm_count[current])v=ui_pcm[current][pos++];
    if(previous>=0 && cross_left>0){
     int old=previous_pos<ui_pcm_count[previous]?ui_pcm[previous][previous_pos++]:0;
     v=(v*(256-cross_left)+old*cross_left)>>8;
     cross_left--;
     if(cross_left==0)previous=-1;
    }
    block[k]=(int16_t)v;
   }
   if(current>=0 && pos>=ui_pcm_count[current])current=-1;
  }
  sceAudioOutOutput(port,block);
  if(current<0 && previous<0 && !menu_open && !ui_sfx_event)idle++;
  else idle=0;
  if(idle>=56){
   sceAudioOutReleasePort(port);
   port=-1;idle=0;
  }
 }
 if(port>=0){
  for(int k=0;k<512;k++)block[k]=0;
  sceAudioOutOutput(port,block);
  sceAudioOutReleasePort(port);
 }
 char msg[160];
 sceClibSnprintf(msg,sizeof(msg),
   "v059 MAIN reuse port=%d samples=%d,%d,%d active=%d\\n",
   last_port,ui_pcm_count[0],ui_pcm_count[1],ui_pcm_count[2],ui_audio_running);
 trace_once("ux0:data/DDTouchUI/sound_status_v059.txt",msg);
 return 0;
}
static void ui_audio_start(void){
 ui_audio_running=1;
 ui_audio_thread=sceKernelCreateThread("ddtouch_sfx",ui_audio_worker,0x10000140,0x6000,0,0,NULL);
 if(ui_audio_thread>=0)sceKernelStartThread(ui_audio_thread,0,NULL);
 else ui_audio_running=0;
}
static void ui_audio_stop(void){
 ui_audio_running=0;
 if(ui_audio_thread>=0){
  sceKernelWaitThreadEnd(ui_audio_thread,NULL,NULL);
  sceKernelDeleteThread(ui_audio_thread);
  ui_audio_thread=-1;
 }
}
static int ui_point_inside(int x,int y){
 return x>=UI_X && x<UI_X+UI_W && y>=UI_Y && y<UI_Y+UI_H;
}
static int ui_row_for_point(int x,int y){
 if(x<350 || x>=615)return 0;
 if(y>=179 && y<223)return 1;
 if(y>=232 && y<279)return 2;
 if(y>=286 && y<336)return 3;
 if(y>=341 && y<395)return 4;
 return 0;
}
static void ui_cancel_gesture(void){
 dd_menu_authorized=0;
 dd_detach_mouse_pointer();
 engine_touch_valid=0;engine_touch_held=0;engine_target_confirmed=0;
 engine_mouse_down_frames=0;engine_mouse_up_frames=0;
 dd_action_frames=0;dd_action_type=0;
 dd_have_first_tap=0;dd_pending_context_action=0;
}
static void ui_activate_row(int row,int is_touch,int down_x,int up_x){
 if(row==3 || row<1 || row>4)return; /* Input mode is a status, not a button. */
 if(is_touch && row==1){
  /* Touch mode is changed only by a deliberate tap on its red/grey
     switch. A stray press on the label can never turn touch off. */
  if(down_x<545 || down_x>=616 ||
     up_x<545 || up_x>=616){
   ui_mode_hold_rejected++;
   return;
  }
 }
 ui_sfx_request(3);
 if(row==1){mode=!mode;popup=180;}
 else if(row==2)diag_runtime_enabled=!diag_runtime_enabled;
 else if(row==4)ui_pointer_resets++;

 ui_cancel_gesture();
 if(row==1 || row==2){
  ui_toggles++;
  save_config();
 }
 ui_nav_row=row;
 ui_status_dirty=1;
 if(is_touch)ui_touch_actions++;else ui_nav_actions++;
}
static void ui_on_tap(int down_x,int down_y,int up_x,int up_y){
 int dx=up_x-down_x,dy=up_y-down_y;
 if(dx*dx+dy*dy>42*42)return;
 int row=ui_row_for_point(down_x,down_y);
 if(!row || row!=ui_row_for_point(up_x,up_y))return;
 ui_nav_row=row;
 ui_status_dirty=1;
 if(row==3){ui_touch_actions++;return;} /* read-only row can be focused */
 ui_activate_row(row,1,down_x,up_x);
}
static int ui_next_nav(int current,int direction){
 /* Row #3 can be highlighted by touch; up/down hand off smoothly. */
 int next=current+direction;
 if(next<1)next=4;
 if(next>4)next=1;
 return next;
}
static void ui_gamepad_actions(uint32_t buttons){
 const uint32_t mask=SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_CROSS|SCE_CTRL_CIRCLE;
 unsigned int now=buttons&mask;
 unsigned int pressed=now&~ui_prev_buttons;
 ui_prev_buttons=now;
 if(!menu_open)return;
 if(pressed&SCE_CTRL_UP){ui_nav_row=ui_next_nav(ui_nav_row,-1);ui_sfx_request(2);}
 else if(pressed&SCE_CTRL_DOWN){ui_nav_row=ui_next_nav(ui_nav_row,1);ui_sfx_request(2);}
 if(pressed&SCE_CTRL_CIRCLE){
  menu_open=0;ui_closes++;ui_last_close_frame=dd_frame_no;
  ui_sfx_request(1);ui_cancel_gesture();
 }else if(pressed&SCE_CTRL_CROSS){
  ui_activate_row(ui_nav_row,0,0,0);
 }
}
static void filter(SceUInt32 port,SceTouchData*data,int n) {
 if(!data)return;
 if(n>8)n=8;
 for(int i=0;i<n;i++){
  if(port==SCE_TOUCH_PORT_FRONT){
   int active=data[i].reportNum>0;
   front_reports++;
   if(active && !seen_front++)
    trace_once("ux0:data/DDTouchUI/front_touch_seen.txt","Front touch passed through hooked sceTouch API\n");
   front_pressed=active;
   if(active){
    fx=data[i].report[0].x;fy=data[i].report[0].y;
    if(seen_front==1){
     int f=sceIoOpen("ux0:data/DDTouchUI/front_xy.bin",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
     if(f>=0){int xy[2]={fx,fy};sceIoWrite(f,xy,sizeof(xy));sceIoClose(f);}
     seen_front++;
    }
    /* Front-panel dimensions are ~1920x1088, overlay uses 960x544 */
   }
   /* Capture pointer gestures that START inside the centered panel.
      Do not steal touches outside its rectangle, even while UI is open. */
   int px=fx/2,py=fy/2;
   /* Some eboot touch calls only deliver active reports; detect a new
      gesture by a gap in samples as well as the previous pressed flag. */
   int new_front_gesture=active &&
      (!front_was_pressed ||
       (ui_last_front_sample_frame &&
        dd_frame_no-ui_last_front_sample_frame>6u));
   if(active){
    if(new_front_gesture && front_was_pressed)ui_gesture_recovered++;
    ui_last_front_sample_frame=dd_frame_no;
   }
   if(new_front_gesture && menu_open && ui_point_inside(px,py)){
    ui_panel_capture=1;ui_touch_start_x=px;ui_touch_start_y=py;
    ui_touch_start_frame=dd_frame_no;
    ui_last_active_frame=dd_frame_no;ui_touch_last_x=px;ui_touch_last_y=py;
    /* Panel buttons react on touch-down. Game may omit touch-up samples. */
    ui_on_tap(px,py,px,py);
    ui_down_action_done=1;
   }
   if(ui_panel_capture && active){
    ui_last_active_frame=dd_frame_no;ui_touch_last_x=px;ui_touch_last_y=py;
   }
   if(ui_panel_capture){
    if(!active && front_was_pressed){
     if(!ui_down_action_done)
      ui_on_tap(ui_touch_start_x,ui_touch_start_y,px,py);
     ui_panel_capture=0;
     ui_down_action_done=0;
    }
    ui_consumed++;
    front_was_pressed=active;
    data[i].reportNum=0;
    continue; /* UI gestures do not reach the engine. */
   }
   if(menu_open && active && !front_was_pressed)ui_passthrough++;
   if(mode && engine_compatible){
    if(active){
     dd_last_touch_frame=dd_frame_no;
     engine_touch_valid=1;
     engine_touch_x=fx/2;engine_touch_y=fy/2;
     if(new_front_gesture){
      /* Some builds return no zero-report on a released gesture. The
         current press must be counted again after a polling gap. */
      if(front_was_pressed){
       engine_touch_held=0;
       engine_mouse_up_frames=0;
       engine_mouse_down_frames=0;
      }
      dd_touch_focus_latched=0;
      dd_hover_latched=0;
      dd_menu_authorized=0;dd_menu_click_ref=0;dd_menu_click_id=0;
      dd_direct_gesture++;
      int rdx=engine_touch_x-dd_repeat_first_x;
      int rdy=engine_touch_y-dd_repeat_first_y;
      dd_repeat_candidate=dd_repeat_confirm && dd_menu_click &&
        dd_repeat_first_id!=0 &&
        dd_frame_no-dd_repeat_first_frame<=DD_REPEAT_FRAMES &&
        rdx*rdx+rdy*rdy<=DD_REPEAT_RADIUS*DD_REPEAT_RADIUS;
      dd_repeat_observed=0;
      dd_touch_down_frame=dd_frame_no;
      dd_touch_start_x=engine_touch_x;dd_touch_start_y=engine_touch_y;
      captured_widget_idlo=0;captured_widget_idhi=0;
      engine_target_confirmed=0; /* do NOT reuse last selected widget */
      engine_mouse_down_frames=4;
      engine_mouse_up_frames=0;
      gesture_start_count++;
      touch_press_edges++;
      if(dd_fast_input && dd_spatial_only){
       dd_probe_sample(1,engine_touch_x,engine_touch_y);
       dd_blocked_global_clicks++;
      }
     }
     engine_touch_held=1;
    }else{
     if(front_was_pressed){
      engine_mouse_up_frames=dd_fast_input?4:2;
      engine_mouse_down_frames=0;touch_release_edges++;
      if(dd_fast_input && dd_spatial_only)
       dd_probe_sample(0,engine_touch_x,engine_touch_y);
      if(dd_fast_input){
       dd_fast_up_events++;
       dd_fast_snapshot_pending=1;
      }
      int delta_x=engine_touch_x-dd_touch_start_x,delta_y=engine_touch_y-dd_touch_start_y;
      if(delta_x*delta_x+delta_y*delta_y<75*75)
       dd_touch_finished_tap(engine_touch_x,engine_touch_y,captured_widget_idlo);
      else {dd_taps_ignored++;dd_have_first_tap=0;dd_pending_context_action=0;}
     }
     engine_touch_held=0;
    if(dd_fast_input && dd_spatial_only && dd_hover_persist &&
       dd_pointer_moved && !menu_open){
     /* Latch on the release edge, not four frames later, avoiding a
        momentary switch back to controller-selected row. */
     if(!dd_hover_latched)dd_hover_latches++;
     dd_hover_latched=1;
     dd_hover_touch_releases++;
    }
    }
    engine_mouse_apply();
   }
   front_was_pressed=active;
   if(mode && engine_compatible)data[i].reportNum=0; /* Native engine pointer owns touch. */
  }else if(port==SCE_TOUCH_PORT_BACK){
   if(data[i].reportNum && !seen_back++)
    trace_once("ux0:data/DDTouchUI/back_touch_seen.txt","Back touch passed through hooked sceTouch API\n");
   rear_pressed=data[i].reportNum>0;
  }
 }
}
static int hook_read(SceUInt32 p,SceTouchData*d,SceUInt32 n){
 int ret=TAI_CONTINUE(int,refs[0],p,d,n);if(ret>0)filter(p,d,ret);
 return ret;
}
static int hook_read2(SceUInt32 p,SceTouchData*d,SceUInt32 n){
 int ret=TAI_CONTINUE(int,refs[1],p,d,n);if(ret>0)filter(p,d,ret);
 return ret;
}
static int hook_peek(SceUInt32 p,SceTouchData*d,SceUInt32 n){
 if(!seen_peek_enter++)
  trace_once("ux0:data/DDTouchUI/peek_enter.txt","sceTouchPeek hook called\n");
 int ret=TAI_CONTINUE(int,refs[2],p,d,n);
 if(!seen_peek_exit++)
  trace_once("ux0:data/DDTouchUI/peek_exit.txt","sceTouchPeek continuation returned\n");
 if(ret>0)filter(p,d,ret);
 return ret;
}
static int hook_peek2(SceUInt32 p,SceTouchData*d,SceUInt32 n){
 int ret=TAI_CONTINUE(int,refs[3],p,d,n);if(ret>0)filter(p,d,ret);
 return ret;
}
static unsigned int ui_glyph_opacity=255;
static void pixel(const SceDisplayFrameBuf *b,int x,int y,uint32_t color){
 if(!b||!b->base||b->pixelformat!=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8)return;
 if(x<0||y<0||(unsigned)x>=b->width||(unsigned)y>=b->height||b->pitch<b->width)return;
 uint32_t *dst=(uint32_t*)b->base+(size_t)y*b->pitch+x;
 if(ui_glyph_opacity>=255){*dst=color;return;}
 unsigned a=ui_glyph_opacity,inv=255-a,old=*dst;
 unsigned red=((color&255)*a+(old&255)*inv+127)/255;
 unsigned green=(((color>>8)&255)*a+((old>>8)&255)*inv+127)/255;
 unsigned blue=(((color>>16)&255)*a+((old>>16)&255)*inv+127)/255;
 *dst=0xFF000000u|(blue<<16)|(green<<8)|red;
}
/* Minimal original 5x7 bitmap glyphs, no external fonts. */
static const uint8_t glyph[36][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,6,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}
};
static void draw(const SceDisplayFrameBuf *b,int x,int y,const char*s,uint32_t col){
 for(;*s;s++,x+=12){
  char ch=*s;
  if(ch>='a'&&ch<='z')ch-=32;
  int ix=ch>='A'&&ch<='Z'?ch-'A':ch>='0'&&ch<='9'?26+ch-'0':-1;
  if(ch=='-'){
   for(int d=1;d<9;d++)pixel(b,x+d,y+7,col);
   continue;
  }
  if(ix<0)continue;
  for(int a=0;a<7;a++)for(int d=0;d<5;d++)if(glyph[ix][a]&(1<<(4-d)))
   for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++)pixel(b,x+d*2+dx,y+a*2+dy,col);
 }
}
static void bar(const SceDisplayFrameBuf *b,int x,int y,int w,int h,uint32_t c){
 if(!b||!b->base||b->pixelformat!=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8)return;
 if(w<=0||h<=0||b->pitch<b->width)return;
 int x1=x+w,y1=y+h;
 if(x<0)x=0;if(y<0)y=0;
 if(x1>(int)b->width)x1=(int)b->width;
 if(y1>(int)b->height)y1=(int)b->height;
 if(x1<=x||y1<=y)return;
 uint32_t *ptr=(uint32_t*)b->base;
 for(int j=y;j<y1;j++){
  uint32_t *row=ptr+(size_t)j*b->pitch+x;
  for(int i=0;i<x1-x;i++)row[i]=c;
 }
}
/* Panel is anchored to the physical 960x544 screen center.
   Draw only if display buffer is 960x544 or bigger. No texture/PSARC needed. */
static void ui_outline(const SceDisplayFrameBuf *b,int x,int y,int w,int h,uint32_t c){
 bar(b,x,y,w,2,c);bar(b,x,y+h-2,w,2,c);
 bar(b,x,y,2,h,c);bar(b,x+w-2,y,2,h,c);
}
static void ui_center_text(const SceDisplayFrameBuf*b,int center,int y,const char*s,uint32_t col){
 int n=(int)strlen(s);
 draw(b,center-(n*12)/2,y,s,col);
}
/* UI MOD sprite atlas: every resource is original PNG converted to RGBA.
   Load once from ux0:data/DDTouchUI, never read files during a frame after first load.
   Large image transparency is preserved; fade is applied per pixel. */
#define BG_X 267
#define BG_Y 0
#define BG_W 430
#define BG_H 513
#define STATUS_W 291
#define STATUS_H 125
#define SELECT_X 357
#define SELECT_Y 180
#define SELECT_W 249
#define SELECT_H 42
#define TOGGLE_W 67
#define TOGGLE_H 53
static uint32_t ui_bg[BG_W*BG_H];
static uint32_t ui_selector[SELECT_W*SELECT_H];
static uint32_t ui_toggle_on[TOGGLE_W*TOGGLE_H];
static uint32_t ui_toggle_off[TOGGLE_W*TOGGLE_H];
/* Cache BOTH status banners at boot: switching mode must never read from SD. */
static uint32_t ui_banners[2][STATUS_W*STATUS_H];
static int ui_loaded=0,ui_banners_loaded=0;
static volatile int ui_banner_ready[2]={0,0};
static volatile int ui_asset_retry_running=0;
static SceUID ui_asset_retry_thread=-1;
static int ui_read_sprite(const char *file,unsigned char *dst,int bytes){
 int fd=sceIoOpen(file,SCE_O_RDONLY,0);
 if(fd<0)return 0;
 int n=0;
 while(n<bytes){
  int got=sceIoRead(fd,dst+n,bytes-n);
  if(got<=0)break;
  n+=got;
 }
 sceIoClose(fd);
 return n==bytes;
}
static volatile int ui_bg_ok=0,ui_selector_ok=0,ui_on_ok=0,ui_off_ok=0;
static void ui_load_sprites(void){
 if(ui_loaded)return;
 ui_loaded=1;
 ui_bg_ok=ui_read_sprite("ux0:data/DDTouchUI/UI.rgba",(unsigned char*)ui_bg,sizeof(ui_bg));
 ui_selector_ok=ui_read_sprite("ux0:data/DDTouchUI/Selector.rgba",(unsigned char*)ui_selector,sizeof(ui_selector));
 ui_on_ok=ui_read_sprite("ux0:data/DDTouchUI/On.rgba",(unsigned char*)ui_toggle_on,sizeof(ui_toggle_on));
 ui_off_ok=ui_read_sprite("ux0:data/DDTouchUI/Off.rgba",(unsigned char*)ui_toggle_off,sizeof(ui_toggle_off));
}
static void ui_load_banners(void){
 if(ui_banners_loaded)return;
 ui_banners_loaded=1;
 ui_banner_ready[0]=ui_read_sprite("ux0:data/DDTouchUI/Mod_off.rgba",
   (unsigned char*)ui_banners[0],sizeof(ui_banners[0]));
 ui_banner_ready[1]=ui_read_sprite("ux0:data/DDTouchUI/Mod_on.rgba",
   (unsigned char*)ui_banners[1],sizeof(ui_banners[1]));
}

/* v0.60: the first ux0: read may fail during taiHEN reload, leaving a
   text-only "ghost" menu. Retry missing assets from a low-priority worker,
   NEVER in the display hook. */
static int ui_assets_complete(void){
 return ui_bg_ok && ui_selector_ok && ui_on_ok && ui_off_ok &&
        ui_banner_ready[0] && ui_banner_ready[1];
}
static const char *ui_first_missing_asset(void){
 if(!ui_bg_ok)return "UI RGBA";
 if(!ui_selector_ok)return "SELECTOR RGBA";
 if(!ui_on_ok)return "ON RGBA";
 if(!ui_off_ok)return "OFF RGBA";
 if(!ui_banner_ready[0])return "MOD OFF RGBA";
 if(!ui_banner_ready[1])return "MOD ON RGBA";
 return "ALL READY";
}
static void ui_assets_report(const char *phase){
 const char *names[6]={"UI.rgba","Selector.rgba","On.rgba","Off.rgba",
                       "Mod_off.rgba","Mod_on.rgba"};
 const int expected[6]={BG_W*BG_H*4,SELECT_W*SELECT_H*4,
  TOGGLE_W*TOGGLE_H*4,TOGGLE_W*TOGGLE_H*4,
  STATUS_W*STATUS_H*4,STATUS_W*STATUS_H*4};
 const int ok[6]={ui_bg_ok,ui_selector_ok,ui_on_ok,ui_off_ok,
                  ui_banner_ready[0],ui_banner_ready[1]};
 char output[790];
 int len=sceClibSnprintf(output,sizeof(output),
    "v0.60 phase=%s complete=%d config_bytes=%d config_fields=%d "
    "config_invalid=%d mode=%d engine=%d\n",
    phase,ui_assets_complete(),config_read_state,config_fields,
    config_unknown,mode,engine_compatible);
 for(int i=0;i<6 && len<(int)sizeof(output)-100;i++){
  char path[96];
  sceClibSnprintf(path,sizeof(path),"ux0:data/DDTouchUI/%s",names[i]);
  SceIoStat st;
  memset(&st,0,sizeof(st));
  int result=sceIoGetstat(path,&st);
  int actual=result<0?-1:(int)st.st_size;
  len+=sceClibSnprintf(output+len,sizeof(output)-len,
     "%s ready=%d actual=%d expected=%d io=%08x\n",
     names[i],ok[i],actual,expected[i],(unsigned int)result);
 }
 trace_once("ux0:data/DDTouchUI/assets_status_v060.txt",output);
}
static void ui_assets_retry_missing(void){
 if(!ui_bg_ok)ui_bg_ok=ui_read_sprite("ux0:data/DDTouchUI/UI.rgba",
    (unsigned char*)ui_bg,sizeof(ui_bg));
 if(!ui_selector_ok)ui_selector_ok=ui_read_sprite(
    "ux0:data/DDTouchUI/Selector.rgba",(unsigned char*)ui_selector,sizeof(ui_selector));
 if(!ui_on_ok)ui_on_ok=ui_read_sprite(
    "ux0:data/DDTouchUI/On.rgba",(unsigned char*)ui_toggle_on,sizeof(ui_toggle_on));
 if(!ui_off_ok)ui_off_ok=ui_read_sprite(
    "ux0:data/DDTouchUI/Off.rgba",(unsigned char*)ui_toggle_off,sizeof(ui_toggle_off));
 if(!ui_banner_ready[0])ui_banner_ready[0]=ui_read_sprite(
    "ux0:data/DDTouchUI/Mod_off.rgba",
    (unsigned char*)ui_banners[0],sizeof(ui_banners[0]));
 if(!ui_banner_ready[1])ui_banner_ready[1]=ui_read_sprite(
    "ux0:data/DDTouchUI/Mod_on.rgba",
    (unsigned char*)ui_banners[1],sizeof(ui_banners[1]));
}
static int ui_assets_retry_worker(SceSize argc,void *args){
 (void)argc;(void)args;
 for(int i=0;i<20 && ui_asset_retry_running;i++){
  sceKernelDelayThread(200000);
  if(i%5==4){
   ui_assets_retry_missing();
   if(ui_assets_complete())break;
  }
 }
 ui_assets_report(ui_assets_complete()?"recovered":"missing_or_invalid");
 return 0;
}
static void ui_assets_start_retry(void){
 ui_assets_report("initial");
 if(ui_assets_complete())return;
 ui_asset_retry_running=1;
 ui_asset_retry_thread=sceKernelCreateThread("ddtouch_assets",ui_assets_retry_worker,
                                        0x10000180,0x6000,0,0,NULL);
 if(ui_asset_retry_thread>=0)sceKernelStartThread(ui_asset_retry_thread,0,NULL);
 else {ui_asset_retry_running=0;ui_assets_report("retry_thread_failed");}
}
static void ui_assets_stop_retry(void){
 ui_asset_retry_running=0;
 if(ui_asset_retry_thread>=0){
  sceKernelWaitThreadEnd(ui_asset_retry_thread,NULL,NULL);
  sceKernelDeleteThread(ui_asset_retry_thread);
  ui_asset_retry_thread=-1;
 }
}
/* Fast RGBA8888 software blend: one 32-bit load per pixel, no division.
   The previous version performed 3 scalar divisions per alpha pixel,
   >300K expensive integer divisions/frame on the parchment alone.
   Pixel channel layout on A8B8G8R8 is 0xAABBGGRR. */
static void ui_blit(const SceDisplayFrameBuf *b,const uint32_t *image,int x0,int y0,int w,int h,int fade){
 if(fade<=0 || !b || !b->base || b->pixelformat!=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8)return;
 uint32_t *base=(uint32_t*)b->base;
 for(int y=0;y<h;y++){
  int yy=y0+y;
  if((unsigned)yy>=b->height)continue;
  uint32_t *dst=base+(size_t)yy*b->pitch;
  const uint32_t *src=image+(size_t)y*w;
  for(int x=0;x<w;x++){
   int xx=x+x0;
   if((unsigned)xx>=b->width)continue;
   uint32_t fg=src[x];
   unsigned sa=fg>>24;
   if(!sa)continue;
   if(fade>=255 && sa==255){dst[xx]=fg;continue;}
   /* /256 scaling is fast and within 1 LSB of ideal /255 alpha. */
   unsigned a=(sa*(unsigned)(fade+1))>>8;
   if(!a)continue;
   if(a==255){dst[xx]=fg;continue;}
   uint32_t bg=dst[xx];
   unsigned inv=256u-a;
   uint32_t rb=((fg&0x00ff00ffu)*a+(bg&0x00ff00ffu)*inv)>>8;
   uint32_t g=((fg&0x0000ff00u)*a+(bg&0x0000ff00u)*inv)>>8;
   dst[xx]=0xff000000u | (rb&0x00ff00ffu) | (g&0x0000ff00u);
  }
 }
}
static void ui_draw_overlay(const SceDisplayFrameBuf*b){
 if(!b || !b->base || b->width<960 || b->height<544 ||
    b->pixelformat!=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8)return;
 if(menu_open){
  ui_fade+=85;if(ui_fade>255)ui_fade=255;
 }else{
  ui_fade-=128;if(ui_fade<0)ui_fade=0;
 }
 if(ui_fade>0){
  ui_load_sprites();
  if(ui_bg_ok)ui_blit(b,ui_bg,BG_X,BG_Y,BG_W,BG_H,ui_fade);
  if(ui_selector_ok && ui_nav_row>=1 && ui_nav_row<=4)
   ui_blit(b,ui_selector,SELECT_X,SELECT_Y+(ui_nav_row-1)*55,SELECT_W,SELECT_H,ui_fade);
  if(ui_on_ok && mode && engine_compatible)
   ui_blit(b,ui_toggle_on,546,177,TOGGLE_W,TOGGLE_H,ui_fade);
  else if(ui_off_ok)
   ui_blit(b,ui_toggle_off,546,177,TOGGLE_W,TOGGLE_H,ui_fade);
  if(ui_on_ok && diag_runtime_enabled)
   ui_blit(b,ui_toggle_on,546,232,TOGGLE_W,TOGGLE_H,ui_fade);
  else if(ui_off_ok)
   ui_blit(b,ui_toggle_off,546,232,TOGGLE_W,TOGGLE_H,ui_fade);
  /* Status of the two non-boolean controls, within the parchment. */
  ui_glyph_opacity=(unsigned int)ui_fade;
  draw(b,550,298,dd_fast_input?(dd_spatial_only?"SAFE":"FAST"):"LEGACY",0xFF27201D);
  draw(b,548,354,"RESET",0xFF27201D);
  ui_glyph_opacity=255;
  if(!engine_compatible)draw(b,342,420,"NO GAME HOOK",0xFF2626C0);
  if(!ui_assets_complete()){
   /* Works without sprites or writable logs: ask user for a screenshot. */
   draw(b,342,409,"UI FILES MISSING",0xFF2626C0);
   draw(b,342,431,ui_first_missing_asset(),0xFF2626C0);
   draw(b,342,452,dd_diag_path==1?"LOGS UX0 DATA":
                   dd_diag_path==2?"LOGS UR0 DATA":
                                   "NO LOG ACCESS",0xFF2626C0);
  }
 }
 if(popup>0){
  int state=(mode && engine_compatible)?1:0;
  ui_load_banners();
  if(ui_banner_ready[state])ui_blit(b,ui_banners[state],0,0,STATUS_W,STATUS_H,
    popup<24?popup*255/24:255);
 }
}
static void ui_state_report(void){
 char b[490];
 sceClibSnprintf(b,sizeof(b),
  "v047 ui_open=%d mode=%d compatible=%d captured=%d opened=%u closed=%u toggles=%u touch_samples=%u outside=%u refused=%u nav_row=%d touch_actions=%u pad_actions=%u pad_filtered=%u resets=%u recover=%u fast=%d runtime_logs=%d\\n",
  menu_open,mode,engine_compatible,ui_panel_capture,ui_opens,ui_closes,
  ui_toggles,ui_consumed,ui_passthrough,ui_mode_hold_rejected,
  ui_nav_row,ui_touch_actions,ui_nav_actions,ui_controls_consumed,
  ui_pointer_resets,ui_gesture_recovered,dd_fast_input,diag_runtime_enabled);
 trace_once("ux0:data/DDTouchUI/overlay_state_v047.txt",b);
}
/* Read-only GUI router trace to research the action callback path.
   No hard-coded GUI bounds, no controller button emulation. */
static volatile unsigned int router_sampling_frames=0;
static volatile unsigned int router_prev_press=0,router_prev_release=0;
static volatile unsigned int router_samples=0;
static void trace_gui_router(void){
 /* Legacy v0.37 emitted a multi-MB append log via open/write/close inside
    game render hook during user touches, causing severe I/O stalls on Vita.
    This routine now only counts transitions in RAM, only when opt-in debug. */
 if(!diag_detailed_enabled || !mode || !engine_compatible)return;
 if(router_prev_press!=touch_press_edges ||
    router_prev_release!=touch_release_edges){
  router_prev_press=touch_press_edges;
  router_prev_release=touch_release_edges;
  router_samples++;
 }
}
static void diag_idle_snapshot(void){
 if(engine_touch_held || engine_touch_valid || front_was_pressed ||
    ui_panel_capture || dd_action_frames || menu_open){
  diag_skipped_active++;
  return;
 }
 if(dd_frame_no<diag_next_frame)return;
 /* One small log rewrite per 12 idle frames, never within a gesture. */
 diag_next_frame=dd_frame_no+12u;
 switch(diag_write_step++){
  case 0: engine_hook_report();break;
  case 1: dd_actions_report();break;
  case 2: ui_state_report();break;
  case 3: widget_report();
   if(dd_direct_click){
    char line[230];
    sceClibSnprintf(line,sizeof(line),
      "v058 direct=1 any=%d gestures=%u attempts=%u allowed=%u denied=%u last_scene=%08x last_widget=%u hooks=%d,%d\\n",
      dd_direct_all_widgets,dd_direct_gesture,dd_direct_attempts,
      dd_direct_allowed,dd_direct_denied,dd_direct_last_ref,dd_direct_last_widget,
      enabled[14],enabled[15]);
    trace_once("ux0:data/DDTouchUI/native_click_v058.txt",line);
   }
   if(dd_repeat_confirm){
    char line[256];
    sceClibSnprintf(line,sizeof(line),
      "repeat=1 first=%u success=%u mismatch=%u candidate=%d first_ref=%08x id=%u hook_gui=%d hook_focus=%d\\n",
      dd_repeat_first_hits,dd_repeat_success,dd_repeat_mismatch,
      dd_repeat_candidate,dd_repeat_first_ref,dd_repeat_first_id,
      enabled[14],enabled[15]);
    trace_once("ux0:data/DDTouchUI/repeat_status_v054.txt",line);
   }
   break;
  case 4: hit_probe_report();break;
  case 5: if(diag_detailed_enabled)xy_caller_log();break;
  default:
   diag_write_step=0;
   diag_snapshots++;
   diag_next_frame=dd_frame_no+300u;
   break;
 }
}
static int hook_frame(const SceDisplayFrameBuf*b,int sync){
 dd_frame_no++;
 if(ui_panel_capture && dd_frame_no-ui_last_active_frame>12u){
  /* Already triggered on press; only release the capture here. */
  ui_panel_capture=0;ui_down_action_done=0;
  front_was_pressed=0;
 }
 if(engine_touch_held && dd_frame_no-dd_last_touch_frame>12u){
  /* Lost release event: never keep a ghost pointer/controller hover forever. */
  engine_touch_held=0;
  engine_mouse_down_frames=0;
  engine_mouse_up_frames=0;
  if(dd_fast_input && dd_spatial_only && dd_hover_persist && dd_pointer_moved){
   if(engine_button_ptr)engine_button_ptr[0]=0;
   if(!dd_hover_latched)dd_hover_latches++;
   dd_hover_latched=1;
  }else dd_detach_mouse_pointer();
  engine_target_confirmed=0;
  engine_touch_valid=0;
  dd_have_first_tap=0;
  dd_pending_context_action=0;
  front_was_pressed=0;
  dd_touch_watchdog++;
 }
 if(dd_pending_context_action && dd_have_first_tap &&
    dd_frame_no>=dd_pending_deadline){
  dd_enqueue_action((unsigned int)dd_pending_context_action,dd_pending_map_x,dd_pending_map_y);
  dd_pending_context_action=0;dd_have_first_tap=0;
 }
 ++hit_pos;
 engine_mouse_apply();
 if(diag_runtime_enabled){
  trace_gui_router();
  diag_idle_snapshot();
 }
 if(engine_mouse_down_frames>0)engine_mouse_down_frames--;
 if(engine_mouse_up_frames>0){
  engine_mouse_up_frames--;
  if(engine_mouse_up_frames==0 && !engine_touch_held){
   /* End the click edge but keep spatial hover. The old behavior
      detached the cursor immediately, so the first gamepad-focused
      widget reappeared on every touch release (main menu snaps up). */
   if(dd_fast_input && dd_spatial_only && dd_hover_persist && dd_pointer_moved){
    /* Opt-in only. By default, release cursor after each touch to avoid
       leaving stale game-hover ownership across state transitions. */
    if(engine_button_ptr)engine_button_ptr[0]=0;
    if(!dd_hover_latched)dd_hover_latches++;
    dd_hover_latched=1;
   }else if(!dd_fast_input && dd_focus_hold_enabled &&
            engine_target_confirmed && dd_pointer_moved){
    if(engine_button_ptr)engine_button_ptr[0]=0;
    dd_touch_focus_latched=1;
    dd_focus_latched_events++;
   }else{
    dd_detach_mouse_pointer();
   }
   engine_target_confirmed=0;
   engine_touch_valid=0;
   dd_menu_authorized=0;
   gesture_cleanup_count++;
  }
 }
 if(!engine_touch_held && !engine_touch_valid)idle_passthrough_frames++;
 if(ui_status_dirty && !menu_open && !engine_touch_held &&
    !front_was_pressed && dd_frame_no-ui_last_close_frame>20u){
  ui_state_report();
  ui_status_dirty=0;
 }
 /* Minimal diagnostics only after 120 frames with no touch, never while
    actively interacting or within a battle input gesture. Throttled to
    one snapshot per 600 frames; module_stop also saves summary on unload. */
 if(dd_fast_input && dd_fast_snapshot_pending &&
    !engine_touch_held && !engine_touch_valid && !front_was_pressed &&
    !menu_open && dd_frame_no-dd_last_touch_frame>120u &&
    dd_frame_no-dd_fast_last_snapshot>600u){
  engine_hook_report();
  dd_actions_report();
  if(dd_fast_input && dd_spatial_only)dd_probe_report();
  if(dd_widget_probe)dd_widget_fast_report();
  dd_fast_last_snapshot=dd_frame_no;
  dd_fast_snapshot_pending=0;
 }
 if(!seen_frame++){
  trace_once("ux0:data/DDTouchUI/frame_seen.txt","sceDisplaySetFrameBuf hook called\n");
  if(b){
   int f=sceIoOpen("ux0:data/DDTouchUI/frame_info.bin",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
   if(f>=0){sceIoWrite(f,b,sizeof(*b));sceIoClose(f);}
  }
 }
 SceCtrlData pad={0};
 if(sceCtrlPeekBufferPositive(0,&pad,1)>0){
  if(pad.buttons && !seen_pad++){
   int f=sceIoOpen("ux0:data/DDTouchUI/pad_buttons.bin",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
   if(f>=0){sceIoWrite(f,&pad.buttons,sizeof(pad.buttons));sceIoClose(f);}
  }
  if(!engine_touch_held && engine_compatible &&
     ((pad.buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT|
                      SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_TRIANGLE|SCE_CTRL_SQUARE|
                      SCE_CTRL_SELECT|SCE_CTRL_START|SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER))||
      (pad.lx<95||pad.lx>160||pad.ly<95||pad.ly>160))){
    if(dd_pointer_moved || dd_touch_focus_latched || dd_hover_latched){
     if(dd_touch_focus_latched)dd_focus_unlatched_pad++;
     if(dd_hover_latched)dd_hover_pad_handoffs++;
     dd_detach_mouse_pointer();
     dd_joystick_handoffs++;
    }
  }
  uint32_t pressed=pad.buttons&CHORD;
  if(pressed==CHORD&&(old_buttons&CHORD)!=CHORD){
   if(!seen_chord++)
    trace_once("ux0:data/DDTouchUI/chord_seen.txt","START L chord was observed\n");
   menu_open^=1;
   ui_sfx_request(1);
   if(menu_open){ui_opens++;popup=0;ui_nav_row=1;}
   else {ui_closes++;popup=90;ui_last_close_frame=dd_frame_no;}
   ui_panel_capture=0;
   ui_down_action_done=0;
   ui_prev_buttons=0;
   front_was_pressed=0;
   ui_last_front_sample_frame=0;
   ui_status_dirty=1;
   ui_cancel_gesture();
  }
  if(menu_open)ui_gamepad_actions(pad.buttons);
  else ui_prev_buttons=0;
  old_buttons=pad.buttons;
 }
 if((menu_open || popup>0 || ui_fade>0) && b && b->base){
  static int overlay_marker=0;
  if(!overlay_marker++)
   trace_once("ux0:data/DDTouchUI/overlay_drawn.txt","Overlay draw routine executed\\n");
  ui_draw_overlay(b);
  if(!menu_open && popup>0)popup--;
 }
 int result=TAI_CONTINUE(int,refs[4],b,sync);
 if(dd_action_frames>0)dd_action_frames--;
 if(dd_action_frames==0)dd_action_type=0;
 return result;
}
static int add(int index,uint32_t nid,const void*fn){
 int r=taiHookFunctionImport(&refs[index],"eboot",TAI_ANY_LIBRARY,nid,fn);
 if(r>=0){hooks[index]=r;enabled[index]=1;}
 return r;
}
/* ARM Thumb-2 MOVW and MOVT instruction imm16 decoders.
   Engine 1.17 absolute address literals are loader-relocated at every launch.
   Avoid comparing absolute pointer bytes across sessions. */
static int thumb_mov_imm16(const uint8_t *p,int upper,unsigned int *reg,unsigned int *value){
 if(!p || !value || !reg)return 0;
 uint16_t h1=(uint16_t)p[0]|((uint16_t)p[1]<<8);
 uint16_t h2=(uint16_t)p[2]|((uint16_t)p[3]<<8);
 const uint16_t opcode=upper?0xF2C0:0xF240;
 if((h1&0xFBF0)!=opcode)return 0;
 *reg=(h2>>8)&15;
 *value=((h1&15)<<12)|(((h1>>10)&1)<<11)|(((h2>>12)&7)<<8)|(h2&255);
 return 1;
}
static int thumb_decode_abs_pair(const uint8_t *text,unsigned int movw_off,
 unsigned int movt_off,unsigned int *pointer){
 if(!text || !pointer)return 0;
 unsigned int reglo=0,reghi=0,low=0,high=0;
 if(!thumb_mov_imm16(text+movw_off,0,&reglo,&low) ||
    !thumb_mov_imm16(text+movt_off,1,&reghi,&high) || reglo!=reghi)return 0;
 *pointer=(high<<16)|low;
 return 1;
}
static void engine_hooks_init(void){
 tai_module_info_t info;
 memset(&info,0,sizeof(info));
 info.size=sizeof(info);
 if(taiGetModuleInfo("eboot",&info)<0){
  trace_once("ux0:data/DDTouchUI/engine_hook_init.txt","eboot module not found");
  return;
 }
 /* The offsets and signatures are for the tested PCSE00919 Vita v1.16 build.
    Resolve actual segment addresses, and fail closed if the executable changed. */
 SceKernelModuleInfo kernel_info;
 memset(&kernel_info,0,sizeof(kernel_info));
 kernel_info.size=sizeof(kernel_info);
 if(sceKernelGetModuleInfo(info.modid,&kernel_info)<0){
  trace_once("ux0:data/DDTouchUI/engine_hook_init.txt","sceKernelGetModuleInfo failed; game input left native");
  return;
 }
 uintptr_t code_base=(uintptr_t)kernel_info.segments[0].vaddr;
 uintptr_t data_base=(uintptr_t)kernel_info.segments[1].vaddr;
 /* v0.32: dual signature verification. Unsupported code never gets engine hooks. */
 char diag[2800];
 int diag_len=0;
 diag_len+=sceClibSnprintf(diag+diag_len,sizeof(diag)-diag_len,
  "DDTOUCHUI_V047_COMPAT_PROBE\\nmodid=%d code_base=%08x code_memsz=%08x data_base=%08x data_memsz=%08x\\n",
  info.modid,(unsigned int)code_base,(unsigned int)kernel_info.segments[0].memsz,
  (unsigned int)data_base,(unsigned int)kernel_info.segments[1].memsz);
 const unsigned int expected_offsets[7]={0x4C670,0x4C566,0x4C594,0x4C534,0x460FE,0x3710C,0x49F66};
 uint16_t expected_inst[7]={0xB570,0xB510,0xB510,0xB510,0xE92D,0xE92D,0xF645};
 const char* labels[7]={"xy","down","up","held","hit","gui","focus"};
 int signatures_ok=1;
 unsigned int code_sz=(unsigned int)kernel_info.segments[0].memsz;
 unsigned int data_sz=(unsigned int)kernel_info.segments[1].memsz;
 /* Runtime layout validation: loader can change all MOVW/MOVT literal
    bytes per launch. Decode addresses, subtract the ACTUAL data_base and
    check against version-specific structural offsets. Never trust only
    common function entry prologues. */
 unsigned int xy_abs=0,xy_y_abs=0,held_abs=0,down_abs=0,up_abs=0,hit_abs=0,gui_abs=0,focus_abs=0;
 const uint8_t *text=(const uint8_t*)code_base;
 int ptr_ok=code_base && code_sz>=0x4C700 && data_base &&
  thumb_decode_abs_pair(text,0x4C680,0x4C684,&xy_abs) &&
  thumb_decode_abs_pair(text,0x4C68E,0x4C692,&xy_y_abs) &&
  thumb_decode_abs_pair(text,0x4C54A,0x4C54E,&held_abs) &&
  thumb_decode_abs_pair(text,0x4C57C,0x4C580,&down_abs) &&
  thumb_decode_abs_pair(text,0x4C5AA,0x4C5AE,&up_abs) &&
  thumb_decode_abs_pair(text,0x46116,0x4611A,&hit_abs) &&
  thumb_decode_abs_pair(text,0x3714A,0x37152,&gui_abs) &&
  thumb_decode_abs_pair(text,0x49F66,0x49F6A,&focus_abs);
 unsigned int c_rel=xy_abs-(unsigned int)data_base;
 unsigned int b_rel=down_abs-(unsigned int)data_base;
 unsigned int g_rel=gui_abs-(unsigned int)data_base;
 unsigned int h_rel=hit_abs-(unsigned int)data_base;
 unsigned int f_rel=focus_abs-(unsigned int)data_base;
 int version=0;
 if(ptr_ok && xy_y_abs==xy_abs+4 && held_abs==down_abs &&
    up_abs==down_abs && f_rel==0xC58){
  if(c_rel==0x0136E148 && b_rel==0x0136E160 &&
     g_rel==0x01112D10 && h_rel==0x0136DBD4)
    version=116;
  else if(c_rel==0x0136E208 && b_rel==0x0136E220 &&
          g_rel==0x01112DD0 && h_rel==0x0136DC94)
    version=117;
 }
 /* The focus MOVW immediate is relocated on every launch. Validation for
    the focus function uses decoded data_base+0xC58 and invariant LDM r2,r3
    instead of an exact immediate-containing first halfword. */
 diag_len+=sceClibSnprintf(diag+diag_len,sizeof(diag)-diag_len,
 "decoded=%d version=%d cursor_abs=%08x rel=%08x buttons_abs=%08x rel=%08x gui_abs=%08x rel=%08x hit_abs=%08x rel=%08x focus_abs=%08x rel=%08x\n",
 ptr_ok,version,xy_abs,c_rel,down_abs,b_rel,gui_abs,g_rel,hit_abs,h_rel,focus_abs,f_rel);
 for(int j=0;j<7;j++){
  unsigned int offset=expected_offsets[j];
  if(code_base && offset+16<=code_sz){
   const volatile uint8_t *addr=(const volatile uint8_t *)(code_base+offset);
   uint16_t found=*(const volatile uint16_t*)addr;
   int match=(j==6)
      ? (version!=0 && ptr_ok && f_rel==0xC58 &&
         *(const volatile uint16_t *)(code_base+0x49F6E)==0xCA0C)
      : (found==expected_inst[j]);
   if(j==6)expected_inst[j]=found; /* Expected MOVW enc varies with load base. */
   if(!match)signatures_ok=0;
   diag_len+=sceClibSnprintf(diag+diag_len,sizeof(diag)-diag_len,
    "%s offset=%05x found=%04x expected=%04x match=%d bytes=%02x%02x%02x%02x%02x%02x%02x%02x\\n",
    labels[j],offset,(unsigned int)found,(unsigned int)expected_inst[j],match,
    addr[0],addr[1],addr[2],addr[3],addr[4],addr[5],addr[6],addr[7]);
  }else{
   signatures_ok=0;
   diag_len+=sceClibSnprintf(diag+diag_len,sizeof(diag)-diag_len,
     "%s offset=%05x OUT_OF_BOUNDS\\n",labels[j],offset);
  }
 }
 int data_compatible=(data_base && data_sz>=0x0136E228);

 diag_len+=sceClibSnprintf(diag+diag_len,sizeof(diag)-diag_len,
   "code_size_sufficient=%d data_size_sufficient=%d signatures_match=%d version=%d\\n",
   code_base && code_sz>=0x4C700,data_compatible,signatures_ok,version);
 trace_once("ux0:data/DDTouchUI/engine_compat_probe_v047.txt",diag);
 /* 160KB snapshot of code containing the GUI/mouse functions for v1.16.
    Read only, segment-bounded, no game data, no PSARC changes. */
 if(diag_detailed_enabled && code_base && code_sz>=0x57000){
  const unsigned int offset=0x30000;
  const unsigned int length=0x27000;
  int fd=sceIoOpen("ux0:data/DDTouchUI/engine_code_gui_v042.bin",
                   SCE_O_CREAT|SCE_O_TRUNC|SCE_O_WRONLY,0666);
  if(fd>=0){
   unsigned int pos=0;
   while(pos<length){
    int chunk=(length-pos<0x2000)?(int)(length-pos):0x2000;
    int n=sceIoWrite(fd,(const void*)(code_base+offset+pos),chunk);
    if(n!=chunk)break;
    pos+=(unsigned int)n;
   }
   sceIoClose(fd);
   char b[125];
   sceClibSnprintf(b,sizeof(b),"text0_offset=0x30000 bytes_written=%u expected=0x27000\\n",pos);
   trace_once("ux0:data/DDTouchUI/engine_code_dump_v042.txt",b);
  }
 }

 if(!code_base || !data_base || code_sz<0x4C700 ||
     !data_compatible || !signatures_ok || !version){
  trace_once("ux0:data/DDTouchUI/engine_hook_init.txt",
    "unsupported engine layout or signatures (v0.32); no native UI hooks installed");
  return;
 }
 const unsigned int c_off=(version==117)?0x0136E208:0x0136E148;
 const unsigned int b_off=(version==117)?0x0136E220:0x0136E160;
 const unsigned int g_off=(version==117)?0x01112DD0:0x01112D10;
 engine_cursor_ptr=(volatile float *)(data_base+c_off);
 engine_button_ptr=(volatile int *)(data_base+b_off);
 engine_gui_reference_id=(volatile const unsigned int *)(data_base+0xC58);
 engine_ui_global_state=(volatile const unsigned int *)(data_base+g_off);
 if(dd_fast_input){
  /* Native input, no patching the seven hottest internal game functions.
     This is a safety boundary: the game retains its own focus and controller
     callback handling. Touch is injected only as mouse coordinates/button. */
  engine_compatible=1;
  if(dd_widget_probe && dd_fast_input){
   int rc=taiHookFunctionOffset(&refs[14],info.modid,0,0x3710C,1,
                               (void*)hook_engine_widget_probe);
   if(rc>=0){hooks[14]=rc;enabled[14]=1;}
   else {dd_widget_probe=0;dd_menu_click=0;}
  }
  if(dd_menu_click && dd_fast_input && enabled[14]){
   int rc=taiHookFunctionOffset(&refs[15],info.modid,0,0x49F66,1,
                               (void*)hook_engine_native_focus);
   if(rc>=0){hooks[15]=rc;enabled[15]=1;}
   else dd_menu_click=0;
  }
  if(dd_menu_click && !enabled[15])dd_menu_click=0;
  char fast_report[260];
  sceClibSnprintf(fast_report,sizeof(fast_report),
   "version=%d mode=FAST_NATIVE_POINTER widget_probe=%d hook_gui=%d hook_focus=%d menu_click=%d compatible=1 spatial_only=%d cursor_off=%08x button_off=%08x gui_off=%08x\\n",
   version,dd_widget_probe,enabled[14],enabled[15],dd_menu_click,
   dd_spatial_only,c_off,b_off,g_off);
  trace_once("ux0:data/DDTouchUI/engine_hook_init.txt",fast_report);
  return;
 }
 int offs[7]={0x4C670,0x4C566,0x4C594,0x4C534,0x460FE,0x3710C,0x49F66};
 const void *funcs[7]={(void*)hook_engine_get_xy,(void*)hook_engine_button_down,(void*)hook_engine_button_up,(void*)hook_engine_button_held,(void*)hook_engine_hit_test,(void*)hook_engine_widget_events,(void*)hook_engine_native_focus};
 char report[220];
 for(int j=0;j<7;j++){
  int i=j+9;
  int r=taiHookFunctionOffset(&refs[i],info.modid,0,offs[j],1,funcs[j]);
  if(r>=0){hooks[i]=r;enabled[i]=1;}
 }
 engine_compatible=enabled[9] && enabled[10] && enabled[11] && enabled[12] && enabled[13] && enabled[14] && enabled[15];
 if(!engine_compatible){
  for(int k=15;k>=9;k--)if(enabled[k]){
   taiHookRelease(hooks[k],refs[k]);
   enabled[k]=0;
  }
  engine_cursor_ptr=0;engine_button_ptr=0;
  engine_gui_reference_id=0;engine_ui_global_state=0;
 }
 sceClibSnprintf(report,sizeof(report),
  "version=%d modid=%d xy=%d down=%d up=%d held=%d hit=%d gui=%d focus=%d compatible=%d cursor_off=%08x button_off=%08x gui_off=%08x\n",
  version,info.modid,enabled[9],enabled[10],enabled[11],enabled[12],
  enabled[13],enabled[14],enabled[15],engine_compatible,c_off,b_off,g_off);
 trace_once("ux0:data/DDTouchUI/engine_hook_init.txt",report);
}
static void release(void){
 for(int i=23;i>=0;i--)if(enabled[i]){
  taiHookRelease(hooks[i],refs[i]);enabled[i]=0;
 }
}
static void status_log(void){
 sceIoMkdir("ux0:data/DDTouchUI",0777);
 char status[]="Read=0\nRead2=0\nPeek=0\nPeek2=0\nDisplay=0\n";
 status[5]='0'+enabled[0];
 status[13]='0'+enabled[1];
 status[20]='0'+enabled[2];
 status[28]='0'+enabled[3];
 status[38]='0'+enabled[4];
 trace_once("ux0:data/DDTouchUI/hooks.txt",status);
}
int module_start(SceSize argc,const void*args);
int _start(SceSize argc,const void*args) __attribute__((weak,alias("module_start")));
int module_start(SceSize argc,const void*args){
 (void)argc;(void)args;
 load_config();
 ui_audio_start();
 add(0,0x169A1D58,hook_read);
 add(1,0x39401BEA,hook_read2);
 add(2,0xFF082DF0,hook_peek);
 add(3,0x3AD3D0A1,hook_peek2);
 add(4,0x7A410B64,hook_frame);

 status_log();
 engine_hooks_init();
 /* v0.59: preload artwork, avoiding synchronous SD/FTP device reads on menu open. */
 ui_load_sprites();
 ui_load_banners();
 ui_assets_start_retry();
 /* Hook only the game's SceCtrl imports; physical controls remain intact. */
 if(engine_compatible){
  const uint32_t ids[8]={
    0xA9C3CED6,0x15F81E8C,0xA59454D3,0x860BF292,
    0x67E7AB83,0xC4226A3E,0xE2D99296,0xA7178860
  };
  const void*funcs[8]={
    (void*)dd_hook_peek_0,(void*)dd_hook_peek_1,
    (void*)dd_hook_peek_2,(void*)dd_hook_peek_3,
    (void*)dd_hook_read_0,(void*)dd_hook_read_1,
    (void*)dd_hook_read_2,(void*)dd_hook_read_3
  };
  for(int i=0;i<8;i++)if(add(i+16,ids[i],funcs[i])>=0)
    dd_ctrl_hook_mask|=1u<<i;
 }
 dd_actions_report();
 {
  char report[410];
  sceClibSnprintf(report,sizeof(report),
   "TouchOnVita v0.60\nconfig_read=%d parsed=%d invalid=%d\n"
   "mode=%d fast=%d native_click=%d spatial=%d menu_click=%d\n"
   "engine_compatible=%d touch_hooks=%d%d%d%d display_hook=%d "
   "gui_hook=%d focus_hook=%d assets_loaded=%d\n",
   config_read_state,config_fields,config_unknown,mode,dd_fast_input,
   dd_direct_click,dd_spatial_only,dd_menu_click,engine_compatible,
   enabled[0],enabled[1],enabled[2],enabled[3],enabled[4],
   enabled[14],enabled[15],ui_assets_complete());
  trace_once("ux0:data/DDTouchUI/startup_v060.txt",report);
 }
 if(!(enabled[0]||enabled[1]||enabled[2]||enabled[3]) || !enabled[4]){
  ui_assets_stop_retry();ui_audio_stop();release();return SCE_KERNEL_START_FAILED;
 }
 return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize argc,const void*args){
 (void)argc;(void)args;
 if(engine_compatible){
  engine_hook_report();
  dd_actions_report();
  if(dd_fast_input && dd_spatial_only)dd_probe_report();
  if(dd_widget_probe)dd_widget_fast_report();
  ui_state_report();
 }
 ui_assets_stop_retry();
 ui_audio_stop();
 release();
 return SCE_KERNEL_STOP_SUCCESS;
}
