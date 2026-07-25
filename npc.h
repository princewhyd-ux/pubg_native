#pragma once
#include "raylib.h"
#include "driving.h"
#include <vector>
#include <string>

// تعريف حالة العدو التكتيكية
struct NPCObject {
    Model model;
    Vector3 position;
    Vector3 velocity;
    Vector3 targetPoint;
    float rotationY;
    
    std::string state;          // 'ROAM', 'SEEK_HOUSE', 'CAMP', 'SEEK_CAR', 'DRIVING', 'ENGAGE', 'TAKE_COVER', 'DEAD'
    std::string tacticalStance; // 'STAND', 'CROUCH', 'PRONE'
    
    float health;
    float fireTimer;
    float taskTimer;
    float aiTimer;
    
    CarObject* targetCar;
    bool active;
};

namespace NPCSystem {
    extern std::vector<NPCObject*> npcs;
    extern Model sharedNpcModel; // لتوفير الذاكرة، نحمل الموديل مرة واحدة ونرسمه للجميع
    
    void Init();
    void Update(float delta);
    void Draw3D();
    void DrawUI(int screenWidth, int screenHeight, Camera3D camera); // رسم شريط الصحة 2D فوق رؤوسهم
    void DamageNPC(NPCObject* npc, float amount);
}
