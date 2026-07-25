#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cmath>
#include <string>

#include "combat.h"
#include "animations.h"
#include "collision.h"
#include "driving.h"
#include "npc.h"

// ==========================================
// المتغيرات الخارجية (External Bindings)
// مربوطة مباشرة بـ main.cpp و driving.cpp
// ==========================================
extern Camera3D camera;
extern Vector3 playerPos;
extern bool isDead;
extern float currentCameraYaw;
extern float targetCameraYaw;
extern float targetCameraPitch;
extern float currentCameraPitch;
extern bool isDraggingCamera;
extern Vector3 playerVelocity;
extern bool isFiring;
extern bool isScoped;

namespace CombatSystem {

    // حالة اللاعب والسلاح
    int playerHealth = 100;
    int currentAmmo = 30;
    bool isReloading = false;
    double lastFireTime = 0;
    double reloadStartTime = 0;

    WeaponStats weaponStats;
    Model weaponModel;
    Texture2D weaponIcon;

    // مسابح الكائنات (Object Pools for Zero Allocation)
    const int BULLET_POOL_SIZE = 200;
    const int SMOKE_POOL_SIZE = 250;
    std::vector<BulletObject> bulletPool(BULLET_POOL_SIZE);
    std::vector<SmokeParticle> smokePool(SMOKE_POOL_SIZE);

    // ==========================================
    // تهيئة النظام
    // ==========================================
    void Init() {
        playerHealth = 100;
        currentAmmo = weaponStats.magSize;
        isReloading = false;
        
        // إعداد المسابح
        for (int i = 0; i < BULLET_POOL_SIZE; i++) bulletPool[i].active = false;
        for (int i = 0; i < SMOKE_POOL_SIZE; i++) smokePool[i].active = false;

        weaponModel = LoadModel("assets/weapon.glb");
        weaponIcon = LoadTexture("assets/hud/weapon_icon.png");
    }

    // ==========================================
    // حساب الارتداد (Recoil)
    // ==========================================
    void ApplyRecoil() {
        if (isDraggingCamera) return;
        targetCameraPitch -= weaponStats.recoilVertical;
        float randomHorizontal = (float)GetRandomValue(-100, 100) / 100000.0f; 
        targetCameraYaw += randomHorizontal * weaponStats.recoilHorizontal * 1000.0f;
    }

    // ==========================================
    // إدارة الرصاص والجسيمات
    // ==========================================
    void SpawnBullet(Vector3 startPos, Vector3 targetPos, bool isPlayerBullet, float baseDamage) {
        // البحث عن رصاصة غير نشطة في المسبح
        BulletObject* bullet = nullptr;
        for (int i = 0; i < BULLET_POOL_SIZE; i++) {
            if (!bulletPool[i].active) { bullet = &bulletPool[i]; break; }
        }
        if (!bullet) return; // المسبح ممتلئ

        Vector3 direction = Vector3Normalize(Vector3Subtract(targetPos, startPos));
        
        float currentSpread = weaponStats.baseSpread;
        if (Vector3LengthSqr(playerVelocity) > 1.0f) currentSpread *= 2.5f;

        // تطبيق الانتشار (Spread)
        direction.x += ((float)GetRandomValue(-100, 100) / 100.0f) * currentSpread;
        direction.y += ((float)GetRandomValue(-100, 100) / 100.0f) * currentSpread;
        direction.z += ((float)GetRandomValue(-100, 100) / 100.0f) * currentSpread;
        direction = Vector3Normalize(direction);

        bullet->position = startPos;
        bullet->velocity = Vector3Scale(direction, weaponStats.bulletSpeed);
        bullet->distanceTraveled = 0;
        bullet->maxDistance = 600.0f;
        bullet->damage = baseDamage;
        bullet->isPlayerBullet = isPlayerBullet;
        bullet->active = true;
    }

    void CreateImpactEffect(Vector3 pos, Vector3 normal, bool isBlood) {
        int particleCount = isBlood ? 8 : 4;
        for (int p = 0; p < particleCount; p++) {
            SmokeParticle* smoke = nullptr;
            for (int i = 0; i < SMOKE_POOL_SIZE; i++) {
                if (!smokePool[i].active) { smoke = &smokePool[i]; break; }
            }
            if (!smoke) break;

            smoke->position = pos;
            smoke->scale = 0.05f + ((float)GetRandomValue(0, 80) / 1000.0f);
            
            Vector3 randomDir = {
                ((float)GetRandomValue(-100, 100) / 100.0f),
                ((float)GetRandomValue(-100, 100) / 100.0f),
                ((float)GetRandomValue(-100, 100) / 100.0f)
            };
            randomDir = Vector3Normalize(randomDir);

            if (isBlood) {
                smoke->velocity = Vector3Scale(randomDir, 1.5f + ((float)GetRandomValue(0, 30) / 10.0f));
            } else {
                Vector3 reflection = Vector3Normalize(Vector3Add(Vector3Scale(randomDir, 0.5f), Vector3Scale(normal, 0.8f)));
                smoke->velocity = Vector3Scale(reflection, 2.0f + ((float)GetRandomValue(0, 30) / 10.0f));
            }

            smoke->life = 1.0f;
            smoke->isBlood = isBlood;
            smoke->active = true;
        }
    }

    // ==========================================
    // نظام الـ CCD (Continuous Collision Detection)
    // ==========================================
    float DistancePointLineSegment(Vector3 p, Vector3 a, Vector3 b, Vector3& closestPoint) {
        Vector3 ab = Vector3Subtract(b, a);
        float lengthSqr = Vector3LengthSqr(ab);
        if (lengthSqr == 0.0f) { closestPoint = a; return Vector3Distance(p, a); }
        
        float t = Vector3DotProduct(Vector3Subtract(p, a), ab) / lengthSqr;
        t = Clamp(t, 0.0f, 1.0f);
        closestPoint = Vector3Add(a, Vector3Scale(ab, t));
        return Vector3Distance(p, closestPoint);
    }

    // ==========================================
    // حلقة التحديث الشاملة
    // ==========================================
    void Update(float delta) {
        // 1. تحديث حالة إعادة التلقيم (Reload)
        if (isReloading) {
            if (GetTime() - reloadStartTime >= weaponStats.reloadTime) {
                currentAmmo = weaponStats.magSize;
                isReloading = false;
            }
        }

        // 2. تحديث الإطلاق (اللاعب)
        if (isFiring && !isDead && !CarEngine::isDriving) {
            // تدوير اللاعب نحو الكاميرا بسلاسة
            float aimRotation = currentCameraYaw + PI;
            float angleDiff = aimRotation - 0.0f; // استبدل 0 بدوران اللاعب الفعلي إذا وجد في main
            angleDiff = atan2f(sinf(angleDiff), cosf(angleDiff));
            // playerModel.transform = MatrixRotateY(...) هنا للتدوير
            
            if (currentAmmo > 0 && !isReloading) {
                double currentTime = GetTime();
                if (currentTime - lastFireTime >= weaponStats.fireRate) {
                    lastFireTime = currentTime;
                    currentAmmo--;
                    ApplyRecoil();

                    // حساب اتجاه الطلقة من الكاميرا
                    Vector3 cameraDir = {
                        -sinf(currentCameraYaw) * cosf(currentCameraPitch),
                        sinf(currentCameraPitch),
                        -cosf(currentCameraYaw) * cosf(currentCameraPitch)
                    };

                    // حساب مكان خروج الطلقة (Muzzle)
                    Vector3 muzzlePos = playerPos;
                    muzzlePos.y += 1.3f;
                    Vector3 rightVec = { cosf(currentCameraYaw), 0.0f, -sinf(currentCameraYaw) };
                    muzzlePos = Vector3Add(muzzlePos, Vector3Scale(rightVec, 0.3f));
                    muzzlePos = Vector3Add(muzzlePos, Vector3Scale(cameraDir, 0.8f));

                    Vector3 targetPos = Vector3Add(camera.position, Vector3Scale(cameraDir, 500.0f));
                    SpawnBullet(muzzlePos, targetPos, true, weaponStats.damage);
                }
            } else if (currentAmmo <= 0 && !isReloading) {
                isReloading = true;
                reloadStartTime = GetTime();
            }
        }

        // 3. تحديث الرصاص (CCD Update)
        for (int i = 0; i < BULLET_POOL_SIZE; i++) {
            if (!bulletPool[i].active) continue;
            BulletObject& b = bulletPool[i];

            Vector3 moveDist = Vector3Scale(b.velocity, delta);
            float moveLen = Vector3Length(moveDist);
            Vector3 nextPos = Vector3Add(b.position, moveDist);

            bool hitRegistered = false;
            Vector3 closestPt;

            // فحص التصادم مع الأرض كمثال مبدئي (البيئة)
            if (nextPos.y <= 0.1f) {
                CreateImpactEffect(nextPos, (Vector3){0, 1, 0}, false);
                b.active = false;
                continue;
            }

            // فحص التصادم مع الأعداء أو اللاعب
            if (b.isPlayerBullet) {
                /* 
                 * ربط ذكي بنظام الـ NPC:
                 * نقوم بالمرور على الأعداء، ونقيس المسافة بين شعاع الرصاصة (من b.position إلى nextPos) ومركز العدو.
                 */
                // for (auto npc : NPCSystem::npcs) { ... }
            } else if (!b.isPlayerBullet && !isDead) {
                Vector3 pCenter = playerPos; pCenter.y += 1.0f;
                if (DistancePointLineSegment(pCenter, b.position, nextPos, closestPt) < 0.6f) {
                    playerHealth -= b.damage;
                    CreateImpactEffect(closestPt, Vector3Normalize(Vector3Negate(b.velocity)), true);
                    
                    if (playerHealth <= 0) {
                        isDead = true;
                        playerHealth = 0;
                    }
                    b.active = false;
                    continue;
                }
            }

            b.position = nextPos;
            b.distanceTraveled += moveLen;
            if (b.distanceTraveled > b.maxDistance) b.active = false;
        }

        // 4. تحديث الدخان والدم (Particles)
        for (int i = 0; i < SMOKE_POOL_SIZE; i++) {
            if (!smokePool[i].active) continue;
            SmokeParticle& p = smokePool[i];

            p.position = Vector3Add(p.position, Vector3Scale(p.velocity, delta));
            p.velocity.y -= 15.0f * delta; // جاذبية
            p.scale += delta * 0.5f;
            p.life -= delta * (p.isBlood ? 2.5f : 4.0f);

            if (p.life <= 0 || p.position.y <= 0.0f) p.active = false;
        }
    }

    // ==========================================
    // الرسم ثلاثي الأبعاد
    // ==========================================
    void Draw3D() {
        // رسم السلاح بيد اللاعب (يتم تعديل موقعه بناءً على الكاميرا إذا كنا Scoped)
        if (!isDead && !CarEngine::isDriving) {
            Vector3 wPos = playerPos;
            wPos.y += 1.2f;
            Vector3 right = { cosf(currentCameraYaw), 0.0f, -sinf(currentCameraYaw) };
            Vector3 forward = { -sinf(currentCameraYaw), 0.0f, -cosf(currentCameraYaw) };
            
            wPos = Vector3Add(wPos, Vector3Scale(right, 0.3f));
            wPos = Vector3Add(wPos, Vector3Scale(forward, 0.4f));

            if (!isScoped) {
                DrawModelEx(weaponModel, wPos, {0,1,0}, currentCameraYaw * (180.0f/PI), {10.0f, 10.0f, 10.0f}, WHITE);
            }
        }

        // رسم الرصاص (كخطوط سريعة متوهجة)
        for (int i = 0; i < BULLET_POOL_SIZE; i++) {
            if (!bulletPool[i].active) continue;
            Vector3 tail = Vector3Subtract(bulletPool[i].position, Vector3Scale(Vector3Normalize(bulletPool[i].velocity), 1.2f));
            DrawCylinderEx(tail, bulletPool[i].position, 0.02f, 0.02f, 4, ORANGE);
        }

        // رسم الجسيمات (دم أو شرار)
        for (int i = 0; i < SMOKE_POOL_SIZE; i++) {
            if (!smokePool[i].active) continue;
            Color pCol = smokePool[i].isBlood ? MAROON : GRAY;
            pCol.a = (unsigned char)(smokePool[i].life * 255.0f);
            DrawCube(smokePool[i].position, smokePool[i].scale, smokePool[i].scale, smokePool[i].scale, pCol);
        }
    }

    // ==========================================
    // رسم واجهة القتال (HUD)
    // ==========================================
    void DrawUI(int sw, int sh) {
        if (CarEngine::isDriving) return;

        // رسم شريط الصحة
        DrawRectangle(sw/2 - 125, sh - (sh * 0.08f), 250, 10, Fade(BLACK, 0.6f));
        DrawRectangle(sw/2 - 125, sh - (sh * 0.08f), (int)(250 * (playerHealth / 100.0f)), 10, playerHealth > 20 ? GREEN : RED);

        // رسم السلاح والذخيرة
        DrawTextureEx(weaponIcon, { (float)sw/2 + 150, (float)sh - 100 }, 0.0f, 0.8f, Fade(WHITE, 0.8f));
        
        std::string ammoStr = isReloading ? "RELOADING..." : TextFormat("%d / %s", currentAmmo, "\u221E");
        Color ammoCol = isReloading ? ORANGE : (currentAmmo <= 5 ? RED : WHITE);
        DrawText(ammoStr.c_str(), sw/2 + 160, sh - 40, 24, ammoCol);
    }

}
