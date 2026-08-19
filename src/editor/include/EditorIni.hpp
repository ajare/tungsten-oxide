// EditorIni.hpp -- minimal [Section]/Key=Value reader for editor.ini (see
// src/editor/resources/editor.ini, copied beside the executable by CMake). Not a
// general-purpose INI implementation, just as much as editor.ini needs today.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace editor {

class EditorIni {
 public:
  // Throws std::runtime_error if the file can't be opened.
  static EditorIni load(const std::filesystem::path& path);

  std::optional<std::string> get(const std::string& section, const std::string& key) const;

 private:
  std::map<std::string, std::map<std::string, std::string>> sections_;
};

}  // namespace editor
