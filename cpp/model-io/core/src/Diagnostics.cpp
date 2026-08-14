#include "modelio/Diagnostics.hpp"

#include <algorithm>

namespace modelio {
namespace {

const char* severityName(Severity severity) { return severity == Severity::Error ? "error" : "warning"; }

}  // namespace

void Report::warn(std::string code, std::string message, std::string mesh, std::string material) {
  diagnostics_.push_back({strict_ ? Severity::Error : Severity::Warning, std::move(code), std::move(message),
                          std::move(mesh), std::move(material)});
}

void Report::error(std::string code, std::string message, std::string mesh, std::string material) {
  diagnostics_.push_back({Severity::Error, std::move(code), std::move(message), std::move(mesh), std::move(material)});
}

bool Report::hasErrors() const {
  return std::any_of(diagnostics_.begin(), diagnostics_.end(),
                     [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

std::size_t Report::errorCount() const {
  return static_cast<std::size_t>(
      std::count_if(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& d) { return d.severity == Severity::Error; }));
}

std::size_t Report::warningCount() const {
  return static_cast<std::size_t>(std::count_if(diagnostics_.begin(), diagnostics_.end(),
                                                [](const Diagnostic& d) { return d.severity == Severity::Warning; }));
}

bool Report::has(const std::string& code) const {
  return std::any_of(diagnostics_.begin(), diagnostics_.end(), [&](const Diagnostic& d) { return d.code == code; });
}

void Report::append(const Report& other) {
  diagnostics_.insert(diagnostics_.end(), other.diagnostics_.begin(), other.diagnostics_.end());
}

std::string Report::format() const {
  std::string out;
  for (const Diagnostic& d : diagnostics_) {
    if (!out.empty()) out += '\n';
    out += severityName(d.severity);
    out += ": [";
    out += d.code;
    out += "] ";
    if (!d.mesh.empty()) {
      out += "mesh '";
      out += d.mesh;
      out += "': ";
    }
    if (!d.material.empty()) {
      out += "material '";
      out += d.material;
      out += "': ";
    }
    out += d.message;
  }
  return out;
}

}  // namespace modelio
