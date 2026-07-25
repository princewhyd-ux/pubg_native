#pragma once
#include "raylib.h"

namespace CombatSystem {
    void Init();
    void Update(float delta);
    void Draw3D();
    void DrawUI(int screenWidth, int screenHeight);
    void Fire();
    void SpawnBullet(Vector3 startPos, Vector3 targetPos, bool isPlayerBullet, int baseDamage);
}
