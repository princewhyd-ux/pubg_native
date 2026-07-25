#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <string>
#include <vector>

#include "animations.h"
#include "collision.h"
#include "combat.h"
#include "driving.h"
#include "npc.h"


// --- متغيرات الحالة العامة (مكافئة لنطاق window في JS) ---
float targetCameraYaw = 0.0f;
float targetCameraPitch = 0.3f;
float currentCameraYaw = 0.0f;
float currentCameraPitch = 0.3f;
bool isDraggingCamera = false;
const float cameraDist = 1.8f;

Vector3 playerVelocity = { 0.0f, 0.0f, 0.0f };
Vector3 currentCameraTarget = { 0.0f, 0.0f, 0.0f };
bool isAirborne = false;
bool isDead = false;
bool isFiring = false;
bool isScoped = false;
std::string playerStance = "STAND"; 
bool isTransitioning = false;
bool isFirstFrame = true;

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

int main() {
    // ==========================================
    // 1. إعداد الشاشة والواجهة الأساسية
    // ==========================================
    const int screenWidth = 1920;
    const int screenHeight = 1080;
    
    InitWindow(screenWidth, screenHeight, "PUBG Mobile TPS - Native C++ Engine");
    SetTargetFPS(120); // قفل الإطارات على 120 فريم حقيقي

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 2.0f, 4.0f };
    camera.target = (Vector3){ 0.0f, 1.5f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 75.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // حالة شاشة التحميل
    bool isLoading = true;
    float loadProgress = 0.0f;

    // مصفوفات البيئة والسيارات
    std::vector<Vector3> environmentPositions = {
        {10.0f, 0.0f, -15.0f},
        {30.0f, 0.0f, -40.0f},
        {20.0f, 0.0f, 20.0f},
        {-20.0f, 0.0f, -20.0f}
    };

    std::vector<Vector3> carPositions = {
        {-20.0f, 0.0f, -10.0f},
        {-10.0f, 0.0f, 10.0f},
        {10.0f, 0.0f, 10.0f}
    };

    Vector3 playerPos = { -200.0f, 0.0f, -200.0f };
    double startTime = GetTime();

    // ==========================================
    // حلقة التحديث الشاملة (مكافئة لدالة animate)
    // ==========================================
    while (!WindowShouldClose()) {
        float delta = GetFrameTime();
        if (delta > 0.1f) delta = 0.1f; 

        // معالجة شاشة التحميل (Loading Screen)
        if (isLoading) {
            loadProgress += delta * 60.0f; 
            if (loadProgress >= 100.0f) {
                loadProgress = 100.0f;
                if (GetTime() - startTime > 1.2) {
                    isLoading = false;
                }
            }
        } else {
            // ==========================================
            // 5. إعدادات الكاميرا والتحكم باللمس / الماوس
            // ==========================================
            Vector2 mousePos = GetMousePosition();
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0) {
                if (mousePos.x > (float)screenWidth / 2.0f) {
                    isDraggingCamera = true;
                    Vector2 mouseDelta = GetMouseDelta();
                    float currentSens = isScoped ? 0.002f : 0.005f;
                    targetCameraYaw -= mouseDelta.x * currentSens;
                    targetCameraPitch += mouseDelta.y * currentSens;
                    targetCameraPitch = Clamp(targetCameraPitch, -0.5f, 1.5f);
                }
            } else {
                isDraggingCamera = false;
            }

            if (isnan(targetCameraYaw)) targetCameraYaw = 0.0f;
            if (isnan(targetCameraPitch)) targetCameraPitch = 0.3f;

            // نعومة الكاميرا (Damping) المتوافقة مع 120Hz
            float dampingFactor = 1.0f - expf(-25.0f * delta);
            
            float yawDiff = targetCameraYaw - currentCameraYaw;
            yawDiff = atan2f(sinf(yawDiff), cosf(yawDiff));
            currentCameraYaw += yawDiff * dampingFactor;

            float pitchDiff = targetCameraPitch - currentCameraPitch;
            currentCameraPitch += pitchDiff * dampingFactor;

            // ==========================================
            // حركة اللاعب والفيزياء
            // ==========================================
            Vector3 forwardVector = { -sinf(currentCameraYaw), 0.0f, -cosf(currentCameraYaw) };
            Vector3 rightVector = { cosf(currentCameraYaw), 0.0f, -sinf(currentCameraYaw) };

            if (!isDead) {
                playerVelocity.y -= 25.0f * delta; 

                // محاكاة عصا التحكم (Joystick) عبر أزرار لوحة المفاتيح للاختبار
                if (IsKeyDown(KEY_W) || IsKeyDown(KEY_S) || IsKeyDown(KEY_A) || IsKeyDown(KEY_D)) {
                    joystickData.active = true;
                    joystickData.x = 0.0f; joystickData.y = 0.0f;
                    if (IsKeyDown(KEY_W)) joystickData.y += 1.0f;
                    if (IsKeyDown(KEY_S)) joystickData.y -= 1.0f;
                    if (IsKeyDown(KEY_A)) joystickData.x -= 1.0f;
                    if (IsKeyDown(KEY_D)) joystickData.x += 1.0f;
                    joystickData.distance = 40.0f;
                } else {
                    joystickData.active = false;
                }

                if (joystickData.active && !isTransitioning) {
                    float speedMod = (playerStance == "PRONE") ? 0.2f : ((playerStance == "CROUCH") ? 0.4f : 1.0f);
                    float currentSpeed = playerSettings.walkSpeed * speedMod;

                    Vector3 moveVector = { 
                        forwardVector.x * joystickData.y + rightVector.x * joystickData.x,
                        0.0f,
                        forwardVector.z * joystickData.y + rightVector.z * joystickData.x
                    };

                    if (Vector3LengthSqr(moveVector) > 0.01f) {
                        moveVector = Vector3Normalize(moveVector);
                        float targetRotation = atan2f(moveVector.x, moveVector.z);
                        
                        // تحديث موقع اللاعب وحركته
                        playerVelocity.x = moveVector.x * currentSpeed;
                        playerVelocity.z = moveVector.z * currentSpeed;
                    } else {
                        playerVelocity.x = 0.0f;
                        playerVelocity.z = 0.0f;
                    }
                } else {
                    playerVelocity.x = 0.0f;
                    playerVelocity.z = 0.0f;
                }

                playerPos.x += playerVelocity.x * delta;
                playerPos.y += playerVelocity.y * delta;
                playerPos.z += playerVelocity.z * delta;

                if (playerPos.y <= 0.0f) {
                    playerPos.y = 0.0f;
                    if (playerVelocity.y < 0.0f) playerVelocity.y = 0.0f;
                    isAirborne = false;
                }
            }

            // تحديث موقع الكاميرا والمستهدف
            Vector3 idealTargetPos = playerPos;
            idealTargetPos.y += 1.5f;
            if (isDead) idealTargetPos.y = 0.5f;

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

            camera.position = Vector3Lerp(camera.position, idealCameraPos, 1.0f - expf(-30.0f * delta));
            camera.target = currentCameraTarget;
        }

        // ==========================================
        // رسم العرض (Rendering Pass)
        // ==========================================
        BeginDrawing();
        ClearBackground((Color){ 214, 234, 248, 255 }); // لون السماء/الضباب

        BeginMode3D(camera);
            DrawPlane((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector2){ 50000.0f, 50000.0f }, LIGHTGRAY);

            for (const auto& pos : environmentPositions) {
                DrawCube(pos, 4.0f, 6.0f, 4.0f, BLUE);
                DrawCubeWires(pos, 4.0f, 6.0f, 4.0f, DARKBLUE);
            }

            for (const auto& pos : carPositions) {
                DrawCube(pos, 2.2f, 1.4f, 4.5f, RED);
                DrawCubeWires(pos, 2.2f, 1.4f, 4.5f, MAROON);
            }

            if (!isScoped && !isDead) {
                DrawCube(playerPos, 0.6f, 1.8f, 0.6f, DARKGREEN);
            }
        EndMode3D();

        // واجهة الـ HUD وشاشة التحميل
        DrawCircle(screenWidth / 2, screenHeight / 2, 2.0f, RED); // Crosshair

        if (isLoading) {
            DrawRectangle(0, 0, screenWidth, screenHeight, (Color){ 10, 10, 12, 255 });
            DrawText("PUBG Mobile TPS - C++ Native Engine", screenWidth / 2 - 220, screenHeight / 2 - 60, 24, WHITE);
            
            DrawRectangle(screenWidth / 2 - 200, screenHeight / 2 + 20, 400, 12, (Color){ 255, 255, 255, 20 });
            DrawRectangle(screenWidth / 2 - 200, screenHeight / 2 + 20, (int)(400 * (loadProgress / 100.0f)), 12, SKYBLUE);
            
            std::string progressStr = "جاري تجهيز العالم... " + std::to_string((int)loadProgress) + "%";
            DrawText(progressStr.c_str(), screenWidth / 2 - 70, screenHeight / 2 + 45, 16, WHITE);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
