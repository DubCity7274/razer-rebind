#pragma once

#include "keys.h"

#include <QList>
#include <QString>

// Both layers of a device's bindings in one profile.
struct BindingLayers
{
    BindingMap normal;
    BindingMap hypershift;

    bool operator==(const BindingLayers &o) const { return normal == o.normal && hypershift == o.hypershift; }
    bool operator!=(const BindingLayers &o) const { return !(*this == o); }
};

// Undo and redo for one device's bindings in one profile. Each step knows the
// bindings on both sides of it, so a step whose bindings have since changed
// some other way (an imported profile, a hand-edited config) can't be applied
// over them.
class BindingHistory
{
public:
    struct Edit
    {
        QString what; // "Bind A", for the buttons and the status bar
        bool hypershiftLayer = false;
        BindingLayers before;
        BindingLayers after;
    };

    static constexpr int MaxSteps = 100;

    void record(const QString &what, bool hypershiftLayer, const BindingLayers &before, const BindingLayers &after)
    {
        if (before == after)
            return;
        if (!m_undo.isEmpty() && m_undo.last().after != before)
            m_undo.clear();
        m_redo.clear();
        m_undo << Edit{what, hypershiftLayer, before, after};
        if (m_undo.size() > MaxSteps)
            m_undo.removeFirst();
    }

    bool canUndo(const BindingLayers &current) const { return !m_undo.isEmpty() && m_undo.last().after == current; }
    bool canRedo(const BindingLayers &current) const { return !m_redo.isEmpty() && m_redo.last().before == current; }
    QString undoName() const { return m_undo.isEmpty() ? QString() : m_undo.last().what; }
    QString redoName() const { return m_redo.isEmpty() ? QString() : m_redo.last().what; }

    // Only after canUndo() / canRedo().
    Edit undo()
    {
        m_redo << m_undo.takeLast();
        return m_redo.last();
    }
    Edit redo()
    {
        m_undo << m_redo.takeLast();
        return m_undo.last();
    }

private:
    QList<Edit> m_undo;
    QList<Edit> m_redo;
};
