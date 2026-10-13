// SPDX-License-Identifier: Apache-2.0
#include "../core/performance_topology.hpp"
#include <cassert>
using namespace aeraui::performance;
int main() {
  CpuPolicy little{"policy0", 1804800}, big{"policy4", 2419200}, prime{"policy7", 3187200};
  assert(ParseCpuList("0-3", &little.cpus));
  assert(ParseCpuList("4 5 6", &big.cpus));
  assert(ParseCpuList("7", &prime.cpus));
  cpu_set_t allowed; assert(ParseCpuList("0-7", &allowed));
  CpuPolicy selected;
  assert(SelectCpuPolicy({little,big,prime}, allowed, &selected));
  assert(selected.path == "policy7" && CPU_COUNT(&selected.cpus) == 1 && CPU_ISSET(7,&selected.cpus));
  assert(ParseCpuList("0-5", &allowed));
  assert(SelectCpuPolicy({little,big,prime}, allowed, &selected));
  assert(selected.path == "policy4" && CPU_COUNT(&selected.cpus) == 2 && !CPU_ISSET(6,&selected.cpus));
  assert(ParseCpuList("1,3,7", &allowed));
  assert(SelectCpuPolicy({little,prime}, allowed, &selected) && CPU_ISSET(7,&selected.cpus));
  CPU_ZERO(&allowed); assert(!SelectCpuPolicy({little,prime}, allowed, &selected));
  for (const char *invalid : {"", "-1", "2-1", "7x", "0-999999", "999999999999999999"})
    assert(!ParseCpuList(invalid, &allowed));
}
