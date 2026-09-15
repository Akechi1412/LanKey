#include "tests/unit/engine_registry.h"

#if defined(LANKEY_ENGINE_OPENKEY)
#include "core/engine/OpenKeyEngineAdapter.h"
#endif

// Adapters are toggled via the CMake option LANKEY_ENGINE_<NAME> (see core/CMakeLists.txt)
// so the tests can be built without pulling the whole upstream tree.

namespace lankey::tests {

std::vector<EngineCandidate> registeredEngines() {
    std::vector<EngineCandidate> engines;
#if defined(LANKEY_ENGINE_OPENKEY)
    engines.push_back({"openkey", [] {
        return std::unique_ptr<core::IVietnameseEngine>(
            std::make_unique<core::engine::OpenKeyEngineAdapter>());
    }});
#endif
    return engines;
}

}  // namespace lankey::tests
