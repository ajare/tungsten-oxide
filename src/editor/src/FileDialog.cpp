#include "FileDialog.hpp"

#include <codecvt>
#include <locale>
#include <string>
#include <vector>

#include <nfd.h>

namespace editor {
namespace {

std::string wideToUtf8Impl(const std::wstring& value) {
  if (value.empty()) return {};
  return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>{}.to_bytes(value);
}

struct Filters {
  std::vector<std::string> names;
  std::vector<std::string> specs;
  std::vector<nfdu8filteritem_t> items;

  explicit Filters(const std::vector<FileDialogFilter>& source) {
    names.reserve(source.size());
    specs.reserve(source.size());
    for (const auto& filter : source) {
      names.push_back(wideToUtf8Impl(filter.name));
      std::string spec = wideToUtf8Impl(filter.pattern);
      for (std::size_t pos = 0; (pos = spec.find("*.", pos)) != std::string::npos;) spec.erase(pos, 2);
      for (char& c : spec) if (c == ';') c = ',';
      specs.push_back(std::move(spec));
    }
    for (std::size_t i = 0; i < names.size(); ++i) items.push_back({names[i].c_str(), specs[i].c_str()});
  }
};

struct NfdScope {
  bool ok{NFD_Init() == NFD_OKAY};
  ~NfdScope() { if (ok) NFD_Quit(); }
};

}  // namespace

FileDialogResult showOpenFileDialog(const std::wstring& title, const std::vector<FileDialogFilter>& filters) {
  NfdScope nfd;
  if (!nfd.ok) return {};
  Filters converted(filters);
  const std::string titleUtf8 = wideToUtf8Impl(title);
  nfdopendialogu8args_t args{};
  args.filterList = converted.items.data();
  args.filterCount = static_cast<nfdfiltersize_t>(converted.items.size());
  args.title = titleUtf8.c_str();
  nfdu8char_t* path = nullptr;
  FileDialogResult result;
  if (NFD_OpenDialogU8_With(&path, &args) == NFD_OKAY) {
    result = {true, std::filesystem::u8path(path)};
    NFD_FreePathU8(path);
  }
  return result;
}

FileDialogResult showSaveFileDialog(const std::wstring& title, const std::vector<FileDialogFilter>& filters,
                                    const std::wstring& defaultFileName, const std::wstring& defaultExtension,
                                    bool /*confirmOverwrite*/) {
  NfdScope nfd;
  if (!nfd.ok) return {};
  Filters converted(filters);
  const std::string titleUtf8 = wideToUtf8Impl(title);
  std::string name = wideToUtf8Impl(defaultFileName);
  if (!defaultExtension.empty() && std::filesystem::path(name).extension().empty())
    name += "." + wideToUtf8Impl(defaultExtension);
  nfdsavedialogu8args_t args{};
  args.filterList = converted.items.data();
  args.filterCount = static_cast<nfdfiltersize_t>(converted.items.size());
  args.defaultName = name.empty() ? nullptr : name.c_str();
  args.title = titleUtf8.c_str();
  nfdu8char_t* path = nullptr;
  FileDialogResult result;
  if (NFD_SaveDialogU8_With(&path, &args) == NFD_OKAY) {
    result = {true, std::filesystem::u8path(path)};
    NFD_FreePathU8(path);
  }
  return result;
}

std::wstring utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>{}.from_bytes(utf8);
}

std::string wideToUtf8(const std::wstring& wide) { return wideToUtf8Impl(wide); }

std::string pathToUtf8(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

}  // namespace editor
