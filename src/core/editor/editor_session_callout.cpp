// Callouts in EditorSession: the Counter tool's drag that pulls a Callout out of a
// Counter (or out of empty picture, placing the Counter first), and finishing the new
// box's text. Moving, deleting and editing a Callout go through the ordinary selection and
// Text code.
#include "annotations/callout.h"
#include "editor_session.h"

namespace tarn {

bool EditorSession::isCallout(int id) const { return dynamic_cast<const CalloutAnnotation *>(m_document->annotation(id)) != nullptr; }

bool EditorSession::hasCallout(int counterId) const {
    for (const auto &item : m_document->items())
        if (item.annotation->anchorId() == counterId)
            return true;
    return false;
}

void EditorSession::pullPressed(int counterId, const QPointF &p) {
    m_mode = Mode::Pulling;
    m_pullCounter = counterId;
    m_pullNew.reset();
    m_draft.reset();
    if (counterId >= 0) {
        setSelection({counterId});
    } else {
        // Empty picture: the Counter lands where the press was; the drag may then pull a Callout out of it.
        const ToolStyle s = style();
        m_pullNew = AnnotationKinds::find(QStringLiteral("counter"))->fromDrag(DragGesture{p, p, {}, s, {p}});
        int placed = 0;
        for (const auto &item : m_document->items())
            if (item.annotation->series() == m_pullNew->series())
                ++placed;
        m_pullOrdinal = placed;
        m_draft = m_pullNew->clone();
    }
    changed();
}

void EditorSession::pullMoved(const QPointF &p) {
    const Annotation *counter = m_pullCounter >= 0 ? m_document->annotation(m_pullCounter) : m_pullNew.get();
    if (!counter)
        return;
    const QRectF b = counter->bounds();
    const qreal beyond = QLineF(b.center(), p).length() - b.width() / 2;
    if (beyond <= CalloutAnnotation::kPullDistance) { // too short: nothing opens (a new Counter still shows)
        m_draft = m_pullNew ? m_pullNew->clone() : nullptr;
        return;
    }
    auto callout = std::make_unique<CalloutAnnotation>(m_pullCounter, p, QString());
    if (m_pullNew)
        callout->carryCounter(m_pullNew, m_pullOrdinal);
    else
        callout->anchorTo(counter);
    callout->translate(-QPointF(callout->boxRect().width(), callout->boxRect().height()) / 2); // the box is centered on the pointer
    m_draft = std::move(callout);
}

void EditorSession::pullReleased(const QPointF &p) {
    pullMoved(p);
    auto *callout = dynamic_cast<CalloutAnnotation *>(m_draft.get());
    if (callout) { // a Callout opens, ready for its text
        m_calloutProto.reset(static_cast<CalloutAnnotation *>(m_draft.release()));
        m_textActive = true;
        m_textId = -1;
        m_textOrigin = m_calloutProto->textOrigin();
        m_text.clear();
        setSelection({});
        m_draft = m_calloutProto->clone();
        emit textEditingChanged();
        emit textStyleChanged();
        emit textChanged();
    } else if (m_pullNew) { // a click or a short drag from empty picture: just the Counter
        const int id = m_document->draw(QStringLiteral("counter"), DragGesture{m_dragFrom, m_dragFrom, {}, style(), {m_dragFrom}});
        if (id >= 0)
            setSelection({id});
    }
    m_pullNew.reset();
    m_pullCounter = -1;
}

// The new Callout's text is done. Empty: no box (a Counter placed by the same drag stays). Else
// the Callout joins the document, with its new Counter in the same undo step.
void EditorSession::finishNewCallout(bool empty) {
    std::unique_ptr<CalloutAnnotation> proto = std::move(m_calloutProto);
    m_draft.reset();
    std::shared_ptr<const Annotation> counter = proto->carriedCounter();
    if (empty) {
        if (counter) {
            const int id = m_document->add(counter->clone());
            setSelection({id});
        }
        return;
    }
    std::vector<std::unique_ptr<Annotation>> made;
    int counterId = proto->counterId();
    if (counter) {
        counterId = m_document->nextId();
        made.push_back(counter->clone());
    }
    made.push_back(std::make_unique<CalloutAnnotation>(counterId, proto->boxRect().topLeft(), m_text));
    const QList<int> ids = m_document->addMany(std::move(made), QStringLiteral("Callout"));
    setSelection({ids.last()});
}

} // namespace tarn
