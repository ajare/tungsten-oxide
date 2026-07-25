#include "FileDialog.hpp"

#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

namespace editor {
namespace {

// RAII COM lifetime: IFileOpenDialog/IFileSaveDialog are only ever driven from a single UI-thread
// button click, so a plain init/uninit pair around each call is simplest -- CoInitializeEx is
// refcounted per thread, so this nests safely even if a caller already initialized COM elsewhere.
struct ComScope {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  ~ComScope() {
    if (SUCCEEDED(hr)) CoUninitialize();
  }
  bool ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

std::vector<COMDLG_FILTERSPEC> toSpecs(const std::vector<FileDialogFilter>& filters) {
  std::vector<COMDLG_FILTERSPEC> specs;
  specs.reserve(filters.size());
  for (const auto& filter : filters) specs.push_back({filter.name.c_str(), filter.pattern.c_str()});
  return specs;
}

FileDialogResult runDialog(IFileDialog* dialog, const std::wstring& title, const std::vector<FileDialogFilter>& filters) {
  FileDialogResult result;
  dialog->SetTitle(title.c_str());
  const std::vector<COMDLG_FILTERSPEC> specs = toSpecs(filters);
  if (!specs.empty()) {
    dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    dialog->SetFileTypeIndex(1);
  }
  if (FAILED(dialog->Show(nullptr))) return result;

  IShellItem* item = nullptr;
  if (FAILED(dialog->GetResult(&item)) || item == nullptr) return result;
  PWSTR pathText = nullptr;
  if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathText)) && pathText != nullptr) {
    result.ok = true;
    result.path = std::filesystem::path(pathText);
    CoTaskMemFree(pathText);
  }
  item->Release();
  return result;
}

}  // namespace

FileDialogResult showOpenFileDialog(const std::wstring& title, const std::vector<FileDialogFilter>& filters) {
  ComScope com;
  if (!com.ok()) return {};
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) || dialog == nullptr)
    return {};
  const FileDialogResult result = runDialog(dialog, title, filters);
  dialog->Release();
  return result;
}

FileDialogResult showSaveFileDialog(const std::wstring& title, const std::vector<FileDialogFilter>& filters,
                                     const std::wstring& defaultFileName, const std::wstring& defaultExtension) {
  ComScope com;
  if (!com.ok()) return {};
  IFileSaveDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) || dialog == nullptr)
    return {};
  if (!defaultFileName.empty()) dialog->SetFileName(defaultFileName.c_str());
  if (!defaultExtension.empty()) dialog->SetDefaultExtension(defaultExtension.c_str());
  const FileDialogResult result = runDialog(dialog, title, filters);
  dialog->Release();
  return result;
}

}  // namespace editor
