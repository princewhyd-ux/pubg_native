#pragma once
#include "raylib.h"

namespace NPCSystem {
    void Init();
    void Update(float delta);
    void DrawHealthBars(Camera3D camera);
}
