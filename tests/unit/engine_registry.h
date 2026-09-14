#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/interfaces/IVietnameseEngine.h"

namespace lankey::tests {

// One engine adapter participating in the conformance test suite.
struct EngineCandidate {
    std::string name;  // used as the test suite name: EngineConformance_<name>
    std::function<std::unique_ptr<core::IVietnameseEngine>()> make;
};

// The list of available adapters. Phase 0: add OpenKeyEngineAdapter and VKeyEngineAdapter
// here (engine_registry.cpp), run the same test suite, compare results to pick the engine.
std::vector<EngineCandidate> registeredEngines();

}  // namespace lankey::tests
