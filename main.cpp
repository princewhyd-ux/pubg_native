#include "raylib.h"
#include "raymath.h"
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <algorithm>

// ==========================================
// تضمين ملفات الأنظمة الأخرى
// ==========================================
#include "animations.h"
#include "collision.h"
#include "combat.h"
#include "driving.h"
#include "npc.h"

// ==========================================
// المتغيرات العامة (Global State) 
// ==========================================
Camera3D camera = { 0 };
Vector3 playerPos = { 0.0f, 0.0f, 0.0f }; // ✅ تم التعديل ليظهر اللاعب بجوار المنازل
bool isDead = false;
float currentCameraYaw = 0.0f;
float targetCameraYaw = 0.0f;
float currentCameraPitch = 0.3f;
float targetCameraPitch = 0.3f;
bool isDraggingCamera = false;
Vector3 playerVelocity = { 0.0f, 0.0f, 0.0f };

// هياكل البيئة
struct EnvironmentObject {
    Vector3 position;
    BoundingBox bounds;
};
std::vector<EnvironmentObject> environmentObjects;
std::vector<CarObject*> gameCars;

// ==========================================
// متغيرات النظام الداخلية (Main.cpp Scope)
// ==========================================
const float cameraDist = 1.8f;
bool isAirborne = false;
bool isFiring = false;
bool isScoped = false;
std::string playerStance = "STAND";
bool isTransitioning = false;
bool isEditMode = false;
bool settingsOpen = false;

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

// إعدادات اللعبة
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
// دوال مساعدة لإنشاء الواجهة والـ HUD
// ==========================================
void InitHUD(int sw, int sh) {
    auto AddBtn = [&](std::string id, std::string path, float xPct, float yPct, float size) {
        HUDElement btn;
        btn.id = id;
        btn.tex = LoadTexture(path.c_str());
        btn.rect = { (xPct/100.0f) * sw, (yPct/100.0f) * sh, size, size };
        hudElements[id] = btn;
    };

    // ✅ إزالة "assets/" لكي تعمل على الأندرويد مباشرة
    AddBtn("btn-fire", "hud/fire.png", 80, 70, 80);
    AddBtn("btn-scope", "hud/scope_btn.png", 85, 50, 60);
    AddBtn("btn-jump", "hud/jump.png", 90, 85, 60);
    AddBtn("btn-crouch", "hud/crouch.png", 80, 85, 60);
    AddBtn("btn-prone", "hud/prone.png", 70, 85, 60);
    AddBtn("btn-enter", "hud/enter_car.png", 70, 50, 60); 
}

void DrawHUD(int sw, int sh) {
    // رسم شريط الصحة للاعب
    extern int playerHealth; // من نظام combat.cpp
    DrawRectangle(sw/2 - 125, sh - (sh * 0.05f), 250, 8, Fade(BLACK, 0.6f));
    DrawRectangle(sw/2 - 125, sh - (sh * 0.05f), (int)(250 * (CombatSystem::playerHealth / 100.0f)), 8, GREEN);

    for (auto& pair : hudElements) {
        HUDElement& btn = pair.second;
        if (btn.id == "btn-enter" && CarEngine::carModel == nullptr && !isEditMode) continue;
        
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
    // ✅ 1. جعل الشاشة بكامل العرض (Full Screen) تلقائياً
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
    Texture2D logoTex = LoadTexture("logo.png"); // مسار الأندرويد الصحيح
    
    // ✅ 2. إعداد الأرضية بشكل صحيح بالصورة (Texture)
    Texture2D groundTex = LoadTexture("ground.png");
    SetTextureWrap(groundTex, TEXTURE_WRAP_REPEAT); 
    Mesh planeMesh = GenMeshPlane(1000.0f, 1000.0f, 50, 50); 
    Model groundModel = LoadModelFromMesh(planeMesh);
    groundModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = groundTex;

    // 3. تحميل بيئة اللعب (المجسمات)
    Model playerModel = LoadModel("player.glb");
    playerModel.transform = MatrixScale(0.01f, 0.01f, 0.01f);

    Model houseModel = LoadModel("house.glb");
    Model carModel = LoadModel("car2.glb");

    // ✅ 4. إرسال المجسمات لنظام الفيزياء ليمنع اختراق الجدران
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
        car->exactSize = {2.0f, 1.5f, 4.5f}; // هام لمنع الانهيار
        gameCars.push_back(car);
    }

    // ✅ 5. تهيئة الأنظمة جميعها (مهم لكي يعمل إطلاق النار والذكاء الاصطناعي)
    CarEngine::Init(gameCars);
    CombatSystem::Init();
    NPCSystem::Init();
    InitHUD(screenWidth, screenHeight);
    
    Texture2D scopeUI = LoadTexture("hud/reddot.png");

    double startTime = GetTime();
    Vector3 currentCameraTarget = playerPos;
    bool isFirstFrame = true;

    // حلقة اللعبة الأساسية
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

        // تحديث الأنظمة المستقلة (الأعداء والقتال)
        NPCSystem::Update(delta);
        CombatSystem::Update(delta);

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
                        else if (pair.first == "btn-enter" && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            CarObject* closest = nullptr; float minDist = 3.5f;
                            for (auto car : gameCars) {
                                float d = Vector3Distance(playerPos, car->position);
                                if (d < minDist) { minDist = d; closest = car; }
                            }
                            if (closest && !CarEngine::isDriving) CarEngine::Enter(closest);
                        }
                    }
                }
            }

            if (hitButton) continue;

            if (p.x < screenWidth / 2.0f && !CarEngine::isDriving) {
                joystickData.active = true;
                Vector2 joyCenter = { 150.0f, screenHeight - 150.0f }; 
                float dist = Vector2Distance(p, joyCenter);
                joystickData.distance = std::min(dist, 50.0f);
                Vector2 dir = Vector2Normalize(Vector2Subtract(p, joyCenter));
                joystickData.x = dir.x;
                joystickData.y = -dir.y; 
            }
            else if (p.x >= screenWidth / 2.0f && !handledCameraDrag) {
                isDraggingCamera = true;
                handledCameraDrag = true;
                Vector2 deltaM = GetMouseDelta();
                float sens = isScoped ? GameSettings["cameraSensScoped"] : GameSettings["cameraSens"];
                targetCameraYaw -= deltaM.x * sens;
                targetCameraPitch += deltaM.y * sens;
                targetCameraPitch = Clamp(targetCameraPitch, -0.5f, 1.5f);
            }
        }
        
        if (!handledCameraDrag) isDraggingCamera = false;
        if (!hudElements["btn-fire"].isPressed) isFiring = false;

        float dampingFactor = 1.0f - expf(-25.0f * delta);
        float yawDiff = targetCameraYaw - currentCameraYaw;
        yawDiff = atan2f(sinf(yawDiff), cosf(yawDiff));
        currentCameraYaw += yawDiff * dampingFactor;
        
        float pitchDiff = targetCameraPitch - currentCameraPitch;
        currentCameraPitch += pitchDiff * dampingFactor;

        // ==========================================
        // تحديث حالة اللاعب
        // ==========================================
        if (CarEngine::isDriving) {
            CarEngine::Update(delta);
        } 
        else if (!isDead) {
            playerVelocity.y -= 25.0f * delta; 

            Vector3 forwardVector = { -sinf(currentCameraYaw), 0.0f, -cosf(currentCameraYaw) };
            Vector3 rightVector = { cosf(currentCameraYaw), 0.0f, -sinf(currentCameraYaw) };

            if (joystickData.active && !isTransitioning) {
                float strength = fminf(joystickData.distance / 50.0f, 1.0f);
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
                } else {
                    playerVelocity.x = 0; playerVelocity.z = 0;
                }
            } else {
                playerVelocity.x = 0; playerVelocity.z = 0;
            }

            // التصادم مع السيارات 
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

            // ✅ تفعيل الاصطدام الفعلي للبيئة (الجدران والمنازل - Hover)
            GameCollision::ResolveMovement(playerPos, playerVelocity, delta);

            if (playerPos.y <= 0.0f) {
                playerPos.y = 0.0f;
                if (playerVelocity.y < 0) playerVelocity.y = 0;
                isAirborne = false;
            } else {
                isAirborne = true;
            }

            Vector3 idealTargetPos = playerPos;
            idealTargetPos.y += 1.5f;

            if (isFirstFrame) {
                currentCameraTarget = idealTargetPos;
                isFirstFrame = false;
            } else {
                currentCameraTarget = Vector3Lerp(currentCameraTarget, idealTargetPos, 20.0f * delta);
            }

            float offsetX = cameraDist * sinf(currentCameraYaw) * cosf(currentCameraPitch);
            float offsetY = cameraDist * sinf(currentCameraPitch);
            float offsetZ = cameraDist * cosf(currentCameraYaw) * cosf(currentCameraPitch);
            Vector3 idealCameraPos = { currentCameraTarget.x + offsetX, currentCameraTarget.y + offsetY, currentCameraTarget.z + offsetZ };

            // ✅ منع الكاميرا من اختراق المنازل والجدران (Raycast)
            Vector3 camDirToIdeal = Vector3Normalize(Vector3Subtract(idealCameraPos, currentCameraTarget));
            float expectedDist = Vector3Distance(idealCameraPos, currentCameraTarget);
            Vector3 hitPoint;
            
            if (GameCollision::Raycast(currentCameraTarget, camDirToIdeal, hitPoint)) {
                float hitDist = Vector3Distance(currentCameraTarget, hitPoint);
                if (hitDist < expectedDist) {
                    idealCameraPos = Vector3Add(currentCameraTarget, Vector3Scale(camDirToIdeal, fmaxf(0.1f, hitDist - 0.2f)));
                }
            }

            camera.position = idealCameraPos; 
            camera.target = currentCameraTarget;
        }

        // ==========================================
        // الرسم (Rendering Pass)
        // ==========================================
        BeginDrawing();
        ClearBackground((Color){ 214, 234, 248, 255 }); 

        BeginMode3D(camera);
            
            // ✅ رسم الأرضية بصورة الـ Texture بدلا من اللون السادة
            DrawModel(groundModel, {0,0,0}, 1.0f, WHITE);

            // رسم البيئة
            DrawModel(houseModel, (Vector3){10, 0, -15}, 1.0f, WHITE);
            DrawModel(houseModel, (Vector3){30, 0, -40}, 1.0f, WHITE);
            DrawModel(houseModel, (Vector3){20, 0, 20}, 1.0f, WHITE);
            DrawModel(houseModel, (Vector3){-20, 0, -20}, 1.0f, WHITE);

            // رسم السيارات
            for (auto car : gameCars) {
                DrawModelEx(carModel, car->position, {0,1,0}, car->quaternion.y * (180.0f/PI), {1.2f, 1.2f, 1.2f}, WHITE);
            }

            // رسم اللاعب
            if (!isScoped && !isDead && !CarEngine::isDriving) {
                DrawModelEx(playerModel, playerPos, {0,1,0}, currentCameraYaw * (180.0f/PI), {0.01f, 0.01f, 0.01f}, WHITE);
            }

            // رسم الأعداء والرصاص
            NPCSystem::Draw3D();
            CombatSystem::Draw3D();

        EndMode3D();

        // ==========================================
        // رسم الواجهة 2D
        // ==========================================
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
                Vector2 joyCenter = { 150.0f, screenHeight - 150.0f };
                DrawCircleV(joyCenter, 60.0f, Fade(WHITE, 0.1f));
                DrawCircleV({joyCenter.x + (joystickData.x * joystickData.distance), 
                             joyCenter.y + (-joystickData.y * joystickData.distance)}, 25.0f, Fade(WHITE, 0.5f));
            }
        }

        EndDrawing();
    }

    // التنظيف
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
