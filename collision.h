#pragma once
#include "raylib.h"

namespace GameCollision {
    // دوال التصادم التي تستدعيها باقي الملفات
    void AddCollider(Model model, Matrix transform);
    void ResolveMovement(Vector3& playerPos, Vector3& playerVelocity, float delta);
    bool Raycast(Vector3 origin, Vector3 dir, Vector3& outHitPoint);
    bool CapsuleIntersect(Vector3 start, Vector3 end, float radius, Vector3& outNormal, float& outDepth);
}
