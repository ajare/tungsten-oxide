// parity_main.cpp — the hand-rolled parity replayer/comparator (CPP_PORT_PLAN.md
// §6: ~30-line assert/report harness, no doctest/Catch2).
//
// For each committed golden trace it does PER-STEP parity: load the step's input
// state (the prior step's `after`, or initialState for step 0), run exactly one
// stepPhysics, and compare the result to the recorded `after` under a single
// mixed absolute+relative tolerance |a-b| <= atol + rtol*|b|. Booleans must match
// exactly. It reports the worst-offending field/step (with ULP delta) so the gate
// can be calibrated from evidence rather than guessed.
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "Vec3.hpp"
#include "Track.hpp"
#include "Ship.hpp"
#include "Simulation.hpp"

using nlohmann::json;
using namespace tox;

static Vec3 jvec(const json& a) { return Vec3(a[0].get<double>(), a[1].get<double>(), a[2].get<double>()); }

static Track loadTrack(const json& world) {
  Track t;
  t.trackFloorY = world.at("trackFloorY").get<double>();
  for (const auto& id : world.at("connectedEndpointIds")) t.connectedEndpointIds.insert(id.get<std::string>());
  for (const auto& pj : world.at("paths")) {
    Path p;
    p.closed = pj.at("closed").get<bool>();
    const auto& eid = pj.at("endpointIds");
    if (eid.contains("start") && !eid.at("start").is_null()) { p.endpointIds.start = eid.at("start").get<std::string>(); p.endpointIds.hasStart = true; }
    if (eid.contains("end") && !eid.at("end").is_null()) { p.endpointIds.end = eid.at("end").get<std::string>(); p.endpointIds.hasEnd = true; }
    for (const auto& a : pj.at("anchors")) p.anchors.push_back(jvec(a));
    for (const auto& f : pj.at("centerline")) {
      Frame fr;
      fr.pos = jvec(f.at("pos")); fr.tangent = jvec(f.at("tangent"));
      fr.edgeRight = jvec(f.at("edgeRight")); fr.normal = jvec(f.at("normal"));
      fr.halfW = f.at("halfW").get<double>();
      fr.sLeft = f.at("sLeft").get<double>(); fr.sRight = f.at("sRight").get<double>();
      fr.crossSectionCurvature = f.at("crossSectionCurvature").get<double>();
      fr.crossSectionTightness = f.at("crossSectionTightness").get<double>();
      p.centerline.push_back(fr);
    }
    t.paths.push_back(std::move(p));
  }
  for (const auto& zj : world.value("zones", json::array())) {
    Zone z;
    z.id = zj.at("id").get<std::string>();
    z.kind = zj.at("kind").get<std::string>();
    z.effect = zj.at("effect").get<std::string>();
    z.factor = zj.value("factor", 0.0);
    z.duration = zj.value("duration", 0.0);
    z.hostPathIndex = zj.at("hostPathIndex").get<int>();
    z.gLo = zj.at("gLo").get<double>(); z.gHi = zj.at("gHi").get<double>();
    z.gMax = zj.at("gMax").get<double>(); z.closed = zj.at("closed").get<bool>();
    z.lateral = zj.at("lateral").get<double>(); z.halfWidth = zj.at("halfWidth").get<double>();
    t.zones.push_back(std::move(z));
  }
  for (const auto& tj : world.value("triggers", json::array())) {
    Trigger tr;
    tr.id = tj.at("id").get<std::string>();
    tr.type = tj.value("type", std::string());
    tr.role = tj.value("role", std::string());
    tr.direction = tj.value("direction", std::string("both"));
    tr.center = jvec(tj.at("center")); tr.right = jvec(tj.at("right"));
    tr.up = jvec(tj.at("up")); tr.fwd = jvec(tj.at("fwd"));
    tr.halfWidth = tj.at("halfWidth").get<double>(); tr.height = tj.at("height").get<double>();
    t.triggers.push_back(std::move(tr));
  }
  return t;
}

static Ship loadShip(const json& s) {
  Ship ship;
  Physics& p = ship.physics;
  const auto& ph = s.at("physics");
  auto D = [&](const char* k) { return ph.at(k).get<double>(); };
  auto B = [&](const char* k) { return ph.at(k).get<bool>(); };
  p.heading = D("heading"); p.speed = D("speed"); p.maxSpeed = D("maxSpeed"); p.maxReverse = D("maxReverse");
  p.accel = D("accel"); p.brakeDecel = D("brakeDecel"); p.friction = D("friction"); p.turnRate = D("turnRate");
  p.grip = D("grip"); p.wallRestitution = D("wallRestitution"); p.weight = D("weight"); p.bobTime = D("bobTime");
  p.visualBank = D("visualBank"); p.visualPitch = D("visualPitch"); p.airborne = B("airborne");
  p.verticalVel = D("verticalVel"); p.gravity = D("gravity"); p.landingBounce = D("landingBounce");
  p.landingBounceVel = D("landingBounceVel"); p.boostActive = B("boostActive"); p.boostReleasing = B("boostReleasing");
  p.boostHold = D("boostHold"); p.boostReleaseT = D("boostReleaseT"); p.boostCap = D("boostCap"); p.boostEffCap = D("boostEffCap");
  p.up = jvec(ph.at("up")); p.forward = jvec(ph.at("forward")); p.right = jvec(ph.at("right"));
  p.groundPos = jvec(ph.at("groundPos")); p.visualGroundPos = jvec(ph.at("visualGroundPos"));
  p.visualUp = jvec(ph.at("visualUp")); p.moveDir = jvec(ph.at("moveDir"));
  ship.prevTriggerPos = jvec(s.at("prevTriggerPos"));

  for (const auto& e : s.value("zoneInside", json::array()))
    ship.zoneInside[e[0].get<std::string>()] = e[1].get<bool>();
  for (const auto& e : s.value("triggerStates", json::array())) {
    TriggerState st; st.armed = e[1].at("armed").get<bool>(); st.flash = e[1].at("flash").get<double>();
    ship.triggerStates[e[0].get<std::string>()] = st;
  }
  const auto& cp = s.at("lastCheckpoint");
  ship.lastCheckpoint.valid = cp.at("valid").get<bool>();
  ship.lastCheckpoint.triggerId = cp.at("triggerId").is_null() ? std::string() : cp.at("triggerId").get<std::string>();
  ship.lastCheckpoint.pos = jvec(cp.at("pos"));
  ship.lastCheckpoint.forward = jvec(cp.at("forward"));
  ship.lastCheckpoint.up = jvec(cp.at("up"));
  if (s.contains("race")) {
    const auto& rj = s.at("race");
    ship.race.laps = rj.at("laps").get<int>();
    for (const auto& h : rj.at("hit")) ship.race.hit.insert(h.get<std::string>());
    for (const auto& id : rj.value("intermediateIds", json::array())) ship.race.intermediateIds.push_back(id.get<std::string>());
    if (rj.contains("finishId") && !rj.at("finishId").is_null()) ship.race.finishId = rj.at("finishId").get<std::string>();
  }
  if (s.contains("startPose") && !s.at("startPose").is_null()) {
    const auto& sp = s.at("startPose");
    ship.startPose = Pose{ jvec(sp.at("pos")), jvec(sp.at("up")), jvec(sp.at("forward")) };
  }
  return ship;
}

// Total-ordering ULP distance between two doubles.
static long long ulpDelta(double a, double b) {
  if (a == b) return 0;
  if (std::isnan(a) || std::isnan(b)) return -1;
  int64_t ia, ib;
  std::memcpy(&ia, &a, sizeof(ia));
  std::memcpy(&ib, &b, sizeof(ib));
  if (ia < 0) ia = (int64_t)0x8000000000000000ULL - ia;
  if (ib < 0) ib = (int64_t)0x8000000000000000ULL - ib;
  long long d = ia - ib;
  return d < 0 ? -d : d;
}

struct Worst {
  double ratio = 0.0, absD = 0.0, a = 0, b = 0;
  long long ulps = 0;
  int step = -1;
  std::string field, trace;
};

static double posDrift(const Ship& ship, const json& after) {
  const json& g = after.at("physics").at("groundPos");
  const double dx = ship.physics.groundPos.x - g[0].get<double>();
  const double dy = ship.physics.groundPos.y - g[1].get<double>();
  const double dz = ship.physics.groundPos.z - g[2].get<double>();
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The documented growing-tolerance envelope for the bounded-trajectory smoke
// check (CPP_PORT_PLAN.md milestone 3). Free-running, the C++ engine threads its
// own output back in, so per-step transcendental drift (~1 ULP) compounds. We do
// NOT pretend a long run stays close — the envelope grows geometrically and the
// assertion is only that the trajectory tracks the JS one out to a documented
// horizon before chaotic divergence is allowed to take over.
static double trajectoryEnvelope(int k) {
  return 1e-9 * std::pow(1.10, (double)k);   // 1 nm base, +10% per 1/120 s step
}

int main(int argc, char** argv) {
  // Evidence-based calibration (CPP_PORT_PLAN.md §4, LOCKED at milestone 3).
  // Across the full committed corpus (4000 steps: kinematics + guard-rail
  // corridor + airborne/landing + zone boost + checkpoint/lap + respawn) the
  // worst per-step divergence is still 1 ULP (|a-b|~1.1e-16), i.e. worst combined
  // ratio ~7.3e-5 at atol=rtol=1e-12 (open-curve moveDir.x). Adding the M2
  // zone/trigger/respawn tracks did not move the worst offender, so the gate
  // stays at ~14x above that observed worst — robust to a few ULP of
  // cross-platform libm variance yet still catching any real regression (which
  // blows the ratio past 1). Override with --atol= --rtol= --gate=.
  double atol = 1e-12, rtol = 1e-12, gate = 1e-3;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto eat = [&](const char* flag, double& dst) {
      if (a.rfind(flag, 0) == 0) { dst = std::stod(a.substr(std::strlen(flag))); return true; }
      return false;
    };
    if (eat("--atol=", atol) || eat("--rtol=", rtol) || eat("--gate=", gate)) continue;
    files.push_back(a);
  }
  if (files.empty()) { std::cerr << "usage: parity [--atol= --rtol= --gate=] trace.json...\n"; return 2; }

  // Minimum number of free-running steps every trace must track the JS
  // trajectory within trajectoryEnvelope() before chaotic divergence is
  // tolerated. Documented, deliberately modest — the per-step gate is the real
  // guarantee; this only catches gross structural drift (a whole branch wrong).
  constexpr int FREE_MIN_HORIZON = 100;

  Worst worst;
  int boolFails = 0, totalSteps = 0, freeFails = 0;

  for (const auto& file : files) {
    std::ifstream in(file);
    if (!in) { std::cerr << "cannot open " << file << "\n"; return 2; }
    json trace; in >> trace;
    const std::string name = trace.value("meta", json::object()).value("name", file);
    Track track = loadTrack(trace.at("world"));
    Simulation sim(track);

    const json* before = &trace.at("initialState");
    const auto& steps = trace.at("steps");
    int localBoolFails = 0;
    double localWorstRatio = 0.0;

    for (size_t i = 0; i < steps.size(); ++i) {
      Ship ship = loadShip(*before);
      const auto& ctrl = steps[i].at("control");
      sim.stepPhysics(ship, ctrl.at("dt").get<double>(), ctrl.at("throttle").get<double>(),
                      ctrl.at("brake").get<double>(), ctrl.at("steer").get<double>());
      const json& exp = steps[i].at("after");
      const json& eph = exp.at("physics");

      auto checkD = [&](const std::string& fld, double got, double want) {
        const double ad = std::fabs(got - want);
        const double ratio = ad / (atol + rtol * std::fabs(want));
        if (ratio > localWorstRatio) localWorstRatio = ratio;
        if (ratio > worst.ratio) {
          worst = { ratio, ad, got, want, ulpDelta(got, want), (int)i, fld, name };
        }
      };
      auto checkV = [&](const std::string& fld, const Vec3& got, const json& want) {
        checkD(fld + ".x", got.x, want[0].get<double>());
        checkD(fld + ".y", got.y, want[1].get<double>());
        checkD(fld + ".z", got.z, want[2].get<double>());
      };
      auto checkB = [&](const std::string& fld, bool got, bool want) {
        if (got != want) { ++localBoolFails; std::cerr << "  BOOL MISMATCH " << name << " step " << i << " " << fld << ": got " << got << " want " << want << "\n"; }
      };
      auto checkStr = [&](const std::string& fld, const std::string& got, const std::string& want) {
        if (got != want) { ++localBoolFails; std::cerr << "  STR MISMATCH " << name << " step " << i << " " << fld << ": got '" << got << "' want '" << want << "'\n"; }
      };

      const Physics& p = ship.physics;
      checkD("heading", p.heading, eph.at("heading")); checkD("speed", p.speed, eph.at("speed"));
      checkD("maxSpeed", p.maxSpeed, eph.at("maxSpeed")); checkD("maxReverse", p.maxReverse, eph.at("maxReverse"));
      checkD("accel", p.accel, eph.at("accel")); checkD("brakeDecel", p.brakeDecel, eph.at("brakeDecel"));
      checkD("friction", p.friction, eph.at("friction")); checkD("turnRate", p.turnRate, eph.at("turnRate"));
      checkD("grip", p.grip, eph.at("grip")); checkD("wallRestitution", p.wallRestitution, eph.at("wallRestitution"));
      checkD("weight", p.weight, eph.at("weight"));
      checkD("verticalVel", p.verticalVel, eph.at("verticalVel")); checkD("gravity", p.gravity, eph.at("gravity"));
      checkD("landingBounce", p.landingBounce, eph.at("landingBounce"));
      checkD("landingBounceVel", p.landingBounceVel, eph.at("landingBounceVel"));
      checkD("boostHold", p.boostHold, eph.at("boostHold")); checkD("boostReleaseT", p.boostReleaseT, eph.at("boostReleaseT"));
      checkD("boostCap", p.boostCap, eph.at("boostCap")); checkD("boostEffCap", p.boostEffCap, eph.at("boostEffCap"));
      checkB("airborne", p.airborne, eph.at("airborne").get<bool>());
      checkB("boostActive", p.boostActive, eph.at("boostActive").get<bool>());
      checkB("boostReleasing", p.boostReleasing, eph.at("boostReleasing").get<bool>());
      checkV("up", p.up, eph.at("up")); checkV("forward", p.forward, eph.at("forward"));
      checkV("right", p.right, eph.at("right")); checkV("groundPos", p.groundPos, eph.at("groundPos"));
      checkV("moveDir", p.moveDir, eph.at("moveDir"));
      checkV("prevTriggerPos", ship.prevTriggerPos, exp.at("prevTriggerPos"));

      // --- M2 detection state: zones, trigger gates, checkpoint, lap gate ---
      for (const auto& e : exp.value("zoneInside", json::array())) {
        const std::string id = e[0].get<std::string>();
        const bool got = ship.zoneInside.count(id) ? ship.zoneInside[id] : false;
        checkB("zoneInside[" + id + "]", got, e[1].get<bool>());
      }
      for (const auto& e : exp.value("triggerStates", json::array())) {
        const std::string id = e[0].get<std::string>();
        const TriggerState& st = ship.triggerStates[id];
        checkB("triggerStates[" + id + "].armed", st.armed, e[1].at("armed").get<bool>());
        checkD("triggerStates[" + id + "].flash", st.flash, e[1].at("flash"));
      }
      const json& ecp = exp.at("lastCheckpoint");
      checkB("lastCheckpoint.valid", ship.lastCheckpoint.valid, ecp.at("valid").get<bool>());
      checkStr("lastCheckpoint.triggerId", ship.lastCheckpoint.triggerId,
               ecp.at("triggerId").is_null() ? std::string() : ecp.at("triggerId").get<std::string>());
      checkV("lastCheckpoint.pos", ship.lastCheckpoint.pos, ecp.at("pos"));
      checkV("lastCheckpoint.forward", ship.lastCheckpoint.forward, ecp.at("forward"));
      checkV("lastCheckpoint.up", ship.lastCheckpoint.up, ecp.at("up"));
      if (exp.contains("race")) {
        const json& erace = exp.at("race");
        if (ship.race.laps != erace.at("laps").get<int>()) {
          ++localBoolFails;
          std::cerr << "  LAPS MISMATCH " << name << " step " << i << ": got " << ship.race.laps << " want " << erace.at("laps").get<int>() << "\n";
        }
        std::set<std::string> wantHit;
        for (const auto& h : erace.at("hit")) wantHit.insert(h.get<std::string>());
        if (ship.race.hit != wantHit) {
          ++localBoolFails;
          std::cerr << "  RACE.HIT MISMATCH " << name << " step " << i << " (size got " << ship.race.hit.size() << " want " << wantHit.size() << ")\n";
        }
      }

      before = &steps[i].at("after");
      ++totalSteps;
    }
    boolFails += localBoolFails;
    std::printf("  %-18s steps=%zu  worstRatio=%.3g%s\n", name.c_str(), steps.size(),
                localWorstRatio, localBoolFails ? "  [BOOL FAIL]" : "");

    // --- Bounded-trajectory smoke check: free-run, own output threaded back ---
    Ship freeShip = loadShip(trace.at("initialState"));
    int horizon = (int)steps.size();
    double driftAtHorizon = 0.0;
    for (size_t i = 0; i < steps.size(); ++i) {
      const auto& ctrl = steps[i].at("control");
      sim.stepPhysics(freeShip, ctrl.at("dt").get<double>(), ctrl.at("throttle").get<double>(),
                      ctrl.at("brake").get<double>(), ctrl.at("steer").get<double>());
      const double drift = posDrift(freeShip, steps[i].at("after"));
      if (drift > trajectoryEnvelope((int)i)) { horizon = (int)i; driftAtHorizon = drift; break; }
    }
    if (horizon < (int)steps.size())
      std::printf("      free-run: tracks JS to step %d/%zu (drift %.3g m exceeds envelope %.3g m); chaotic beyond\n",
                  horizon, steps.size(), driftAtHorizon, trajectoryEnvelope(horizon));
    else
      std::printf("      free-run: tracks JS within envelope for all %zu steps\n", steps.size());
    if (horizon < FREE_MIN_HORIZON) {
      ++freeFails;
      std::fprintf(stderr, "  FREE-RUN TOO SHORT %s: horizon %d < required %d steps\n", name.c_str(), horizon, FREE_MIN_HORIZON);
    }
  }

  std::printf("\nparity over %d steps @ atol=%g rtol=%g (gate=%gx)\n", totalSteps, atol, rtol, gate);
  std::printf("  worst combined ratio = %.4g  (step %d, field %s, trace %s)\n",
              worst.ratio, worst.step, worst.field.c_str(), worst.trace.c_str());
  std::printf("    a=%.17g  b=%.17g  |a-b|=%.3g  ulpDelta=%lld\n", worst.a, worst.b, worst.absD, worst.ulps);

  const bool pass = (worst.ratio <= gate) && (boolFails == 0) && (freeFails == 0);
  std::printf("  %s (worst ratio %.4g %s gate %g; bool mismatches %d; free-run failures %d)\n",
              pass ? "PASS" : "FAIL", worst.ratio, worst.ratio <= gate ? "<=" : ">", gate, boolFails, freeFails);
  return pass ? 0 : 1;
}
