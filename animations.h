#pragma once
#include <string>
#include <vector>
#include <map>

namespace GameAnimations {
    
    // خريطة (قاموس) يربط اسم الأنميشن بالرقم (Index) الخاص به
    extern const std::map<std::string, int> animIndices;
    
    // قائمة الأنميشنات التي تعمل مرة واحدة
    extern const std::vector<std::string> once;

    // دالة لجلب رقم الأنميشن عن طريق اسمه
    int GetIndex(const std::string& animName);

    // دالة للتحقق مما إذا كان الأنميشن يعمل مرة واحدة فقط
    bool IsPlayOnce(const std::string& animName);
}
