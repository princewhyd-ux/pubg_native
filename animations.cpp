#include <vector>
#include <string>
#include <algorithm>

#include "animations.h"
#include "collision.h"
#include "combat.h"
#include "driving.h"
#include "npc.h"


// ==========================================
// ملف إدارة الأنميشن (Animation Engine - C++ Native)
// ==========================================

namespace GameAnimations {
    
    // قائمة بكل أسماء الأنميشنات الموجودة في ملف player.glb
    const std::vector<std::string> list = {
        "idle_20", "walk_34", "run_32", "j_run_21", "jump_22", 
        "p_to_c_30", "idle_c_18", "fire_idle_12", "fire_13", 
        "idle_p_19", "c_to_p_6", "c_fire_2", "drive_8", 
        "fall_idle_11", "land_24", "p_fire_26",
        
        // الأنميشنات الجديدة
        "p_for_27", "c_back_1", "c_for_3", "p_left_28", "p_right_29", "die_7"
    };

    // الأنميشنات التي يجب أن تعمل مرة واحدة فقط (بدون تكرار)
    const std::vector<std::string> once = {
        "p_to_c_30", "c_to_p_6", "jump_22", "j_run_21", "land_24", "die_7"
    };

    // ==========================================
    // دوال مساعدة لتعويض خصائص الـ JavaScript
    // ==========================================

    // للتحقق مما إذا كان الأنميشن موجوداً في القائمة الأساسية
    inline bool IsInList(const std::string& animName) {
        return std::find(list.begin(), list.end(), animName) != list.end();
    }

    // للتحقق مما إذا كان الأنميشن يعمل لمرة واحدة فقط (LoopOnce)
    inline bool IsPlayOnce(const std::string& animName) {
        return std::find(once.begin(), once.end(), animName) != once.end();
    }
}
