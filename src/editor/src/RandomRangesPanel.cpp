#include "RandomRangesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"

namespace editor {
namespace {

double clampNum(double v, double lo, double hi, double fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }
int clampInt(int v, int lo, int hi) { return std::clamp(v, lo, hi); }

// Clamps every field to its own range, then fixes each min/max pair's ordering so a lerp never
// sees max < min.
void sanitize(RandomTrackRanges& r) {
  const RandomTrackRanges d;  // default-constructed == RANDOM_RANGE_DEFAULTS
  r.lengthMin = clampNum(r.lengthMin, 500.0, 100000.0, d.lengthMin);
  r.lengthMax = clampNum(r.lengthMax, 500.0, 100000.0, d.lengthMax);
  r.turnsMin = clampInt(r.turnsMin, 4, 40);
  r.turnsMax = clampInt(r.turnsMax, 4, 40);
  r.maxBanking = clampNum(r.maxBanking, 0.0, 60.0, d.maxBanking);
  r.maxHill = clampNum(r.maxHill, 0.0, 5000.0, d.maxHill);
  r.widthMin = clampNum(r.widthMin, 1.0, 2000.0, d.widthMin);
  r.widthMax = clampNum(r.widthMax, 1.0, 2000.0, d.widthMax);
  r.maxCurvature = clampNum(r.maxCurvature, 0.0, 1.0, d.maxCurvature);
  r.boostMin = clampInt(r.boostMin, 0, 50);
  r.boostMax = clampInt(r.boostMax, 0, 50);

  if (r.lengthMax < r.lengthMin) r.lengthMax = r.lengthMin;
  if (r.turnsMax < r.turnsMin) r.turnsMax = r.turnsMin;
  if (r.widthMax < r.widthMin) r.widthMax = r.widthMin;
  if (r.boostMax < r.boostMin) r.boostMax = r.boostMin;
}

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

bool doubleRow(const char* label, double& lo, double& hi, const char* fmt = "%.1f") {
  bool changed = false;
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble((std::string("Min##") + label).c_str(), &lo, 0.0, 0.0, fmt, kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble((std::string("Max##") + label).c_str(), &hi, 0.0, 0.0, fmt, kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  return changed;
}

bool intRow(const char* label, int& lo, int& hi) {
  bool changed = false;
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputInt((std::string("Min##") + label).c_str(), &lo);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputInt((std::string("Max##") + label).c_str(), &hi);
  return changed;
}

}  // namespace

bool DrawRandomRangesPanel(EditorState& state, RandomTrackRanges& ranges, int& seed, int& complexity) {
  bool changed = false;

  // Formerly the menu bar's "Random" menu -- moved here so the generator controls sit directly
  // above the ranges they use.
  ImGui::TextUnformatted("Single-loop generator; see RandomTrack.hpp for scope.");
  ImGui::SetNextItemWidth(160);
  ImGui::InputInt("Seed", &seed);
  ImGui::SetNextItemWidth(160);
  ImGui::SliderInt("Complexity", &complexity, 1, 10);
  bool generated = false;
  if (ImGui::Button("Generate New Random Track")) {
    // Mirrors applyRandomTrack()'s pushUndo(): replaceTrack() alone doesn't touch history, so the
    // pre-generation state has to be pushed explicitly to stay undoable.
    state.history().push(state.track());
    state.replaceTrack(generateRandomTrack(complexity, static_cast<std::uint32_t>(seed), ranges));
    generated = true;
  }
  ImGui::Separator();

  ImGui::TextUnformatted("Length (m)");
  changed |= doubleRow("length", ranges.lengthMin, ranges.lengthMax);
  ImGui::TextUnformatted("Turns");
  changed |= intRow("turns", ranges.turnsMin, ranges.turnsMax);
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Max Banking (deg)", &ranges.maxBanking, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Max Hill (m)", &ranges.maxHill, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::TextUnformatted("Width");
  changed |= doubleRow("width", ranges.widthMin, ranges.widthMax);
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Max Curvature", &ranges.maxCurvature, 0.0, 0.0, "%.2f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::Separator();
  ImGui::TextUnformatted("Boost Zones");
  changed |= intRow("boost", ranges.boostMin, ranges.boostMax);

  if (changed) sanitize(ranges);

  ImGui::Separator();
  if (ImGui::Button("Reset to Default")) {
    ranges = RandomTrackRanges{};
    changed = true;
  }

  return generated;
}

}  // namespace editor
