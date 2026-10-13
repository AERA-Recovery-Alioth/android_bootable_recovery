// Copyright (C) 2026 AERA Recovery Project contributors
// SPDX-License-Identifier: Apache-2.0
#include "../../aera_wlan_mac_file.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <stdlib.h>
int main() {
  using namespace aera::wlan;
  assert(ValidMacFile("Intf0MacAddress=02123456789A\nEND\n"));
  assert(!ValidMacFile("Intf0MacAddress=000000000000\nEND\n"));
  assert(!ValidMacFile("Intf0MacAddress=01123456789A\nEND\n"));
  assert(!ValidMacFile("Intf0MacAddress=02123456789AZ\n"));
  assert(!ValidMacFile("xIntf0MacAddress=02123456789A\n"));
  std::string normalized;
  assert(NormalizeMacFile("wlan0=02123456789a", normalized));
  assert(normalized == "Intf0MacAddress=02123456789a\nEND\n");
  assert(NormalizeMacFile("wlan0=02123456789A\r\n", normalized));
  assert(!NormalizeMacFile("wlan0=000000000000", normalized));
  assert(!NormalizeMacFile("wlan0=01123456789A", normalized));
  assert(!NormalizeMacFile("wlan0=02123456789AZ", normalized));
  assert(!NormalizeMacFile("wlan0=02123456789A\nwlan1=02123456789B", normalized));
  assert(!NormalizeMacFile("wlan0=02123456789G", normalized));
  char directory[] = "/dev/shm/aera-mac-test-XXXXXX";
  assert(mkdtemp(directory));
  const std::string file = std::string(directory) + "/wlan_mac.bin";
  const std::string link = std::string(directory) + "/firmware-link";
  assert(WriteRamFile(file, "factory"));
  assert(symlink(file.c_str(), link.c_str()) == 0);
  assert(!WriteRamFile(link, "overwrite"));
  std::ifstream input(file);
  assert(std::string(std::istreambuf_iterator<char>(input), {}) == "factory");
  input.close();
  const std::string fifo = std::string(directory) + "/fifo";
  assert(mkfifo(fifo.c_str(), 0600) == 0);
  assert(!WriteRamFile(fifo, "overwrite"));
  unlink(fifo.c_str());
  char disk_directory[] = "/tmp/aera-mac-disk-XXXXXX";
  assert(mkdtemp(disk_directory));
  struct statfs disk{};
  assert(statfs(disk_directory, &disk) == 0);
  if (disk.f_type != TMPFS_MAGIC && disk.f_type != RAMFS_MAGIC) {
    const std::string disk_file = std::string(disk_directory) + "/factory";
    std::ofstream(disk_file) << "factory";
    assert(!WriteRamFile(disk_file, "overwrite"));
    std::ifstream disk_input(disk_file);
    assert(std::string(std::istreambuf_iterator<char>(disk_input), {}) == "factory");
    disk_input.close(); unlink(disk_file.c_str());
  }
  rmdir(disk_directory);
  unlink(link.c_str()); unlink(file.c_str()); rmdir(directory);
}
