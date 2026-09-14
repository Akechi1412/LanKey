#include "tests/unit/engine_registry.h"

// When an adapter exists, include it here and add it to the list below. Example:
//
//   #include "core/engine/OpenKeyEngineAdapter.h"
//   ...
//   {"openkey", [] { return std::make_unique<core::engine::OpenKeyEngineAdapter>(); }},
//
// Adapters are toggled via the CMake option LANKEY_ENGINE_<NAME> (see core/CMakeLists.txt)
// so the tests can be built without pulling the whole upstream tree.

namespace lankey::tests {

std::vector<EngineCandidate> registeredEngines() {
    std::vector<EngineCandidate> engines;
#if defined(LANKEY_ENGINE_OPENKEY)
    // engines.push_back({"openkey", [] { return std::make_unique<...>(); }});
#endif
#if defined(LANKEY_ENGINE_VKEY)
    // engines.push_back({"vkey", [] { return std::make_unique<...>(); }});
#endif
    return engines;
}

}  // namespace lankey::tests
