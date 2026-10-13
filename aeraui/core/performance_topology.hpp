/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <algorithm>
#include <charconv>
#include <sched.h>
#include <sstream>
#include <string>
#include <vector>

namespace aeraui::performance {
struct CpuPolicy {
  std::string path;
  unsigned long max_frequency = 0;
  cpu_set_t cpus{};
};

inline bool ParseCpuList(std::string text, cpu_set_t *mask) {
  CPU_ZERO(mask);
  std::replace(text.begin(), text.end(), ',', ' ');
  std::istringstream input(text);
  std::string token;
  bool found = false;
  while (input >> token) {
    int first = 0, last = 0;
    const auto end = token.data() + token.size();
    const auto parsed = std::from_chars(token.data(), end, first);
    if (parsed.ec != std::errc{} || first < 0 || first >= CPU_SETSIZE) return false;
    last = first;
    if (parsed.ptr != end) {
      if (*parsed.ptr != '-') return false;
      const auto range = std::from_chars(parsed.ptr + 1, end, last);
      if (range.ec != std::errc{} || range.ptr != end || last < first || last >= CPU_SETSIZE)
        return false;
    }
    for (int cpu = first; cpu <= last; ++cpu) CPU_SET(cpu, mask);
    found = true;
  }
  return found;
}

inline bool SelectCpuPolicy(const std::vector<CpuPolicy>& policies,
                            const cpu_set_t& allowed, CpuPolicy *selected) {
  bool found = false;
  for (const auto& policy : policies) {
    cpu_set_t intersection;
    CPU_AND(&intersection, &policy.cpus, &allowed);
    if (CPU_COUNT(&intersection) == 0 || policy.max_frequency == 0) continue;
    if (!found || policy.max_frequency > selected->max_frequency) {
      *selected = policy;
      selected->cpus = intersection;
      found = true;
    }
  }
  return found;
}
}  // namespace aeraui::performance
