#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

#include "npc.h"
#include "driving.h"
#include "combat.h"
#include "animations.h"
#include "collision.h"

// ==========================================
// المتغيرات الخارجية (External Bindings)
// للربط مع الأنظمة الأخرى بسلاسة
// ==========================================
extern Vector3 playerPos;
extern bool isDead;

struct EnvironmentObject {
    Vector3 position;
    BoundingBox bounds;
};
extern std::vector<EnvironmentObject> environmentObjects;
extern std::vector<CarObject*> gameCars;

// ربط دالة الإطلاق من نظام القتال
namespace CombatSystem {
    extern void SpawnBullet(Vector3 startPos, Vector3 targetPos, bool isPlayerBullet, float baseDamage);
}

namespace NPCSystem {

    std::vector<NPCObject*> npcs;
    Model sharedNpcModel;
    bool isInitialized = false;

    // إعدادات الذكاء الاصطناعي
    struct {
        float walkSpeed = 1.8f;
        float runSpeed = 4.8f;
        float driveSpeed = 12.0f;
        float rotationSpeed = 6.0f;
        int spawnCount = 6;
        float maxHealth = 100.0f;
        float sightRange = 45.0f;
        float npcDamage = 10.0f;
    } settings;

    // دوال مساعدة معرّفة مسبقاً
    void AssignNewTask(NPCObject* npc);
    void FindCover(NPCObject* npc);
    void HandleDriving(NPCObject* npc, float delta);

    // ==========================================
    // تهيئة الأعداء (Spawn)
    // ==========================================
    void Init() {
        if (isInitialized) return;

        // تحميل الموديل مرة واحدة فقط للجميع (وفر هائل في الذاكرة)
        sharedNpcModel = LoadModel("assets/player.glb");
        sharedNpcModel.transform = MatrixScale(0.01f, 0.01f, 0.01f);

        for (int i = 0; i < settings.spawnCount; i++) {
            NPCObject* npc = new NPCObject();
            
            float startX = ((float)GetRandomValue(-400, 400) / 10.0f);
            float startZ = ((float)GetRandomValue(-400, 400) / 10.0f);
            
            npc->position = { startX, 10.0f, startZ }; // السقوط من السماء لتصحيح الارتفاع
            npc->velocity = { 0.0f, 0.0f, 0.0f };
            npc->rotationY = 0.0f;
            npc->state = "ROAM";
            npc->tacticalStance = "STAND";
            npc->health = settings.maxHealth;
            npc->fireTimer = 0.0f;
            npc->taskTimer = 0.0f;
            npc->aiTimer = 0.0f;
            npc->targetCar = nullptr;
            npc->active = true;
            
            AssignNewTask(npc);
            npcs.push_back(npc);
        }
        isInitialized = true;
    }

    // ==========================================
    // نظام تلقي الضرر واتخاذ القرارات
    // ==========================================
    void DamageNPC(NPCObject* npc, float amount) {
        if (npc->state == "DEAD") return;

        npc->health -= amount;

        if (npc->health <= 0) {
            npc->health = 0;
            npc->state = "DEAD";
            npc->velocity = { 0.0f, 0.0f, 0.0f };
            // (سيتم تشغيل أنميشن الموت 'die_7' عبر نظام الأنميشن الخارجي)
        } else {
            // تكتيك: الانسحاب والبحث عن ساتر إذا قاربت الصحة على الانتهاء
            if (npc->health < 40.0f && npc->state != "TAKE_COVER" && npc->state != "CAMP") {
                npc->state = "TAKE_COVER";
                FindCover(npc);
            } else if (npc->state != "TAKE_COVER" && npc->state != "CAMP") {
                // الالتفاف والاشتباك فوراً عند تلقي طلقة
                npc->state = "ENGAGE";
            }
        }
    }

    // ==========================================
    // التكتيك: البحث عن ساتر (Cover System)
    // ==========================================
    void FindCover(NPCObject* npc) {
        if (environmentObjects.empty()) {
            AssignNewTask(npc);
            return;
        }

        // اختيار مبنى عشوائي كساتر
        int randIdx = GetRandomValue(0, environmentObjects.size() - 1);
        Vector3 bestCover = environmentObjects[randIdx].position;
        
        Vector3 dirFromPlayer = Vector3Normalize(Vector3Subtract(bestCover, playerPos));
        // الاختباء 4 أمتار خلف المبنى عكس اتجاه اللاعب
        npc->targetPoint = Vector3Add(bestCover, Vector3Scale(dirFromPlayer, 4.0f));
        npc->taskTimer = 8.0f;
    }

    // ==========================================
    // التكتيك: تعيين مهمة جديدة (Task Assignment)
    // ==========================================
    void AssignNewTask(NPCObject* npc) {
        if (npc->state == "DEAD") return;

        if (npc->state == "DRIVING" && npc->targetCar) {
            npc->targetCar->isNpcDriven = false;
            npc->position = Vector3Add(npc->targetCar->position, {2.0f, 0.0f, 0.0f});
            npc->targetCar = nullptr;
        }

        float randVal = (float)GetRandomValue(0, 100) / 100.0f;
        npc->tacticalStance = "STAND";

        if (randVal < 0.35f) {
            npc->state = "ROAM";
            npc->targetPoint = { npc->position.x + GetRandomValue(-20, 20), 0.0f, npc->position.z + GetRandomValue(-20, 20) };
        } else if (randVal < 0.70f && !environmentObjects.empty()) {
            npc->state = "SEEK_HOUSE";
            int idx = GetRandomValue(0, environmentObjects.size() - 1);
            npc->targetPoint = environmentObjects[idx].position;
            npc->targetPoint.x += (float)GetRandomValue(-2, 2);
            npc->targetPoint.z += (float)GetRandomValue(-2, 2);
        } else if (!gameCars.empty()) {
            npc->state = "SEEK_CAR";
            std::vector<CarObject*> availableCars;
            for (auto car : gameCars) {
                // تجنب ركوب سيارة اللاعب
                if (!car->isNpcDriven && car != CarEngine::carModel) availableCars.push_back(car);
            }
            
            if (!availableCars.empty()) {
                npc->targetCar = availableCars[GetRandomValue(0, availableCars.size() - 1)];
                npc->targetPoint = npc->targetCar->position;
            } else {
                npc->state = "ROAM";
                npc->targetPoint = { npc->position.x + 10, 0, npc->position.z + 10 };
            }
        }
    }

    // ==========================================
    // حلقة التحديث الشاملة للذكاء الاصطناعي
    // ==========================================
    void Update(float delta) {
        if (!isInitialized) return;

        for (auto npc : npcs) {
            if (!npc->active) continue;

            // 1. فيزياء الجاذبية ومنع السقوط (Safety Net)
            npc->velocity.y -= 25.0f * delta;

            if (npc->state != "DEAD") {
                npc->position = Vector3Add(npc->position, Vector3Scale(npc->velocity, delta));
            } else {
                npc->position.y += npc->velocity.y * delta; // الميت يسقط فقط
            }

            // منع السقوط تحت الأرضية الافتراضية
            if (npc->position.y < 0.0f) {
                npc->position.y = 0.0f;
                npc->velocity.y = 0.0f;
            }

            if (npc->state == "DEAD") continue;

            if (npc->state == "DRIVING") {
                HandleDriving(npc, delta);
                continue;
            }

            float distToPlayer = Vector3Distance(npc->position, playerPos);

            // 2. نظام الرؤية التكتيكي (Line of Sight) كل 0.3 ثانية لتخفيف الضغط
            npc->aiTimer -= delta;
            if (npc->aiTimer <= 0.0f) {
                npc->aiTimer = 0.3f;
                bool hasLineOfSight = false;
                
                if (!isDead && distToPlayer < settings.sightRange) {
                    Vector3 npcHead = npc->position;
                    npcHead.y += (npc->tacticalStance == "PRONE") ? 0.4f : 1.5f;
                    
                    Vector3 playerHead = playerPos;
                    playerHead.y += 1.5f;

                    Ray sightRay = { npcHead, Vector3Normalize(Vector3Subtract(playerHead, npcHead)) };
                    bool hitEnvironment = false;

                    // فحص الشعاع ضد المجسمات (المباني)
                    for (auto& env : environmentObjects) {
                        RayCollision col = GetRayCollisionBox(sightRay, env.bounds);
                        if (col.hit && col.distance < distToPlayer) {
                            hitEnvironment = true;
                            break;
                        }
                    }

                    if (!hitEnvironment) hasLineOfSight = true; // اللاعب مكشوف!
                }

                if (hasLineOfSight) {
                    if (npc->state == "CAMP" || npc->state != "TAKE_COVER") npc->state = "ENGAGE";
                } else if (npc->state == "ENGAGE") {
                    AssignNewTask(npc);
                }
            }

            // 3. تنفيذ الحالات التكتيكية (State Machine)
            npc->targetPoint.y = npc->position.y;
            float distanceToTarget = Vector3Distance(npc->position, npc->targetPoint);

            if (npc->state == "ENGAGE") {
                npc->velocity.x = 0; npc->velocity.z = 0;
                
                Vector3 dirToPlayer = Vector3Normalize(Vector3Subtract(playerPos, npc->position));
                float targetRotation = atan2f(dirToPlayer.x, dirToPlayer.z);
                
                float angleDiff = targetRotation - npc->rotationY;
                angleDiff = atan2f(sinf(angleDiff), cosf(angleDiff));
                npc->rotationY += angleDiff * settings.rotationSpeed * delta * 2.5f;

                npc->fireTimer -= delta;
                if (npc->fireTimer <= 0.0f) {
                    npc->fireTimer = 0.2f + ((float)GetRandomValue(0, 30) / 100.0f);
                    
                    Vector3 gunMuzzle = npc->position;
                    gunMuzzle.y += (npc->tacticalStance == "PRONE") ? 0.3f : ((npc->tacticalStance == "CROUCH") ? 1.0f : 1.3f);
                    
                    Vector3 targetPoint = playerPos;
                    targetPoint.y += 1.0f;
                    
                    // تشتيت الرصاص التكتيكي
                    targetPoint.x += ((float)GetRandomValue(-15, 15) / 10.0f);
                    targetPoint.z += ((float)GetRandomValue(-15, 15) / 10.0f);

                    CombatSystem::SpawnBullet(gunMuzzle, targetPoint, false, settings.npcDamage);

                    // نسبة 10% أن يجلس أثناء القتال للمراوغة
                    if (GetRandomValue(1, 100) <= 10 && npc->tacticalStance == "STAND") {
                        npc->tacticalStance = "CROUCH";
                    }
                }
            } 
            else if (npc->state == "CAMP") {
                npc->velocity.x = 0; npc->velocity.z = 0;
                npc->taskTimer -= delta;
                if (npc->taskTimer <= 0.0f) AssignNewTask(npc);
            } 
            else { // الحركة (ROAM, SEEK_HOUSE, TAKE_COVER)
                if (distanceToTarget > 1.5f) {
                    Vector3 dir = Vector3Normalize(Vector3Subtract(npc->targetPoint, npc->position));
                    float targetRotation = atan2f(dir.x, dir.z);
                    
                    float angleDiff = targetRotation - npc->rotationY;
                    angleDiff = atan2f(sinf(angleDiff), cosf(angleDiff));
                    npc->rotationY += angleDiff * settings.rotationSpeed * delta;

                    bool isPanicking = (npc->state == "TAKE_COVER");
                    float speed = (distanceToTarget > 15.0f || isPanicking) ? settings.runSpeed : settings.walkSpeed;
                    
                    npc->velocity.x = sinf(npc->rotationY) * speed;
                    npc->velocity.z = cosf(npc->rotationY) * speed;
                    npc->tacticalStance = "STAND";
                } else {
                    npc->velocity.x = 0; npc->velocity.z = 0;
                    
                    if (npc->state == "SEEK_HOUSE") {
                        npc->state = "CAMP";
                        npc->tacticalStance = GetRandomValue(0, 1) == 0 ? "PRONE" : "CROUCH";
                        npc->taskTimer = 20.0f + GetRandomValue(0, 30);
                    } else if (npc->state == "TAKE_COVER") {
                        npc->state = "CAMP";
                        npc->tacticalStance = "CROUCH";
                        npc->taskTimer = 15.0f;
                    } else if (npc->state == "SEEK_CAR" && npc->targetCar) {
                        npc->state = "DRIVING";
                        npc->targetCar->isNpcDriven = true;
                        npc->targetPoint = { npc->targetCar->position.x + GetRandomValue(-200, 200), 0, npc->targetCar->position.z + GetRandomValue(-200, 200) };
                        npc->taskTimer = 20.0f;
                    } else {
                        npc->taskTimer -= delta;
                        if (npc->taskTimer <= 0.0f) {
                            npc->taskTimer = (float)GetRandomValue(1, 4);
                            AssignNewTask(npc);
                        }
                    }
                }
            }
        }
    }

    // ==========================================
    // قيادة السيارات المستقلة للأعداء
    // ==========================================
    void HandleDriving(NPCObject* npc, float delta) {
        if (!npc->targetCar) return;
        CarObject* car = npc->targetCar;
        
        float distance = Vector3Distance(car->position, npc->targetPoint);
        npc->taskTimer -= delta;

        if (distance < 5.0f || npc->taskTimer <= 0.0f) {
            AssignNewTask(npc);
            return;
        }

        Vector3 dir = Vector3Normalize(Vector3Subtract(npc->targetPoint, car->position));
        float targetRotation = atan2f(dir.x, dir.z);
        
        float angleDiff = targetRotation - car->rotation.y;
        angleDiff = atan2f(sinf(angleDiff), cosf(angleDiff));
        car->rotation.y += angleDiff * 2.0f * delta;
        car->quaternion = QuaternionFromEuler(0, car->rotation.y, 0);

        Vector3 forwardVec = { sinf(car->rotation.y), 0, cosf(car->rotation.y) };
        Vector3 testPos = Vector3Add(car->position, Vector3Scale(forwardVec, settings.driveSpeed * delta));
        
        // تبسيط الاصطدام للسيارات التي يقودها الأعداء (فحص حافة الشاشة أو الجدران البسيطة)
        car->position = testPos;
        
        if (car->position.y < 0.0f) car->position.y = 0.0f;
    }

    // ==========================================
    // رسم الأعداء 3D
    // ==========================================
    void Draw3D() {
        if (!isInitialized) return;
        for (auto npc : npcs) {
            if (!npc->active || npc->state == "DRIVING") continue;
            
            // في حالة الموت، نجعلهم منبطحين
            float drawRot = npc->rotationY * (180.0f / PI);
            if (npc->state == "DEAD") {
                DrawModelEx(sharedNpcModel, npc->position, {1,0,0}, -90.0f, {0.01f, 0.01f, 0.01f}, GRAY);
            } else {
                DrawModelEx(sharedNpcModel, npc->position, {0,1,0}, drawRot, {0.01f, 0.01f, 0.01f}, WHITE);
            }
        }
    }

    // ==========================================
    // رسم واجهة الصحة 2D بذكاء (World To Screen)
    // ==========================================
    void DrawUI(int screenWidth, int screenHeight, Camera3D camera) {
        if (!isInitialized) return;
        for (auto npc : npcs) {
            if (!npc->active || npc->state == "DEAD" || npc->state == "DRIVING" || npc->health >= settings.maxHealth) continue;

            // تحديد موقع شريط الصحة بناءً على وضعية العدو
            Vector3 headPos = npc->position;
            headPos.y += (npc->tacticalStance == "PRONE") ? 0.8f : (npc->tacticalStance == "CROUCH" ? 1.5f : 2.2f);
            
            // تحويل النقطة من عالم 3D إلى شاشة 2D
            Vector2 screenPos = GetWorldToScreen(headPos, camera);
            
            // التأكد أن العدو أمام الكاميرا (في الشاشة)
            if (screenPos.x > 0 && screenPos.x < screenWidth && screenPos.y > 0 && screenPos.y < screenHeight) {
                float dist = Vector3Distance(camera.position, headPos);
                
                // تصغير الشريط كلما ابتعد العدو
                float scale = 1.0f / fmaxf(1.0f, dist * 0.1f);
                float width = 80.0f * scale;
                float height = 10.0f * scale;
                float x = screenPos.x - (width / 2.0f);
                float y = screenPos.y;

                DrawRectangle(x, y, width, height, Fade(BLACK, 0.6f));
                DrawRectangle(x, y, width * (npc->health / settings.maxHealth), height, RED);
                DrawRectangleLines(x, y, width, height, Fade(BLACK, 0.8f));
            }
        }
    }

}
