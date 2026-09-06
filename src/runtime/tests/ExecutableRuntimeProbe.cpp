#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "ExecutableRuntime.hpp"

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--throw") {
    return tox::runtime::guardedMain([]() -> int { throw std::runtime_error("runtime probe exception"); });
  }
  if (argc == 2 && std::string(argv[1]) == "--abort") std::abort();
#ifdef _WIN32
  if (argc == 2 && std::string(argv[1]) == "--seh")
    RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#endif
  return tox::runtime::reportError("usage: executable_runtime_probe <--throw|--abort|--seh>", 2);
}
