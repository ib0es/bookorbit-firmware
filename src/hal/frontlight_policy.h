#pragma once

// Pure frontlight helper logic shared between UI controls and host unit tests.
namespace FrontlightPolicy {

// Toggles brightness between 0 and `rememberedBrightness` (updating the
// remembered level whenever turning off from a positive brightness).
inline int toggleLevel(int currentBrightness, int& rememberedBrightness, int defaultOn = 50) {
    if (currentBrightness > 0) {
        rememberedBrightness = currentBrightness > 100 ? 100 : currentBrightness;
        return 0;
    }
    return (rememberedBrightness > 0 && rememberedBrightness <= 100) ? rememberedBrightness : defaultOn;
}

}  // namespace FrontlightPolicy
