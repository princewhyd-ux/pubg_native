#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <map>
#include <string>
#include <cmath>

#include "animations.h"
#include "collision.h"
#include "combat.h"
#include "driving.h"
#include "npc.h"

// ==========================================
// محرك قيادة السيارات الاحترافي (C++ Native Engine)
// (Smart UI Merge Fix + Collision + Headlights + Horn + 4-Speed Gearbox)
// ==========================================

// متغيرات خارجية (من main.cpp)
extern Camera3D camera;
extern Vector3 playerPos;
extern bool isDead;
extern float currentCameraYaw;
extern float targetCameraYaw;
extern float targetCameraPitch;
extern float currentCameraPitch;
extern bool isDraggingCamera;

// هياكل وهمية لربطها بفيزياء البيئة
struct EnvironmentObject {
    Vector3 position;
    BoundingBox bounds;
};
extern std::vector<EnvironmentObject> environmentObjects;

namespace GameCollision {
    extern bool CapsuleIntersect(Vector3 start, Vector3 end, float radius, Vector3& outNormal, float& outDepth);
    extern bool Raycast(Vector3 origin, Vector3 dir, Vector3& outHitPoint);
}

// هيكل الجزيئات
struct DustParticle {
    Vector3 position;
    float life;
    float scale;
};

namespace CarEngine {

    bool isDriving = false;
    CarObject* carModel = nullptr;
    std::vector<CarObject*> allCars;
    bool isInitialized = false;
    std::vector<DustParticle> dustParticles;

    // الخصائص الفيزيائية الأساسية
    int currentGear = 0;
    float rpm = 0.0f;
    float gasVal = 0.0f;
    float brakeVal = 0.0f;
    float clutchVal = 0.0f;
    float steeringAngle = 0.0f;
    int kmh = 0;
    bool hasStalled = false;
    bool isStartingEngine = false;

    float mass = 1400.0f;
    float wheelRadius = 0.33f;
    float finalDrive = 3.42f;

    std::map<int, float> gearRatios = { {-1, -3.50f}, {0, 0.0f}, {1, 3.80f}, {2, 2.15f}, {3, 1.52f}, {4, 1.18f} };
    float speed = 0.0f;
    float heading = 0.0f;
    float maxSteering = 0.65f;

    struct Inputs { bool gas; bool brake; } inputs;
    bool clutchPressed = false;

    // الإضاءة
    bool lightsSetup = false;
    bool isHeadlightsOn = false;

    // نظام الصوت المتقدم
    struct AudioNode {
        Sound sound;
        float targetVolume;
        float targetPitch;
        bool isPlaying;
    };
    std::map<std::string, AudioNode> audioNodes;
    bool audioInitialized = false;
    bool wasHighRpm = false;

    float baseFov = 60.0f;

    // UI Textures
    Texture2D gasTex, brakeTex, clutchTex, steeringTex, weaponIconTex;
    Vector2 gearKnobPos = { 50.0f, 50.0f }; // نسبة مئوية
    std::map<std::string, Vector2> gearPositions = {
        {"N", {50, 50}}, {"1", {23, 15}}, {"2", {23, 85}}, 
        {"3", {53, 15}}, {"4", {53, 85}}, {"-1", {83, 85}}
    };

    // Touch Data
    int steeringTouchId = -1;
    float startSteerAngle = 0.0f;
    float currentSteerRotation = 0.0f;
    int gearTouchId = -1;
    bool isDraggingGear = false;

    // ==========================================
    // تهيئة المحرك الأساسي
    // ==========================================
    void Init(std::vector<CarObject*>& carsArray) {
        if (isInitialized) return;
        allCars = carsArray;

        gasTex = LoadTexture("assets/car/gas.png");
        brakeTex = LoadTexture("assets/car/brake.png");
        clutchTex = LoadTexture("assets/car/clutch.png");
        steeringTex = LoadTexture("assets/car/steering.png");
        weaponIconTex = LoadTexture("assets/hud/weapon_icon.png");

        for (auto car : allCars) {
            if (car->exactSize.x == 0) {
                // افتراض حجم السيارة إذا لم يتم حسابه
                car->exactSize = { 2.0f, 1.5f, 4.5f };
                car->centerOffset = { 0, 0.75f, 0 };
            }
        }

        isInitialized = true;
    }

    void InitAudioSystem() {
        if (audioInitialized) return;
        InitAudioDevice();

        auto LoadCarSound = [](std::string file) -> AudioNode {
            AudioNode node;
            node.sound = LoadSound(("assets/sounds/" + file).c_str());
            node.targetVolume = 0.0f;
            node.targetPitch = 1.0f;
            node.isPlaying = false;
            return node;
        };

        audioNodes["idle"] = LoadCarSound("idle.wav");
        audioNodes["low"] = LoadCarSound("low_rpm.wav");
        audioNodes["high"] = LoadCarSound("high_rpm.wav");
        audioNodes["gear"] = LoadCarSound("gear_whine.wav");
        audioNodes["screech"] = LoadCarSound("tire_screech.wav");

        audioInitialized = true;
    }

    void PlaySampleEffect(std::string key, float vol = 1.0f) {
        // في الواقع نقوم بتحميل الأصوات المؤقتة (Starter, Horn, Blowoff) وتشغيلها
        Sound fx = LoadSound(("assets/sounds/" + key + ".wav").c_str());
        SetSoundVolume(fx, vol);
        PlaySound(fx);
    }

    void UpdateAdvancedAudio(bool engineOn) {
        if (!audioInitialized) return;

        if (!engineOn || isStartingEngine || rpm < 50) {
            for (auto& pair : audioNodes) SetSoundVolume(pair.second.sound, 0.0f);
            return;
        }

        float basePitch = fmaxf(0.6f, 0.4f + (rpm / 4000.0f));
        float idleVol = 0, lowVol = 0, highVol = 0;

        if (rpm < 1200) { idleVol = 1.0f; lowVol = (rpm - 800) / 400; }
        else if (rpm < 4000) { lowVol = 1.0f; highVol = (rpm - 2500) / 1500; idleVol = fmaxf(0, 1.0f - ((rpm - 1200) / 1000)); }
        else { highVol = 1.0f; lowVol = fmaxf(0, 1.0f - ((rpm - 4000) / 1500)); }

        if (rpm >= 7100) {
            bool isCut = ((int)(GetTime() * 1000) % 120 < 60);
            highVol = isCut ? 0.0f : 1.0f;
            if (isCut && GetRandomValue(0, 100) > 80) PlaySampleEffect("backfire", 0.8f);
        }

        if (rpm > 5000 && gasVal > 0.5f) wasHighRpm = true;
        if (wasHighRpm && gasVal < 0.1f && clutchPressed) { PlaySampleEffect("blowoff", 1.0f); wasHighRpm = false; }

        auto ApplyNode = [](AudioNode& node, float vol, float pitch) {
            if (!IsSoundPlaying(node.sound)) PlaySound(node.sound);
            SetSoundVolume(node.sound, Clamp(vol, 0.0f, 1.0f));
            SetSoundPitch(node.sound, pitch);
        };

        ApplyNode(audioNodes["idle"], idleVol, basePitch * 0.9f);
        ApplyNode(audioNodes["low"], lowVol, basePitch * 0.9f);
        ApplyNode(audioNodes["high"], highVol, basePitch * 0.8f);

        float gearPitch = 0.5f + (fabs(speed) / 20.0f);
        float gearVol = fminf(0.4f, fabs(speed) / 80.0f);
        ApplyNode(audioNodes["gear"], gearVol, gearPitch);

        float screechVol = (fabs(steeringAngle) > 0.5f && fabs(speed) > 15.0f) ? 0.7f : 0.0f;
        ApplyNode(audioNodes["screech"], screechVol, 1.0f);
    }

    void Enter(CarObject* targetCar) {
        if (!audioInitialized) InitAudioSystem();
        isDriving = true;
        carModel = targetCar;

        heading = carModel->rotation.y;
        speed = 0.0f; currentGear = 0;
        gearKnobPos = gearPositions["N"];
        
        targetCameraYaw = heading + PI;
        currentCameraYaw = heading + PI;
        targetCameraPitch = 0.15f; currentCameraPitch = 0.15f;
        
        dustParticles.clear();
        gasVal = 0; brakeVal = 0; clutchVal = 0;
        clutchPressed = false; inputs.gas = false; inputs.brake = false;
    }

    void Exit() {
        isDriving = false;
        for (auto& pair : audioNodes) SetSoundVolume(pair.second.sound, 0.0f);
        
        // إخراج اللاعب بجانب السيارة
        Vector3 offset = Vector3RotateByQuaternion({-2.5f, 0, 0}, carModel->quaternion);
        playerPos = Vector3Add(carModel->position, offset);
        playerPos.y += 1.0f;
        
        carModel = nullptr;
    }

    // ==========================================
    // نظام اصطدام السيارات (8 نقاط الاحترافي)
    // ==========================================
    bool CheckCarToCarCollision(Vector3 nextPos) {
        if (!carModel) return false;

        Vector3 mySize = carModel->exactSize;
        Vector3 myOffset = carModel->centerOffset;
        Vector3 myActualCenter = Vector3Add(nextPos, Vector3RotateByQuaternion(myOffset, carModel->quaternion));

        auto GetPoints = [](Vector3 size) -> std::vector<Vector3> {
            float hX = size.x / 2; float hZ = size.z / 2;
            return {
                {hX, 0, hZ}, {-hX, 0, hZ}, {hX, 0, -hZ}, {-hX, 0, -hZ},
                {0, 0, hZ}, {0, 0, -hZ}, {hX, 0, 0}, {-hX, 0, 0}
            };
        };

        std::vector<Vector3> myPointsLocal = GetPoints(mySize);
        Quaternion myInvQuat = QuaternionInvert(carModel->quaternion);

        for (auto otherCar : allCars) {
            if (otherCar == carModel) continue;

            Vector3 otherSize = otherCar->exactSize;
            Vector3 otherOffset = otherCar->centerOffset;
            Vector3 otherActualCenter = Vector3Add(otherCar->position, Vector3RotateByQuaternion(otherOffset, otherCar->quaternion));

            for (auto p : myPointsLocal) {
                Vector3 worldPoint = Vector3Add(myActualCenter, Vector3RotateByQuaternion(p, carModel->quaternion));
                // World to Local for other car
                Vector3 localToOther = Vector3RotateByQuaternion(Vector3Subtract(worldPoint, otherActualCenter), QuaternionInvert(otherCar->quaternion));
                
                if (fabs(localToOther.x) < otherSize.x/2 && fabs(localToOther.z) < otherSize.z/2) return true;
            }

            std::vector<Vector3> otherPointsLocal = GetPoints(otherSize);
            for (auto p : otherPointsLocal) {
                Vector3 worldPoint = Vector3Add(otherActualCenter, Vector3RotateByQuaternion(p, otherCar->quaternion));
                Vector3 localToMe = Vector3RotateByQuaternion(Vector3Subtract(worldPoint, myActualCenter), myInvQuat);
                
                if (fabs(localToMe.x) < mySize.x/2 && fabs(localToMe.z) < mySize.z/2) return true;
            }
        }
        return false;
    }

    float GetEngineTorque(float r) {
        if (r < 400) return 0; 
        if (r < 1000) return 200; 
        if (r < 4500) return 200 + ((r - 1000) / 3500) * 250; 
        if (r < 7000) return 450 - ((r - 4500) / 2500) * 150; 
        return 0; 
    }

    // ==========================================
    // حلقة التحديث الفيزيائية الشاملة
    // ==========================================
    void Update(float delta) {
        if (!isDriving || !carModel) return;

        gasVal = Lerp(gasVal, inputs.gas ? 1.0f : 0.0f, 5.0f * delta);
        brakeVal = Lerp(brakeVal, inputs.brake ? 1.0f : 0.0f, 8.0f * delta);
        clutchVal = Lerp(clutchVal, clutchPressed ? 1.0f : 0.0f, clutchPressed ? 15.0f * delta : 5.0f * delta);

        bool engineOn = carModel->engineOn;
        if (engineOn && carModel->fuel > 0 && rpm > 500) {
            carModel->fuel -= (rpm * 0.00002f) * delta;
            if (carModel->fuel <= 0) { carModel->fuel = 0; carModel->engineOn = false; }
        }

        float currentRatio = (currentGear == 0) ? 0.0f : gearRatios[currentGear];
        float totalRatio = currentRatio * finalDrive;
        float wheelRPM = (speed / wheelRadius) * 9.549f; 
        float transRPM = fabs(wheelRPM * totalRatio); 
        float tractionForce = 0; 
        float clutchEngagement = 1.0f - clutchVal; 

        if (engineOn && !isStartingEngine) {
            float actualGas = (rpm >= 7200) ? 0.0f : gasVal;
            if (currentGear == 0 || clutchEngagement < 0.1f) {
                rpm = Lerp(rpm, 800 + (actualGas * 6500), 5.0f * delta);
            } else {
                if (clutchEngagement >= 0.9f) {
                    rpm = Lerp(rpm, transRPM, 10.0f * delta);
                    if (rpm < 400 && actualGas < 0.1f && fabs(speed) < 2.0f) { 
                        carModel->engineOn = false; hasStalled = true; rpm = 0; 
                    } else if (rpm < 800 && actualGas > 0.1f) { 
                        rpm = 800 + (actualGas * 1500); 
                    }
                } else { 
                    rpm = Lerp(800 + (actualGas * 6500), transRPM, clutchEngagement); 
                }
                float engineTorque = GetEngineTorque(rpm) * actualGas;
                if (actualGas < 0.05f) engineTorque = -((rpm / 1000.0f) * 30.0f); 
                tractionForce = (engineTorque * clutchEngagement * totalRatio) / wheelRadius;
            }
            rpm = Clamp(rpm, 0.0f, 7100.0f);
        } else { 
            rpm = Lerp(rpm, 0.0f, 5.0f * delta); 
        }

        UpdateAdvancedAudio(engineOn);

        float dragForce = 0.5f * 1.2f * 2.2f * 0.3f * (speed * speed) * ((speed > 0) - (speed < 0));
        float rollingRes = fabs(speed) > 0.1f ? 15.0f * mass * 9.81f * 0.015f * ((speed > 0) - (speed < 0)) : 0.0f;
        float brakeForce = fabs(speed) > 0.1f ? brakeVal * 15000.0f * ((speed > 0) - (speed < 0)) : (brakeVal > 0.1f ? (speed = 0) : 0);
        float acceleration = (tractionForce - dragForce - rollingRes - brakeForce) / mass;

        if (fabs(speed) < 0.1f && fabs(acceleration) < 0.5f && tractionForce == 0) { speed = 0; acceleration = 0; }
        speed += acceleration * delta;

        float turnAmount = 0.0f;
        if (fabs(speed) > 0.1f) {
            float wheelBase = 2.6f; 
            float steerAngleRad = steeringAngle * maxSteering; 
            float turnRate = (speed / wheelBase) * tanf(steerAngleRad);
            turnRate = Clamp(turnRate, -5.0f, 5.0f);
            turnAmount = turnRate * delta;
            heading += turnAmount;
        }

        Vector3 velocityVec = { sinf(heading) * speed * delta, 0.0f, cosf(heading) * speed * delta };
        Vector3 nextPos = Vector3Add(carModel->position, velocityVec);
        
        bool canMove = true;
        
        // Capsule Collision (الجدران)
        Vector3 carForward = { sinf(heading), 0.0f, cosf(heading) };
        Vector3 frontPos = Vector3Add(nextPos, Vector3Scale(carForward, 1.2f));
        Vector3 backPos = Vector3Add(nextPos, Vector3Scale(carForward, -1.2f));

        Vector3 outNormal; float outDepth;
        if (GameCollision::CapsuleIntersect(frontPos, Vector3Add(frontPos, {0, 1, 0}), 1.0f, outNormal, outDepth)) {
            nextPos = Vector3Add(nextPos, Vector3Scale(outNormal, outDepth)); speed *= -0.3f;
        }
        if (GameCollision::CapsuleIntersect(backPos, Vector3Add(backPos, {0, 1, 0}), 1.0f, outNormal, outDepth)) {
            nextPos = Vector3Add(nextPos, Vector3Scale(outNormal, outDepth)); speed *= -0.3f;
        }

        if (CheckCarToCarCollision(nextPos)) {
            speed *= -0.5f; 
            canMove = false;
        }

        // Raycast للأرض
        Vector3 rayHit;
        if (GameCollision::Raycast((Vector3){carModel->position.x, carModel->position.y + 2.0f, carModel->position.z}, (Vector3){0, -1, 0}, rayHit) && canMove) {
            carModel->position = nextPos;
            carModel->position.y = rayHit.y; 
            carModel->rotation.y = heading;
            carModel->quaternion = QuaternionFromEuler(0, heading, 0); // YXZ
        }

        // غبار السيارات
        if (fabs(speed) > 2.0f && GetRandomValue(0, 100) < 40) {
            Vector3 dustOffset = Vector3RotateByQuaternion({(GetRandomValue(0, 100)/100.0f - 0.5f) * 1.5f, 0.05f, -1.8f}, carModel->quaternion);
            dustParticles.push_back({ Vector3Add(carModel->position, dustOffset), 1.0f, 0.25f });
        }
        for (int i = dustParticles.size() - 1; i >= 0; i--) {
            dustParticles[i].life -= delta * 1.5f;
            dustParticles[i].scale += delta * 0.8f;
            if (dustParticles[i].life <= 0) dustParticles.erase(dustParticles.begin() + i);
        }

        kmh = abs(round(speed * 3.6f));

        // الكاميرا (Damping & Shake)
        float shake = (kmh * 0.0002f) + (rpm > 6800 ? 0.02f : 0.0f);
        float shakeX = sinf(GetTime() * 50.0f) * shake; 
        float shakeY = cosf(GetTime() * 40.0f) * shake;

        if (!isDraggingCamera) {
            float tYaw = heading + PI;
            float diff = tYaw - targetCameraYaw;
            diff = atan2f(sinf(diff), cosf(diff));
            targetCameraYaw += diff * fmaxf(3.0f, 8.0f - (kmh * 0.01f)) * delta;
            targetCameraPitch = Lerp(targetCameraPitch, 0.15f, 5.0f * delta);
        } else { targetCameraYaw += turnAmount; }

        float camDistSafe = fmaxf(6.5f, 7.0f + (kmh * 0.005f)); 
        Vector3 idealCamPos = carModel->position;
        idealCamPos.y += 1.5f;
        idealCamPos.x += camDistSafe * sinf(currentCameraYaw) * cosf(currentCameraPitch) + shakeX;
        idealCamPos.y += camDistSafe * sinf(currentCameraPitch) + shakeY;
        idealCamPos.z += camDistSafe * cosf(currentCameraYaw) * cosf(currentCameraPitch);

        camera.position = Vector3Lerp(camera.position, idealCamPos, 20.0f * delta);
        camera.target = (Vector3){ carModel->position.x, carModel->position.y + 1.5f, carModel->position.z };
    }

    // ==========================================
    // نظام الـ UI للسيارة (C++ Immediate Mode)
    // ==========================================
    void UpdateAndDrawUI(int screenWidth, int screenHeight) {
        if (!isDriving) return;

        int touches = GetTouchPointCount();
        inputs.gas = false; inputs.brake = false; clutchPressed = false;
        bool isSteering = false;
        bool isTouchingGear = false;

        Rectangle gasRec = { 20.0f, (float)screenHeight - 130.0f, 60.0f, 110.0f };
        Rectangle brakeRec = { 100.0f, (float)screenHeight - 130.0f, 60.0f, 110.0f };
        Rectangle clutchRec = { 180.0f, (float)screenHeight - 130.0f, 60.0f, 110.0f };
        Vector2 steerCenter = { screenWidth - 130.0f, screenHeight - 130.0f };
        float steerRadius = 90.0f;
        
        Rectangle gearRec = { 20.0f, screenHeight / 2.0f - 80.0f, 150.0f, 160.0f };

        // فحص اللمس لجميع العناصر
        for (int i = 0; i < touches; i++) {
            Vector2 tPos = GetTouchPosition(i);

            if (CheckCollisionPointRec(tPos, gasRec)) inputs.gas = true;
            if (CheckCollisionPointRec(tPos, brakeRec)) inputs.brake = true;
            if (CheckCollisionPointRec(tPos, clutchRec)) clutchPressed = true;

            // عجلة القيادة
            if (CheckCollisionPointCircle(tPos, steerCenter, steerRadius)) {
                isSteering = true;
                if (steeringTouchId == -1) {
                    steeringTouchId = i;
                    startSteerAngle = atan2f(tPos.y - steerCenter.y, tPos.x - steerCenter.x) * (180.0f / PI);
                } else if (steeringTouchId == i) {
                    float currentAngle = atan2f(tPos.y - steerCenter.y, tPos.x - steerCenter.x) * (180.0f / PI);
                    float rotDiff = currentAngle - startSteerAngle;
                    if (rotDiff > 180.0f) rotDiff -= 360.0f; if (rotDiff < -180.0f) rotDiff += 360.0f;
                    currentSteerRotation = Clamp(currentSteerRotation + rotDiff, -450.0f, 450.0f);
                    steeringAngle = -(currentSteerRotation / 450.0f);
                    startSteerAngle = currentAngle;
                }
            }

            // الجير بوكس
            if (CheckCollisionPointRec(tPos, gearRec)) {
                isTouchingGear = true;
                if (gearTouchId == -1) { gearTouchId = i; isDraggingGear = true; }
                if (gearTouchId == i) {
                    gearKnobPos.x = Clamp(((tPos.x - gearRec.x) / gearRec.width) * 100.0f, 0.0f, 100.0f);
                    gearKnobPos.y = Clamp(((tPos.y - gearRec.y) / gearRec.height) * 100.0f, 0.0f, 100.0f);
                }
            }
        }

        // إرجاع الدركسون
        if (!isSteering) {
            steeringTouchId = -1;
            currentSteerRotation = Lerp(currentSteerRotation, 0.0f, 0.1f); // 0.1 factor per frame approx
            steeringAngle = -(currentSteerRotation / 450.0f);
        }

        // إرجاع الجير لأقرب نقطة
        if (!isTouchingGear && isDraggingGear) {
            isDraggingGear = false; gearTouchId = -1;
            std::string closestGear = "N"; float minDistance = 9999.0f;
            for (auto const& [gear, pos] : gearPositions) {
                float dist = hypotf(gearKnobPos.x - pos.x, gearKnobPos.y - pos.y);
                if (dist < minDistance) { minDistance = dist; closestGear = gear; }
            }
            int newGearNum = (closestGear == "N") ? 0 : std::stoi(closestGear);
            if (newGearNum == -1 && speed > 2.0f) { closestGear = "N"; newGearNum = 0; }
            gearKnobPos = gearPositions[closestGear]; currentGear = newGearNum;
        }

        // =======================
        // الرسم (Drawing)
        // =======================
        DrawTextureEx(gasTex, {gasRec.x, gasRec.y}, 0.0f, 1.0f, inputs.gas ? WHITE : GRAY);
        DrawTextureEx(brakeTex, {brakeRec.x, brakeRec.y}, 0.0f, 1.0f, inputs.brake ? WHITE : GRAY);
        DrawTextureEx(clutchTex, {clutchRec.x, clutchRec.y}, 0.0f, 1.0f, clutchPressed ? WHITE : GRAY);

        // رسم الدركسون بدوران
        Rectangle source = { 0, 0, (float)steeringTex.width, (float)steeringTex.height };
        Rectangle dest = { steerCenter.x, steerCenter.y, steerRadius*2, steerRadius*2 };
        Vector2 origin = { steerRadius, steerRadius };
        DrawTexturePro(steeringTex, source, dest, origin, currentSteerRotation, WHITE);

        // رسم الجير بوكس
        DrawRectangleRounded(gearRec, 0.1f, 10, (Color){20, 20, 20, 210});
        DrawRectangleLinesEx(gearRec, 3, DARKGRAY);
        DrawRectangle(gearRec.x + gearRec.width*0.2f, gearRec.y + gearRec.height*0.46f, gearRec.width*0.6f, gearRec.height*0.08f, DARKGRAY);
        DrawRectangle(gearRec.x + gearRec.width*0.2f, gearRec.y + gearRec.height*0.15f, gearRec.width*0.06f, gearRec.height*0.70f, DARKGRAY);
        DrawRectangle(gearRec.x + gearRec.width*0.5f, gearRec.y + gearRec.height*0.15f, gearRec.width*0.06f, gearRec.height*0.70f, DARKGRAY);
        DrawRectangle(gearRec.x + gearRec.width*0.8f, gearRec.y + gearRec.height*0.46f, gearRec.width*0.06f, gearRec.height*0.39f, DARKGRAY);
        
        DrawText("1", gearRec.x + gearRec.width*0.23f - 5, gearRec.y + gearRec.height*0.06f - 10, 20, LIGHTGRAY);
        DrawText("2", gearRec.x + gearRec.width*0.23f - 5, gearRec.y + gearRec.height*0.94f - 10, 20, LIGHTGRAY);
        DrawText("3", gearRec.x + gearRec.width*0.53f - 5, gearRec.y + gearRec.height*0.06f - 10, 20, LIGHTGRAY);
        DrawText("4", gearRec.x + gearRec.width*0.53f - 5, gearRec.y + gearRec.height*0.94f - 10, 20, LIGHTGRAY);
        DrawText("R", gearRec.x + gearRec.width*0.83f - 5, gearRec.y + gearRec.height*0.94f - 10, 20, LIGHTGRAY);

        Vector2 knobActualPos = { gearRec.x + (gearRec.width * gearKnobPos.x / 100.0f), gearRec.y + (gearRec.height * gearKnobPos.y / 100.0f) };
        DrawCircleV(knobActualPos, 17.5f, RED);

        // عدادات السرعة والـ RPM
        std::string rpmStr = std::to_string((int)rpm) + " RPM";
        std::string speedStr = std::to_string(kmh) + " KM/H";
        DrawText(rpmStr.c_str(), screenWidth/2 - MeasureText(rpmStr.c_str(), 24)/2, screenHeight - 80, 24, rpm > 6500 ? RED : WHITE);
        DrawText(speedStr.c_str(), screenWidth/2 - MeasureText(speedStr.c_str(), 32)/2, screenHeight - 50, 32, GREEN);

        // شريط البنزين
        DrawRectangle(screenWidth/2 - 100, screenHeight - 15, 200, 12, (Color){0,0,0,150});
        DrawRectangle(screenWidth/2 - 100, screenHeight - 15, (int)(200.0f * (carModel->fuel / 100.0f)), 12, YELLOW);
    }
}
