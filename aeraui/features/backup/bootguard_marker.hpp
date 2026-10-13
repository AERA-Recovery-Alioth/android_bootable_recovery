/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <bootloader_message/bootloader_message.h>
#include <cstring>
#include <string>
namespace aeraui::appvault::bootguard {
enum class State { Clear, Writing, Registration, Error };
inline bool ValidIdentity(const std::string& identity) {
  if (identity.size() != 64) return false;
  for (char c : identity) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
inline State ParseMarker(const bootloader_message& message, std::string& identity) {
  identity.clear();
  const std::string recovery(message.recovery, sizeof(message.recovery));
  if (recovery.find("--aera-appvault") == std::string::npos) return State::Clear;
  if (std::string(message.command, sizeof(message.command)) !=
      std::string("boot-recovery", 13) + std::string(sizeof(message.command) - 13, '\0')) return State::Error;
  for (State state : {State::Writing, State::Registration}) {
    const std::string prefix = state == State::Writing ? "recovery\n--aera-appvault=writing-v1:" :
                                                      "recovery\n--aera-appvault=registration-v1:";
    if (recovery.compare(0, prefix.size(), prefix) != 0) continue;
    const std::string candidate = recovery.substr(prefix.size(), 64);
    if (!ValidIdentity(candidate)) return State::Error;
    const std::string expected = prefix + candidate + "\n";
    if (recovery != expected + std::string(sizeof(message.recovery) - expected.size(), '\0')) return State::Error;
    identity = candidate;
    return state;
  }
  return State::Error;
}
inline bool HasProtectedMarker(const bootloader_message& message) {
  std::string identity;
  return ParseMarker(message, identity) != State::Clear;
}
}  // namespace aeraui::appvault::bootguard
