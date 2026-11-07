#include "undo_history.h"

namespace tarn {

void UndoHistory::record(Step step) {
    m_undone.clear();
    m_done.push_back(std::move(step));
}

bool UndoHistory::undo() {
    if (m_done.empty())
        return false;
    Step step = std::move(m_done.back());
    m_done.pop_back();
    step.undo();
    m_undone.push_back(std::move(step));
    return true;
}

bool UndoHistory::redo() {
    if (m_undone.empty())
        return false;
    Step step = std::move(m_undone.back());
    m_undone.pop_back();
    step.redo();
    m_done.push_back(std::move(step));
    return true;
}

} // namespace tarn
