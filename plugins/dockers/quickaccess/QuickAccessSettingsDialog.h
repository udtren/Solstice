/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QUICKACCESSSETTINGSDIALOG_H
#define QUICKACCESSSETTINGSDIALOG_H

#include <QColor>
#include <QDialog>
#include <QJsonArray>
#include <QStringList>

class QPushButton;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;
class QTextEdit;

class QuickAccessSettingsDialog : public QDialog
{
public:
    struct Settings {
        int columns{8};
        int dockerIconSize{42};
        QColor headerButtonColor{QStringLiteral("#828282")};
        QColor headerButtonFontColor{Qt::white};
        int activeTabFontSize{12};
        QColor activeTabFontColor{Qt::white};
        QColor activeTabBackgroundColor{QStringLiteral("#3f3f3f")};
        int inactiveTabFontSize{12};
        QColor inactiveTabFontColor{QStringLiteral("#a0a0a0")};
        QColor inactiveTabBackgroundColor{QStringLiteral("#2b2b2b")};
        int dialogWidth{340};
        int dialogHeight{480};
        bool gestureEnabled{true};
        bool hueSvcEnabled{true};
        bool quickAdjustEnabled{true};
        int popupIconSize{42};
        QString rgbDisplayMode{QStringLiteral("percentage")};
        int huePopupWidth{350};
        int huePopupHeight{600};
        int hueControlsWidth{220};
        int hueControlsFontSize{16};
        bool sizeSliderEnabled{true};
        bool opacitySliderEnabled{true};
        bool flowSliderEnabled{true};
        bool layerOpacitySliderEnabled{true};
        bool colorHistoryEnabled{true};
        int colorHistoryTotal{14};
        int colorHistoryIconSize{30};
        bool brushHistoryEnabled{true};
        int brushHistoryTotal{14};
        int brushHistoryIconSize{34};
        QString altEraseKey;
        QString preserveAlphaKey;
        QString selectOutlineKey;
        bool toolOptionsEnabled{false};
        QString toolOptionsPosition{QStringLiteral("left_align_top")};
        QStringList blendModes{QStringLiteral("normal"),
                               QStringLiteral("multiply"),
                               QStringLiteral("screen"),
                               QStringLiteral("dodge"),
                               QStringLiteral("overlay"),
                               QStringLiteral("soft_light_svg"),
                               QStringLiteral("hard_light"),
                               QStringLiteral("darken"),
                               QStringLiteral("lighten"),
                               QStringLiteral("greater")};
        QJsonArray tempBrushSets;
    };

    explicit QuickAccessSettingsDialog(const Settings &settings, QWidget *parent = nullptr);

    Settings settings() const;

private:
    QPushButton *createColorButton(const QColor &color);
    static QColor buttonColor(const QPushButton *button);

    QSpinBox *m_columns{nullptr};
    QSpinBox *m_dockerIconSize{nullptr};
    QPushButton *m_headerButtonColor{nullptr};
    QPushButton *m_headerButtonFontColor{nullptr};
    QSpinBox *m_activeTabFontSize{nullptr};
    QPushButton *m_activeTabFontColor{nullptr};
    QPushButton *m_activeTabBackgroundColor{nullptr};
    QSpinBox *m_inactiveTabFontSize{nullptr};
    QPushButton *m_inactiveTabFontColor{nullptr};
    QPushButton *m_inactiveTabBackgroundColor{nullptr};
    QSpinBox *m_dialogWidth{nullptr};
    QSpinBox *m_dialogHeight{nullptr};
    QCheckBox *m_gestureEnabled{nullptr};
    QCheckBox *m_hueSvcEnabled{nullptr};
    QCheckBox *m_quickAdjustEnabled{nullptr};
    QSpinBox *m_popupIconSize{nullptr};
    QComboBox *m_rgbDisplayMode{nullptr};
    QSpinBox *m_huePopupWidth{nullptr};
    QSpinBox *m_huePopupHeight{nullptr};
    QSpinBox *m_hueControlsWidth{nullptr};
    QSpinBox *m_hueControlsFontSize{nullptr};
    QCheckBox *m_sizeSliderEnabled{nullptr};
    QCheckBox *m_opacitySliderEnabled{nullptr};
    QCheckBox *m_flowSliderEnabled{nullptr};
    QCheckBox *m_layerOpacitySliderEnabled{nullptr};
    QCheckBox *m_colorHistoryEnabled{nullptr};
    QSpinBox *m_colorHistoryTotal{nullptr};
    QSpinBox *m_colorHistoryIconSize{nullptr};
    QCheckBox *m_brushHistoryEnabled{nullptr};
    QSpinBox *m_brushHistoryTotal{nullptr};
    QSpinBox *m_brushHistoryIconSize{nullptr};
    QLineEdit *m_altEraseKey{nullptr};
    QLineEdit *m_preserveAlphaKey{nullptr};
    QLineEdit *m_selectOutlineKey{nullptr};
    QCheckBox *m_toolOptionsEnabled{nullptr};
    QComboBox *m_toolOptionsPosition{nullptr};
    QTextEdit *m_blendModes{nullptr};
    QTableWidget *m_tempBrushSets{nullptr};
};

#endif
