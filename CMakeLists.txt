cmake_minimum_required(VERSION 3.10)

# اسم المشروع (بديل وسم <title>)
project(PUBG_Mobile_TPS_Native)

# تحديد معيار C++ (نستخدم C++17 للأداء العالي)
set(CMAKE_CXX_STANDARD 17)

# تضمين مكتبة Raylib (بديل three.min.js و three-mesh-bvh)
find_package(raylib REQUIRED)

# جمع كل ملفات اللعبة معاً (البديل الحرفي لترتيب وسوم <script>)
set(GAME_SOURCES
    main.cpp
    settings.cpp
    animations.cpp
    collision.cpp
    driving.cpp
    combat.cpp
    npc.cpp
)

# بناء التطبيق التنفيذي (أو الـ APK)
add_executable(${PROJECT_NAME} ${GAME_SOURCES})

# ربط محرك Raylib بملفات اللعبة
target_link_libraries(${PROJECT_NAME} raylib)
