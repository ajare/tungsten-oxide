// raw_session_parity_main.cpp — the raw-session parity replayer/comparator,
// modeled directly on parity_main.cpp's structure but operating on a roster instead of one ship.
//
// Two fixture kinds, dispatched by `meta.kind`:
//  - "raw-session-init": Track::fromJson(sourceTrack) -> GameSession(track,
//    shipCount) -> compare the freshly-built roster against the fixture's
//    recorded roster. Proves independent native ship-factory and
//    starting-grid initialization against the recorded fixture, with no externally-created
//    initialState involved.
//  - "raw-session-step": for each recorded frame, load the "before" roster +
//    session time into a GameSession, call step(intents, dt), and compare the
//    resulting roster + emitted events against the recorded "after"/events —
//    the same bit-exact-replay-from-recorded-state technique raw_parity uses,
//    so this gate stays drift-free rather than tolerance-growing.
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "GameSession.hpp"
#include "ShipFactory.hpp"
#include "Ship.hpp"
#include "Simulation.hpp"
#include "Track.hpp"
#include "Vec3.hpp"

using nlohmann::json;
using namespace tox;

static Vec3 jvec(const json& a) { return Vec3(a[0].get<double>(), a[1].get<double>(), a[2].get<double>()); }

static Ship loadShip(const json& s) {
  Ship ship;
  Physics& p = ship.physics;
  const auto& ph = s.at("physics");
  auto D = [&](const char* k) { return ph.at(k).get<double>(); };
  auto B = [&](const char* k) { return ph.at(k).get<bool>(); };
  p.heading = D("heading");
  p.speed = D("speed");
  p.maxSpeed = D("maxSpeed");
  p.maxReverse = D("maxReverse");
  p.accel = D("accel");
  p.brakeDecel = D("brakeDecel");
  p.friction = D("friction");
  p.turnRate = D("turnRate");
  p.grip = D("grip");
  p.wallRestitution = D("wallRestitution");
  p.weight = D("weight");
  p.bobTime = D("bobTime");
  p.visualBank = D("visualBank");
  p.visualPitch = D("visualPitch");
  p.airborne = B("airborne");
  p.verticalVel = D("verticalVel");
  p.gravity = D("gravity");
  p.landingBounce = D("landingBounce");
  p.landingBounceVel = D("landingBounceVel");
  p.boostActive = B("boostActive");
  p.boostReleasing = B("boostReleasing");
  p.boostHold = D("boostHold");
  p.boostReleaseT = D("boostReleaseT");
  p.boostCap = D("boostCap");
  p.boostEffCap = D("boostEffCap");
  p.up = jvec(ph.at("up"));
  p.forward = jvec(ph.at("forward"));
  p.right = jvec(ph.at("right"));
  p.groundPos = jvec(ph.at("groundPos"));
  p.visualGroundPos = jvec(ph.at("visualGroundPos"));
  p.visualUp = jvec(ph.at("visualUp"));
  p.moveDir = jvec(ph.at("moveDir"));
  ship.prevTriggerPos = jvec(s.at("prevTriggerPos"));

  for (const auto& e : s.value("zoneInside", json::array()))
    ship.zoneInside[e[0].get<std::string>()] = e[1].get<bool>();
  for (const auto& e : s.value("triggerStates", json::array())) {
    TriggerState st;
    st.armed = e[1].at("armed").get<bool>();
    st.flash = e[1].at("flash").get<double>();
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
    ship.startPose = Pose{jvec(sp.at("pos")), jvec(sp.at("up")), jvec(sp.at("forward"))};
  }
  return ship;
}

static GameEventType eventTypeFromString(const std::string& s) {
  if (s == "TriggerFired") return GameEventType::TriggerFired;
  if (s == "CheckpointAccepted") return GameEventType::CheckpointAccepted;
  if (s == "LapCompleted") return GameEventType::LapCompleted;
  if (s == "Respawned") return GameEventType::Respawned;
  return GameEventType::RailHit;
}
static const char* eventTypeName(GameEventType type) {
  switch (type) {
    case GameEventType::TriggerFired: return "TriggerFired";
    case GameEventType::CheckpointAccepted: return "CheckpointAccepted";
    case GameEventType::LapCompleted: return "LapCompleted";
    case GameEventType::Respawned: return "Respawned";
    case GameEventType::RailHit: return "RailHit";
  }
  return "?";
}

struct Worst {
  double ratio = 0.0, absD = 0.0, a = 0, b = 0;
  int frame = -1;
  std::string field, trace;
};

int failures = 0;
Worst worst;

static void checkD(const std::string& trace, int frame, const std::string& field, double got, double want, double atol,
                    double rtol) {
  const double ad = std::fabs(got - want);
  const double ratio = ad / (atol + rtol * std::fabs(want));
  if (ratio > worst.ratio) worst = {ratio, ad, got, want, frame, field, trace};
}
static void checkV(const std::string& trace, int frame, const std::string& field, const Vec3& got, const json& want,
                    double atol, double rtol) {
  checkD(trace, frame, field + ".x", got.x, want[0].get<double>(), atol, rtol);
  checkD(trace, frame, field + ".y", got.y, want[1].get<double>(), atol, rtol);
  checkD(trace, frame, field + ".z", got.z, want[2].get<double>(), atol, rtol);
}
static void checkB(const std::string& trace, int frame, const std::string& field, bool got, bool want) {
  if (got != want) {
    ++failures;
    std::cerr << "  BOOL MISMATCH " << trace << " frame " << frame << " " << field << ": got " << got << " want " << want
              << "\n";
  }
}
static void checkStr(const std::string& trace, int frame, const std::string& field, const std::string& got,
                      const std::string& want) {
  if (got != want) {
    ++failures;
    std::cerr << "  STR MISMATCH " << trace << " frame " << frame << " " << field << ": got '" << got << "' want '"
              << want << "'\n";
  }
}

// Compares one ship's full state against its serialized fixture form. Mirrors
// parity_main.cpp's per-field checks so the same fields are held to the same
// discipline as the rest of the parity corpus.
static void compareShip(const std::string& trace, int frame, int shipIndex, const Ship& ship, const json& exp,
                         double atol, double rtol) {
  const std::string prefix = "ship[" + std::to_string(shipIndex) + "].";
  const json& eph = exp.at("physics");
  const Physics& p = ship.physics;
  checkD(trace, frame, prefix + "heading", p.heading, eph.at("heading"), atol, rtol);
  checkD(trace, frame, prefix + "speed", p.speed, eph.at("speed"), atol, rtol);
  checkD(trace, frame, prefix + "maxSpeed", p.maxSpeed, eph.at("maxSpeed"), atol, rtol);
  checkD(trace, frame, prefix + "accel", p.accel, eph.at("accel"), atol, rtol);
  checkD(trace, frame, prefix + "turnRate", p.turnRate, eph.at("turnRate"), atol, rtol);
  checkD(trace, frame, prefix + "weight", p.weight, eph.at("weight"), atol, rtol);
  checkD(trace, frame, prefix + "verticalVel", p.verticalVel, eph.at("verticalVel"), atol, rtol);
  checkD(trace, frame, prefix + "landingBounce", p.landingBounce, eph.at("landingBounce"), atol, rtol);
  checkD(trace, frame, prefix + "boostHold", p.boostHold, eph.at("boostHold"), atol, rtol);
  checkD(trace, frame, prefix + "boostCap", p.boostCap, eph.at("boostCap"), atol, rtol);
  checkB(trace, frame, prefix + "airborne", p.airborne, eph.at("airborne").get<bool>());
  checkB(trace, frame, prefix + "boostActive", p.boostActive, eph.at("boostActive").get<bool>());
  checkV(trace, frame, prefix + "up", p.up, eph.at("up"), atol, rtol);
  checkV(trace, frame, prefix + "forward", p.forward, eph.at("forward"), atol, rtol);
  checkV(trace, frame, prefix + "groundPos", p.groundPos, eph.at("groundPos"), atol, rtol);
  checkV(trace, frame, prefix + "moveDir", p.moveDir, eph.at("moveDir"), atol, rtol);

  const auto& cp = exp.at("lastCheckpoint");
  checkB(trace, frame, prefix + "lastCheckpoint.valid", ship.lastCheckpoint.valid, cp.at("valid").get<bool>());
  checkStr(trace, frame, prefix + "lastCheckpoint.triggerId", ship.lastCheckpoint.triggerId,
           cp.at("triggerId").is_null() ? std::string() : cp.at("triggerId").get<std::string>());

  if (exp.contains("race")) {
    const auto& rj = exp.at("race");
    if (ship.race.laps != rj.at("laps").get<int>()) {
      ++failures;
      std::cerr << "  INT MISMATCH " << trace << " frame " << frame << " " << prefix << "race.laps: got "
                << ship.race.laps << " want " << rj.at("laps").get<int>() << "\n";
    }
    if (ship.race.hit.size() != rj.at("hit").size()) {
      ++failures;
      std::cerr << "  RACE HIT SIZE MISMATCH " << trace << " frame " << frame << " " << prefix << "\n";
    }
  }
}

static void compareRoster(const std::string& trace, int frame, const std::vector<Ship>& ships, const json& expRoster,
                           double atol, double rtol) {
  if (ships.size() != expRoster.size()) {
    ++failures;
    std::cerr << "  ROSTER SIZE MISMATCH " << trace << " frame " << frame << ": got " << ships.size() << " want "
              << expRoster.size() << "\n";
    return;
  }
  for (size_t i = 0; i < ships.size(); ++i) compareShip(trace, frame, static_cast<int>(i), ships[i], expRoster[i], atol, rtol);
}

static void compareEvents(const std::string& trace, int frame, const std::vector<GameEvent>& got, const json& exp) {
  if (got.size() != exp.size()) {
    ++failures;
    std::cerr << "  EVENT COUNT MISMATCH " << trace << " frame " << frame << ": got " << got.size() << " want "
              << exp.size() << "\n";
    return;
  }
  for (size_t i = 0; i < got.size(); ++i) {
    const std::string prefix = "events[" + std::to_string(i) + "].";
    checkStr(trace, frame, prefix + "type", eventTypeName(got[i].type), exp[i].at("type").get<std::string>());
    if (got[i].shipIndex != exp[i].at("shipIndex").get<int>()) {
      ++failures;
      std::cerr << "  INT MISMATCH " << trace << " frame " << frame << " " << prefix << "shipIndex: got "
                << got[i].shipIndex << " want " << exp[i].at("shipIndex").get<int>() << "\n";
    }
    checkStr(trace, frame, prefix + "triggerId", got[i].triggerId, exp[i].value("triggerId", std::string()));
    checkStr(trace, frame, prefix + "direction", got[i].direction, exp[i].value("direction", std::string()));
    checkB(trace, frame, prefix + "automatic", got[i].automatic, exp[i].value("automatic", false));
  }
}

static int runInitTrace(const json& trace, const std::string& name, double atol, double rtol) {
  TrackLoadResult loaded = Track::fromJson(trace.at("sourceTrack").dump());
  if (!loaded) {
    std::cerr << "native load failed for " << name << ": " << loaded.error << "\n";
    return 2;
  }
  auto trackPtr = std::make_shared<Track>(std::move(*loaded.track));
  const int shipCount = trace.at("meta").at("shipCount").get<int>();
  GameSession session(trackPtr, shipCount);
  compareRoster(name, -1, session.ships(), trace.at("roster"), atol, rtol);
  return 0;
}

static int runStepTrace(const json& trace, const std::string& name, double atol, double rtol) {
  TrackLoadResult loaded = Track::fromJson(trace.at("sourceTrack").dump());
  if (!loaded) {
    std::cerr << "native load failed for " << name << ": " << loaded.error << "\n";
    return 2;
  }
  auto trackPtr = std::make_shared<Track>(std::move(*loaded.track));
  const int shipCount = trace.at("meta").at("shipCount").get<int>();
  GameSession session(trackPtr, shipCount);

  const auto& steps = trace.at("steps");
  for (size_t i = 0; i < steps.size(); ++i) {
    const auto& step = steps[i];
    const auto& beforeRoster = step.at("before").at("roster");
    std::vector<Ship>& ships = session.ships();
    if (ships.size() != beforeRoster.size()) {
      std::cerr << "roster size mismatch loading frame " << i << " of " << name << "\n";
      return 2;
    }
    for (size_t s = 0; s < ships.size(); ++s) ships[s] = loadShip(beforeRoster[s]);
    session.setSessionTime(step.at("before").at("sessionTime").get<double>());

    std::vector<ControlIntent> intents;
    for (const auto& ij : step.at("intents")) {
      ControlIntent intent;
      intent.throttle = ij.value("throttle", 0.0);
      intent.brake = ij.value("brake", 0.0);
      intent.steer = ij.value("steer", 0.0);
      intent.respawn = ij.value("respawn", false);
      intents.push_back(intent);
    }
    session.step(intents, step.at("dt").get<double>());

    compareRoster(name, static_cast<int>(i), session.ships(), step.at("after").at("roster"), atol, rtol);
    checkD(name, static_cast<int>(i), "sessionTime", session.sessionTime(), step.at("after").at("sessionTime").get<double>(),
           atol, rtol);
    compareEvents(name, static_cast<int>(i), session.events(), step.at("events"));
  }
  return 0;
}

int main(int argc, char** argv) {
  // Native ship/session initialization and GameSession's own step()/event
  // plumbing are new (not physics transliteration), so this starts with the
  // same discipline as the native-bake checks in track_tests.cpp rather than
  // the ultra-tight physics-replay gate: independently-baked doubles can
  // differ by a handful of ULPs before any runtime drift is even involved.
  double atol = 1e-9, rtol = 1e-9, gate = 1e3;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto eat = [&](const char* flag, double& dst) {
      if (a.rfind(flag, 0) == 0) {
        dst = std::stod(a.substr(std::strlen(flag)));
        return true;
      }
      return false;
    };
    if (eat("--atol=", atol) || eat("--rtol=", rtol) || eat("--gate=", gate)) continue;
    files.push_back(a);
  }
  if (files.empty()) {
    std::cerr << "usage: raw_session_parity [--atol= --rtol= --gate=] trace.json...\n";
    return 2;
  }

  for (const auto& file : files) {
    std::ifstream in(file);
    if (!in) {
      std::cerr << "cannot open " << file << "\n";
      return 2;
    }
    json trace;
    in >> trace;
    const std::string name = trace.value("meta", json::object()).value("name", file);
    const std::string kind = trace.at("meta").at("kind").get<std::string>();
    int rc = 0;
    if (kind == "raw-session-init") rc = runInitTrace(trace, name, atol, rtol);
    else if (kind == "raw-session-step") rc = runStepTrace(trace, name, atol, rtol);
    else {
      std::cerr << "unknown raw-session trace kind '" << kind << "' in " << file << "\n";
      return 2;
    }
    if (rc != 0) return rc;
  }

  std::cout << "raw_session_parity worst: ratio=" << worst.ratio << " field=" << worst.field << " trace=" << worst.trace
            << " frame=" << worst.frame << " (got " << worst.a << ", want " << worst.b << ", |delta|=" << worst.absD
            << ")\n";
  if (failures > 0) {
    std::cerr << failures << " boolean/string/count mismatch(es)\n";
    return 1;
  }
  if (worst.ratio > gate) {
    std::cerr << "worst ratio " << worst.ratio << " exceeds gate " << gate << "\n";
    return 1;
  }
  std::cout << "PASS: raw-session parity over " << files.size() << " trace(s)\n";
  return 0;
}
