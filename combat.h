#pragma once
#include "raylib.h"
#include <vector>

// ==========================================
// هياكل نظام القتال (Zero Allocation Pools)
// ==========================================
struct BulletObject {
    Vector3 position;
    Vector3 velocity;
    float distanceTraveled;
    float maxDistance;
    float damage;
    bool isPlayerBullet;
    bool active;
};

struct SmokeParticle {
    Vector3 position;
    Vector3 velocity;
    float life;
    float scale;
    bool isBlood;
    bool active;
};

struct WeaponStats {
    float damage = 34.0f;
    float headshotMultiplier = 3.0f;
    float fireRate = 0.1f;
    float bulletSpeed = 500.0f;
    int magSize = 30;
    float reloadTime = 2.2f;
    float recoilVertical = 0.010f;
    float recoilHorizontal = 0.002f;
    float baseSpread = 0.001f;
};

namespace CombatSystem {
    extern int playerHealth;
    extern int currentAmmo;
    extern bool isReloading;
    
    void Init();
    void Update(float delta);
    void Draw3D();
    void DrawUI(int screenWidth, int screenHeight);
    
    // دوال مساعدة للاستخدام الخارجي
    void Fire(Vector3 startPos, Vector3 targetPos, bool isPlayerBullet, float baseDamage);
    void CreateImpactEffect(Vector3 pos, Vector3 normal, bool isBlood);
}
