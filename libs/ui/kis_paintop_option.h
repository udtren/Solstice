/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2008 Boudewijn Rempt <boud@valdyas.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KIS_PAINTOP_OPTION_H
#define KIS_PAINTOP_OPTION_H

#include <kis_types.h>
#include <kritaui_export.h>
#include <kis_properties_configuration.h>
#include <brushengine/kis_locked_properties_proxy.h>
#include <KisPaintopPropertiesBase.h>

#include <lager/reader.hpp>
#include <lager/cursor.hpp>
#include <QPointer>

class QWidget;
class QString;
class QToolButton;
class KisPaintopLodLimitations;


/**
 * Base interface for paintop options. A paintop option
 * can be enabled/disabled, has a configuration page
 * (for example, a curve), a user-visible name and can
 * be serialized and deserialized into KisPaintOpPresets
 *
 * Because KisPaintOpOption classes create a QWidget in
 * their constructor (the configuration page) you CANNOT
 * create those objects in a KisPaintOp. KisPaintOps are
 * created in non-gui threads.
 *
 * Options are disabled by default.
 */
class KRITAUI_EXPORT KisPaintOpOption : public QObject
{
    Q_OBJECT
public:

    using OptionalLodLimitationsReader = std::optional<lager::reader<KisPaintopLodLimitations>>;

    enum PaintopCategory {
        GENERAL,
        COLOR,
        TEXTURE,
        FILTER,
        MASKING_BRUSH
    };

    KisPaintOpOption(const QString &label, KisPaintOpOption::PaintopCategory category, bool checked);
    KisPaintOpOption(const QString &label, KisPaintOpOption::PaintopCategory category,
                     lager::cursor<bool> checkedCursor);
    KisPaintOpOption(const QString &label, KisPaintOpOption::PaintopCategory category,
                     lager::cursor<bool> checkedCursor,
                     lager::reader<bool> externallyEnabledLink);
    ~KisPaintOpOption() override;

    KisPaintOpOption::PaintopCategory category() const;
    virtual bool isCheckable() const;

    virtual bool isChecked() const;
    virtual void setChecked(bool checked);

    bool isEnabled() const;

    void setLocked(bool value);
    bool isLocked() const;

    QString label() const;

    /**
     * Solstice (docs/agent/tool-options-brush.md): a non-empty id lets the
     * option's checkbox be shown in the Tool Options docker. The id is stable
     * (not translated) and unique within the brush engine.
     */
    void setToolOptionsId(const QString &id);
    QString toolOptionsId() const;

    /// Whether the checkbox is shown in Tool Options (the eye in the editor)
    void setShownInToolOptions(bool shown);
    bool isShownInToolOptions() const;

    /// Solstice (phase 3b): a control of the option's page that can be
    /// shown in Tool Options, with its eye button in the page
    struct ToolOptionsParameter {
        QString id; ///< unique within the option, stable
        QString label;
        QPointer<QWidget> control;
        QPointer<QToolButton> eye;
        /// shown only in one mode of the page (e.g. the Auto tip), or null
        QPointer<QWidget> modeWidget;
    };

    /**
     * Registers @p control of the option's page as a Tool Options parameter
     * and puts its eye button in front of @p labelWidget (or of the label in
     * the control's layout row). The eye stays hidden unless the engine
     * supports Tool Options (KisPaintOpSettingsWidget::setPaintOpId()).
     * Only controls KisToolOptionsParameterMirror supports are registered.
     * With @p modeWidget, Tool Options shows the parameter only while that
     * widget is shown in the page (e.g. the Auto or Predefined tip page);
     * other tabs of a page do not hide it.
     */
    void addToolOptionsParameter(const QString &id,
                                 const QString &label,
                                 QWidget *control,
                                 QWidget *labelWidget = nullptr,
                                 QWidget *modeWidget = nullptr);
    QList<ToolOptionsParameter> toolOptionsParameters() const;
    /// Unregisters the parameter @p id and deletes its eye button, e.g. for
    /// a control that the engine hides from the page
    void removeToolOptionsParameter(const QString &id);

    /**
     * Reimplement this to use the image in the option widget
     */
    virtual void setImage(KisImageWSP image);
    virtual void setNode(KisNodeWSP node);
    virtual void setResourcesInterface(KisResourcesInterfaceSP resourcesInterface);
    virtual void setCanvasResourcesInterface(KoCanvasResourcesInterfaceSP canvasResourcesInterface);

    void startReadOptionSetting(const KisPropertiesConfigurationSP setting);
    void startWriteOptionSetting(KisPropertiesConfigurationSP setting) const;

    QWidget *configurationPage() const;

    virtual void lodLimitations(KisPaintopLodLimitations *l) const;
    OptionalLodLimitationsReader effectiveLodLimitations() const;

protected:
    virtual OptionalLodLimitationsReader lodLimitationsReader() const;
    void setConfigurationPage(QWidget *page);

    KisResourcesInterfaceSP resourcesInterface() const;
    KoCanvasResourcesInterfaceSP canvasResourcesInterface() const;

protected:
    /**
     * Re-implement this to save the configuration to the paint configuration.
     */
    virtual void writeOptionSetting(KisPropertiesConfigurationSP setting) const {
        Q_UNUSED(setting);
    }

    /**
     * Re-implement this to set the widgets with the values in @p setting.
     */
    virtual void readOptionSetting(const KisPropertiesConfigurationSP setting) {
        Q_UNUSED(setting);
    }

protected Q_SLOTS:
    void emitSettingChanged();
    void emitCheckedChanged(bool checked);
    void emitEnabledChanged(bool enabled);

Q_SIGNALS:

    /**
     * Q_EMIT this whenever a setting has changed. It will update the preview
     */
    void sigSettingChanged();

    /**
     * Q_EMIT this whenever a checked state of the option has changed. It as always
     * emitted *before* sigSettingChanged()
     */
    void sigCheckedChanged(bool value);
    void sigEnabledChanged(bool value);
    void sigShownInToolOptionsChanged(bool shown);

private:
    void slotEnablePageWidget(bool value);

protected:

    bool m_checkable {false};
    bool m_locked {false};

private:

    struct Private;
    Private* const m_d;
};

#endif
