#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>

// التعريف الهندسي الكامل للسيارة مطابقاً تماماً للفيزياء الخاصة بك
struct CarObject {
    Vector3 position;
    Quaternion quaternion;
    Vector3 rotation; // Euler YXZ
    Vector3 exactSize;
    Vector3 centerOffset;
    float fuel = 100.0f;
    bool engineOn = false;
    bool isNpcDriven = false;
    bool active = true; // تم إضافة هذا المتغير المطلوب في main.cpp
};

namespace CarEngine {
    // كشف المتغيرات للملفات الأخرى (هذا ما كان ينقص المترجم!)
    extern bool isDriving;
    extern CarObject* carModel;

    void Init(std::vector<CarObject*>& carsArray);
    void Update(float delta);
    void Enter(CarObject* targetCar);
    void Exit();
    void UpdateAndDrawUI(int screenWidth, int screenHeight);
}
