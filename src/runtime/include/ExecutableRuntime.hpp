#pragma once

#include <exception>
#include <string_view>
#include <utility>

namespace tox::runtime {

// Installs process-wide handlers that write fatal failures to <executable>.log and suppress
// Windows Error Reporting, CRT assertion, abort, and invalid-parameter dialogs. CMake also adds an
// automatic installer to every repository executable, so this is safe (and cheap) to call again.
void installFailureHandlers() noexcept;

// Appends one clearly-labelled error to <executable>.log.
void logError(std::string_view message) noexcept;

// Reports to stderr and <executable>.log, then returns the requested non-zero status.
int reportError(std::string_view message, int exitCode = 1) noexcept;

// Reports an exception without allocating while constructing the diagnostic.
int reportUnhandledException(const char* message) noexcept;

// Outermost exception boundary for application entry points. Process-level handlers remain the
// fallback for failures that cannot be represented as C++ exceptions (SEH, abort, purecall, etc.).
template <typename Function>
int guardedMain(Function&& function) noexcept {
  installFailureHandlers();
  try {
    return std::forward<Function>(function)();
  } catch (const std::exception& error) {
    return reportUnhandledException(error.what());
  } catch (...) {
    return reportUnhandledException("unknown exception");
  }
}

}  // namespace tox::runtime
