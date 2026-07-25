// cpp/app/main.cpp — a minimal native command-line host
// (NATIVE_GAME_RUNTIME_PLAN.md step 6: "Integrate the session into a native
// executable or embedding host"). Loads a schema-10 track file, builds a
// complete race session (native ship/roster initialization on top of the
// renderer-neutral geometry Track::fromFile already bakes), and runs the
// deterministic simulation loop in real time until the user presses Escape.
//
// No rendering, audio, networking, or driving input: the roster stays idle
// (zero throttle) every frame. This is a lifecycle/session smoke host, not a
// playable client — see cpp/README.md and NATIVE_GAME_RUNTIME_PLAN.md §2.7/§4
// for what deliberately stays outside cpp/core and this executable.
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <conio.h>
#endif

#include "GameSession.hpp"
#include "Ship.hpp"
#include "Track.hpp"

using namespace tox;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kEscapeKey = 27;
constexpr auto kFramePace = std::chrono::milliseconds(16);
constexpr double kStatusIntervalSeconds = 1.0;

// Non-blocking check for a pending Escape keypress on the console. Drains any
// other buffered keys along the way so input never backs up. Windows-only:
// the native build is documented as an MSVC/Windows target (cpp/README.md);
// there is no other platform input path to fall back to here.
bool escapePressed() {
#ifdef _WIN32
  bool escape = false;
  while (_kbhit()) {
    if (_getch() == kEscapeKey) escape = true;
  }
  return escape;
#else
  return false;
#endif
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: track_runner <track.json>\n";
    return 2;
  }

  TrackLoadResult loaded = Track::fromFile(argv[1]);
  for (const TrackWarning& warning : loaded.warnings) {
    std::cerr << "warning [" << warning.code << "] " << warning.message;
    if (!warning.objectId.empty()) std::cerr << " (" << warning.objectId << ")";
    std::cerr << "\n";
  }
  if (!loaded) {
    std::cerr << "failed to load '" << argv[1] << "': " << loaded.error << "\n";
    return 1;
  }

  auto track = std::make_shared<Track>(std::move(*loaded.track));
  std::cout << "loaded '" << track->definition.name << "': " << track->paths.size() << " path(s), "
            << track->meshRegions.size() << " mesh region(s), " << track->geometry.size()
            << " renderer-neutral geometry batch(es)" << std::endl;

  GameSession session(track);
  std::cout << "session ready: " << session.ships().size() << " ship(s) on the starting grid. Press Escape to stop."
            << std::endl;

  const std::vector<ControlIntent> idleIntents(session.ships().size());
  auto lastFrame = Clock::now();
  auto lastStatus = lastFrame;

  bool running = true;
  while (running) {
    const auto now = Clock::now();
    const double dt = std::chrono::duration<double>(now - lastFrame).count();
    lastFrame = now;

    session.step(idleIntents, dt);

    if (std::chrono::duration<double>(now - lastStatus).count() >= kStatusIntervalSeconds) {
      lastStatus = now;
      const Ship& ship0 = session.ships()[0];
      const Physics& p = ship0.physics;
      std::cout << "t=" << session.sessionTime() << "s  ship0 pos=(" << p.groundPos.x << ", " << p.groundPos.y << ", "
                << p.groundPos.z << ")  speed=" << p.speed << "  laps=" << ship0.race.laps << std::endl;
    }

    if (escapePressed()) running = false;
    std::this_thread::sleep_for(kFramePace);
  }

  std::cout << "shutting down" << std::endl;
  return 0;
}
