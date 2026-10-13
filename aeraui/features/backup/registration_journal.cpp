/* SPDX-License-Identifier: Apache-2.0 */
#include "registration_journal.hpp"
#include "staging_file.hpp"
#include <tinyxml2.h>
#include <selinux/selinux.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <set>
#include <mutex>
#include <openssl/evp.h>

namespace aeraui::appvault::journal {
namespace {
constexpr char kDirectory[] = ".aera-registration";
constexpr size_t kMaximumBytes = 32U << 20;
constexpr size_t kMaximumEntries = 1024;
constexpr size_t kMaximumTotal = 256U << 20;
std::mutex transaction_mutex;
struct Fd {
  int value;
  explicit Fd(int fd = -1) : value(fd) {}
  ~Fd() { if (value >= 0) close(value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
bool Fail(std::string& error, const char* message) { error = message; return false; }
std::string Hash(const std::string& bytes) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned size = 0;
  if (EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr) != 1) return {};
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (unsigned i = 0; i < size; ++i) { result += digits[digest[i] >> 4]; result += digits[digest[i] & 15]; }
  return result;
}
bool SafeTarget(const std::string& path) {
  const std::string reserve = ".reservecopy";
  std::string base = path;
  if (base.size() >= reserve.size() && base.compare(base.size() - reserve.size(), reserve.size(), reserve) == 0)
    base.resize(base.size() - reserve.size());
  if (base == "packages.xml") return true;
  if (base.compare(0, 6, "users/") != 0) return false;
  const size_t slash = base.find('/', 6);
  if (slash == std::string::npos || base.substr(slash) != "/package-restrictions.xml") return false;
  const std::string user = base.substr(6, slash - 6);
  if (user.empty() || user.size() > 5 || (user.size() > 1 && user.front() == '0') ||
      user.find_first_not_of("0123456789") != std::string::npos) return false;
  return strtoul(user.c_str(), nullptr, 10) <= 21474;
}
bool Private(int fd) {
  struct stat st{};
  return fd >= 0 && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) &&
         st.st_uid == geteuid() && (st.st_mode & 0777) == 0700;
}
bool Sync(int fd, std::string& error) {
  return fsync(fd) == 0 || Fail(error, "Cannot synchronize app registration transaction");
}
bool ReadAt(int directory, const std::string& name, size_t maximum,
            std::string& bytes, std::string& error) {
  Fd file(openat(directory, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  struct stat st{};
  if (file.value < 0 || fstat(file.value, &st) || !S_ISREG(st.st_mode) ||
      st.st_uid != geteuid() || (st.st_mode & 0777) != 0600 ||
      st.st_size < 0 || static_cast<uint64_t>(st.st_size) > maximum)
    return Fail(error, "Unsafe app registration journal file");
  bytes.clear();
  char buffer[16384];
  while (true) {
    const ssize_t count = read(file.value, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) return Fail(error, "Cannot read app registration journal");
    if (!count) break;
    bytes.append(buffer, static_cast<size_t>(count));
    if (bytes.size() > maximum) return Fail(error, "App registration journal is too large");
  }
  return true;
}
bool Contents(int fd, const std::string& bytes, std::string& error) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count = write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return Fail(error, "Cannot write app registration transaction");
    offset += static_cast<size_t>(count);
  }
  return true;
}
bool SaveAt(int directory, const std::string& name, const std::string& bytes, std::string& error) {
  Fd file(openat(directory, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
  if (file.value < 0) return Fail(error, "Cannot create app registration journal file");
  return Contents(file.value, bytes, error) && Sync(file.value, error);
}
int OpenJournal(const std::string& root, bool create, std::string& error) {
  Fd system(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (system.value < 0) { Fail(error, "Cannot open Android package directory"); return -1; }
  if (create && mkdirat(system.value, kDirectory, 0700) && errno != EEXIST) {
    Fail(error, "Cannot create app registration journal"); return -1;
  }
  const int fd = openat(system.value, kDirectory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0 && errno == ENOENT && !create) return -2;
  if (!Private(fd)) { if (fd >= 0) close(fd); Fail(error, "Unsafe app registration journal directory"); return -1; }
  if (create && !Sync(system.value, error)) { close(fd); return -1; }
  return fd;
}
int Parent(int root, const std::string& path, std::string& leaf) {
  int current = fcntl(root, F_DUPFD_CLOEXEC, 0);
  size_t begin = 0;
  while (current >= 0) {
    const size_t slash = path.find('/', begin);
    if (slash == std::string::npos) { leaf = path.substr(begin); return current; }
    const std::string component = path.substr(begin, slash - begin);
    const int next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    close(current);
    current = next;
    begin = slash + 1;
  }
  return -1;
}
struct Snapshot {
  bool exists = false;
  std::string bytes, context;
  unsigned mode = 0, uid = 0, gid = 0;
};
struct Record { Replacement replacement; Snapshot original; };
bool SnapshotAt(int parent, const std::string& leaf, bool context_required,
                Snapshot& snapshot, std::string& error) {
  Fd input(openat(parent, leaf.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (input.value < 0 && errno == ENOENT) return true;
  struct stat st{};
  if (input.value < 0 || fstat(input.value, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      static_cast<uint64_t>(st.st_size) > kMaximumBytes)
    return Fail(error, "Unsafe app registration target file");
  snapshot.exists = true; snapshot.mode = st.st_mode & 07777; snapshot.uid = st.st_uid; snapshot.gid = st.st_gid;
  char* context = nullptr;
  if (context_required) {
    if (fgetfilecon(input.value, &context) < 0) return Fail(error, "Cannot inspect app registration context");
    snapshot.context = context; freecon(context);
  }
  snapshot.bytes.clear();
  char buffer[16384];
  while (true) {
    const ssize_t count = read(input.value, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) return Fail(error, "Cannot inspect app registration contents");
    if (!count) break;
    snapshot.bytes.append(buffer, static_cast<size_t>(count));
    if (snapshot.bytes.size() > kMaximumBytes) return Fail(error, "App registration target is too large");
  }
  return true;
}
bool Matches(const Snapshot& actual, const Snapshot& expected) {
  return actual.exists == expected.exists && (!actual.exists ||
      (actual.bytes == expected.bytes && actual.mode == expected.mode && actual.uid == expected.uid &&
       actual.gid == expected.gid && actual.context == expected.context));
}
bool CheckTarget(int root, const Record& record, std::string& error) {
  const auto& replacement = record.replacement;
  std::string leaf;
  Fd parent(Parent(root, replacement.path, leaf));
  if (parent.value < 0) return Fail(error, "Unsafe app registration target directory");
  Snapshot current;
  if (!SnapshotAt(parent.value, leaf, !replacement.context.empty(), current, error)) return false;
  Snapshot intended; intended.exists = true; intended.bytes = replacement.bytes; intended.context = replacement.context;
  intended.mode = replacement.mode; intended.uid = replacement.uid; intended.gid = replacement.gid;
  if (!Matches(current, record.original) && !Matches(current, intended))
    return Fail(error, "Android package state changed outside this transaction; registration recovery stopped");
  return true;
}
bool Interrupted(int completed, std::string& error);
bool Install(int root, const Record& record, int completed, std::string& error) {
  if (!CheckTarget(root, record, error)) return false;
  const auto& replacement = record.replacement;
  std::string leaf;
  Fd parent(Parent(root, replacement.path, leaf));
  if (parent.value < 0) return Fail(error, "Unsafe app registration target directory");
  std::string temporary;
  const std::string pinned = "/proc/self/fd/" + std::to_string(parent.value) + "/" + leaf;
  Fd output(CreateStagingFile(pinned, temporary));
  if (output.value < 0) return Fail(error, "Cannot stage recovered app registration");
  const std::string temporary_leaf = temporary.substr(temporary.find_last_of('/') + 1);
  const bool written = Contents(output.value, replacement.bytes, error) &&
      fchown(output.value, replacement.uid, replacement.gid) == 0 &&
      fchmod(output.value, replacement.mode) == 0 &&
      (replacement.context.empty() || fsetfilecon(output.value, replacement.context.c_str()) == 0) &&
      Sync(output.value, error);
  if (!written) { unlinkat(parent.value, temporary_leaf.c_str(), 0); return Fail(error, "Cannot prepare recovered app registration"); }
  if (renameat(parent.value, temporary_leaf.c_str(), parent.value, leaf.c_str())) {
    unlinkat(parent.value, temporary_leaf.c_str(), 0);
    return Fail(error, "Cannot replace recovered app registration");
  }
  if (Interrupted(1000 + completed, error)) return false;
  return Sync(parent.value, error);
}
void Clean(int parent, const std::string& name, size_t count) {
  Fd directory(openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (!Private(directory.value)) return;
  for (size_t i = 0; i < count; ++i) {
    unlinkat(directory.value, std::to_string(i).c_str(), 0);
    unlinkat(directory.value, ("old-" + std::to_string(i)).c_str(), 0);
  }
  unlinkat(directory.value, "manifest.xml", 0);
  unlinkat(parent, name.c_str(), AT_REMOVEDIR);
}
#ifdef AERA_JOURNAL_TESTING
int interrupt_after = -1;
bool Interrupted(int completed, std::string& error) {
  if (interrupt_after != completed) return false;
  interrupt_after = -1;
  Fail(error, "Injected interruption; replay app registration in AERA before booting Android");
  return true;
}
#else
bool Interrupted(int, std::string&) { return false; }
#endif
bool ReplayOpen(const std::string& root, int parent, std::string& error) {
  Fd active(openat(parent, "active", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (active.value < 0 && errno == ENOENT) return true;
  if (!Private(active.value)) return Fail(error, "Unsafe active app registration transaction");
  std::string xml;
  if (!ReadAt(active.value, "manifest.xml", 1U << 20, xml, error)) return false;
  tinyxml2::XMLDocument document;
  if (document.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS || !document.RootElement() ||
      std::string(document.RootElement()->Name()) != "app-registration-redo" ||
      document.RootElement()->IntAttribute("version") != 1)
    return Fail(error, "Invalid app registration transaction manifest");
  std::vector<Record> replacements;
  std::set<std::string> targets;
  size_t total_bytes = 0;
  for (auto* entry = document.RootElement()->FirstChildElement(); entry; entry = entry->NextSiblingElement()) {
    if (std::string(entry->Name()) != "file" || replacements.size() >= kMaximumEntries)
      return Fail(error, "Invalid app registration transaction entry");
    Record record{};
    auto& value = record.replacement;
    const char* path = entry->Attribute("path");
    const char* context = entry->Attribute("context");
    unsigned mode = 0, uid = 0, gid = 0;
    if (!path || !SafeTarget(path) || !targets.insert(path).second || !context ||
        entry->QueryUnsignedAttribute("mode", &mode) != tinyxml2::XML_SUCCESS || mode > 0777 ||
        entry->QueryUnsignedAttribute("uid", &uid) != tinyxml2::XML_SUCCESS ||
        entry->QueryUnsignedAttribute("gid", &gid) != tinyxml2::XML_SUCCESS)
      return Fail(error, "Invalid app registration transaction metadata");
    value.path = path; value.context = context; value.mode = mode; value.uid = uid; value.gid = gid;
    if (!ReadAt(active.value, std::to_string(replacements.size()), kMaximumBytes, value.bytes, error)) return false;
    bool original_exists = false;
    if (entry->QueryBoolAttribute("original-exists", &original_exists) != tinyxml2::XML_SUCCESS)
      return Fail(error, "Invalid original app registration state");
    record.original.exists = original_exists;
    if (original_exists) {
      const char* original_context = entry->Attribute("original-context");
      if (!original_context ||
          entry->QueryUnsignedAttribute("original-mode", &record.original.mode) != tinyxml2::XML_SUCCESS ||
          record.original.mode > 07777 ||
          entry->QueryUnsignedAttribute("original-uid", &record.original.uid) != tinyxml2::XML_SUCCESS ||
          entry->QueryUnsignedAttribute("original-gid", &record.original.gid) != tinyxml2::XML_SUCCESS ||
          !ReadAt(active.value, "old-" + std::to_string(replacements.size()), kMaximumBytes, record.original.bytes, error))
        return Fail(error, "Invalid original app registration metadata");
      record.original.context = original_context;
    }
    const char* intended_hash = entry->Attribute("sha256");
    const char* original_hash = entry->Attribute("original-sha256");
    if (!intended_hash || std::string(intended_hash) != Hash(value.bytes) ||
        (record.original.exists && (!original_hash || std::string(original_hash) != Hash(record.original.bytes))))
      return Fail(error, "App registration journal payload changed");
    total_bytes += value.bytes.size() + record.original.bytes.size();
    if (total_bytes > kMaximumTotal) return Fail(error, "App registration transaction is too large");
    replacements.push_back(std::move(record));
  }
  if (replacements.empty()) return Fail(error, "Empty app registration transaction");
  Fd system(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (system.value < 0) return Fail(error, "Cannot open app registration targets");
  for (const auto& record : replacements)
    if (!CheckTarget(system.value, record, error)) return false;
  for (size_t i = 0; i < replacements.size(); ++i) {
    if (!Install(system.value, replacements[i], static_cast<int>(i + 1), error) || Interrupted(static_cast<int>(i + 1), error)) return false;
  }
  // First durably retire the complete journal, then remove its payloads. A
  // crash while cleaning never leaves a half-deleted active transaction.
  const std::string pattern = "/proc/self/fd/" + std::to_string(parent) + "/retired-XXXXXX";
  std::vector<char> retired(pattern.begin(), pattern.end()); retired.push_back('\0');
  if (!mkdtemp(retired.data())) return Fail(error, "Cannot retire app registration transaction");
  const std::string name = std::string(retired.data()).substr(pattern.find_last_of('/') + 1);
  if (renameat(parent, "active", parent, name.c_str())) {
    unlinkat(parent, name.c_str(), AT_REMOVEDIR);
    return Fail(error, "Cannot retire app registration transaction");
  }
  if (!Sync(parent, error)) return false;
  if (Interrupted(static_cast<int>(replacements.size() + 1), error)) return false;
  Clean(parent, name, replacements.size());
  return Sync(parent, error);
}
}  // namespace
#ifdef AERA_JOURNAL_TESTING
void InterruptAfter(int count) { interrupt_after = count; }
#endif
bool Replay(const std::string& root, std::string& error) {
  std::lock_guard<std::mutex> lock(transaction_mutex);
  Fd directory(OpenJournal(root, false, error));
  if (directory.value == -2) return true;
  return directory.value >= 0 && ReplayOpen(root, directory.value, error);
}
bool Commit(const std::string& root, const std::vector<Replacement>& files, std::string& error) {
  std::lock_guard<std::mutex> lock(transaction_mutex);
  if (files.empty() || files.size() > kMaximumEntries) return Fail(error, "Invalid app registration transaction size");
  Fd parent(OpenJournal(root, true, error));
  if (parent.value < 0 || !ReplayOpen(root, parent.value, error)) return false;
  std::set<std::string> targets;
  for (const auto& entry : files)
    if (!SafeTarget(entry.path) || !targets.insert(entry.path).second || entry.mode > 0777 || entry.bytes.size() > kMaximumBytes)
      return Fail(error, "Unsafe app registration transaction target");
  const std::string pattern = "/proc/self/fd/" + std::to_string(parent.value) + "/staging-XXXXXX";
  std::vector<char> temporary(pattern.begin(), pattern.end()); temporary.push_back('\0');
  if (!mkdtemp(temporary.data())) return Fail(error, "Cannot stage app registration transaction");
  const std::string name = std::string(temporary.data()).substr(pattern.find_last_of('/') + 1);
  Fd stage(openat(parent.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  tinyxml2::XMLDocument document;
  auto* manifest = document.NewElement("app-registration-redo"); manifest->SetAttribute("version", 1); document.InsertEndChild(manifest);
  Fd system(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (system.value < 0) { Clean(parent.value, name, 0); return Fail(error, "Cannot inspect Android package targets"); }
  size_t count = 0, total_bytes = 0;
  for (const auto& entry : files) {
    if (!SaveAt(stage.value, std::to_string(count), entry.bytes, error)) { Clean(parent.value, name, count + 1); return false; }
    Snapshot original;
    std::string leaf;
    Fd target_parent(Parent(system.value, entry.path, leaf));
    if (target_parent.value < 0 || !SnapshotAt(target_parent.value, leaf, !entry.context.empty(), original, error) ||
        (original.exists && original.uid != entry.uid) ||
        (entry.require_original && (!original.exists || original.bytes != entry.original_bytes))) {
      Clean(parent.value, name, count + 1); return Fail(error, "Unsafe original app registration target");
    }
    if (original.exists && !SaveAt(stage.value, "old-" + std::to_string(count), original.bytes, error)) {
      Clean(parent.value, name, count + 1); return false;
    }
    total_bytes += entry.bytes.size() + original.bytes.size();
    if (total_bytes > kMaximumTotal) { Clean(parent.value, name, count + 1); return Fail(error, "App registration transaction is too large"); }
    auto* record = document.NewElement("file"); record->SetAttribute("path", entry.path.c_str());
    record->SetAttribute("sha256", Hash(entry.bytes).c_str());
    if (original.exists) record->SetAttribute("original-sha256", Hash(original.bytes).c_str());
    record->SetAttribute("original-exists", original.exists);
    if (original.exists) {
      record->SetAttribute("original-mode", original.mode); record->SetAttribute("original-uid", original.uid);
      record->SetAttribute("original-gid", original.gid); record->SetAttribute("original-context", original.context.c_str());
    }
    record->SetAttribute("mode", static_cast<unsigned>(entry.mode)); record->SetAttribute("uid", entry.uid);
    record->SetAttribute("gid", entry.gid); record->SetAttribute("context", entry.context.c_str()); manifest->InsertEndChild(record);
    ++count;
  }
  tinyxml2::XMLPrinter printer; document.Print(&printer);
  if (!SaveAt(stage.value, "manifest.xml", std::string(printer.CStr(), printer.CStrSize() - 1), error) || !Sync(stage.value, error)) {
    Clean(parent.value, name, count); return false;
  }
  if (renameat(parent.value, name.c_str(), parent.value, "active")) { Clean(parent.value, name, count); return Fail(error, "Cannot publish app registration transaction"); }
  if (!Sync(parent.value, error) || Interrupted(0, error)) return false;
  const bool okay = ReplayOpen(root, parent.value, error);
  if (!okay) error += "; return to AERA App Vault to finish registration before booting Android";
  return okay;
}
}  // namespace aeraui::appvault::journal
