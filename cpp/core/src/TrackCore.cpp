// TrackCore.cpp — bodies of the stateless shared math declared in
// include/TrackCore.hpp (transliterated 1:1 from track-core.js).
#include "TrackCore.hpp"
#include <algorithm>
#include <cmath>

namespace tox {
namespace TrackCore {

double clamp(double v, double lo, double hi) {
  return std::max(lo, std::min(hi, v));
}

double clampSignedUnit(double n) {
  if (!std::isfinite(n)) return 0.0;
  return std::max(-1.0, std::min(1.0, n));
}
double clampTightness(double n) {
  if (!std::isfinite(n)) return DEFAULT_CROSS_SECTION_TIGHTNESS;
  return std::max(0.2, std::min(4.0, n));
}

double crossSectionHeight(double curvature, double tightness, double v, double chordWidth) {
  const double c = clampSignedUnit(curvature);
  if (c == 0.0) return 0.0;
  const double u = 2.0 * std::max(0.0, std::min(1.0, v)) - 1.0;
  const double base = std::sqrt(std::max(0.0, 1.0 - u * u));
  return c * (chordWidth / 2.0) * std::pow(base, clampTightness(tightness));
}

double crossSectionHeightDerivative(double curvature, double tightness, double v, double chordWidth) {
  const double c = clampSignedUnit(curvature), k = clampTightness(tightness);
  if (c == 0.0) return 0.0;
  const double u = 2.0 * std::max(0.001, std::min(0.999, v)) - 1.0;
  const double base = std::sqrt(std::max(0.000001, 1.0 - u * u));
  return c * (chordWidth / 2.0) * k * (-2.0 * u) * std::pow(base, k - 2.0);
}

bool zoneAlongContains(double gShip, double gLo, double gHi, double gMax, bool closed) {
  if (!closed) return gShip >= gLo - 1e-9 && gShip <= gHi + 1e-9;
  const double center = (gLo + gHi) / 2.0;
  double g = gShip;
  while (g < center - gMax / 2.0) g += gMax;
  while (g > center + gMax / 2.0) g -= gMax;
  return g >= gLo - 1e-9 && g <= gHi + 1e-9;
}

}  // namespace TrackCore
}  // namespace tox
