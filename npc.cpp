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
// ==========================================
extern Vector3 playerPos;
extern bool isDead;

struct EnvironmentObject {
    Vector3 position;
    BoundingBox bounds;
};
extern std::vector<EnvironmentObject> environmentObjects;
extern std::vector<CarObject*> gameCars;

namespace CombatSystem {
    extern void SpawnBullet(Vector3 startPos, Vector3 targetPos, bool isPlayerBullet, float baseDamage);
}

namespace NPCSystem {

    std::vector<NPCObject*> npcs;
    Model sharedNpcModel;
    bool isInitialized = false;
    
    int totalAlivePlayers = 7; 
    int myKillCount = 0;

    // إعدادات الذكاء الاصطناعي التكتيكي
    struct {
        float walkSpeed = 1.8f;
        float runSpeed = 4.8f;
        float driveSpeed = 12.0f;
        float rotationSpeed = 6.0f;
        int spawnCount = 6;
        float maxHealth = 100.0f;
        float sightRange = 60.0f; // زيادة مسافة الرؤية لتناسب الخريطة المفتوحة
        float npcDamage = 10.0f;
    } settings;

    void AssignNewTask(NPCObject* npc);
    void FindCover(NPCObject* npc);
    void HandleDriving(NPCObject* npc, float delta);

    // ==========================================
    // تهيئة الأعداء (Spawn)
    // ==========================================
    void Init() {
        if (isInitialized) return;

        sharedNpcModel = LoadModel("player.glb");
        // تم إلغاء MatrixScale لتجنب التصغير المزدوج والمجهري

        for (int i = 0; i < settings.spawnCount; i++) {
            NPCObject* npc = new NPCObject();
            
            // خوارزمية إبعاد الأعداء بمسافة آمنة (من 50 إلى 150 متر) حول اللاعب
            float distX = (float)GetRandomValue(500, 1500) / 10.0f;
            float distZ = (float)GetRandomValue(500, 1500) / 10.0f;
            
            // توزيع عشوائي في جميع الاتجاهات (شمال، جنوب، شرق، غرب)
            if (GetRandomValue(0, 1) == 0) distX *= -1.0f;
            if (GetRandomValue(0, 1) == 0) distZ *= -1.0f;
            
            npc->position = { playerPos.x + distX, 10.0f, playerPos.z + distZ };
            npc->velocity = { 0.0f, 0.0f, 0.0f };
            npc->rotationY = 0.0f;
            npc->state = "ROAM";
            npc->tacticalStance = "STAND";
            npc->health = settings.maxHealth;
            npc->fireTimer = 0.0f;
            npc->taskTimer = 0.0f;
            
            // 🔥 فترة سماح تكتيكية 10 ثوانٍ لا يلاحظونك فيها 🔥
            npc->aiTimer = 10.0f; 
            
            npc->targetCar = nullptr;
            npc->active = true;
            
            AssignNewTask(npc);
            npcs.push_back(npc);
        }
        isInitialized = true;
    }

    // ==========================================
    // نظام تلقي الضرر
    // ==========================================
    // ربط متغيرات القتلات والأحياء من main.cpp
extern int totalAlivePlayers;
extern int myKillCount;

void DamageNPC(NPCObject* npc, float amount) {
        if (npc->state == "DEAD") return;

        npc->health -= amount;

        if (npc->health <= 0) {
            npc->health = 0;
            npc->state = "DEAD";
            npc->velocity = { 0.0f, 0.0f, 0.0f };

            // 🔥 تحديث العدادات بدقة متناهية فور موت العدو 🔥
            myKillCount++;
            totalAlivePlayers--;
            if (totalAlivePlayers < 1) totalAlivePlayers = 1;

        } else {
            if (npc->health < 40.0f && npc->state != "TAKE_COVER" && npc->state != "CAMP") {
                npc->state = "TAKE_COVER";
                FindCover(npc);
            } else if (npc->state != "TAKE_COVER" && npc->state != "CAMP") {
                npc->state = "ENGAGE";
            }
        }
    }


    // ==========================================
    // التكتيك: البحث عن ساتر
    // ==========================================
    void FindCover(NPCObject* npc) {
        if (environmentObjects.empty()) {
            AssignNewTask(npc);
            return;
        }

        int randIdx = GetRandomValue(0, environmentObjects.size() - 1);
        Vector3 bestCover = environmentObjects[randIdx].position;
        Vector3 dirFromPlayer = Vector3Normalize(Vector3Subtract(bestCover, playerPos));
        
        npc->targetPoint = Vector3Add(bestCover, Vector3Scale(dirFromPlayer, 4.0f));
        npc->taskTimer = 8.0f;
    }

    // ==========================================
    // التكتيك: تعيين مهمة جديدة
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
            npc->targetPoint = { npc->position.x + GetRandomValue(-30, 30), 0.0f, npc->position.z + GetRandomValue(-30, 30) };
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
    // حلقة التحديث الشاملة
    // ==========================================
    void Update(float delta) {
        if (!isInitialized) return;

        for (auto npc : npcs) {
            if (!npc->active) continue;

            npc->velocity.y -= 25.0f * delta;

            if (npc->state != "DEAD") {
                npc->position = Vector3Add(npc->position, Vector3Scale(npc->velocity, delta));
            } else {
                npc->position.y += npc->velocity.y * delta; 
            }

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

            // ==========================================
            // نظام الرؤية مع احترام فترة السماح
            // ==========================================
            npc->aiTimer -= delta;
            if (npc->aiTimer <= 0.0f) {
                npc->aiTimer = 0.3f; // العودة لنبضات الفحص الطبيعية
                bool hasLineOfSight = false;
                
                if (!isDead && distToPlayer < settings.sightRange) {
                    Vector3 npcHead = npc->position;
                    npcHead.y += (npc->tacticalStance == "PRONE") ? 0.4f : 1.5f;
                    
                    Vector3 playerHead = playerPos;
                    playerHead.y += 1.5f;

                    Ray sightRay = { npcHead, Vector3Normalize(Vector3Subtract(playerHead, npcHead)) };
                    bool hitEnvironment = false;

                    for (auto& env : environmentObjects) {
                        RayCollision col = GetRayCollisionBox(sightRay, env.bounds);
                        if (col.hit && col.distance < distToPlayer) {
                            hitEnvironment = true;
                            break;
                        }
                    }

                    if (!hitEnvironment) hasLineOfSight = true; 
                }

                if (hasLineOfSight) {
                    if (npc->state == "CAMP" || npc->state != "TAKE_COVER") npc->state = "ENGAGE";
                } else if (npc->state == "ENGAGE") {
                    AssignNewTask(npc);
                }
            }

            // ==========================================
            // تنفيذ الحالات التكتيكية
            // ==========================================
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
                    npc->fireTimer = 0.25f + ((float)GetRandomValue(0, 30) / 100.0f); // سرعة إطلاق واقعية
                    
                    Vector3 gunMuzzle = npc->position;
                    gunMuzzle.y += (npc->tacticalStance == "PRONE") ? 0.3f : ((npc->tacticalStance == "CROUCH") ? 1.0f : 1.3f);
                    
                    Vector3 targetPoint = playerPos;
                    targetPoint.y += 1.0f;
                    
                    targetPoint.x += ((float)GetRandomValue(-15, 15) / 10.0f);
                    targetPoint.z += ((float)GetRandomValue(-15, 15) / 10.0f);

                    CombatSystem::SpawnBullet(gunMuzzle, targetPoint, false, settings.npcDamage);

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
            else { 
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
            
            float drawRot = npc->rotationY * (180.0f / PI);
            
            // تلوين الأعداء بمسحة حمراء خفيفة جداً لتمييزهم، مع الحفاظ على الحجم الطبيعي 0.015
            Color tintColor = { 255, 200, 200, 255 }; 

            if (npc->state == "DEAD") {
                DrawModelEx(sharedNpcModel, npc->position, {1,0,0}, -90.0f, {0.015f, 0.015f, 0.015f}, GRAY);
            } else {
                DrawModelEx(sharedNpcModel, npc->position, {0,1,0}, drawRot, {0.015f, 0.015f, 0.015f}, tintColor);
            }
        }
    }

    // ==========================================
    // رسم واجهة الصحة 2D بذكاء
    // ==========================================
    void DrawUI(int screenWidth, int screenHeight, Camera3D camera) {
        if (!isInitialized) return;
        for (auto npc : npcs) {
            if (!npc->active || npc->state == "DEAD" || npc->state == "DRIVING" || npc->health >= settings.maxHealth) continue;

            Vector3 headPos = npc->position;
            headPos.y += (npc->tacticalStance == "PRONE") ? 0.8f : (npc->tacticalStance == "CROUCH" ? 1.5f : 2.2f);
            
            Vector2 screenPos = GetWorldToScreen(headPos, camera);
            
            if (screenPos.x > 0 && screenPos.x < screenWidth && screenPos.y > 0 && screenPos.y < screenHeight) {
                float dist = Vector3Distance(camera.position, headPos);
                
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
