#pragma once
// The undo history of one Capture document: unlimited undo and redo.
//
// This is the mechanism every kind of change plugs into, not an Annotation
// feature. A change that has ALREADY been applied is recorded with its two
// halves (how to take it back, how to do it again); undo() and redo() just run
// them. Annotation edits record themselves from CaptureDocument. Crop and
// Frame do the same from their own code, through `document.history()`:
//
//     const auto before = m_crop;
//     m_crop = after;                                  // apply it
//     history().record(u"Crop"_s, [=] { m_crop = before; }, [=] { m_crop = after; });
//
// Steps run strictly last-in first-out, so a step may rely on the document being
// exactly as it was when the step was recorded.
#include <QString>
#include <functional>
#include <vector>

namespace tarn {

class UndoHistory {
public:
    struct Step {
        QString text; // "Move", "Delete", "Crop": for a future Edit menu or tooltip
        std::function<void()> undo;
        std::function<void()> redo;
    };

    // Records a change that was just applied. Anything that could be redone is dropped.
    void record(Step step);
    void record(const QString &text, std::function<void()> undo, std::function<void()> redo) {
        record(Step{text, std::move(undo), std::move(redo)});
    }

    bool canUndo() const { return !m_done.empty(); }
    bool canRedo() const { return !m_undone.empty(); }
    QString undoText() const { return m_done.empty() ? QString() : m_done.back().text; }
    QString redoText() const { return m_undone.empty() ? QString() : m_undone.back().text; }
    int undoCount() const { return int(m_done.size()); }
    int redoCount() const { return int(m_undone.size()); }

    // Return false when there is nothing to undo / redo.
    bool undo();
    bool redo();

private:
    std::vector<Step> m_done;   // oldest first
    std::vector<Step> m_undone; // the next redo is the last
};

} // namespace tarn
