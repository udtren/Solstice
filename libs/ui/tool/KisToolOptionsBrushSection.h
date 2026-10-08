/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTOOLOPTIONSBRUSHSECTION_H
#define KISTOOLOPTIONSBRUSHSECTION_H

#include <QPointer>
#include <QWidget>

#include "kritaui_export.h"

class KisPaintopBox;
class KisPaintOpSettingsWidget;
class QToolButton;
class QVBoxLayout;

/**
 * The "Brush" section of a paint tool's Tool Options: the Brush Editor
 * options of the current brush engine whose eye is on, as checkboxes bound
 * to the editor's options. See docs/agent/tool-options-brush.md.
 */
class KRITAUI_EXPORT KisToolOptionsBrushSection : public QWidget
{
    Q_OBJECT
public:
    /// Follows the current engine of @p paintopBox (may be null)
    KisToolOptionsBrushSection(KisPaintopBox *paintopBox, QWidget *parent = nullptr);
    ~KisToolOptionsBrushSection() override;

    /// The Brush Editor settings widget whose options are shown
    void setSettingsWidget(KisPaintOpSettingsWidget *settingsWidget);

private Q_SLOTS:
    void rebuild();
    void slotCollapsedChanged(bool collapsed);

private:
    QPointer<KisPaintOpSettingsWidget> m_settingsWidget;
    QToolButton *m_header{nullptr};
    QWidget *m_content{nullptr};
    QVBoxLayout *m_contentLayout{nullptr};
};

#endif // KISTOOLOPTIONSBRUSHSECTION_H
