#include "rf_analyzer_scene.h"

// Assemble the handler arrays from the scene table.
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const rf_analyzer_scene_on_enter_handlers[])(void*) = {
#include "rf_analyzer_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const rf_analyzer_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "rf_analyzer_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const rf_analyzer_scene_on_exit_handlers[])(void* context) = {
#include "rf_analyzer_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers rf_analyzer_scene_handlers = {
    .on_enter_handlers = rf_analyzer_scene_on_enter_handlers,
    .on_event_handlers = rf_analyzer_scene_on_event_handlers,
    .on_exit_handlers = rf_analyzer_scene_on_exit_handlers,
    .scene_num = RfSceneNum,
};
