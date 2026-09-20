#pragma once

// Debug-only tracing helpers for bringing up the Direct3D 9 layer.  Every call
// site is guarded by a per-name budget taken from the environment, so one build
// can serve several traces (MSE_TRACE_API, MSE_DUMP_DRAW, ...) and the default
// build stays silent.
//
// The guest side of this layer runs under the interpreter, so a debug hook that
// costs one getenv() (or one std::map lookup) per call is *not* free: with ~890
// draws and ~530 texture locks per frame it shows up in the guest instruction
// budget.  MSE_ENV_FLAG() resolves the variable once per call site, which turns
// the check into a single load and branch when the switch is off.

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

namespace dxmt {

// Resolve a boolean environment switch once per call site.  The switch is a
// process-lifetime constant, so caching it cannot change behaviour *within* a
// run, while removing the lookup from every frame.
#define MSE_ENV_FLAG(name)                                                     \
  ([]() -> bool {                                                             \
    static const bool mse_env_flag_ = getenv(name) != nullptr;                 \
    return mse_env_flag_;                                                      \
  }())

inline int &DebugTraceBudget(const char *name) {
  static std::map<std::string, int> budgets;
  auto it = budgets.find(name);
  if (it == budgets.end()) {
    const char *env = getenv(name);
    it = budgets.emplace(name, env ? atoi(env) : 0).first;
  }
  return it->second;
}

// Monotonic counter so traces from different call sites can be interleaved and
// still be ordered relative to each other.
inline uint64_t DebugTraceSeq() {
  static uint64_t seq = 0;
  return ++seq;
}

} // namespace dxmt
