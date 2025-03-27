#include "CommonUtils.h"
#include <string>
#include <algorithm> // Include this header for std::transform

namespace CommonUtils {
    std::wstring ToLower(const std::wstring& str) {
        std::wstring lowerStr = str;

        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::towlower);

        return lowerStr;
    }
}
