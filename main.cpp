#include "raylib.h"
#include "raymath.h"
#include <iostream>
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
// المتغيرات العامة (Global State)
// ==========================================
Camera3D camera = { 0 };
Vector3 playerPos = { 0.0f, 0.0f, 0.0f };
bool isDead = false;
float currentCameraYaw = 0.0f;
float targetCameraYaw = 0.0f;
float currentCameraPitch = 0.3f;
float targetCameraPitch = 0.3f;
bool isDraggingCamera = false;
Vector3 playerVelocity = { 0.0f, 0.0f, 0.0f };
float playerVisualRotation = 0.0f; 

extern int totalAlivePlayers; 
extern int myKillCount;       

struct EnvironmentObject {
    Vector3 position;
    BoundingBox bounds;
};
std::vector<EnvironmentObject> environmentObjects;
std::vector<CarObject*> gameCars;

const float cameraDist = 1.8f;
bool isAirborne = false;
bool isFiring = false;
bool isScoped = false;

enum PlayerStance { STANCE_STAND, STANCE_CROUCH, STANCE_PRONE };
PlayerStance currentStance = STANCE_STAND;

bool isTransitioning = false;
bool isEditMode = false;
bool canEnterCar = false; 

// تم رفع سرعة استجابة اللاعب ودورانه
struct PlayerSettings {
    float walkSpeed = 6.0f;
    float runSpeed = 12.0f;
    float rotationSpeed = 12.0f; 
} playerSettings;

struct JoystickData {
    bool active = false;
    float x = 0.0f;
    float y = 0.0f;
    float distance = 0.0f;
} joystickData;

// 🔥 تم رفع حساسية الكاميرا بمقدار 3 أضعاف لتصبح سلسة وسريعة
struct EngineSettings {
    float cameraSens = 0.015f; 
    float cameraSensScoped = 0.005f;
    int targetFPS = 60; 
} gameSettings;

enum BtnType { BTN_FIRE, BTN_SCOPE, BTN_JUMP, BTN_CROUCH, BTN_PRONE, BTN_ENTER, BTN_COUNT };
struct HUDElement {
    Texture2D tex;
    Rectangle rect; 
    float scale = 1.0f;
    float opacity = 0.8f;
    bool isPressed = false;
};
HUDElement hudBtns[BTN_COUNT];

enum LODLevel { LOD_HIGH, LOD_MED, LOD_LOW, LOD_HIDDEN };

inline LODLevel GetLODLevel(float distSqr) {
    if (distSqr < 3600.0f)  return LOD_HIGH;
    if (distSqr < 14400.0f) return LOD_MED; 
    if (distSqr < 40000.0f) return LOD_LOW; 
    return LOD_HIDDEN;                      
}

inline bool FastConeCulling(Vector3 pos, Vector3 camPos, Vector3 camForward, float safeRadiusSqr) {
    if (Vector3DistanceSqr(pos, camPos) < safeRadiusSqr) return true;
    Vector3 dirToObj = Vector3Normalize(Vector3Subtract(pos, camPos));
    return Vector3DotProduct(dirToObj, camForward) > 0.2f; 
}

void InitHUD(int sw, int sh) {
    auto SetupBtn = [&](BtnType type, std::string path, float xPct, float yPct, float size) {
        hudBtns[type].tex = LoadTexture(path.c_str());
        hudBtns[type].rect = { (xPct/100.0f) * sw, (yPct/100.0f) * sh, size, size };
        hudBtns[type].scale = 1.0f;
        hudBtns[type].opacity = 0.8f;
    };

    SetupBtn(BTN_FIRE, "hud/fire.png", 82, 70, 100);       
    SetupBtn(BTN_SCOPE, "hud/scope.png", 82, 45, 80);  
    SetupBtn(BTN_JUMP, "hud/jump.png", 92, 80, 80);
    SetupBtn(BTN_CROUCH, "hud/crouch.png", 82, 88, 80);
    SetupBtn(BTN_PRONE, "hud/prone.png", 72, 88, 80);
    SetupBtn(BTN_ENTER, "hud/enter_car.png", 72, 50, 80); 
}

void DrawHUD(int sw, int sh) {
    extern int playerHealth; 
    
    DrawRectangle(sw/2 - 125, sh - (sh * 0.05f), 250, 8, Fade(BLACK, 0.6f));
    DrawRectangle(sw/2 - 125, sh - (sh * 0.05f), (int)(250 * (CombatSystem::playerHealth / 100.0f)), 8, GREEN);

    DrawRectangle(sw - 220, 20, 200, 65, Fade(BLACK, 0.5f));
    DrawRectangleLines(sw - 220, 20, 200, 65, Fade(WHITE, 0.2f));

    DrawText("ALIVE", sw - 200, 30, 14, YELLOW);
    DrawText(TextFormat("%02d", NPCSystem::totalAlivePlayers), sw - 140, 27, 22, WHITE);

    DrawLine(sw - 110, 28, sw - 110, 75, Fade(WHITE, 0.2f));

    DrawText("KILLS", sw - 90, 30, 14, RED);
    DrawText(TextFormat("%02d", NPCSystem::myKillCount), sw - 40, 27, 22, WHITE);

    for (int i = 0; i < BTN_COUNT; i++) {
        if (i == BTN_ENTER && !canEnterCar && !isEditMode) continue;
        
        Color tint = Fade(hudBtns[i].isPressed ? GRAY : WHITE, hudBtns[i].opacity);
        Rectangle dest = { hudBtns[i].rect.x, hudBtns[i].rect.y, hudBtns[i].rect.width * hudBtns[i].scale, hudBtns[i].rect.height * hudBtns[i].scale };
        Vector2 origin = { dest.width/2, dest.height/2 };
        
        DrawTexturePro(hudBtns[i].tex, {0, 0, (float)hudBtns[i].tex.width, (float)hudBtns[i].tex.height}, dest, origin, 0.0f, tint);
    }
}

int main() {
    // 🔥 تم إزالة MSAA 4X القاتل لكرت الشاشة 🔥
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_FULLSCREEN_MODE);
    InitWindow(0, 0, "PUBG Mobile Native - Performance Optimized");
    SetTargetFPS(gameSettings.targetFPS); 

    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    camera.position = (Vector3){ 0.0f, 2.0f, 4.0f };
    camera.target = (Vector3){ 0.0f, 1.5f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 75.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    bool isLoading = true;
    float loadProgress = 0.0f;
    Texture2D logoTex = LoadTexture("logo.png");
    
    Texture2D groundTex = LoadTexture("ground.png");
    SetTextureWrap(groundTex, TEXTURE_WRAP_REPEAT); 
    
    // 🔥 تم تقليل مضلعات الأرضية من 5000 إلى 2 مثلث فقط! 🔥
    Mesh planeMesh = GenMeshPlane(1000.0f, 1000.0f, 1, 1); 
    for (int i = 0; i < planeMesh.vertexCount; i++) {
        planeMesh.texcoords[i*2] *= 100.0f;     
        planeMesh.texcoords[i*2 + 1] *= 100.0f; 
    }
    UpdateMeshBuffer(planeMesh, 1, planeMesh.texcoords, planeMesh.vertexCount * 2 * sizeof(float), 0);
    Model groundModel = LoadModelFromMesh(planeMesh);
    groundModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = groundTex;
    groundModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE; 

    Model playerModel = LoadModel("player.glb");
    int animsCount = 0;
    ModelAnimation* playerAnimations = LoadModelAnimations("player.glb", &animsCount);

    Model houseModelHigh = LoadModel("house.glb");
    Model carModel = LoadModel("car2.glb");
    
    struct HouseInst { Vector3 pos; };
    std::vector<HouseInst> houses = {
        {{10, 0, -15}}, {{30, 0, -40}}, {{20, 0, 20}}, {{-20, 0, -20}}
    };

    for (auto h : houses) {
        GameCollision::AddCollider(houseModelHigh, MatrixTranslate(h.pos.x, h.pos.y, h.pos.z));
    }

    std::vector<Vector3> carSpawns = { {-20, 0, -10}, {-10, 0, 10}, {10, 0, 10} };
    for (auto pos : carSpawns) {
        CarObject* car = new CarObject();
        car->position = pos;
        car->quaternion = QuaternionIdentity();
        car->active = true;
        car->exactSize = {2.0f, 1.5f, 4.5f}; 
        gameCars.push_back(car);
    }

    CarEngine::Init(gameCars);
    CombatSystem::Init();
    NPCSystem::Init();
    InitHUD(screenWidth, screenHeight);
    
    Texture2D scopeUI = LoadTexture("hud/reddot.png");

    double startTime = GetTime();
    Vector3 currentCameraTarget = playerPos;
    bool isFirstFrame = true;
    
    float animFrameCounter = 0.0f;
    int lastIntAnimFrame = -1; 
    int currentAnimIndex = GameAnimations::GetIndex("idle_20");

    int cameraTouchId = -1;
    Vector2 lastCameraTouchPos = { -1.0f, -1.0f };

    Vector3 lastRaycastTarget = {0};
    float cachedCamSafeDist = cameraDist;

    while (!WindowShouldClose()) {
        
        // 🔥 تم إلغاء القيد الخانق 0.05 لكي لا تعمل اللعبة كعرض بطيء 🔥
        float delta = GetFrameTime();
        if (delta > 0.1f) delta = 0.1f; 

        if (isLoading) {
            loadProgress += delta * 40.0f; 
            if (loadProgress >= 100.0f) {
                loadProgress = 100.0f;
                if (GetTime() - startTime > 2.5) isLoading = false;
            }

            BeginDrawing();
            ClearBackground((Color){10, 10, 12, 255});
            float pulse = 1.0f + sinf(GetTime() * 4.0f) * 0.05f;
            Rectangle dest = { screenWidth/2.0f, screenHeight/2.0f - 100.0f, logoTex.width * pulse, logoTex.height * pulse };
            DrawTexturePro(logoTex, { 0, 0, (float)logoTex.width, (float)logoTex.height }, dest, { dest.width/2, dest.height/2 }, 0.0f, WHITE);
            DrawRectangle(screenWidth/2 - 200, screenHeight/2 + 50, (int)(400 * (loadProgress/100.0f)), 12, (Color){0, 212, 255, 255});
            EndDrawing();
            continue;
        }

        canEnterCar = false;
        CarObject* nearestCar = nullptr;
        float minCarDistSqr = 16.0f; 
        
        for (auto car : gameCars) {
            float distSqr = Vector3DistanceSqr(playerPos, car->position);
            if (distSqr < minCarDistSqr) {
                minCarDistSqr = distSqr;
                nearestCar = car;
                canEnterCar = true;
            }
        }

        int touches = GetTouchPointCount();
        joystickData.active = false;
        for (int i=0; i<BTN_COUNT; i++) hudBtns[i].isPressed = false;
        
        Vector2 pointers[10];
        int pointerCount = 0;
        if (touches > 0) {
            for(int i=0; i<touches; i++) pointers[pointerCount++] = GetTouchPosition(i);
        } else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            pointers[pointerCount++] = GetMousePosition();
        }

        bool handledCameraDrag = false;
        bool handledJoystick = false;

        for (int i = 0; i < pointerCount; i++) {
            Vector2 p = pointers[i];
            bool hitButton = false;
            
            if (!isEditMode) {
                for (int b = 0; b < BTN_COUNT; b++) {
                    Rectangle hitBox = { hudBtns[b].rect.x - (hudBtns[b].rect.width/2), hudBtns[b].rect.y - (hudBtns[b].rect.height/2), hudBtns[b].rect.width, hudBtns[b].rect.height };
                    if (CheckCollisionPointRec(p, hitBox)) {
                        hudBtns[b].isPressed = true;
                        hitButton = true;
                        
                        if (b == BTN_FIRE) isFiring = true;
                        else if (b == BTN_SCOPE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { isScoped = !isScoped; camera.fovy = isScoped ? 35.0f : 75.0f; }
                        else if (b == BTN_JUMP && !isAirborne && currentStance == STANCE_STAND) { playerVelocity.y = 8.5f; isAirborne = true; }
                        else if (b == BTN_CROUCH && !isAirborne && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) currentStance = (currentStance == STANCE_STAND) ? STANCE_CROUCH : STANCE_STAND;
                        else if (b == BTN_PRONE && !isAirborne && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) currentStance = (currentStance == STANCE_PRONE) ? STANCE_STAND : STANCE_PRONE;
                        else if (b == BTN_ENTER && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && nearestCar) { CarEngine::Enter(nearestCar); }
                    }
                }
            }

            if (hitButton) continue;

            if (p.x < screenWidth / 2.0f && !CarEngine::isDriving && !handledJoystick) {
                joystickData.active = true;
                handledJoystick = true;
                Vector2 joyCenter = { 200.0f, (float)screenHeight - 200.0f }; 
                float dist = Vector2Distance(p, joyCenter);
                joystickData.distance = std::min(dist, 80.0f);
                Vector2 dir = Vector2Normalize(Vector2Subtract(p, joyCenter));
                joystickData.x = dir.x; joystickData.y = -dir.y; 
            }
            else if (p.x >= screenWidth / 2.0f && !CarEngine::isDriving) {
                if (cameraTouchId == -1 || cameraTouchId == i) {
                    cameraTouchId = i;
                    isDraggingCamera = true;
                    handledCameraDrag = true;

                    if (lastCameraTouchPos.x == -1.0f) lastCameraTouchPos = p; 

                    Vector2 touchDelta = Vector2Subtract(p, lastCameraTouchPos);
                    lastCameraTouchPos = p;

                    float sens = isScoped ? gameSettings.cameraSensScoped : gameSettings.cameraSens;
                    targetCameraYaw -= touchDelta.x * sens;
                    targetCameraPitch = Clamp(targetCameraPitch + touchDelta.y * sens, -0.5f, 1.5f);
                }
            }
        }
        
        if (!handledCameraDrag) {
            isDraggingCamera = false;
            cameraTouchId = -1;
            lastCameraTouchPos = { -1.0f, -1.0f };
        }

        if (!hudBtns[BTN_FIRE].isPressed) isFiring = false;

        float dampingFactor = 1.0f - expf(-25.0f * delta);
        currentCameraYaw += atan2f(sinf(targetCameraYaw - currentCameraYaw), cosf(targetCameraYaw - currentCameraYaw)) * dampingFactor;
        currentCameraPitch += (targetCameraPitch - currentCameraPitch) * dampingFactor;

        if (animsCount > 0 && !isDead) {
            std::string targetAnim = "idle_20"; 
            float currentAnimSpeed = 1.0f; 

            if (CarEngine::isDriving) targetAnim = "drive_8";
            else if (isAirborne) targetAnim = "jump_22";
            else if (joystickData.active) {
                if (currentStance == STANCE_STAND) { targetAnim = "run_32"; currentAnimSpeed = 1.2f; } 
                else if (currentStance == STANCE_CROUCH) { targetAnim = "c_for_3"; currentAnimSpeed = 1.0f; }
                else if (currentStance == STANCE_PRONE) { targetAnim = "p_for_27"; currentAnimSpeed = 1.0f; }
            } 
            else {
                if (currentStance == STANCE_STAND) targetAnim = "idle_20";
                else if (currentStance == STANCE_CROUCH) targetAnim = "idle_c_18";
                else if (currentStance == STANCE_PRONE) targetAnim = "idle_p_19";
            }

            int desiredAnimIndex = GameAnimations::GetIndex(targetAnim);

            if (desiredAnimIndex != currentAnimIndex) {
                animFrameCounter = 0.0f;
                currentAnimIndex = desiredAnimIndex;
                lastIntAnimFrame = -1; 
            }

            if (currentAnimIndex < animsCount) {
                animFrameCounter += 30.0f * delta * currentAnimSpeed; 
                if (animFrameCounter >= playerAnimations[currentAnimIndex].keyframeCount) animFrameCounter = 0.0f;
                
                int currentIntFrame = (int)animFrameCounter;
                if (currentIntFrame != lastIntAnimFrame) {
                    UpdateModelAnimation(playerModel, playerAnimations[currentAnimIndex], currentIntFrame);
                    lastIntAnimFrame = currentIntFrame;
                }
            }
        }

        NPCSystem::Update(delta);
        CombatSystem::Update(delta);

        if (CarEngine::isDriving) {
            CarEngine::Update(delta);
        } 
        else if (!isDead) {
            playerVelocity.y -= 25.0f * delta; 
            Vector3 forwardVector = { -sinf(currentCameraYaw), 0.0f, -cosf(currentCameraYaw) };
            Vector3 rightVector = { cosf(currentCameraYaw), 0.0f, -sinf(currentCameraYaw) };

            float targetSpeedX = 0.0f;
            float targetSpeedZ = 0.0f;

            if (joystickData.active && !isTransitioning) {
                float strength = fminf(joystickData.distance / 80.0f, 1.0f);
                float speedMod = (currentStance == STANCE_PRONE) ? 0.2f : ((currentStance == STANCE_CROUCH) ? 0.4f : 1.0f);
                float currentSpeed = Lerp(playerSettings.walkSpeed, playerSettings.runSpeed, strength) * speedMod;

                Vector3 moveVector = { 
                    (forwardVector.x * joystickData.y) + (rightVector.x * joystickData.x), 0.0f,
                    (forwardVector.z * joystickData.y) + (rightVector.z * joystickData.x)
                };

                if (Vector3LengthSqr(moveVector) > 0.01f) {
                    moveVector = Vector3Normalize(moveVector);
                    targetSpeedX = moveVector.x * currentSpeed;
                    targetSpeedZ = moveVector.z * currentSpeed;
    
                    float targetPlayerRot = atan2f(moveVector.x, moveVector.z) * (180.0f / PI);
                    float angleDiff = targetPlayerRot - playerVisualRotation;
                    while (angleDiff < -180.0f) angleDiff += 360.0f;
                    while (angleDiff > 180.0f) angleDiff -= 360.0f;
                    playerVisualRotation += angleDiff * playerSettings.rotationSpeed * delta;
                }
            }

            playerVelocity.x = Lerp(playerVelocity.x, targetSpeedX, 12.0f * delta);
            playerVelocity.z = Lerp(playerVelocity.z, targetSpeedZ, 12.0f * delta);

            const float pWidth = 1.0f; const float pLength = 2.4f;
            for (auto car : gameCars) {
                Vector3 localPos = Vector3Subtract(playerPos, car->position); 
                if (fabs(localPos.x) < pWidth && fabs(localPos.z) < pLength && localPos.y > -1 && localPos.y < 2) {
                    float pushX = pWidth - fabs(localPos.x);
                    float pushZ = pLength - fabs(localPos.z);
                    if (pushX < pushZ) playerPos.x += (localPos.x > 0 ? pushX : -pushX);
                    else playerPos.z += (localPos.z > 0 ? pushZ : -pushZ);
                }
            }

            GameCollision::ResolveMovement(playerPos, playerVelocity, delta);

            if (playerPos.y <= 0.0f) {
                playerPos.y = 0.0f;
                if (playerVelocity.y < 0) playerVelocity.y = 0;
                isAirborne = false;
            } else isAirborne = true;
        }

        if (!isDead && !CarEngine::isDriving) {
            Vector3 idealTargetPos = { playerPos.x, playerPos.y + 1.5f, playerPos.z };

            if (isFirstFrame) {
                currentCameraTarget = idealTargetPos;
                isFirstFrame = false;
            } else {
                currentCameraTarget = Vector3Lerp(currentCameraTarget, idealTargetPos, fminf(20.0f * delta, 1.0f)); 
            }

            Vector3 idealCameraPos = { 
                currentCameraTarget.x + cameraDist * sinf(currentCameraYaw) * cosf(currentCameraPitch), 
                currentCameraTarget.y + cameraDist * sinf(currentCameraPitch), 
                currentCameraTarget.z + cameraDist * cosf(currentCameraYaw) * cosf(currentCameraPitch) 
            };

            Vector3 camDirToIdeal = Vector3Normalize(Vector3Subtract(idealCameraPos, currentCameraTarget));
            float expectedDist = Vector3Distance(idealCameraPos, currentCameraTarget);
            
            if (Vector3DistanceSqr(currentCameraTarget, lastRaycastTarget) > 0.01f || isDraggingCamera) {
                Vector3 hitPoint;
                if (GameCollision::Raycast(currentCameraTarget, camDirToIdeal, hitPoint)) {
                    float hitDist = Vector3Distance(currentCameraTarget, hitPoint);
                    cachedCamSafeDist = (hitDist < expectedDist) ? fmaxf(0.5f, hitDist - 0.2f) : expectedDist;
                } else {
                    cachedCamSafeDist = expectedDist;
                }
                lastRaycastTarget = currentCameraTarget;
            }

            Vector3 targetCamPos = Vector3Add(currentCameraTarget, Vector3Scale(camDirToIdeal, cachedCamSafeDist));
            camera.position = Vector3Lerp(camera.position, targetCamPos, fminf(40.0f * delta, 1.0f));
            camera.target = currentCameraTarget;
        }

        Vector3 camForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

        BeginDrawing();
        ClearBackground((Color){ 135, 206, 235, 255 }); 

        BeginMode3D(camera);
            
            DrawModel(groundModel, {0,0,0}, 1.0f, WHITE);

            for (auto& h : houses) {
                if (FastConeCulling(h.pos, camera.position, camForward, 2500.0f)) {
                    DrawModel(houseModelHigh, h.pos, 1.0f, WHITE);
                }
            }

            for (auto car : gameCars) {
                if (FastConeCulling(car->position, camera.position, camForward, 400.0f)) {
                    DrawModelEx(carModel, car->position, {0,1,0}, car->quaternion.y * (180.0f/PI), {1.2f, 1.2f, 1.2f}, WHITE);
                }
            }

            if (!isScoped && !isDead && !CarEngine::isDriving) {
                DrawModelEx(playerModel, playerPos, {0,1,0}, playerVisualRotation, {0.015f, 0.015f, 0.015f}, WHITE);
            }

            NPCSystem::Draw3D(); 
            CombatSystem::Draw3D();

        EndMode3D();

        if (CarEngine::isDriving) {
            CarEngine::UpdateAndDrawUI(screenWidth, screenHeight);
        } else {
            if (isScoped && !isDead) {
                DrawTexturePro(scopeUI, {0, 0, (float)scopeUI.width, (float)scopeUI.height}, {0, 0, (float)screenWidth, (float)screenHeight}, {0,0}, 0.0f, WHITE);
            } else if (!isDead) {
                DrawCircle(screenWidth/2, screenHeight/2, 2.0f, RED);
            }
            
            if (!isDead) {
                DrawHUD(screenWidth, screenHeight);
                NPCSystem::DrawUI(screenWidth, screenHeight, camera);
                CombatSystem::DrawUI(screenWidth, screenHeight);

                if (joystickData.active) {
                    Vector2 joyCenter = { 200.0f, (float)screenHeight - 200.0f };
                    DrawCircleV(joyCenter, 100.0f, Fade(WHITE, 0.1f)); 
                    DrawCircleV({joyCenter.x + (joystickData.x * joystickData.distance), joyCenter.y + (-joystickData.y * joystickData.distance)}, 40.0f, Fade(WHITE, 0.5f)); 
                }
            } else {
                DrawText("YOU DIED", screenWidth/2 - MeasureText("YOU DIED", 40)/2, screenHeight/2, 40, RED);
            }
        }

        EndDrawing();
    }

    if (animsCount > 0) UnloadModelAnimations(playerAnimations, animsCount);
    UnloadModel(playerModel);
    UnloadModel(houseModelHigh);
    UnloadModel(carModel);
    UnloadModel(groundModel);
    UnloadTexture(groundTex);
    UnloadTexture(logoTex);
    UnloadTexture(scopeUI);
    for (int i=0; i<BTN_COUNT; i++) UnloadTexture(hudBtns[i].tex);
    
    CloseWindow();
    return 0;
}
