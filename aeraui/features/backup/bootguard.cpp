/* SPDX-License-Identifier: Apache-2.0 */
#include "bootguard.hpp"
#include <cstring>
#include <mutex>

namespace aeraui::appvault::bootguard {
namespace {
std::mutex mutex;
bool system_boot_requested = false;
bool Empty(const char* data, size_t size) {
  for (size_t i = 0; i < size; ++i) if (data[i]) return false;
  return true;
}
State Parse(const bootloader_message& message, std::string& error) {
  std::string identity;
  const State state = ParseMarker(message, identity);
  if (state == State::Error) error = "Malformed AppVault boot guard; boot remains blocked.";
  return state;
}
}
State Guard::Inspect(std::string& error) {
  error.clear();
  bootloader_message message{};
  if (!platform_.Read(message, error)) return State::Error;
  return Parse(message, error);
}
bool Guard::Store(bootloader_message& message, State state, const std::string& identity, std::string& error) {
  memset(message.command, 0, sizeof(message.command));
  memset(message.recovery, 0, sizeof(message.recovery));
  if (state != State::Clear) {
    memcpy(message.command, "boot-recovery", sizeof("boot-recovery"));
    const std::string marker = std::string("recovery\n--aera-appvault=") +
        (state == State::Writing ? "writing-v1:" : "registration-v1:") + identity + "\n";
    memcpy(message.recovery, marker.c_str(), marker.size() + 1);
  }
  if (!platform_.Write(message, error)) return false;
  bootloader_message actual{};
  if (!platform_.Read(actual, error)) return false;
  if (memcmp(&actual, &message, sizeof(message))) {
    error = "AppVault boot guard durable readback mismatch.";
    return false;
  }
  return true;
}
bool Guard::Arm(const std::string& identity, std::string& error) {
  error.clear();
  if (!ValidIdentity(identity)) { error = "Invalid AppVault transaction identity."; return false; }
  bootloader_message message{};
  if (!platform_.Read(message, error)) return false;
  const State state = Parse(message, error);
  if (state == State::Error) return false;
  std::string pending_identity;
  ParseMarker(message, pending_identity);
  if ((state == State::Writing || state == State::Registration) && pending_identity == identity) {
    if (!platform_.ProveInstalledRecovery(error)) return false;
    bootloader_message latest{};
    if (!platform_.Read(latest, error)) return false;
    if (memcmp(&latest, &message, sizeof(message))) {
      error = "Bootloader state changed during recovery validation.";
      return false;
    }
    return state == State::Writing ? true : Store(message, State::Writing, identity, error);
  }
  if (state != State::Clear) {
    error = "An interrupted AppVault restore must be resolved before another restore.";
    return false;
  }
  if (!Empty(message.command, sizeof(message.command)) ||
      !Empty(message.recovery, sizeof(message.recovery)) ||
      !Empty(message.stage, sizeof(message.stage))) {
    error = "Another bootloader operation is pending; AppVault restore refused.";
    return false;
  }
  if (!platform_.ProveInstalledRecovery(error)) return false;
  // Re-read after slow partition validation, preserving unrelated BCB bytes.
  bootloader_message latest{};
  if (!platform_.Read(latest, error)) return false;
  if (memcmp(&latest, &message, sizeof(message))) {
    error = "Bootloader state changed during recovery validation.";
    return false;
  }
  return Store(message, State::Writing, identity, error);
}
bool Guard::MarkRegistration(const std::string& identity, std::string& error) {
  error.clear();
  if (!ValidIdentity(identity)) { error = "Invalid AppVault transaction identity."; return false; }
  bootloader_message message{};
  if (!platform_.Read(message, error)) return false;
  const State state = Parse(message, error);
  std::string pending_identity;
  ParseMarker(message, pending_identity);
  if (pending_identity != identity) { error = "AppVault transaction identity differs from pending restore."; return false; }
  if (state == State::Registration) return true;
  if (state != State::Writing) {
    if (error.empty()) error = "AppVault writing guard is missing.";
    return false;
  }
  return Store(message, State::Registration, identity, error);
}
bool Guard::Complete(const std::string& identity, std::string& error) {
  error.clear();
  if (!ValidIdentity(identity)) { error = "Invalid AppVault transaction identity."; return false; }
  bootloader_message message{};
  if (!platform_.Read(message, error)) return false;
  const State state = Parse(message, error);
  std::string pending_identity;
  ParseMarker(message, pending_identity);
  if (pending_identity != identity) { error = "AppVault transaction identity differs from pending restore."; return false; }
  if (state != State::Registration) {
    if (error.empty()) error = "AppVault registration guard is missing; completion refused.";
    return false;
  }
  return Store(message, State::Clear, identity, error);
}
// Defined by the platform adapter; this is never a permissive fallback.
Platform& RecoveryPlatform();
State Inspect(std::string& error) { std::lock_guard<std::mutex> lock(mutex); return Guard(RecoveryPlatform()).Inspect(error); }
bool Arm(const std::string& identity, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex);
  if (system_boot_requested) {
    error = "System reboot is already requested; restart recovery before restoring apps.";
    return false;
  }
  return Guard(RecoveryPlatform()).Arm(identity, error);
}
bool MarkRegistration(const std::string& identity, std::string& error) { std::lock_guard<std::mutex> lock(mutex); return Guard(RecoveryPlatform()).MarkRegistration(identity, error); }
bool Complete(const std::string& identity, std::string& error) { std::lock_guard<std::mutex> lock(mutex); return Guard(RecoveryPlatform()).Complete(identity, error); }
bool PreserveBootMessage(std::string& error) { return Inspect(error) != State::Clear; }
bool CheckSystemBoot(std::string& error) {
  const State state = Inspect(error);
  if (state == State::Clear) return true;
  if (error.empty()) error = "Interrupted AppVault restore: system boot is blocked until recovery completes it.";
  return false;
}
bool BeginSystemBoot(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex);
  const State state = Guard(RecoveryPlatform()).Inspect(error);
  if (state != State::Clear) {
    if (error.empty()) error = "Interrupted AppVault restore: system boot remains blocked.";
    return false;
  }
  system_boot_requested = true;
  return true;
}
}  // namespace aeraui::appvault::bootguard
