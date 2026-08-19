// Diagnostics.hpp — the one report type every model-io entry point speaks.
//
// Conversion here fails in many small, specific ways (a channel the source didn't supply, a
// material feature the renderer can't express, a texture that can't be reached by a relative path),
// and both consumers need the same information presented differently: gltf_convert prints it to
// stderr, the editor's import dialog lists it before the user commits. So diagnostics are collected
// as structured records rather than thrown or logged -- a caller decides how to render them, and a
// failed conversion still reports everything it found rather than only the first problem.
//
// `code` is a stable, machine-readable id (e.g. "channel.synthesised.colour4"). Tests assert on it;
// UI groups by it; the message text stays free to change.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace modelio {

enum class Severity { Warning,
                      Error };

struct Diagnostic {
  Severity severity{Severity::Error};
  std::string code;
  std::string message;
  // Context, either or both empty when the finding isn't scoped to one of them.
  std::string mesh;
  std::string material;
};

class Report {
public:
  // In strict mode every warn() is recorded as an error instead -- the --strict flag
  // (docs/GLTF_IMPORT_PLAN.md), which promotes "we invented this channel for you" into a refusal.
  void setStrict(bool strict) { strict_ = strict; }
  bool strict() const { return strict_; }

  void warn(std::string code, std::string message, std::string mesh = {}, std::string material = {});
  void error(std::string code, std::string message, std::string mesh = {}, std::string material = {});

  bool hasErrors() const;
  std::size_t errorCount() const;
  std::size_t warningCount() const;
  bool has(const std::string& code) const;

  const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
  void append(const Report& other);

  // One line per diagnostic, newline-separated, no trailing newline. Empty for an empty report.
  std::string format() const;

private:
  std::vector<Diagnostic> diagnostics_;
  bool strict_{false};
};

}  // namespace modelio
