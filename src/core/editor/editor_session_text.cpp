// The Text tool's part of EditorSession: typing a Text, finishing it, and
// editing it again later. The QML puts a TextEdit over the canvas; it reports its
// whole contents through setText(). While a Text is being typed it is either
// the session's draft (a new Text, not yet in the document) or, for a Text edited
// again, the document's own Annotation changed in place with setLive; finishing
// records ONE undo step either way.
#include "annotations/callout.h"
#include "annotations/text_annotation.h"
#include "editor_session.h"

#include <algorithm>

namespace tarn {

namespace {
TextAnnotation *asText(Annotation *a) { return dynamic_cast<TextAnnotation *>(a); }
const TextAnnotation *asText(const Annotation *a) { return dynamic_cast<const TextAnnotation *>(a); }
} // namespace

void EditorSession::connectText() {
    connect(&m_options, &ToolOptions::colorChanged, this, [this] {
        if (m_textActive && m_textId < 0)
            rebuildText(); // the draft is not selected, so setColor does not reach it
    });
    connect(&m_options, &ToolOptions::sizeChanged, this, [this](const QString &group) {
        if (group == QLatin1String("text"))
            textOptionChanged(true);
    });
}

bool EditorSession::isText(int id) const { return asText(m_document->annotation(id)) != nullptr; }

qreal EditorSession::textSize() const {
    if (m_calloutProto)
        return m_calloutProto->size();
    if (m_textActive && m_textId >= 0)
        if (const auto *callout = dynamic_cast<const CalloutAnnotation *>(m_document->annotation(m_textId)))
            return callout->size();
    return style().textSize;
}

int EditorSession::textLook() const { return style().textLook; }

void EditorSession::setTextLook(int look) {
    look = std::clamp(look, 0, TextAnnotation::kLooks - 1);
    if (look == textLook())
        return;
    m_options.setStyleValue(QStringLiteral("text_look"), look);
    textOptionChanged(false);
}

// The size step or the look changed: the Text being typed, else the selected Texts, follow.
void EditorSession::textOptionChanged(bool sizeChanged) {
    const ToolStyle st = style();
    auto restyle = [&](Annotation &a) {
        if (auto *t = asText(&a)) {
            if (sizeChanged)
                t->setSize(st.textSize);
            else
                t->setLook(TextAnnotation::Look(st.textLook));
        }
    };
    if (m_textActive) {
        rebuildText(true);
    } else {
        QList<int> texts;
        for (int id : m_selection)
            if (isText(id))
                texts << id;
        if (!texts.isEmpty()) {
            m_document->modify(sizeChanged ? QStringLiteral("Text size") : QStringLiteral("Text style"), texts, restyle);
            changed();
        }
    }
    emit textStyleChanged();
}

void EditorSession::beginText(qreal x, qreal y) {
    if (!m_open)
        return;
    finishText();
    m_textActive = true;
    m_textId = -1;
    m_textOrigin = QPointF(x, y);
    m_text.clear();
    m_draft.reset();
    setSelection({});
    emit textEditingChanged();
    emit textChanged();
    changed();
}

bool EditorSession::editText(int id) {
    if (!m_open)
        return false;
    if (!asText(m_document->annotation(id)) && !isCallout(id))
        return false;
    finishText();
    m_textActive = true;
    m_textId = id;
    if (const auto *callout = dynamic_cast<const CalloutAnnotation *>(m_document->annotation(id))) {
        m_textOrigin = callout->textOrigin();
        m_text = callout->text();
    } else {
        const TextAnnotation *t = asText(m_document->annotation(id));
        m_textOrigin = t->origin();
        m_text = t->text();
    }
    m_textBefore = m_document->snapshot({id});
    m_draft.reset();
    setSelection({id});
    emit textEditingChanged();
    emit textStyleChanged();
    emit textChanged();
    changed();
    return true;
}

bool EditorSession::editSelectedCallout() {
    return m_mode == Mode::None && !m_textActive && m_selection.size() == 1 && isCallout(m_selection.first()) && editText(m_selection.first());
}

bool EditorSession::editTextAt(qreal x, qreal y) {
    if (m_mode != Mode::None)
        return false;
    const int id = topHit(QPointF(x, y));
    return id >= 0 && (isText(id) || isCallout(id)) && editText(id);
}

void EditorSession::setText(const QString &text) {
    if (!m_textActive || text == m_text)
        return;
    m_text = text;
    rebuildText();
    emit textChanged();
}

// Brings the draft, or the Annotation being edited again, in line with the text
// and the current style.
void EditorSession::rebuildText(bool restyle) {
    const ToolStyle st = style();
    if (m_calloutProto) { // a new Callout: the box shows from the first moment, even empty
        m_calloutProto->setText(m_text);
        m_draft = m_calloutProto->clone();
    } else if (auto *callout = dynamic_cast<const CalloutAnnotation *>(m_document->annotation(m_textId))) {
        auto next = std::make_unique<CalloutAnnotation>(*callout);
        next->setText(m_text);
        m_document->setLive(m_textId, std::move(next));
    } else if (m_textId < 0) {
        if (m_text.trimmed().isEmpty())
            m_draft.reset();
        else
            m_draft = std::make_unique<TextAnnotation>(m_textOrigin, m_text, st);
    } else if (const auto *current = asText(m_document->annotation(m_textId))) {
        auto next = std::make_unique<TextAnnotation>(*current);
        next->setText(m_text);
        if (restyle) { // the size or look was just changed: only then does the Text take them
            next->setSize(st.textSize);
            next->setLook(TextAnnotation::Look(st.textLook));
        }
        m_document->setLive(m_textId, std::move(next));
    }
    changed();
}

void EditorSession::finishText() {
    if (!m_textActive)
        return;
    m_textActive = false;
    const bool empty = m_text.trimmed().isEmpty();
    if (m_calloutProto) {
        finishNewCallout(empty);
    } else if (m_textId < 0) {
        std::unique_ptr<Annotation> made = std::move(m_draft);
        if (made && !empty) // a new Text joins the document, selected
            setSelection({m_document->add(std::move(made))});
    } else if (empty) { // everything deleted: the Text goes (one undo step brings it back)
        for (const auto &entry : m_textBefore)
            m_document->setLive(entry.first, entry.second->clone());
        m_document->remove({m_textId});
        setSelection({});
    } else {
        const auto *beforeCallout = m_textBefore.empty() ? nullptr : dynamic_cast<const CalloutAnnotation *>(m_textBefore.front().second.get());
        const auto *nowCallout = dynamic_cast<const CalloutAnnotation *>(m_document->annotation(m_textId));
        const auto *before = m_textBefore.empty() ? nullptr : asText(m_textBefore.front().second.get());
        const auto *now = asText(m_document->annotation(m_textId));
        if (beforeCallout && nowCallout && beforeCallout->text() != nowCallout->text())
            m_document->recordChange(QStringLiteral("Edit text"), m_textBefore);
        else if (before && now && (before->text() != now->text() || before->size() != now->size() || before->look() != now->look()))
            m_document->recordChange(QStringLiteral("Edit text"), m_textBefore);
    }
    m_draft.reset();
    m_textBefore.clear();
    m_textId = -1;
    emit textEditingChanged();
    emit textStyleChanged();
    changed();
}

} // namespace tarn
