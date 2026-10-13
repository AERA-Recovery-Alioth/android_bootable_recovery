/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "bootguard_marker.hpp"
#include <string>

namespace aeraui::appvault::bootguard {
// The adapter owns durable storage and positive installed-recovery proof.
class Platform {
 public:
  virtual ~Platform() = default;
  virtual bool Read(bootloader_message&, std::string&) = 0;
  virtual bool Write(const bootloader_message&, std::string&) = 0;
  virtual bool ProveInstalledRecovery(std::string&) = 0;
};
class Guard {
 public:
  explicit Guard(Platform& platform) : platform_(platform) {}
  State Inspect(std::string& error);
  bool Arm(const std::string& identity, std::string& error);
  bool MarkRegistration(const std::string& identity, std::string& error);
  bool Complete(const std::string& identity, std::string& error);
 private:
  bool Store(bootloader_message&, State, const std::string&, std::string&);
  Platform& platform_;
};
State Inspect(std::string& error);
bool Arm(const std::string& identity, std::string& error);
bool MarkRegistration(const std::string& identity, std::string& error);
bool Complete(const std::string& identity, std::string& error);
// Read errors and malformed guards also preserve the BCB and block system boot.
bool PreserveBootMessage(std::string& error);
bool CheckSystemBoot(std::string& error);
// Latches system reboot intent under the same lock as Arm.
bool BeginSystemBoot(std::string& error);
}  // namespace aeraui::appvault::bootguard
