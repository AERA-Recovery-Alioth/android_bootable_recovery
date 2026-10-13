/* SPDX-License-Identifier: Apache-2.0 */
#include "bootguard.hpp"
#include <cutils/properties.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>

namespace aeraui::appvault::bootguard {
namespace {
struct Fd {
  int value;
  explicit Fd(int fd) : value(fd) {}
  ~Fd() { if (value >= 0) close(value); }
};
struct Workspace {
  std::string path;
  ~Workspace() { if (!path.empty()) Cleanup(); }
  bool Cleanup() {
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;
    bool success = true;
    while (dirent* entry = readdir(dir)) {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
      // Only our private direct children; never follow extracted symlinks.
      if (unlinkat(dirfd(dir), entry->d_name, 0)) success = false;
    }
    if (closedir(dir)) success = false;
    if (rmdir(path.c_str())) success = false;
    if (success) path.clear();
    return success;
  }
};
std::string Property(const char* name) {
  char value[PROPERTY_VALUE_MAX]{};
  property_get(name, value, "");
  return value;
}
bool Run(const std::string& tool, const std::string& directory,
         const std::vector<std::string>& arguments) {
  std::vector<char*> argv{const_cast<char*>(tool.c_str())};
  for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
  argv.push_back(nullptr);
  pid_t child = fork();
  if (child < 0) return false;
  if (!child) {
    alarm(30);
    if (chdir(directory.c_str())) _exit(126);
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) _exit(126);
    dup2(null_fd, STDIN_FILENO); dup2(null_fd, STDOUT_FILENO); dup2(null_fd, STDERR_FILENO);
    if (null_fd > STDERR_FILENO) close(null_fd);
    execv(tool.c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  pid_t result;
  do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
  return result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
bool EqualExecutables(const std::string& installed) {
  Fd left(open(installed.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  Fd right(open("/proc/self/exe", O_RDONLY | O_CLOEXEC));
  struct stat a{}, b{};
  if (left.value < 0 || right.value < 0 || fstat(left.value, &a) || fstat(right.value, &b) ||
      !S_ISREG(a.st_mode) || !S_ISREG(b.st_mode) || a.st_size <= 0 ||
      a.st_size != b.st_size || a.st_size > 256 * 1024 * 1024) return false;
  char x[32768], y[32768];
  off_t offset = 0;
  while (offset < a.st_size) {
    size_t size = static_cast<size_t>(a.st_size - offset);
    if (size > sizeof(x)) size = sizeof(x);
    ssize_t nx = pread(left.value, x, size, offset), ny = pread(right.value, y, size, offset);
    if (nx != static_cast<ssize_t>(size) || ny != nx || memcmp(x, y, size)) return false;
    offset += nx;
  }
  return true;
}
bool ProveSlot(const std::string& slot, const std::string& tool, std::string& error) {
  Fd source(open(("/dev/block/bootdevice/by-name/boot" + slot).c_str(), O_RDONLY | O_CLOEXEC));
  struct stat info{};
  uint64_t size = 0;
  unsigned char header[44]{};
  if (source.value < 0 || fstat(source.value, &info) || !S_ISBLK(info.st_mode) ||
      ioctl(source.value, BLKGETSIZE64, &size) || size < 4096 || size > 256 * 1024 * 1024 ||
      pread(source.value, header, sizeof(header), 0) != sizeof(header) ||
      memcmp(header, "ANDROID!", 8) || header[40] != 3 || header[41] || header[42] || header[43]) {
    error = "Cannot verify Alioth boot header v3 in slot " + slot;
    return false;
  }
  char temporary[] = "/tmp/aera-bootguard-XXXXXX";
  if (!mkdtemp(temporary)) { error = "Cannot stage installed recovery verification."; return false; }
  Workspace workspace{temporary};
  Fd target(open((workspace.path + "/boot.img").c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
  if (target.value < 0) { error = "Cannot create installed boot snapshot."; return false; }
  char buffer[32768];
  for (uint64_t offset = 0; offset < size;) {
    size_t amount = size - offset;
    if (amount > sizeof(buffer)) amount = sizeof(buffer);
    ssize_t got = pread(source.value, buffer, amount, offset);
    if (got != static_cast<ssize_t>(amount)) { error = "Incomplete installed boot snapshot."; return false; }
    size_t written = 0;
    while (written < amount) {
      ssize_t count = write(target.value, buffer + written, amount - written);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) { error = "Cannot write installed boot snapshot."; return false; }
      written += count;
    }
    offset += amount;
  }
  if (!Run(tool, workspace.path, {"unpack", "boot.img"})) {
    error = "Cannot unpack installed recovery in slot " + slot;
    return false;
  }
  // Targeted extraction avoids allowing CPIO paths to write outside the workspace.
  for (const char* member : {"system/bin/recovery", "sbin/recovery"}) {
    unlink((workspace.path + "/recovery-executable").c_str());
    if (Run(tool, workspace.path, {"cpio", "ramdisk.cpio",
          std::string("extract ") + member + " recovery-executable"}) &&
        EqualExecutables(workspace.path + "/recovery-executable")) {
      if (!workspace.Cleanup()) { error = "Cannot clean recovery verification workspace; restore refused."; return false; }
      return true;
    }
  }
  error = "Installed recovery in slot " + slot + " does not match this guard-capable AERA executable; restore refused.";
  return false;
}
class NativePlatform final : public Platform {
 public:
  bool Read(bootloader_message& message, std::string& error) override {
    return read_bootloader_message(&message, &error);
  }
  bool Write(const bootloader_message& message, std::string& error) override {
    return write_appvault_bootguard_message(message, &error);
  }
  bool ProveInstalledRecovery(std::string& error) override {
    bool alioth = false;
    for (const char* key : {"ro.product.device", "ro.product.vendor.device", "ro.build.product"}) {
      const std::string device = Property(key);
      if (device == "alioth" || device == "aliothin") alioth = true;
    }
    const std::string slot = Property("ro.boot.slot_suffix");
    if (!alioth || (slot != "_a" && slot != "_b")) {
      error = "Persistent recovery guard proof supports Alioth A/B only.";
      return false;
    }
    std::string tool;
    for (const char* candidate : {"/system/bin/magiskboot", "/sbin/magiskboot", "/system/bin/aera/magiskboot"}) {
      struct stat info{};
      if (!lstat(candidate, &info) && S_ISREG(info.st_mode) && info.st_uid == 0 &&
          !(info.st_mode & 0022) && access(candidate, X_OK) == 0) { tool = candidate; break; }
    }
    if (tool.empty()) { error = "Trusted ramdisk unpack tool unavailable; restore refused."; return false; }
    // A/B boot fallback must never bypass the persistent guard handler.
    return ProveSlot(slot, tool, error) && ProveSlot(slot == "_a" ? "_b" : "_a", tool, error);
  }
};
}
Platform& RecoveryPlatform() { static NativePlatform platform; return platform; }
}  // namespace aeraui::appvault::bootguard
