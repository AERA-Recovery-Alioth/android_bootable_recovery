// Copyright (C) 2026 AERA Recovery Project contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cerrno>
#include <fcntl.h>
#include <linux/magic.h>
#include <string>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace aera::wlan {
inline bool ValidMacFile(const std::string& text) {
  const std::string key = "Intf0MacAddress=";
  const auto offset = text.find(key);
  if (offset == std::string::npos || (offset && text[offset - 1] != '\n')) return false;
  const auto start = offset + key.size();
  if (text.size() < start + 12) return false;
  unsigned nonzero = 0, first = 0;
  for (unsigned i = 0; i < 12; ++i) {
    const char c = text[start + i];
    unsigned value;
    if (c >= '0' && c <= '9') value = c - '0';
    else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
    else return false;
    nonzero |= value;
    if (i < 2) first = (first << 4) | value;
  }
  const auto end = start + 12;
  return nonzero && !(first & 1) &&
      (end == text.size() || text[end] == '\r' || text[end] == '\n');
}
// Xiaomi nv_mac provisioning uses wlan0=<12 hex digits>, unlike qcacld firmware.
inline bool NormalizeMacFile(const std::string& text, std::string& firmware) {
  if (ValidMacFile(text)) { firmware = text; return true; }
  std::string provisioning = text;
  while (!provisioning.empty() &&
         (provisioning.back() == '\r' || provisioning.back() == '\n'))
    provisioning.pop_back();
  if (provisioning.size() != 18 || provisioning.compare(0, 6, "wlan0=") != 0)
    return false;
  const std::string candidate = "Intf0MacAddress=" + provisioning.substr(6) + "\nEND\n";
  if (!ValidMacFile(candidate)) return false;
  firmware = candidate;
  return true;
}
// Never follow a firmware symlink into persist or truncate an on-disk file.
inline bool WriteRamFile(const std::string& path, const std::string& text) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos) return false;
  const int directory = open(path.substr(0, slash).c_str(),
                             O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0) return false;
  struct statfs filesystem{};
  const bool ram = fstatfs(directory, &filesystem) == 0 &&
      (filesystem.f_type == TMPFS_MAGIC || filesystem.f_type == RAMFS_MAGIC);
  if (!ram) { close(directory); return false; }
  const int fd = openat(directory, path.substr(slash + 1).c_str(),
      O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0644);
  close(directory);
  if (fd < 0) return false;
  struct stat status{};
  struct statfs opened_filesystem{};
  bool ok = fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
      fstatfs(fd, &opened_filesystem) == 0 &&
      (opened_filesystem.f_type == TMPFS_MAGIC || opened_filesystem.f_type == RAMFS_MAGIC) &&
      ftruncate(fd, 0) == 0;
  size_t offset = 0;
  while (ok && offset < text.size()) {
    const auto count = write(fd, text.data() + offset, text.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) ok = false;
    else offset += static_cast<size_t>(count);
  }
  if (close(fd) != 0) ok = false;
  return ok;
}
}  // namespace aera::wlan
