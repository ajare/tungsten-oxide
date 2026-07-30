// TrackCore.hpp — shared constants and stateless math used by native track
// loading/baking and the runtime physics step. TrackBake.cpp carries the larger
// authored-path evaluator while these reusable profile/query helpers stay here.
//
// The constants stay in the header (constexpr); the function bodies live in
// src/TrackCore.cpp.
#pragma once

namespace tox {
namespace TrackCore {

// --- schema/geometry constants ---
constexpr int TRACK_SCHEMA_VERSION = 11;
// Oldest schema version the native loader still accepts (no reservations field, always empty).
// CENTRAL_RESERVATION_PLAN.md M0: the committed golden fixture corpus under cpp/test-data/ is
// permanently schema 10, so the loader must keep reading it even though C++ now writes/normalizes
// to 11.
constexpr int TRACK_SCHEMA_VERSION_MIN_SUPPORTED = 10;
constexpr int N_DEFAULT = 400;
constexpr double COLLISION_WALL_MARGIN = 1.8;
constexpr double DEFAULT_WIDTH = 36.0;
constexpr double DEFAULT_RAIL_HEIGHT = 6.0;
constexpr double DEFAULT_CROSS_SECTION_TIGHTNESS = 1.0;
constexpr double DEFAULT_CROSS_SECTION_THICKNESS = 4.0;
constexpr double DEFAULT_BOOST_FACTOR = 1.5;
constexpr double DEFAULT_BOOST_DURATION = 2.0;
// Default local window (in segments) for self-intersection collapse: a crossing whose two
// branches are within this many segments of each other collapses by default; farther ones are
// kept. Public (not TrackBake.cpp-local) because the editor re-derives the same auto-collapse/
// auto-keep state at draw time and must agree with what the bake actually did.
constexpr int DEFAULT_SELF_INTERSECTION_SPAN = 100;

// Standard clamp, but pinned to a specific formula (not just "clamp to [lo, hi]" written however):
// several fixtures in the committed golden corpus (cpp/test-data/) depend on its exact floating-
// point behavior at the boundary, inherited from this engine's original reference implementation.
double clamp(double v, double lo, double hi);

// clampSignedUnit / clampTightness. Inputs here are already finite doubles from the baked trace,
// so the "non-finite -> fallback" branch is preserved only for faithfulness to that same reference
// behavior.
double clampSignedUnit(double n);
double clampTightness(double n);

// Road-surface rise above the flat chord, as a function of v (0 left .. 1 right).
double crossSectionHeight(double curvature, double tightness, double v, double chordWidth);
// d(height)/dv, used to build the surface normal across the road.
double crossSectionHeightDerivative(double curvature, double tightness, double v, double chordWidth);

// Is the ship's evaluator parameter gShip within a path zone's [gLo, gHi] window?
// For a closed path the window may straddle the wrap, so gShip is shifted by whole
// cycles into the window's neighbourhood before the range test.
bool zoneAlongContains(double gShip, double gLo, double gHi, double gMax, bool closed);

}  // namespace TrackCore

// --- centralized physics-loop constants ---
// Named Consts (not Physics) to avoid colliding with the Ship's `struct Physics`.
namespace Consts {
constexpr double ZONE_RELEASE = 1.0;
constexpr double CHECKPOINT_FLASH_MS = 500.0;
constexpr double TRIGGER_REARM_MARGIN = 3.0;
constexpr double SURFACE_SNAP_UP = 3.0;
constexpr double RESPAWN_FALL_DEPTH = 100.0;
constexpr double CORRIDOR_ALONG_TOL = 8.0;
constexpr double SEGMENT_ALONG_TOL = 0.5;
constexpr double MAX_PHYSICS_STEP = 1.0 / 120.0;
// Launches must crest beyond the collision surface's 4 m contact probe. At the
// default gravity this reaches roughly 7.5 m above a flat launch surface.
constexpr double MIN_LAUNCH_UPWARD_SPEED = 30.0;
constexpr double HANDLING_BASE_WEIGHT = 1000.0;
}  // namespace Consts

}  // namespace tox
