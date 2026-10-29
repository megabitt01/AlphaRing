#pragma once

#include "Offset.h"

DefOffset(OFFSET_HALO3_PF_DRAW_STRUCTURE, 0x280164) //0x27FDB4
DefOffset(OFFSET_HALO3_PF_DRAW_MODEL, 0x2B5590) //0x2B51E0
DefOffset(OFFSET_HALO3_PF_ENGINE, 0xB2D40) //0xB2A20
DefOffset(OFFSET_HALO3_PF_WORLD, 0xEF120) //0xEEDD4
DefOffset(OFFSET_HALO3_PF_MAP, 0x42EF8) //0x42EF8
DefOffset(OFFSET_HALO3_PF_RENDER, 0x18553C) //0x1851EC

DefOffset(OFFSET_HALO3_PF_BUMP, 0x37F828)  //0x37F438

DefOffset(OFFSET_HALO3_PF_COOP_JOIN, 0x11DF8) //0x11DF8
DefOffset(OFFSET_HALO3_PF_OBJECT_CHANGE_COLOR, 0x28B960) //0x28B5B0

DefOffset(OFFSET_HALO3_PF_PLAYER_PUSH_MESSAGE, 0x2D4C34) //0x2D4884
DefOffset(OFFSET_HALO3_PF_PLAYER_POSSESS, 0xE3748) //0xE33FC
DefOffset(OFFSET_HALO3_PF_PLAYERS_CONTROL_CAMERA, 0x1313C8) //0x131078

#define OFFSET_HALO3_DAT_SKINNING_POOL 0x4680DC4
#define OFFSET_HALO3_PF_SKINNING_POOL_ALLOCATE 0x2A84F8


DefOffset(OFFSET_HALO3_PV_TAG_NAMES, 0xA49148)//0xA48148
DefOffset(OFFSET_HALO3_PV_ENABLE_INPUT, 0x20AD061)//0x20AC061
DefOffset(OFFSET_HALO3_PV_GLOBAL_VARIABLE, 0x2D3ED70)//0x2D3DD70
DefOffset(OFFSET_HALO3_PV_OBJECT_INFOS, 0x815160)//0x814050
DefOffset(OFFSET_HALO3_PV_LOG, 0xA49170) /*0xA48170*/ // the recent log lines, {char title[0x100], content[0x200]}[0x20]
DefOffset(OFFSET_HALO3_PV_TAG_SALTS, 0xA49018) /*0xA48018*/ // tag instances*: {int16 group index, int16 salt, int32 address}
DefOffset(OFFSET_HALO3_PV_TAGS_HEADER, 0x20AD058) /*0x20AC058*/ // tags_header*: +0 group count, +8 groups[16], +0x10 tag count, +0x18 instances[8]

DefOffset(OFFSET_HALO3_PF_LOG_FORMAT, 0xB3F3C) /*0xB3C1C*/ // char* (char* header, char* format, data1, data2) - a log line's text (halo3/log.cpp)
DefOffset(OFFSET_HALO3_PF_SIMULATION_TICKS, 0xEEFA8) /*0xEEC5C*/ // (int ticks, float*) - the ticks the simulation runs this frame (halo3/simulation.cpp)
DefOffset(OFFSET_HALO3_PF_UNIT_CONTROL, 0x350ED4) /*0x350B24*/ // (unsigned short unit, unit_control_definition*) - a unit's input for the tick

// Halo3::Native::Function::object_create
DefOffset(OFFSET_HALO3_PF_OBJECT_PLACEMENT_DATA_NEW, 0x341508) // (data*, int tag, int owner_object, damage_owner*)
DefOffset(OFFSET_HALO3_PF_OBJECT_NEW, 0x341AE8) // int (data*)
DefOffset(OFFSET_HALO3_PF_OBJECT_POST_CREATE, 0x1EA04) // (int object) - called by the engine after object_new

// c_splitscreen_config::m_config_table; the black-bar patches rewrite entries 8, 9 and 12 (0x8AE150, 0x8AE164, 0x8AE1A0)
DefOffset(OFFSET_HALO3_PV_SPLITSCREEN_TABLE, 0x8AE0B0)//0x8AD0C0

// Left/Right split screen and each view's title-safe box (mcc/splitscreen/LeftRight)
DefOffset(OFFSET_HALO3_PV_SCREEN_SIZE, 0x8AC3E8) // int width, height the views are laid out on
DefOffset(OFFSET_HALO3_PF_SPLITSCREEN_PLAYER_COUNT, 0x2E0928) // int ()
DefOffset(OFFSET_HALO3_PF_DRAW_SPLITSCREEN_BARS, 0x2D8174) // void () - black bars and dividers
DefOffset(OFFSET_HALO3_PF_FILL_RECT, 0x1890C4) // void (short rect[4] {top, left, bottom, right}, unsigned argb)
DefOffset(OFFSET_HALO3_PF_RT_CREATE, 0x2757C8) // (target*, int sizes[], descriptor*, int variant) - one pool render target
DefOffset(OFFSET_HALO3_PF_RT_POOL_RELEASE, 0x27613C) // void () - the resize path's teardown ...
DefOffset(OFFSET_HALO3_PF_RT_POOL_INIT, 0x275BBC) // void () - ... and rebuild
DefOffset(OFFSET_HALO3_PF_HUD_RESOLUTION, 0x2F1E38) // int (int user) - HUD layout of a view: 0 full, 1 half, 4 quarter (2, 5, 6 on 4:3)
DefOffset(OFFSET_HALO3_PF_HUD_LAYOUT, 0x2ECF38) // record* (int user, int resolution) - +0x10 int canvas width, height; 0x64 bytes
DefOffset(OFFSET_HALO3_PF_VIEW_SETUP, 0x282EC4) // void (view*, int slot, int players, int, int, void*) - a split-screen view's rect, title-safe box, projection
DefOffset(OFFSET_HALO3_PF_TITLE_SAFE, 0x272010) // void (short rect[4] {top, left, bottom, right}) - the screen's title-safe box, 5% in from each edge
DefOffset(OFFSET_HALO3_VIEW_SETUP_TITLE_SAFE_RETURN, 0x282F10) // where the view setup's call to it returns

struct halo3_teb_data_definition {
    void* zero;
    void* actor;
    void* players;
    int current_tread;
    int padding;
    void* game_engine_globals;
    void* prop_ref;
    void* ai_globals;
    void* object;
    void* players_globals;
    void* game_globals;
    void* squad;
    void* scripted_camera_globals;
    void* effect;
    void* prop;
    void* un;
    void* command_scripts;
    int tag_resource_cache_paging;
    int padding2;
    void* halo_interpolation; // frame interpolation data
    void* cinematic_new_globals;
    void* render_texture_globals;
    void* clump;
    void* cinematic_globals;
    void* object_globals;
    void* breakable_surface_breakable_surface_globals;
    void* player_control_globals;
    void* game_time_globals;
    void* joint_state;
    void* campaign_meta_game_globals;
    void* object_broadphase;
    void* effect_event;
    void* chud_persistent_user_data1;
    void* object_looping_sounds;
    void* player_effects;
    void* player_training_globals;
    void* player_mapping_globals;
    void* gamestate_timing_samples;
    void* lights;
    void* scripted_exposure_globals;
    void* physics_constants;
    void* vocalization_records;
    void* recycling_group;
    void* effect_location;
    void* un1;
    void* chud_cortana_effect;
    void* impacts;
    void* game_sound_globals;
    void* object_early_movers;
    void* cached_object_render_states;
    void* game_allegiance_globals;
    void* director_globals;
    void* swarm;
    void* device_groups;
    void* rasterizer_game_states;
    void* game_save_globals;
    void* recycling_volumes;
    void* hs_runtime_globals;
    void* flocks;
    void* object_scripting;
    void* effect_counts;
    void* squad_group;
    void* cloth;
    void* particle_system;
    void* impact_globals;
    void* breakable_surface_set_broken_events;
    void* main_timing;
    void* tracking;
    void* widget;
    void* objectives;
    void* chud_persistent_user_data2;
    void* leaf_system;
    void* damage_globals;
    void* local_game_engine_globals;
    void* sim_gamestate_entities;
    void* dynamic_firing_points;
    void* DOF_globals;
    void* beam_system;
    void* light_volume_system;
    void* particle_location;
    void* contrail_system;
    void* un2;
    void* scenario_interpolator_globals;
    void* havok_proxies;
    void* ai_player_state_globals;
    void* particle_emitter;
    void* game_sound_scripted_impulses;
    void* decal_system;
    void* rumble;
    void* effect_geometry_sample;
    void* object_schedule_globals;
    void* c_particle_emitter_gpu;
    void* object_list_header;
    void* decal_counts;
    void* hs_globals;
    void* swarm_spawner;
    void* screen_effect;
    void* muffin;
    void* antenna;
    void* lights_globals;
    void* c_particle_emitter_gpu_s_row;
    void* beam_location;
    void* light_volume_location;
    void* contrail_location;
    void* impact_arrarys;
    void* s_structure_seam_globals;
    void* tracking_hs_thread;
    void* render_timing_samples;
    void* rasterizer;
    void* interaction_ripples;
    void* particles;
    void* non_det_hs_thread;
    void* beam;
    void* light_volume;
    void* contrail;
    void* objects;
    void* c_beam_gpu;
    void* c_light_volume_gpu;
    void* det_hs_thread;
    void* ai_reference_frame;
    void* object_activation_regions;
    void* c_contrail_gpu_s_row;
    void* c_contrail_gpu;
    void* recorded_animations;
    void* c_beam_gpu_s_row;
    void* c_light_volume_gpu_s_row;
    void* hue_saturation_control;
    void* contrail_profile;
    void* decal;
    void* place_holder;
    void* object_name_list;
    void* campaign_objectives;
    void* list_object_reference;
    void* kill_trigger_volume_state;
    void* sound_classes;
    void* vocalization_timers;
    void* effect_lightprobes;
    void* game_sound_player_effects_globals;
    void* bink_game_state;
    void* effect_messaging_queue;
    void* director_scripting;
    void* chud_widgets0;
    void* chud_widgets1;
    void* chud_widgets2;
    void* chud_widgets3;
    void* un3;
    void* object_messaging_queue;
    void* decal_messaging_queue;
    void* havok_gamestate;
    void* player_control_globals_deterministic;
    void* cluster_collideable_object_reference;
    void* collideable_object_cluster_reference;
    void* place_holder2;
    void* spawner_globals;
    void* observer_gamestate_globals;
    void* cluster_noncollideable_object_reference;
    void* noncollideable_object_cluster_reference;
    void* place_holder3;
    void* un4;
    void* object_placement_globals;
    void* c_particle_emitter_gpu1;
    void* c_particle_emitter_gpu2;
    void* c_particle_emitter_gpu4;
    void* c_particle_emitter_gpu3;
    void* render_game_globals;
    void* soft_surface_globals;
    void* cluster_light_reference;
    void* light_cluster_reference;
    void* place_holder4;
    void* OBJ_Render_Data;
    void* cinematic_light_globals;
    void* visibility_active_portals;
    void* random_math;
    void* task_records;
    void* fp_orientations;
    void* fp_weapons;
    void* deterministic_game_sound_globals;
    void* observer_globals;
    void* nondet_light_data;
    void* render_object;
};