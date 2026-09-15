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

// The list of available adapters. Every registered adapter runs the full conformance
// suite; add a new one here (engine_registry.cpp) to compare it against OpenKey.
std::vector<EngineCandidate> registeredEngines();

}  // namespace lankey::tests
