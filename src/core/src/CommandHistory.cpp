#include "core/CommandHistory.h"

namespace sst::core {

void CommandHistory::execute(Annotation annotation,
                             std::function<void(Annotation)> doAction,
                             std::function<void()> undoAction) {
    // Clear redo stack when a new command is executed
    redoStack_.clear();

    doAction(annotation);
    undoStack_.push_back({ std::move(annotation), std::move(doAction), std::move(undoAction) });
}

void CommandHistory::undo() {
    if (undoStack_.empty()) return;

    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();

    cmd.undoAction();
    redoStack_.push_back(std::move(cmd));
}

void CommandHistory::redo() {
    if (redoStack_.empty()) return;

    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();

    cmd.doAction(cmd.annotation);
    undoStack_.push_back(std::move(cmd));
}

void CommandHistory::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace sst::core
