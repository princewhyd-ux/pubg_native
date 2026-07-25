#pragma once
#include "raylib.h"

namespace GameCollision {
    // 1. إدارة مجسمات البيئة وتوليد شجرة الاكتشاف (Octree)
    void AddCollider(Model model, Matrix transform);
    
    // 2. معالجة حركة اللاعب ضد الجدران والسلالم 
    void ResolveMovement(Vector3& playerPos, Vector3& playerVelocity, float delta);
    
    // 3. دالة رمي الشعاع (تُستخدم لكشف الأرضيات وأهداف الرصاص)
    bool Raycast(Vector3 origin, Vector3 dir, Vector3& outHitPoint);
    
    // 4. تصادم كبسولة مخصصة (تُستخدم خصيصاً في نظام قيادة السيارات لدفع السيارات عن الجدران)
    bool CapsuleIntersect(Vector3 start, Vector3 end, float radius, Vector3& outNormal, float& outDepth);
}
