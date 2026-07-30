// EditorHistory.hpp — whole-track deep-copy undo/redo, mirroring web/js/editor.js's
// undoStack/redoStack (MAX_HISTORY-capped, a fresh edit clears redo). editor::TrackDefinition is
// plain data (no live handles like editor.js's meshCache), so a deep copy is just a struct copy.
#pragma once

#include <deque>
#include <optional>

#include "EditorTrackDefinition.hpp"

namespace editor {

class History {
public:
  // Call once *before* mutating, capturing pre-edit state -- one discrete edit or one continuous
  // gesture (a drag, a field edit) = one recorded step, matching editor.js's pushUndo() contract.
  void push(const TrackDefinition& current) {
    undoStack_.push_back(current);
    if (undoStack_.size() > kMaxHistory) undoStack_.pop_front();
    redoStack_.clear();
  }

  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }

  // Returns the state to restore, or nullopt if there is nothing to undo/redo. `current` is the
  // live state being replaced, pushed onto the opposite stack so the edit can be redone/re-undone.
  std::optional<TrackDefinition> undo(const TrackDefinition& current) {
    if (undoStack_.empty()) return std::nullopt;
    redoStack_.push_back(current);
    if (redoStack_.size() > kMaxHistory) redoStack_.pop_front();
    TrackDefinition restored = std::move(undoStack_.back());
    undoStack_.pop_back();
    return restored;
  }

  std::optional<TrackDefinition> redo(const TrackDefinition& current) {
    if (redoStack_.empty()) return std::nullopt;
    undoStack_.push_back(current);
    if (undoStack_.size() > kMaxHistory) undoStack_.pop_front();
    TrackDefinition restored = std::move(redoStack_.back());
    redoStack_.pop_back();
    return restored;
  }

  void clear() {
    undoStack_.clear();
    redoStack_.clear();
  }

  std::size_t undoDepth() const { return undoStack_.size(); }
  std::size_t redoDepth() const { return redoStack_.size(); }

private:
  static constexpr std::size_t kMaxHistory = 30;
  std::deque<TrackDefinition> undoStack_;
  std::deque<TrackDefinition> redoStack_;
};

}  // namespace editor
