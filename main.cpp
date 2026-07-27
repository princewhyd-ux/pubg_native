#include "raylib.h"
#include "raymath.h"
#include <iostream>
#include <vector>
#include <string>
#include <map>
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
int totalAlivePlayers = 7; 
int myKillCount = 0;       

// هياكل البيئة
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
std::string playerStance = "STAND"; // STAND, CROUCH, PRONE
bool isTransitioning = false;
bool isEditMode = false;
bool settingsOpen = false;
bool canEnterCar = false; 

struct PlayerSettings {
    float walkSpeed = 4.2f;
    float runSpeed = 8.5f;
    float rotationSpeed = 15.0f;
} playerSettings;

struct JoystickData {
    bool active = false;
    float x = 0.0f;
    float y = 0.0f;
    float distance = 0.0f;
} joystickData;

std::map<std::string, float> GameSettings = {
    {"cameraSens", 0.005f},
    {"cameraSensScoped", 0.002f},
    {"gyroEnabled", 0.0f},
    {"gyroSens", 0.01f},
    {"gyroSensScoped", 0.005f}
};

struct HUDElement {
    std::string id;
    Texture2D tex;
    Rectangle rect; 
    float scale = 1.0f;
    float opacity = 0.8f;
    bool isPressed = false;
};
std::map<std::string, HUDElement> hudElements;
std::string selectedEditBtn = "";

// ==========================================
// دوال الواجهة والـ HUD
// ==========================================
void InitHUD(int sw, int sh) {
    auto AddBtn = [&](std::string id, std::string path, float xPct, float yPct, float size) {
        HUDElement btn;
        btn.id = id;
        btn.tex = LoadTexture(path.c_str());
        btn.rect = { (xPct/100.0f) * sw, (yPct/100.0f) * sh, size, size };
        hudElements[id] = btn;
    };

    AddBtn("btn-fire", "hud/fire.png", 82, 70, 100);       
    AddBtn("btn-scope", "hud/scope.png", 82, 45, 80);  
    AddBtn("btn-jump", "hud/jump.png", 92, 80, 80);
    AddBtn("btn-crouch", "hud/crouch.png", 82, 88, 80);
    AddBtn("btn-prone", "hud/prone.png", 72, 88, 80);
    AddBtn("btn-enter", "hud/enter_car.png", 72, 50, 80); 
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

    for (auto& pair : hudElements) {
        HUDElement& btn = pair.second;
        if (btn.id == "btn-enter" && !canEnterCar && !isEditMode) continue;
        
        Color tint = Fade(btn.isPressed ? GRAY : WHITE, btn.opacity);
        Rectangle dest = { btn.rect.x, btn.rect.y, btn.rect.width * btn.scale, btn.rect.height * btn.scale };
        Vector2 origin = { dest.width/2, dest.height/2 };
        
        if (isEditMode) {
            DrawRectangleLinesEx({dest.x - origin.x, dest.y - origin.y, dest.width, dest.height}, 2, 
                selectedEditBtn == btn.id ? GREEN : Fade(WHITE, 0.8f));
        }

        DrawTexturePro(btn.tex, {0, 0, (float)btn.tex.width, (float)btn.tex.height}, dest, origin, 0.0f, tint);
    }
}

// ==========================================
// الدالة الرئيسية (حلقة اللعبة)
// ==========================================
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI | FLAG_FULLSCREEN_MODE);
    InitWindow(0, 0, "PUBG Mobile Native - C++ Engine");
    SetTargetFPS(120);

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
    Mesh planeMesh = GenMeshPlane(1000.0f, 1000.0f, 50, 50); 
    for (int i = 0; i < planeMesh.vertexCount; i++) {
        planeMesh.texcoords[i*2] *= 100.0f;     
        planeMesh.texcoords[i*2 + 1] *= 100.0f; 
    }
    UpdateMeshBuffer(planeMesh, 1, planeMesh.texcoords, planeMesh.vertexCount * 2 * sizeof(float), 0);
    Model groundModel = LoadModelFromMesh(planeMesh);
    groundModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = groundTex;
    groundModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE; 

    // تحميل اللاعب والأنيميشن
    Model playerModel = LoadModel("player.glb");
    int animsCount = 0;
    ModelAnimation* playerAnimations = LoadModelAnimations("player.glb", &animsCount);

    Model houseModel = LoadModel("house.glb");
    Model carModel = LoadModel("car2.glb");

    GameCollision::AddCollider(houseModel, MatrixTranslate(10, 0, -15));
    GameCollision::AddCollider(houseModel, MatrixTranslate(30, 0, -40));
    GameCollision::AddCollider(houseModel, MatrixTranslate(20, 0, 20));
    GameCollision::AddCollider(houseModel, MatrixTranslate(-20, 0, -20));

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
    
    // متغيرات الأنميشن
    int animFrameCounter = 0;
    int currentAnimIndex = 19; // الافتراضي: الوقوف (idle_20 -> Index 19)
    int previousAnimIndex = 19;

    // متغيرات تتبع اللمس المتعدد
    int cameraTouchId = -1;
    Vector2 lastCameraTouchPos = { -1.0f, -1.0f };

    while (!WindowShouldClose()) {
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
            Rectangle source = { 0, 0, (float)logoTex.width, (float)logoTex.height };
            Rectangle dest = { screenWidth/2.0f, screenHeight/2.0f - 100.0f, logoTex.width * pulse, logoTex.height * pulse };
            Vector2 origin = { dest.width/2, dest.height/2 };
            DrawTexturePro(logoTex, source, dest, origin, 0.0f, WHITE);

            DrawRectangle(screenWidth/2 - 200, screenHeight/2 + 50, 400, 12, Fade(WHITE, 0.05f));
            DrawRectangle(screenWidth/2 - 200, screenHeight/2 + 50, (int)(400 * (loadProgress/100.0f)), 12, (Color){0, 212, 255, 255});
            DrawText(TextFormat("Loading World... %d%%", (int)loadProgress), screenWidth/2 - 80, screenHeight/2 + 75, 20, WHITE);
            
            EndDrawing();
            continue;
        }

        NPCSystem::Update(delta);
        CombatSystem::Update(delta);

        canEnterCar = false;
        CarObject* nearestCar = nullptr;
        float minCarDist = 3.5f;
        for (auto car : gameCars) {
            float dist = Vector3Distance(playerPos, car->position);
            if (dist < minCarDist) {
                minCarDist = dist;
                nearestCar = car;
                canEnterCar = true;
            }
        }

        int touches = GetTouchPointCount();
        joystickData.active = false;
        
        for(auto& pair : hudElements) pair.second.isPressed = false;
        
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
                for (auto& pair : hudElements) {
                    Rectangle hitBox = { pair.second.rect.x - (pair.second.rect.width/2), 
                                         pair.second.rect.y - (pair.second.rect.height/2), 
                                         pair.second.rect.width, pair.second.rect.height };
                    if (CheckCollisionPointRec(p, hitBox)) {
                        pair.second.isPressed = true;
                        hitButton = true;
                        
                        if (pair.first == "btn-fire") { isFiring = true; }
                        else if (pair.first == "btn-scope" && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { 
                            isScoped = !isScoped; camera.fovy = isScoped ? 35.0f : 75.0f; 
                        }
                        else if (pair.first == "btn-jump" && !isAirborne && playerStance == "STAND") {
                            playerVelocity.y = 8.5f; isAirborne = true;
                        }
                        else if (pair.first == "btn-crouch" && !isAirborne && !isTransitioning && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            playerStance = (playerStance == "STAND") ? "CROUCH" : "STAND";
                        }
                        else if (pair.first == "btn-prone" && !isAirborne && !isTransitioning && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            playerStance = (playerStance == "PRONE") ? "STAND" : "PRONE";
                        }
                        else if (pair.first == "btn-enter" && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && nearestCar) {
                            CarEngine::Enter(nearestCar);
                        }
                    }
                }
            }

            if (hitButton) continue;

            // منطقة الجويستيك (اليسار)
            if (p.x < screenWidth / 2.0f && !CarEngine::isDriving && !handledJoystick) {
                joystickData.active = true;
                handledJoystick = true;
                Vector2 joyCenter = { 200.0f, (float)screenHeight - 200.0f }; 
                float dist = Vector2Distance(p, joyCenter);
                joystickData.distance = std::min(dist, 80.0f);
                Vector2 dir = Vector2Normalize(Vector2Subtract(p, joyCenter));
                joystickData.x = dir.x;
                joystickData.y = -dir.y; 
            }
            // الكاميرا باللمس المتعدد (اليمين)
            else if (p.x >= screenWidth / 2.0f && !CarEngine::isDriving) {
                if (cameraTouchId == -1 || cameraTouchId == i) {
                    cameraTouchId = i;
                    isDraggingCamera = true;
                    handledCameraDrag = true;

                    if (lastCameraTouchPos.x == -1.0f) {
                        lastCameraTouchPos = p; 
                    }

                    Vector2 touchDelta = Vector2Subtract(p, lastCameraTouchPos);
                    lastCameraTouchPos = p;

                    float sens = isScoped ? GameSettings["cameraSensScoped"] : GameSettings["cameraSens"];
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

        if (!hudElements["btn-fire"].isPressed) isFiring = false;

        float dampingFactor = 1.0f - expf(-25.0f * delta);
        currentCameraYaw += atan2f(sinf(targetCameraYaw - currentCameraYaw), cosf(targetCameraYaw - currentCameraYaw)) * dampingFactor;
        currentCameraPitch += (targetCameraPitch - currentCameraPitch) * dampingFactor;

        // ==========================================
        // 🔥 محرك الأنميشن الذكي (Animation State Machine) 🔥
        // ==========================================
        if (animsCount > 0 && !isDead) {
            
            if (CarEngine::isDriving) {
                currentAnimIndex = 7; // الأنميشن رقم 8 (drive_8) في القائمة
            } 
            else if (isAirborne) {
                currentAnimIndex = 21; // الأنميشن رقم 22 (jump_22) في القائمة
            }
            else if (joystickData.active) {
                // حالة الحركة
                if (playerStance == "STAND") currentAnimIndex = 31; // ركض (run_32)
                else if (playerStance == "CROUCH") currentAnimIndex = 2; // مشي قرفصاء (c_for_3)
                else if (playerStance == "PRONE") currentAnimIndex = 26; // زحف (p_for_27)
            } 
            else {
                // حالة الثبات
                if (playerStance == "STAND") currentAnimIndex = 19; // وقوف (idle_20)
                else if (playerStance == "CROUCH") currentAnimIndex = 17; // ثبات قرفصاء (idle_c_18)
                else if (playerStance == "PRONE") currentAnimIndex = 18; // ثبات انبطاح (idle_p_19)
            }

            // إعادة تعيين الفريمات إذا تغير الأنميشن لكي لا يحدث ارتعاش
            if (currentAnimIndex != previousAnimIndex) {
                animFrameCounter = 0;
                previousAnimIndex = currentAnimIndex;
            }

            // حماية لتجنب الأعطال (Index Out of Bounds)
            if (currentAnimIndex < animsCount) {
                animFrameCounter++;
                if (animFrameCounter >= playerAnimations[currentAnimIndex].keyframeCount) {
                    animFrameCounter = 0;
                }
                UpdateModelAnimation(playerModel, playerAnimations[currentAnimIndex], animFrameCounter);
            }
        }

        if (CarEngine::isDriving) {
            CarEngine::Update(delta);
        } 
        else if (!isDead) {
            playerVelocity.y -= 25.0f * delta; 

            Vector3 forwardVector = { -sinf(currentCameraYaw), 0.0f, -cosf(currentCameraYaw) };
            Vector3 rightVector = { cosf(currentCameraYaw), 0.0f, -sinf(currentCameraYaw) };

            if (joystickData.active && !isTransitioning) {
                float strength = fminf(joystickData.distance / 80.0f, 1.0f);
                float speedMod = (playerStance == "PRONE") ? 0.2f : ((playerStance == "CROUCH") ? 0.4f : 1.0f);
                float currentSpeed = Lerp(playerSettings.walkSpeed, playerSettings.runSpeed, strength) * speedMod;

                Vector3 moveVector = { 
                    (forwardVector.x * joystickData.y) + (rightVector.x * joystickData.x),
                    0.0f,
                    (forwardVector.z * joystickData.y) + (rightVector.z * joystickData.x)
                };

                if (Vector3LengthSqr(moveVector) > 0.01f) {
                    moveVector = Vector3Normalize(moveVector);
                    playerVelocity.x = moveVector.x * currentSpeed;
                    playerVelocity.z = moveVector.z * currentSpeed;
    
                    float targetPlayerRot = atan2f(moveVector.x, moveVector.z) * (180.0f / PI);
                    float angleDiff = targetPlayerRot - playerVisualRotation;
                    while (angleDiff < -180.0f) angleDiff += 360.0f;
                    while (angleDiff > 180.0f) angleDiff -= 360.0f;
                    playerVisualRotation += angleDiff * playerSettings.rotationSpeed * delta;
              
                } else {
                    playerVelocity.x = 0; playerVelocity.z = 0;
                }
            } else {
                playerVelocity.x = 0; playerVelocity.z = 0;
            }

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
            } else {
                isAirborne = true;
            }

            Vector3 idealTargetPos = { playerPos.x, playerPos.y + 1.5f, playerPos.z };
            if (isFirstFrame) { currentCameraTarget = idealTargetPos; isFirstFrame = false; } 
            else { currentCameraTarget = Vector3Lerp(currentCameraTarget, idealTargetPos, 20.0f * delta); }

            Vector3 idealCameraPos = { 
                currentCameraTarget.x + cameraDist * sinf(currentCameraYaw) * cosf(currentCameraPitch), 
                currentCameraTarget.y + cameraDist * sinf(currentCameraPitch), 
                currentCameraTarget.z + cameraDist * cosf(currentCameraYaw) * cosf(currentCameraPitch) 
            };

            Vector3 targetCamPos = idealCameraPos;
            Vector3 camDirToIdeal = Vector3Normalize(Vector3Subtract(idealCameraPos, currentCameraTarget));
            float expectedDist = Vector3Distance(idealCameraPos, currentCameraTarget);
            Vector3 hitPoint;
            
            if (GameCollision::Raycast(currentCameraTarget, camDirToIdeal, hitPoint)) {
                float hitDist = Vector3Distance(currentCameraTarget, hitPoint);
                if (hitDist < expectedDist) {
                    float safeDist = fmaxf(0.5f, hitDist - 0.2f);
                    targetCamPos = Vector3Add(currentCameraTarget, Vector3Scale(camDirToIdeal, safeDist));
                }
            }

            camera.position = Vector3Lerp(camera.position, targetCamPos, 30.0f * delta);
            camera.target = currentCameraTarget;
        }

        // ==========================================
        // الرسم (Rendering)
        // ==========================================
        BeginDrawing();
        ClearBackground((Color){ 135, 206, 235, 255 }); 

        BeginMode3D(camera);
            
            DrawModel(groundModel, {0,0,0}, 1.0f, WHITE);

            DrawModel(houseModel, (Vector3){10, 0, -15}, 1.0f, WHITE);
            DrawModel(houseModel, (Vector3){30, 0, -40}, 1.0f, WHITE);
            DrawModel(houseModel, (Vector3){20, 0, 20}, 1.0f, WHITE);
            DrawModel(houseModel, (Vector3){-20, 0, -20}, 1.0f, WHITE);

            for (auto car : gameCars) {
                DrawModelEx(carModel, car->position, {0,1,0}, car->quaternion.y * (180.0f/PI), {1.2f, 1.2f, 1.2f}, WHITE);
            }

            // رسم اللاعب بالأنيميشن الذكي (عدا إن كان يقود السيارة فيختفي ليركبها)
            if (!isScoped && !isDead && !CarEngine::isDriving) {
                DrawModelEx(playerModel, playerPos, {0,1,0}, playerVisualRotation, {0.015f, 0.015f, 0.015f}, WHITE);
            }

            NPCSystem::Draw3D();
            CombatSystem::Draw3D();

        EndMode3D();

        if (CarEngine::isDriving) {
            CarEngine::UpdateAndDrawUI(screenWidth, screenHeight);
        } else {
            if (isScoped) {
                DrawTexturePro(scopeUI, {0, 0, (float)scopeUI.width, (float)scopeUI.height}, 
                              {0, 0, (float)screenWidth, (float)screenHeight}, {0,0}, 0.0f, WHITE);
            } else {
                DrawCircle(screenWidth/2, screenHeight/2, 2.0f, RED);
            }
            
            DrawHUD(screenWidth, screenHeight);
            NPCSystem::DrawUI(screenWidth, screenHeight, camera);
            CombatSystem::DrawUI(screenWidth, screenHeight);

            if (joystickData.active) {
                Vector2 joyCenter = { 200.0f, (float)screenHeight - 200.0f };
                DrawCircleV(joyCenter, 100.0f, Fade(WHITE, 0.1f)); 
                DrawCircleV({joyCenter.x + (joystickData.x * joystickData.distance), 
                             joyCenter.y + (-joystickData.y * joystickData.distance)}, 40.0f, Fade(WHITE, 0.5f)); 
            }
        }

        EndDrawing();
    }

    if (animsCount > 0) UnloadModelAnimations(playerAnimations, animsCount);
    UnloadModel(playerModel);
    UnloadModel(houseModel);
    UnloadModel(carModel);
    UnloadModel(groundModel);
    UnloadTexture(groundTex);
    UnloadTexture(logoTex);
    UnloadTexture(scopeUI);
    for (auto& pair : hudElements) UnloadTexture(pair.second.tex);
    
    CloseWindow();
    return 0;
}
