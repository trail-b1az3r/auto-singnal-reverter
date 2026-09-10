#pragma once

#include <gui/scene_manager.h>

// Scene identifiers, generated from the scene table.
#define ADD_SCENE(prefix, name, id) RfScene##id,
typedef enum {
#include "rf_analyzer_scene_config.h"
    RfSceneNum,
} RfScene;
#undef ADD_SCENE

extern const SceneManagerHandlers rf_analyzer_scene_handlers;

// Per-scene handler prototypes, generated from the scene table.
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "rf_analyzer_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "rf_analyzer_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "rf_analyzer_scene_config.h"
#undef ADD_SCENE
