/* SPDX-License-Identifier: Apache-2.0 */
#include "../features/backup/staging_file.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

int main() {
  char folder[] = "/tmp/aera-stage-test-XXXXXX";
  assert(mkdtemp(folder));
  const std::string target = std::string(folder) + "/packages.xml";
  const std::string stale = target + ".aera-new";
  std::ofstream(stale) << "interrupted old staging";
  const std::string victim = std::string(folder) + "/unrelated";
  std::ofstream(victim) << "do not change";
  assert(symlink(victim.c_str(), (target + ".aera-rollback").c_str()) == 0);
  std::string first, second;
  const int first_fd = aeraui::appvault::CreateStagingFile(target, first);
  assert(first_fd >= 0);
  assert(write(first_fd, "first", 5) == 5);
  close(first_fd);  // Simulate an interrupted attempt leaving its staged inode.
  const int second_fd = aeraui::appvault::CreateStagingFile(target, second);
  assert(second_fd >= 0 && first != second);
  assert(std::filesystem::path(second).parent_path() == folder);
  struct stat info{};
  assert(fstat(second_fd, &info) == 0 && S_ISREG(info.st_mode));
  assert((info.st_mode & 0777) == 0600);
  assert((fcntl(second_fd, F_GETFD) & FD_CLOEXEC) != 0);
  assert(write(second_fd, "second", 6) == 6);
  assert(fsync(second_fd) == 0);
  close(second_fd);
  assert(rename(second.c_str(), target.c_str()) == 0);
  std::string contents;
  std::ifstream(target) >> contents;
  assert(contents == "second");
  std::ifstream(first) >> contents;
  assert(contents == "first");
  std::ifstream victim_file(victim);
  std::getline(victim_file, contents);
  assert(contents == "do not change");
  assert(std::filesystem::exists(stale));
  std::filesystem::remove_all(folder);
}
