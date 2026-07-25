#pragma once
#include "raylib.h"
#include <vector>

// تعريف وهمي للهيكل لكي يتعرف عليه المحرك
struct CarObject; 

namespace CarEngine {
    void Init(std::vector<CarObject*>& carsArray);
    void Update(float delta);
    void Enter(CarObject* targetCar);
    void Exit();
    void UpdateAndDrawUI(int screenWidth, int screenHeight);
}
