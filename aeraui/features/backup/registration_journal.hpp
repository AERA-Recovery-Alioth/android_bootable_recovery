/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <string>
#include <vector>
#include <sys/types.h>

namespace aeraui::appvault::journal {
struct Replacement {
  std::string path, bytes, context;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  bool require_original = false;
  std::string original_bytes = {};
};
// Production callers use /data/system. A private fixture root exercises the
// same path validation, durable writes, and replay without touching Android.
bool Replay(const std::string& system_root, std::string& error);
bool Commit(const std::string& system_root, const std::vector<Replacement>& files,
            std::string& error);
#ifdef AERA_JOURNAL_TESTING
// Fail after publication (0), after N durable replacements (N), after a
// target rename before its parent sync (1000 + N), or retirement (file count + 1).
void InterruptAfter(int replacements);
#endif
}  // namespace aeraui::appvault::journal
