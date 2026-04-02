#pragma once

#include "Types.h"
#include <functional>
#include <vector>

namespace sst::core {

/// Undo/redo stack using the Command pattern.
class CommandHistory {
public:
    /// Execute a command (add annotation) and push onto undo stack.
    void execute(Annotation annotation,
                 std::function<void(Annotation)> doAction,
                 std::function<void()> undoAction);

    /// Undo the last command.
    void undo();

    /// Redo the last undone command.
    void redo();

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    void clear();

private:
    struct Command {
        Annotation annotation;
        std::function<void(Annotation)> doAction;
        std::function<void()> undoAction;
    };

    std::vector<Command> undoStack_;
    std::vector<Command> redoStack_;
};

} // namespace sst::core
