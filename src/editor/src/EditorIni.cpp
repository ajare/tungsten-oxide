#include "EditorIni.hpp"

#include <fstream>
#include <stdexcept>

namespace editor {

namespace {

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

}  // namespace

EditorIni EditorIni::load(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("EditorIni: could not open '" + path.string() + "'.");
  }

  EditorIni ini;
  std::string currentSection;
  std::string line;
  while (std::getline(file, line)) {
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;

    if (trimmed.front() == '[' && trimmed.back() == ']') {
      currentSection = trim(trimmed.substr(1, trimmed.size() - 2));
      continue;
    }

    const auto eq = trimmed.find('=');
    if (eq == std::string::npos) continue;

    const std::string key = trim(trimmed.substr(0, eq));
    const std::string value = trim(trimmed.substr(eq + 1));
    if (!key.empty()) ini.sections_[currentSection][key] = value;
  }

  return ini;
}

std::optional<std::string> EditorIni::get(const std::string& section, const std::string& key) const {
  const auto secIt = sections_.find(section);
  if (secIt == sections_.end()) return std::nullopt;

  const auto keyIt = secIt->second.find(key);
  if (keyIt == secIt->second.end()) return std::nullopt;

  return keyIt->second;
}

}  // namespace editor
