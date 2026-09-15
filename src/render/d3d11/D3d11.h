#pragma once

#include "Graphics.h"

namespace AlphaRing::Render::D3d11 {
    bool Initialize();

    void* GetFunction(int index);

    // Called once per frame (from the Present hook) so the splitscreen-composite fix in
    // Hook.cpp knows which player slot the next composite draw belongs to.
    void ResetCompositeFrameCounter();
}
