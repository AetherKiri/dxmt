#pragma once

// Debug-only tracing helpers for bringing up the Direct3D 9 layer.  Every call
// site is guarded by a per-name budget taken from the environment, so one build
// can serve several traces (MSE_TRACE_API, MSE_DUMP_DRAW, ...) and the default
// build stays silent.

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

namespace dxmt {

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
