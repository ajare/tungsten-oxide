#include "ExecutableRuntime.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <exception>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <crtdbg.h>
#include <stdlib.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace tox::runtime {
namespace {

#ifdef _WIN32
wchar_t gLogPath[32768]{};
SRWLOCK gLogLock = SRWLOCK_INIT;
#else
char gLogPath[PATH_MAX]{};
#endif

void initialiseLogPath() noexcept {
#ifdef _WIN32
  const DWORD length = GetModuleFileNameW(nullptr, gLogPath, static_cast<DWORD>(std::size(gLogPath)));
  if (length == 0 || length >= std::size(gLogPath)) {
    std::wcscpy(gLogPath, L"application.log");
    return;
  }

  wchar_t* filename = gLogPath;
  for (wchar_t* cursor = gLogPath; *cursor != L'\0'; ++cursor) {
    if (*cursor == L'\\' || *cursor == L'/') filename = cursor + 1;
  }
  wchar_t* extension = nullptr;
  for (wchar_t* cursor = filename; *cursor != L'\0'; ++cursor) {
    if (*cursor == L'.') extension = cursor;
  }
  wchar_t* suffix = extension != nullptr ? extension : gLogPath + length;
  const std::size_t remaining = std::size(gLogPath) - static_cast<std::size_t>(suffix - gLogPath);
  if (remaining >= 5) std::wcscpy(suffix, L".log");
#else
  const ssize_t length = readlink("/proc/self/exe", gLogPath, sizeof(gLogPath) - 1);
  if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(gLogPath)) {
    std::strcpy(gLogPath, "application.log");
    return;
  }
  gLogPath[length] = '\0';
  char* filename = std::strrchr(gLogPath, '/');
  filename = filename != nullptr ? filename + 1 : gLogPath;
  char* extension = std::strrchr(filename, '.');
  char* suffix = extension != nullptr ? extension : gLogPath + length;
  if (static_cast<std::size_t>(suffix - gLogPath) + 5 <= sizeof(gLogPath)) std::strcpy(suffix, ".log");
#endif
}

void appendError(const char* message, std::size_t length, const char* detailPrefix = nullptr,
                 std::size_t detailPrefixLength = 0) noexcept {
  constexpr char errorPrefix[] = "ERROR: ";
#ifdef _WIN32
  AcquireSRWLockExclusive(&gLogLock);
  HANDLE file = CreateFileW(gLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    auto write = [&](const char* bytes, std::size_t bytesLeft) {
      while (bytesLeft > 0) {
        const DWORD chunk = bytesLeft > MAXDWORD ? MAXDWORD : static_cast<DWORD>(bytesLeft);
        DWORD written = 0;
        if (!WriteFile(file, bytes, chunk, &written, nullptr) || written == 0) break;
        bytes += written;
        bytesLeft -= written;
      }
    };
    write(errorPrefix, sizeof(errorPrefix) - 1);
    if (detailPrefix != nullptr) write(detailPrefix, detailPrefixLength);
    write(message, length);
    write("\n", 1);
    FlushFileBuffers(file);
    CloseHandle(file);
  }
  ReleaseSRWLockExclusive(&gLogLock);
#else
  FILE* file = std::fopen(gLogPath, "ab");
  if (file != nullptr) {
    std::fwrite(errorPrefix, 1, sizeof(errorPrefix) - 1, file);
    if (detailPrefix != nullptr) std::fwrite(detailPrefix, 1, detailPrefixLength, file);
    std::fwrite(message, 1, length, file);
    std::fwrite("\n", 1, 1, file);
    std::fflush(file);
    std::fclose(file);
  }
#endif
}

[[noreturn]] void terminateHandler() noexcept {
  const std::exception_ptr failure = std::current_exception();
  if (failure != nullptr) {
    try {
      std::rethrow_exception(failure);
    } catch (const std::exception& error) {
      const char* message = error.what() != nullptr ? error.what() : "exception with no message";
      constexpr char prefix[] = "Unhandled exception: ";
      appendError(message, std::strlen(message), prefix, sizeof(prefix) - 1);
    } catch (...) {
      constexpr char message[] = "Unhandled unknown exception";
      appendError(message, sizeof(message) - 1);
    }
  } else {
    constexpr char message[] = "std::terminate called without an active exception";
    appendError(message, sizeof(message) - 1);
  }
  std::_Exit(EXIT_FAILURE);
}

[[noreturn]] void fatalSignalHandler(int signalNumber) noexcept {
  const char* message = "Process terminated by a fatal signal";
  switch (signalNumber) {
    case SIGABRT:
      message = "Process aborted (SIGABRT)";
      break;
    case SIGFPE:
      message = "Fatal arithmetic error (SIGFPE)";
      break;
    case SIGILL:
      message = "Illegal instruction (SIGILL)";
      break;
    case SIGSEGV:
      message = "Invalid memory access (SIGSEGV)";
      break;
  }
  appendError(message, std::strlen(message));
  std::_Exit(EXIT_FAILURE);
}

#ifdef _WIN32
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* details) noexcept {
  char message[96]{};
  const unsigned long code = details != nullptr && details->ExceptionRecord != nullptr ? details->ExceptionRecord->ExceptionCode : 0;
  const int length = std::snprintf(message, sizeof(message), "Unhandled Windows exception 0x%08lX", code);
  appendError(message, length > 0 ? static_cast<std::size_t>(length) : std::strlen(message));
  return EXCEPTION_EXECUTE_HANDLER;
}

void invalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) noexcept {
  constexpr char message[] = "Invalid parameter passed to the C runtime";
  appendError(message, sizeof(message) - 1);
  std::_Exit(EXIT_FAILURE);
}

void pureCallHandler() noexcept {
  constexpr char message[] = "Pure virtual function call";
  appendError(message, sizeof(message) - 1);
  std::_Exit(EXIT_FAILURE);
}
#endif

struct AutomaticInstaller {
  AutomaticInstaller() noexcept { installFailureHandlers(); }
};

const AutomaticInstaller automaticInstaller;

}  // namespace

void installFailureHandlers() noexcept {
  static const bool logPathInitialised = [] {
    initialiseLogPath();
    return true;
  }();
  (void)logPathInitialised;

  // Reapply these at the explicit main() boundary as well as during static initialisation, in case
  // a third-party global constructor installed its own interactive handler after ours.
  std::set_terminate(terminateHandler);
  std::signal(SIGABRT, fatalSignalHandler);

#ifdef _WIN32
  SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetUnhandledExceptionFilter(unhandledExceptionFilter);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_invalid_parameter_handler(invalidParameterHandler);
  _set_purecall_handler(pureCallHandler);
#ifdef _DEBUG
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
}

void logError(std::string_view message) noexcept {
  installFailureHandlers();
  appendError(message.data(), message.size());
}

int reportError(std::string_view message, int exitCode) noexcept {
  installFailureHandlers();
  constexpr char prefix[] = "ERROR: ";
  std::fwrite(prefix, 1, sizeof(prefix) - 1, stderr);
  std::fwrite(message.data(), 1, message.size(), stderr);
  std::fwrite("\n", 1, 1, stderr);
  std::fflush(stderr);
  appendError(message.data(), message.size());
  return exitCode == 0 ? EXIT_FAILURE : exitCode;
}

int reportUnhandledException(const char* message) noexcept {
  constexpr char prefix[] = "Unhandled exception: ";
  std::fwrite("ERROR: ", 1, 7, stderr);
  std::fwrite(prefix, 1, sizeof(prefix) - 1, stderr);
  if (message != nullptr) std::fwrite(message, 1, std::strlen(message), stderr);
  std::fwrite("\n", 1, 1, stderr);
  std::fflush(stderr);

  const char* safeMessage = message != nullptr ? message : "exception with no message";
  appendError(safeMessage, std::strlen(safeMessage), prefix, sizeof(prefix) - 1);
  return EXIT_FAILURE;
}

}  // namespace tox::runtime
