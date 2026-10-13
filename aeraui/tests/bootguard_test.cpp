/* SPDX-License-Identifier: Apache-2.0 */
#include "../features/backup/bootguard.hpp"
#include <cassert>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
using namespace aeraui::appvault::bootguard;
namespace {
class FilePlatform final : public Platform {
 public:
  FilePlatform() { char name[] = "/tmp/aera-bootguard-test-XXXXXX"; fd = mkstemp(name); assert(fd >= 0); unlink(name); bootloader_message zero{}; assert(pwrite(fd, &zero, sizeof(zero), 0) == sizeof(zero)); }
  ~FilePlatform() { close(fd); }
  bool Read(bootloader_message& message, std::string& error) override {
    if (fail_read) { error = "Injected read failure"; return false; }
    return pread(fd, &message, sizeof(message), 0) == sizeof(message);
  }
  bool Write(const bootloader_message& message, std::string& error) override {
    if (fail_write) { error = "Injected write failure"; return false; }
    if (pwrite(fd, &message, sizeof(message), 0) != sizeof(message) || fsync(fd)) return false;
    if (fail_after_sync) { error = "Injected post-sync interruption"; return false; }
    return true;
  }
  bool ProveInstalledRecovery(std::string& error) override {
    if (!proof) error = "Installed recovery proof failed";
    return proof;
  }
  int fd;
  bool proof = true, fail_read = false, fail_write = false, fail_after_sync = false;
};
FilePlatform native;
}
namespace aeraui::appvault::bootguard { Platform& RecoveryPlatform() { return native; } }
int main() {
  const std::string identity(64, 'a'), other(64, 'b');
  std::string error;
  FilePlatform platform;
  Guard guard(platform);
  bootloader_message message{};
  message.status[0] = 's'; message.reserved[100] = 'r';
  assert(platform.Write(message, error));
  platform.proof = false;
  assert(!guard.Arm(identity, error)); assert(guard.Inspect(error) == State::Clear);
  platform.proof = true; platform.fail_after_sync = true;
  assert(!guard.Arm(identity, error));
  platform.fail_after_sync = false;
  assert(guard.Inspect(error) == State::Writing);
  assert(!guard.Complete(identity, error));
  assert(!guard.Arm(other, error)); assert(!guard.MarkRegistration(other, error));
  assert(guard.Arm(identity, error));
  platform.fail_write = true; assert(!guard.MarkRegistration(identity, error));
  platform.fail_write = false; assert(guard.Inspect(error) == State::Writing);
  platform.fail_after_sync = true; assert(!guard.MarkRegistration(identity, error));
  platform.fail_after_sync = false; assert(guard.Inspect(error) == State::Registration);
  assert(!guard.Complete(other, error));
  assert(guard.Arm(identity, error)); assert(guard.Inspect(error) == State::Writing);
  assert(guard.MarkRegistration(identity, error)); assert(guard.Complete(identity, error));
  assert(platform.Read(message, error)); assert(message.status[0] == 's' && message.reserved[100] == 'r');
  strcpy(message.command, "boot-recovery"); strcpy(message.recovery, "recovery\n--wipe_data\n");
  assert(platform.Write(message, error)); assert(!guard.Arm(identity, error));
  strcpy(message.recovery, "recovery\n--aera-appvault=writing-v1:bad\n");
  assert(platform.Write(message, error)); assert(guard.Inspect(error) == State::Error);
  assert(HasProtectedMarker(message)); assert(!guard.Complete(identity, error));
  memset(message.recovery, 'x', sizeof(message.recovery)); memcpy(message.recovery + 700, "--aera-appvault", strlen("--aera-appvault"));
  assert(ParseMarker(message, error) == State::Error);
  assert(native.Write(bootloader_message{}, error));
  assert(CheckSystemBoot(error)); assert(Arm(identity, error));
  assert(PreserveBootMessage(error)); assert(!CheckSystemBoot(error));
  native.fail_read = true; assert(PreserveBootMessage(error)); assert(!CheckSystemBoot(error));
  assert(!BeginSystemBoot(error));
  native.fail_read = false;
  assert(!BeginSystemBoot(error));
  assert(MarkRegistration(identity, error)); assert(Complete(identity, error));
  assert(BeginSystemBoot(error));
  assert(!Arm(identity, error));
  assert(Inspect(error) == State::Clear);

}
