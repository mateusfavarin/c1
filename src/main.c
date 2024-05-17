#include "common.h"
#include "globals.h"
#include "ns.h"
#include "pad.h"
#include "gfx.h"
#include "misc.h"
#include "gool.h"
#include "level.h"
#include "slst.h"
#include "solid.h"
#include "pbak.h"
#include "audio.h"
#include "midi.h"
#include "title.h"

#include "pc/init.h"
#include "pc/time.h"
#include "pc/gfx/gl.h"
#include "pc/gfx/soft.h" // for ext only

/* .data */
const ns_subsystem subsys[21] = {
  { .name = "NONE", .init =       GLSetupPrims, .init2 =                  0, .on_load =          0, .unused = 0, .kill =             GLKill },
  { .name = "SVTX", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "TGEO", .init =                  0, .init2 =                  0, .on_load = TgeoOnLoad, .unused = 0, .kill =                  0 },
  { .name = "WGEO", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "SLST", .init =           SlstInit, .init2 =                  0, .on_load =          0, .unused = 0, .kill =           SlstKill },
  { .name = "TPAG", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "LDAT", .init =                  0, .init2 =           LdatInit, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "ZDAT", .init =                  0, .init2 =                  0, .on_load = ZdatOnLoad, .unused = 0, .kill =                  0 },
  { .name = "CPAT", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "BINF", .init =           BinfInit, .init2 =                  0, .on_load =          0, .unused = 0, .kill =           BinfKill },
  { .name = "OPAT", .init = GoolInitAllocTable, .init2 =        GoolInitLid, .on_load =          0, .unused = 0, .kill = GoolKillAllocTable },
  { .name = "GOOL", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "ADIO", .init =          AudioInit, .init2 =                  0, .on_load =          0, .unused = 0, .kill =          AudioKill },
  { .name = "MIDI", .init =           MidiInit, .init2 =                  0, .on_load =          0, .unused = 0, .kill =           MidiKill },
  { .name = "INST", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "IMAG", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "LINK", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "MDAT", .init =          TitleInit, .init2 = TitleLoadNextState, .on_load = MdatOnLoad, .unused = 0, .kill =          TitleKill },
  { .name = "IPAL", .init =                  0, .init2 =                  0, .on_load =          0, .unused = 0, .kill =                  0 },
  { .name = "PBAK", .init =           PbakInit, .init2 =                  0, .on_load =          0, .unused = 0, .kill =           PbakKill }
};
/* .sdata */
int wgeom_disabled = 0;         /* 800563FC; gp[0x0]  */
int paused = 0;                 /* 80056400; gp[0x1]  */
int pause_status = 0;           /* 8005640C; gp[0x4]  */
int use_cd = 1;                 /* 80056410; gp[0x5]  */
int done = 0;                   /* 80056428; gp[0xB]  */
/* .sbss */
uint32_t pause_stamp;           /* 800565B8; gp[0x6F] */
uint32_t pause_draw_stamp;      /* 800565BC; gp[0x70] */

extern ns_struct ns;
extern pad pads[2];
extern lid_t cur_lid, next_lid;
extern entry *cur_zone;
extern gool_object *crash;
extern eid_t crash_eid;
extern gool_handle handles[8];
extern int bonus_return;
extern level_state savestate;

#ifdef CFLAGS_DRAW_EXTENSIONS
extern zone_query cur_zone_query;
extern uint32_t *wall_bitmap;
extern gool_bound object_bounds[96];
extern int object_bound_count;
int draw_octrees = 0;
int draw_wallmap = 0;
int draw_objbounds = 0;
#endif

extern gl_context context;

void CoreLoop(lid_t lid);

//----- (80011D88) --------------------------------------------------------
int main() {
  use_cd = 0;
  init();
  CoreLoop(LID_BOOTLEVEL);
  _kill();
  return 0;
}

//----- (80011DD0) --------------------------------------------------------
void CoreObjectsCreate() {
  PadUpdate();
  pause_obj = 0;
  if (cur_lid == LID_TITLE) { /* title level? */
    NSOpen(&ns.ldat->exec_map[4], 0, 1);
    NSOpen(&ns.ldat->exec_map[52], 0, 1);
  }
  else if (cur_lid == LID_LEVELEND) { /* level completion screen? */
    NSOpen(&ns.ldat->exec_map[29], 0, 1);
    NSOpen(&ns.ldat->exec_map[30], 0, 1);
    NSOpen(&ns.ldat->exec_map[3], 0, 1);
  }
  else if (cur_lid != LID_INTRO && cur_lid != LID_GAMEWIN) { /* not intro or ending? */
    life_hud = GoolObjectCreate(&handles[1], 4, 0, 0, 0, 0);
    fruit_hud = GoolObjectCreate(&handles[1], 4, 1, 0, 0, 0);
    pickup_hud = GoolObjectCreate(&handles[1], 4, 5, 0, 0, 0);
    NSOpen(&ns.ldat->exec_map[0], 0, 1);
    NSOpen(&ns.ldat->exec_map[5], 0, 1);
    NSOpen(&ns.ldat->exec_map[29], 0, 1);
    if (cur_lid != LID_THEGREATHALL) /* not the great hall? */
      NSOpen(&ns.ldat->exec_map[34], 0, 1); /* load boxes code */
    NSOpen(&ns.ldat->exec_map[3], 0, 1);
    NSOpen(&ns.ldat->exec_map[4], 0, 1);
  }
  LevelInitMisc(1);
}

//----- (80011FC4) --------------------------------------------------------
void CoreLoop(lid_t lid) {
  int is_pause_lid, can_pause;
  zone_header *header;
  void *ot;
  uint32_t arg;
  int bonus_return2;
  int ticks_elapsed;

  LevelInitGlobals();
  NSInit(&ns, lid);
  CoreObjectsCreate();
  do {
    lid = ns.ldat->lid;
    is_pause_lid = lid != LID_TITLE && lid != LID_LEVELEND && lid != LID_INTRO;
    can_pause = (pbak_state == 0) && ((is_pause_lid && title_pause_state != -1) || title_pause_state > 0);
    if ((pads[0].tapped & 0x800) && can_pause) {
      paused = 1 - paused;
      if (!paused) {
        if (pause_obj) { /* pause screen object exists? */
          arg = 0;
          GoolSendEvent(0, pause_obj, 0xC00, 1, &arg); /* send resume/kill? event to pause screen object */
          pause_obj = 0;
          pause_status = -1;
          ticks_elapsed = GetTicksElapsed();
          ticks_elapsed = pause_stamp;
          context.draw_stamp = pause_draw_stamp;
        }
      }
      else if (!pause_obj) {
        pause_obj = GoolObjectCreate(&handles[7], 4, 4, 0, 0, 0);
        if (ISERRORCODE(pause_obj)) {
          pause_status = 0;
          paused = 0;
          pause_obj = 0;
        }
        else {
          pause_status = 1;
          pause_stamp = GetTicksElapsed();
          pause_draw_stamp = context.draw_stamp;
        }
      }
    }
    else { pause_status = 0; }
    if (crash && crash_eid != EID_NONE) /* crash exists and there is a pbak entry to play? */
      PbakPlay(&crash_eid);
    if (next_lid == -1 && lid != LID_TITLE
      && (game_state == GAME_STATE_GAMEOVER
       || game_state == GAME_STATE_CONTINUE
       || game_state == 0x400))
      next_lid = LID_TITLE;
    if (next_lid != -1) {
      GoolSendToColliders(0, GOOL_EVENT_LEVEL_END, 0, 0, 0);
      if (next_lid == -2) {
        lid = savestate.lid;
        bonus_return = 1; /* i.e. loading nsf and there is a savestate to load */
        bonus_return2 = 1; /* LdatInit clears bonus_return so we need a persistent variant */
      }
      else {
        lid = next_lid;
        bonus_return = 0;
        bonus_return2 = 0;
      }
      ns.draw_skip_counter = 2;
      NSKill(&ns);
      paused = 0;
      if (lid == LID_TITLE) {
        respawn_count = 0;
        death_count = 0;
        cortex_count = 0;
        brio_count = 0;
        tawna_count = 0;
        checkpoint_id = -1;
      }
      NSInit(&ns, lid);
      CoreObjectsCreate();
      if (bonus_return2) {
        next_lid = -2;
        LevelSpawnObjects();
        next_lid = -1;
        LevelRestart(&savestate);
      }
      bonus_return = 0;
    }
#ifdef PSX
    NSUpdate(-1);
#endif
    LevelSpawnObjects();
    if (!paused) {
      header = (zone_header*)cur_zone->items[0];
      if (header->flags & (ZONE_FLAG_DARK2 | ZONE_FLAG_LIGHTNING))
        ShaderParamsUpdate(0);
      /* if (!globals->paused) { ??? */
      if (header->flags & ZONE_FLAG_RIPPLE)
        ShaderParamsUpdateRipple(0);
      /* if (!globals->paused) ???*/
      CamUpdate();
    }
    GfxUpdateMatrices();
    ot = context.ot;
    header = (zone_header*)cur_zone->items[0];
    if ((cur_display_flags & GOOL_FLAG_DISPLAY_WORLDS) && header->world_count && !wgeom_disabled) {
      if (header->flags & ZONE_FLAG_DARK2)
        GfxTransformWorldsDark2(ot);
      else if ((header->flags & ZONE_FLAG_FOG_LIGHTNING) == ZONE_FLAG_FOG_LIGHTNING)
        GfxTransformWorldsDark(ot);
      else if (header->flags & ZONE_FLAG_FOG)
        GfxTransformWorldsFog(ot);
      else if (header->flags & ZONE_FLAG_RIPPLE)
        GfxTransformWorldsRipple(ot);
      else if (header->flags & ZONE_FLAG_LIGHTNING)
        GfxTransformWorldsLightning(ot);
      else
        GfxTransformWorlds(ot);
    }
    GoolUpdateObjects(!paused);
#if defined(CFLAGS_DRAW_EXTENSIONS) && !defined(PSX)
    if (pads[0].tapped & 4)
      draw_octrees = !draw_octrees;
    if (draw_octrees)
      SwTransformZoneQuery(&cur_zone_query, ot, GLGetPrimsTail());
    if (pads[0].tapped & 8)
      draw_wallmap = !draw_wallmap;
    if (draw_wallmap)
      SwDrawWallMap(wall_bitmap, ot, GLGetPrimsTail());
    if (pads[0].tapped & 1)
      draw_objbounds = !draw_objbounds;
    if (draw_objbounds)
      SwTransformObjectBounds(object_bounds, object_bound_count, ot, GLGetPrimsTail());
#endif
    GLClear();
    if (ns.ldat->lid == LID_TITLE) { TitleUpdate(ot); }
    GLUpdate();
  } while (!done);
  cur_display_flags = 0;
  NSKill(&ns);
}
