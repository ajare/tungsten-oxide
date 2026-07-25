#pragma once
#include <string>
#include <vector>
#include "Track.hpp"
namespace tox {
bool bakeTrack(Track& track, std::vector<TrackWarning>& warnings, std::string& error);
}
