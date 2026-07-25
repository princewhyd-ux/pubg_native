#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

#include "animations.h"
#include "collision.h"
#include "combat.h"
#include "driving.h"
#include "npc.h"


// ==========================================
// نظام الذكاء الاصطناعي التكتيكي للأعداء (Tactical NPC System V4.0 - C++ Native)
// حل مشكلة السقوط + كمبرة + أخذ ساتر + قتال تكتيكي + حل اختفاء الشخصيات
// ==========================================

// هياكل وهمية (Forward Declarations) لربطها مع محرك السيارات والقتال
struct Car {
    Vector3 position;
    float rotationY;
    bool isNpcDriven;
};

struct EnvironmentObject {
    Vector3 position;
    BoundingBox bounds;
};

// تعريف الدوال الخارجية التي كتبناها في CombatSystem
namespace CombatSystem {
    extern void SpawnBullet(Vector3 startPos, Vector3 targetPos, bool isPlayerBullet, int baseDamage);
}

// المتغيرات الخارجية من main.cpp
extern Vector3 playerPos;
extern bool isDead;
extern std::vector<EnvironmentObject> environmentObjects;
extern std::vector<Car*> gameCars;

namespace NPCSystem {

    // 🔥 إعدادات الذكاء الاصطناعي (متطابقة 100%) 🔥
    struct Settings {
        float walkSpeed = 1.8f;
        float runSpeed = 4.8f;
        float driveSpeed = 12.0f;
        float rotationSpeed = 6.0f;
        int spawnCount = 6;       // عدد الأعداء
        float maxHealth = 100.0f;
        float sightRange = 45.0f; // مسافة الرؤية
        int npcDamage = 10;       // ضرر طلقة العدو
    } settings;

    // هيكل بيانات الصحة
    struct HealthData {
        float health;
        bool visible;
    };

    // هيكل بيانات العدو (NPC)
    struct NPC {
        Vector3 position;
        float rotationY;
        bool visible;
        
        std::string currentAction;
        std::string state; // ROAM, SEEK_HOUSE, CAMP, SEEK_CAR, DRIVING, ENGAGE, TAKE_COVER, DEAD
        Vector3 targetPoint;
        Vector3 velocity;
        HealthData healthData;
        
        float fireTimer;
        float taskTimer;
        float aiTimer;
        std::string tacticalStance; // STAND, CROUCH, PRONE
        
        Car* targetCar;

        // دالة تشغيل الأنميشن
        void FadeToAction(std::string name, float duration = 0.2f) {
            if (currentAction == name) return;
            currentAction = name;
            // في محرك حقيقي يتم دمج إطارات الأنميشن (Blend) هنا
        }
    };

    std::vector<NPC> npcs;
    bool isInitialized = false;

    // متجهات مساعدة عالية الأداء لتخفيف الضغط عن الذاكرة
    Vector3 _dir = {0};
    
    // دوال مساعدة للرياضيات العشوائية
    float GetRandomFloat(float min, float max) {
        return min + static_cast<float>(GetRandomValue(0, 10000)) / 10000.0f * (max - min);
    }

    // تعريف مسبق للدوال
    void AssignNewTask(NPC& npc);
    void FindCover(NPC& npc);

    void Init() {
        if (isInitialized) return;
        
        for (int i = 0; i < settings.spawnCount; i++) {
            NPC npc = {0};
            npc.position = (Vector3){ GetRandomFloat(-40.0f, 40.0f), 10.0f, GetRandomFloat(-40.0f, 40.0f) }; // السقوط من ارتفاع 10 أمتار
            npc.rotationY = 0.0f;
            npc.visible = true;
            
            npc.healthData.health = settings.maxHealth;
            npc.healthData.visible = true;

            npc.currentAction = "idle_20";
            npc.state = "ROAM";
            npc.fireTimer = 0.0f;
            npc.taskTimer = 0.0f;
            npc.aiTimer = 0.0f;
            npc.tacticalStance = "STAND";
            npc.targetCar = nullptr;

            AssignNewTask(npc);
            npcs.push_back(npc);
        }
        isInitialized = true;
    }

    // 🔥 دالة تلقي الضرر واتخاذ قرارات تكتيكية 🔥
    void DamageNPC(NPC& npc, int amount) {
        if (npc.state == "DEAD") return;
        
        npc.healthData.health -= amount;
        
        if (npc.healthData.health <= 0.0f) {
            npc.state = "DEAD";
            npc.healthData.visible = false;
            
            npc.FadeToAction("die_7", 0.2f);
            npc.velocity = {0, 0, 0}; 
        } else {
            // تكتيك: إذا كانت الصحة أقل من 40%، انسحب وابحث عن ساتر
            if (npc.healthData.health < 40.0f && npc.state != "TAKE_COVER" && npc.state != "CAMP") {
                npc.state = "TAKE_COVER";
                FindCover(npc);
            } else if (npc.state != "TAKE_COVER" && npc.state != "CAMP") {
                // إذا انضرب ولم يمت، يلتف فوراً للقتال
                npc.state = "ENGAGE";
            }
        }
    }

    void FindCover(NPC& npc) {
        if (environmentObjects.empty()) {
            AssignNewTask(npc);
            return;
        }
        
        // اختيار مبنى عشوائي كساتر
        int randIndex = GetRandomValue(0, environmentObjects.size() - 1);
        Vector3 bestCover = environmentObjects[randIndex].position;
        
        Vector3 dirFromPlayer = Vector3Normalize(Vector3Subtract(bestCover, playerPos));
        // الاختباء 4 أمتار خلف المبنى
        npc.targetPoint = Vector3Add(bestCover, Vector3Scale(dirFromPlayer, 4.0f)); 
        npc.taskTimer = 8.0f; 
    }

    void AssignNewTask(NPC& npc) {
        if (npc.state == "DEAD") return;
        
        if (npc.state == "DRIVING" && npc.targetCar != nullptr) {
            npc.targetCar->isNpcDriven = false;
            npc.visible = true;
            npc.position = Vector3Add(npc.targetCar->position, (Vector3){2.0f, 0.0f, 0.0f});
            npc.targetCar = nullptr;
        }

        float randVal = GetRandomFloat(0.0f, 1.0f);
        npc.tacticalStance = "STAND";

        if (randVal < 0.35f) {
            npc.state = "ROAM";
            npc.targetPoint = (Vector3){ npc.position.x + GetRandomFloat(-20.0f, 20.0f), 0.0f, npc.position.z + GetRandomFloat(-20.0f, 20.0f) };
        } else if (randVal < 0.70f && !environmentObjects.empty()) {
            npc.state = "SEEK_HOUSE";
            int randIndex = GetRandomValue(0, environmentObjects.size() - 1);
            npc.targetPoint = environmentObjects[randIndex].position;
            npc.targetPoint.x += GetRandomFloat(-1.0f, 1.0f); // التمركز داخل المبنى
            npc.targetPoint.z += GetRandomFloat(-1.0f, 1.0f);
        } else if (!gameCars.empty()) {
            npc.state = "SEEK_CAR";
            // بحث عن سيارة غير مقادة
            for (auto& car : gameCars) {
                if (!car->isNpcDriven) {
                    npc.targetCar = car;
                    npc.targetPoint = car->position;
                    break;
                }
            }
            if (npc.targetCar == nullptr) AssignNewTask(npc);
        } else {
            npc.state = "ROAM";
            npc.targetPoint = (Vector3){ npc.position.x + GetRandomFloat(-20.0f, 20.0f), 0.0f, npc.position.z + GetRandomFloat(-20.0f, 20.0f) };
        }
    }

    void HandleDriving(NPC& npc, float delta) {
        if (npc.targetCar == nullptr) return;
        
        Car* car = npc.targetCar;
        float distance = Vector3Distance(car->position, npc.targetPoint);
        npc.taskTimer -= delta;

        if (distance < 5.0f || npc.taskTimer <= 0.0f) {
            AssignNewTask(npc);
            return;
        }

        Vector3 dir = Vector3Normalize(Vector3Subtract(npc.targetPoint, car->position));
        float targetRotation = atan2f(dir.x, dir.z);
        
        float angleDiff = targetRotation - car->rotationY;
        angleDiff = atan2f(sinf(angleDiff), cosf(angleDiff));
        car->rotationY += angleDiff * 2.0f * delta; 

        Vector3 forwardVec = { sinf(car->rotationY), 0.0f, cosf(car->rotationY) };
        Vector3 testPos = Vector3Add(car->position, Vector3Scale(forwardVec, settings.driveSpeed * delta));
        
        // (في الـ C++ نتحقق من تصادم السيارة هنا عبر دوال الـ BoundingBox)
        bool hitWall = false; 

        if (!hitWall) {
            car->position = testPos;
        } else {
            AssignNewTask(npc); 
        }

        // فحص الأرضية للسيارة
        car->position.y = 0.0f; // مؤقتاً نفترض الأرض مستوية
    }

    void Update(float delta) {
        if (!isInitialized) return;

        for (auto& npc : npcs) {

            // ==========================================
            // 🔥 1. فيزياء الجاذبية ومنع السقوط (Safety Net) 🔥
            // ==========================================
            npc.velocity.y -= 25.0f * delta; 

            if (npc.state != "DEAD") {
                // (في الـ C++ نستدعي هنا دالة الـ Collision الخاصة بك)
                npc.position = Vector3Add(npc.position, Vector3Scale(npc.velocity, delta));
            } else {
                npc.position.y += npc.velocity.y * delta;
            }

            // 🔥 الأرضية الصلبة: الحماية المطلقة من السقوط للأسفل 🔥
            if (npc.position.y < 0.0f) {
                npc.position.y = 0.0f;
                npc.velocity.y = 0.0f;
            }

            if (npc.state == "DEAD") continue; 

            if (npc.state == "DRIVING") {
                HandleDriving(npc, delta);
                continue;
            }

            float distToPlayer = Vector3Distance(npc.position, playerPos);

            // ==========================================
            // 🔥 3. نظام الرؤية التكتيكي (Combat Sight AI) 🔥
            // ==========================================
            npc.aiTimer -= delta;
            if (npc.aiTimer <= 0.0f) {
                npc.aiTimer = 0.3f; 
                
                bool hasLineOfSight = false;
                if (!isDead && distToPlayer < settings.sightRange) {
                    Vector3 npcHead = npc.position; 
                    npcHead.y += (npc.tacticalStance == "PRONE") ? 0.4f : 1.5f;
                    Vector3 pHead = playerPos; 
                    pHead.y += 1.5f;
                    
                    Vector3 dirToPlayer = Vector3Normalize(Vector3Subtract(pHead, npcHead));
                    
                    // Raycast للتحقق من وجود جدران (Raylib)
                    Ray ray = { npcHead, dirToPlayer };
                    bool hitWall = false; // افتراضياً (يفضل ربطها بـ GetRayCollisionBox)
                    
                    if (!hitWall) hasLineOfSight = true; 
                }

                if (hasLineOfSight) {
                    if (npc.state == "CAMP") {
                        npc.state = "ENGAGE";
                    } else if (npc.state != "TAKE_COVER") {
                        npc.state = "ENGAGE";
                    }
                } else if (npc.state == "ENGAGE") {
                    AssignNewTask(npc);
                }
            }

            // ==========================================
            // 🔥 4. تنفيذ حالة السلوك (State Machine) 🔥
            // ==========================================
            float distanceToTarget = Vector3Distance((Vector3){npc.position.x, 0.0f, npc.position.z}, (Vector3){npc.targetPoint.x, 0.0f, npc.targetPoint.z});

            if (npc.state == "ENGAGE") {
                npc.velocity.x = 0.0f; npc.velocity.z = 0.0f;
                
                Vector3 dirToPlayer = Vector3Normalize(Vector3Subtract(playerPos, npc.position));
                
                float targetRotation = atan2f(dirToPlayer.x, dirToPlayer.z);
                float angleDiff = targetRotation - npc.rotationY;
                angleDiff = atan2f(sinf(angleDiff), cosf(angleDiff));
                npc.rotationY += angleDiff * settings.rotationSpeed * delta * 2.5f; 

                if (npc.tacticalStance == "PRONE") npc.FadeToAction("p_fire_26");
                else if (npc.tacticalStance == "CROUCH") npc.FadeToAction("c_fire_2");
                else npc.FadeToAction("fire_idle_12");

                npc.fireTimer -= delta;
                if (npc.fireTimer <= 0.0f) {
                    npc.fireTimer = 0.2f + GetRandomFloat(0.0f, 0.3f); 
                    
                    Vector3 gunMuzzle = npc.position;
                    gunMuzzle.y += (npc.tacticalStance == "PRONE") ? 0.3f : (npc.tacticalStance == "CROUCH" ? 1.0f : 1.3f);
                    
                    Vector3 tPoint = playerPos;
                    tPoint.y += 1.0f; 
                    tPoint.x += GetRandomFloat(-0.75f, 0.75f); 
                    tPoint.z += GetRandomFloat(-0.75f, 0.75f);

                    CombatSystem::SpawnBullet(gunMuzzle, tPoint, false, settings.npcDamage);
                    
                    if (GetRandomFloat(0.0f, 1.0f) < 0.1f && npc.tacticalStance == "STAND") npc.tacticalStance = "CROUCH";
                }

            } else if (npc.state == "CAMP") {
                npc.velocity.x = 0.0f; npc.velocity.z = 0.0f;
                
                if (npc.tacticalStance == "PRONE") npc.FadeToAction("idle_p_19");
                else if (npc.tacticalStance == "CROUCH") npc.FadeToAction("idle_c_18");
                else npc.FadeToAction("idle_20");
                
                npc.taskTimer -= delta;
                if (npc.taskTimer <= 0.0f) AssignNewTask(npc); 
                
            } else {
                if (distanceToTarget > 1.5f) {
                    _dir = Vector3Normalize(Vector3Subtract((Vector3){npc.targetPoint.x, 0, npc.targetPoint.z}, (Vector3){npc.position.x, 0, npc.position.z}));
                    float targetRotation = atan2f(_dir.x, _dir.z);
                    float angleDiff = targetRotation - npc.rotationY;
                    angleDiff = atan2f(sinf(angleDiff), cosf(angleDiff));
                    npc.rotationY += angleDiff * settings.rotationSpeed * delta;

                    bool isPanicking = (npc.state == "TAKE_COVER");
                    float speed = (distanceToTarget > 15.0f || isPanicking) ? settings.runSpeed : settings.walkSpeed;
                    
                    npc.velocity.x = sinf(npc.rotationY) * speed;
                    npc.velocity.z = cosf(npc.rotationY) * speed;
                    
                    npc.FadeToAction(speed == settings.runSpeed ? "run_32" : "walk_34");
                    npc.tacticalStance = "STAND";
                } else {
                    npc.velocity.x = 0.0f; npc.velocity.z = 0.0f;
                    
                    if (npc.state == "SEEK_HOUSE") {
                        npc.state = "CAMP";
                        npc.tacticalStance = GetRandomFloat(0.0f, 1.0f) > 0.5f ? "PRONE" : "CROUCH";
                        npc.taskTimer = 20.0f + GetRandomFloat(0.0f, 30.0f); 
                    } else if (npc.state == "TAKE_COVER") {
                        npc.state = "CAMP";
                        npc.tacticalStance = "CROUCH"; 
                        npc.taskTimer = 15.0f;
                    } else if (npc.state == "SEEK_CAR" && npc.targetCar != nullptr) {
                        npc.state = "DRIVING";
                        npc.visible = false; 
                        npc.targetCar->isNpcDriven = true;
                        npc.targetPoint = (Vector3){ npc.targetCar->position.x + GetRandomFloat(-100.0f, 100.0f), 0.0f, npc.targetCar->position.z + GetRandomFloat(-100.0f, 100.0f) };
                        npc.taskTimer = 20.0f; 
                    } else {
                        npc.FadeToAction("idle_20");
                        npc.taskTimer -= delta;
                        if (npc.taskTimer <= 0.0f) {
                            npc.taskTimer = GetRandomFloat(1.0f, 4.0f); 
                            AssignNewTask(npc);
                        }
                    }
                }
            }
        }
    }

    // ==========================================
    // دالة رسم أشرطة الصحة كـ Billboards في بيئة الـ 3D
    // تُستدعى من دالة الرسم الأساسية (Draw3D)
    // ==========================================
    void DrawHealthBars(Camera3D camera) {
        for (auto& npc : npcs) {
            if (!npc.healthData.visible || npc.state == "DEAD" || !npc.visible) continue;

            // تحديد ارتفاع الشريط فوق الرأس بناءً على وضعية الشخصية
            float barYOffset = (npc.tacticalStance == "PRONE") ? 0.8f : (npc.tacticalStance == "CROUCH" ? 1.5f : 2.2f);
            Vector3 barPos = { npc.position.x, npc.position.y + barYOffset, npc.position.z };

            // استخدام BillBoard ثنائي الأبعاد مرسوم في مساحة ثلاثية الأبعاد (Native UI)
            float healthPercentage = npc.healthData.health / settings.maxHealth;
            
            // خلفية حمراء
            DrawBillboardRec(camera, (Texture2D){0}, (Rectangle){0,0,1,1}, barPos, (Vector2){1.5f, 0.2f}, MAROON);
            
            // صحة خضراء (يتم إزاحتها قليلاً لتتطابق مع النسبة)
            Vector3 greenPos = barPos;
            greenPos.x -= (1.5f - (1.5f * healthPercentage)) / 2.0f; // لكي يبدأ النقصان من اليمين
            DrawBillboardRec(camera, (Texture2D){0}, (Rectangle){0,0,1,1}, greenPos, (Vector2){1.5f * healthPercentage, 0.2f}, GREEN);
        }
    }
}
