/* SPDX-License-Identifier: Apache-2.0 */
#include "../features/backup/registration_journal.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
namespace fs = std::filesystem;
using namespace aeraui::appvault::journal;
std::string Read(const fs::path& path) { std::ifstream file(path); return {std::istreambuf_iterator<char>(file), {}}; }
void Write(const fs::path& path, const std::string& value) { std::ofstream file(path); file << value; file.close(); assert(chmod(path.c_str(), 0600) == 0); }
struct Fixture {
  fs::path root;
  Fixture() { char name[] = "/tmp/aera-journal-test-XXXXXX"; assert(mkdtemp(name)); root = name; fs::create_directories(root / "users/0"); Write(root / "packages.xml", "old-global"); Write(root / "users/0/package-restrictions.xml", "old-user"); }
  ~Fixture() { fs::remove_all(root); }
  std::vector<Replacement> Changes() const { return {{"users/0/package-restrictions.xml", "new-user", "", 0600, geteuid(), getegid()}, {"packages.xml", "new-global", "", 0600, geteuid(), getegid()}}; }
};
int main() {
  std::string error;
  for (int interruption : {0, 1, 2, 3, 1001, 1002}) {
    Fixture fixture;
    InterruptAfter(interruption);
    assert(!Commit(fixture.root, fixture.Changes(), error));
    assert(Replay(fixture.root, error));
    assert(Read(fixture.root / "packages.xml") == "new-global");
    assert(Read(fixture.root / "users/0/package-restrictions.xml") == "new-user");
    assert(!fs::exists(fixture.root / ".aera-registration/active"));
    assert(Replay(fixture.root, error));
  }
  {
    Fixture fixture;
    // A reserve copy can contain a different old generation; capture it separately.
    Write(fixture.root / "packages.xml.reservecopy", "different-reserve");
    auto entries = fixture.Changes();
    entries.push_back({"packages.xml.reservecopy", "new-global", "", 0600, geteuid(), getegid()});
    InterruptAfter(0); assert(!Commit(fixture.root, entries, error));
    assert(Replay(fixture.root, error));
    assert(Read(fixture.root / "packages.xml.reservecopy") == "new-global");
  }
  {
    Fixture fixture; InterruptAfter(0); assert(!Commit(fixture.root, fixture.Changes(), error));
    Write(fixture.root / "packages.xml", "Android changed last target");
    assert(!Replay(fixture.root, error));
    assert(Read(fixture.root / "users/0/package-restrictions.xml") == "old-user");
    assert(Read(fixture.root / "packages.xml") == "Android changed last target");
  }
  {
    Fixture fixture; InterruptAfter(0); assert(!Commit(fixture.root, fixture.Changes(), error));
    Write(fixture.root / ".aera-registration/active/0", "tampered-payload");
    assert(!Replay(fixture.root, error));
    assert(Read(fixture.root / "packages.xml") == "old-global");
  }
  {
    Fixture fixture; InterruptAfter(0); assert(!Commit(fixture.root, fixture.Changes(), error));
    auto manifest = Read(fixture.root / ".aera-registration/active/manifest.xml");
    const auto position = manifest.find("packages.xml"); assert(position != std::string::npos);
    manifest.replace(position, std::string("packages.xml").size(), "../unrelated");
    Write(fixture.root / ".aera-registration/active/manifest.xml", manifest);
    assert(!Replay(fixture.root, error));
    assert(Read(fixture.root / "users/0/package-restrictions.xml") == "old-user");
  }
  {
    Fixture fixture; InterruptAfter(0); assert(!Commit(fixture.root, fixture.Changes(), error));
    fs::remove(fixture.root / "packages.xml"); Write(fixture.root / "unrelated", "do not change");
    fs::create_symlink(fixture.root / "unrelated", fixture.root / "packages.xml");
    assert(!Replay(fixture.root, error));
    assert(Read(fixture.root / "unrelated") == "do not change");
    assert(Read(fixture.root / "users/0/package-restrictions.xml") == "old-user");
  }
  {
    Fixture fixture; InterruptAfter(0); assert(!Commit(fixture.root, fixture.Changes(), error));
    fs::rename(fixture.root / "users", fixture.root / "original-users");
    fs::create_directory_symlink(fixture.root / "original-users", fixture.root / "users");
    assert(!Replay(fixture.root, error));
    assert(Read(fixture.root / "packages.xml") == "old-global");
  }
  {
    Fixture fixture; auto entries = fixture.Changes();
    entries[1].require_original = true; entries[1].original_bytes = "old-global";
    Write(fixture.root / "packages.xml", "changed since preparation");
    assert(!Commit(fixture.root, entries, error));
    assert(Read(fixture.root / "users/0/package-restrictions.xml") == "old-user");
    assert(!fs::exists(fixture.root / ".aera-registration/active"));
  }
  {
    Fixture fixture; InterruptAfter(0); assert(!Commit(fixture.root, fixture.Changes(), error));
    assert(chmod((fixture.root / "packages.xml").c_str(), 0644) == 0);
    assert(!Replay(fixture.root, error));
    assert(Read(fixture.root / "users/0/package-restrictions.xml") == "old-user");
  }
  {
    Fixture fixture; InterruptAfter(0); assert(!Commit(fixture.root, fixture.Changes(), error));
    assert(chmod((fixture.root / ".aera-registration/active/manifest.xml").c_str(), 0644) == 0);
    assert(!Replay(fixture.root, error));
    assert(Read(fixture.root / "packages.xml") == "old-global");
  }
  for (const std::string path : {"../packages.xml", "/data/system/packages.xml", "users/../package-restrictions.xml", "users/01/package-restrictions.xml", "users/99999/package-restrictions.xml", "packages.xml.aera-new"}) {
    Fixture fixture; auto entries = fixture.Changes(); entries[0].path = path;
    assert(!Commit(fixture.root, entries, error)); assert(Read(fixture.root / "packages.xml") == "old-global");
  }
}
