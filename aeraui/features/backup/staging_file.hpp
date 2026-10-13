/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>

namespace aeraui::appvault {
// Keep the staged inode on the target filesystem and exclusively own its fd.
// Previous interrupted attempts cannot prevent another staging operation.
inline int CreateStagingFile(const std::string& target, std::string& path) {
  const std::string pattern = target + ".aera-XXXXXX";
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back('\0');
  const int fd = mkstemp(name.data());
  if (fd < 0) return -1;
  if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
    close(fd);
    unlink(name.data());
    return -1;
  }
  path = name.data();
  return fd;
}
}  // namespace aeraui::appvault
