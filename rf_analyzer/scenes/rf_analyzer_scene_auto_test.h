#pragma once

#include "rf_analyzer_scene.h"

void rf_analyzer_scene_auto_test_on_enter(void* context);
bool rf_analyzer_scene_auto_test_on_event(void* context, SceneManagerEvent event);
void rf_analyzer_scene_auto_test_on_exit(void* context);