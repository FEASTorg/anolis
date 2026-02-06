#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "automation/mode_manager.hpp"

namespace anolis::tests {

class MockModeManager : public automation::ModeManager {
public:
    MockModeManager() : automation::ModeManager(nullptr) {} // Pass nullptr registry if base ctor requires it
    virtual ~MockModeManager() = default;

    // We can't easily mock non-virtual methods, but looking at ModeManager,
    // we might need to rely on its real behavior or see if 'current_mode' is virtual.
    // If it's not virtual, we can't mock it with GMock directly unless we change the design 
    // or use a template.
    
    // However, for this test, we might just need to SET the mode.
    // Let's check ModeManager definition first.
};

}
