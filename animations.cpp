#include "animations.h"
#include <algorithm>

// ==========================================
// ملف إدارة الأنميشن (Animation Engine - C++ Native)
// ==========================================

namespace GameAnimations {

    // 🔥 القاموس الذكي: يربط كل اسم برقم الـ Index الحقيقي (رقم الصورة ناقص 1) 🔥
    const std::map<std::string, int> animIndices = {
        {"c_back_1", 0}, {"c_fire_2", 1}, {"c_for_3", 2}, {"c_left_4", 3}, {"c_right_5", 4},
        {"c_to_p_6", 5}, {"die_7", 6}, {"drive_8", 7}, {"ent_car_9", 8}, {"ex_car_10", 9},
        {"fall_idle_11", 10}, {"fire_idle_12", 11}, {"fire_13", 12}, {"grenade_b_14", 13}, 
        {"grenade_a_15", 14}, {"hit_16", 15}, {"grenade_17", 16}, {"idle_c_18", 17}, 
        {"idle_p_19", 18}, {"idle_20", 19}, {"j_run_21", 20}, {"jump_22", 21}, 
        {"kick_23", 22}, {"land_24", 23}, {"p_back_25", 24}, {"p_fire_26", 25}, 
        {"p_for_27", 26}, {"p_left_28", 27}, {"p_right_29", 28}, {"p_to_c_30", 29}, 
        {"power_31", 30}, {"run_32", 31}, {"stand_33", 32}, {"walk_34", 33}
    };

    // الأنميشنات التي يجب أن تعمل مرة واحدة فقط (مثل القفز والموت)
    const std::vector<std::string> once = {
        "p_to_c_30", "c_to_p_6", "jump_22", "j_run_21", "land_24", "die_7"
    };

    // ==========================================
    // دوال مساعدة لتعويض خصائص الـ JavaScript
    // ==========================================

    // جلب رقم الأنميشن بأمان
    int GetIndex(const std::string& animName) {
        auto it = animIndices.find(animName);
        if (it != animIndices.end()) {
            return it->second; // يرجع الرقم الحقيقي (مثلاً run_32 يرجع 31)
        }
        return 19; // في حال حدث خطأ أملائي في الاسم، يرجع أنميشن الوقوف (idle_20) كعامل أمان
    }

    // للتحقق مما إذا كان الأنميشن يعمل لمرة واحدة فقط
    bool IsPlayOnce(const std::string& animName) {
        return std::find(once.begin(), once.end(), animName) != once.end();
    }
}
