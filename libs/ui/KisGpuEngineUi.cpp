/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuEngineUi.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QThreadPool>
#include <QVBoxLayout>

#include <atomic>

#include <klocalizedstring.h>

#include <KoColorConversionTransformation.h>
#include <KoColorSpace.h>

#include <kis_debug.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisPart.h"
#include "gpu/KisGpuEngineSettings.h"
#include "gpu/KisGpuMergeBatch.h"
#include "kis_image.h"

#ifdef HAVE_KRITA_GPU_ENGINE
#include "gpu/KisGpuTileBackend.h"
#include <KisGpuContext.h>
#endif

namespace
{
/// Lost tiles the user agreed to save with (confirmSave()).
std::atomic<quint64> s_acknowledgedLoss{0};

bool gpuEngineFailed(quint64 *lostTiles = nullptr)
{
#ifdef HAVE_KRITA_GPU_ENGINE
    KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance();
    if (backend && backend->hasFailed()) {
        if (lostTiles) {
            *lostTiles = backend->contentLossCount();
        }
        return true;
    }
#endif
    Q_UNUSED(lostTiles);
    return false;
}

bool gpuEngineUsable()
{
#ifdef HAVE_KRITA_GPU_ENGINE
    if (!KisGpuMergeBatch::isEnabled()) {
        return false;
    }
    KisGpuTileBackend *backend = KisGpuTileBackend::instance();
    return backend && !backend->hasFailed();
#else
    return false;
#endif
}

QWidget *messageParent()
{
    KisMainWindow *window = KisPart::instance()->currentMainwindow();
    return window ? static_cast<QWidget *>(window) : QApplication::activeWindow();
}

void showFailureMessage()
{
    quint64 lostTiles = 0;
    gpuEngineFailed(&lostTiles);
    QMessageBox *box = new QMessageBox(QMessageBox::Warning,
                                       i18nc("@title:window", "GPU Engine Stopped"),
                                       KisGpuEngineUi::failureMessage(lostTiles),
                                       QMessageBox::Ok,
                                       messageParent());
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->show();
}
} // namespace

void KisGpuEngineUi::install()
{
#ifdef HAVE_KRITA_GPU_ENGINE
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    // Called on the thread that detected the loss (a worker): show the
    // message from the GUI thread.
    KisGpuTileBackend::setFailureListener([]() {
        QMetaObject::invokeMethod(qApp, &showFailureMessage, Qt::QueuedConnection);
    });
    // Phase 4.91: compile the compositor pipelines in the background so the
    // first stroke (notably the first Wash stroke) does not wait for them.
    if (KisGpuMergeBatch::isEnabled()) {
        QThreadPool::globalInstance()->start([]() {
            KisGpuTileBackend::preparePipelines();
        });
    }
#endif
}

QString KisGpuEngineUi::failureMessage(quint64 lostTiles)
{
    return i18n(
        "The GPU engine could not read back %1 tile(s) of image data from the graphics card and has been "
        "stopped. Solstice continues without it.\n\n"
        "The most recent changes in those areas are lost: they show older content. Check your open "
        "documents before saving them, and save to a new file if in doubt.",
        lostTiles);
}

bool KisGpuEngineUi::shouldOfferConversion(const KoColorSpace *colorSpace)
{
    return colorSpace && !KisGpuEngineSettings::isGpuColorSpace(colorSpace)
        && KisGpuEngineSettings::conversionTarget(colorSpace) && gpuEngineUsable();
}

bool KisGpuEngineUi::convertForGpu(KisImageSP image)
{
    const KoColorSpace *target = image ? KisGpuEngineSettings::conversionTarget(image->colorSpace()) : nullptr;
    if (!target) {
        return false;
    }
    image->convertImageColorSpace(target,
                                  KoColorConversionTransformation::internalRenderingIntent(),
                                  KoColorConversionTransformation::internalConversionFlags());
    return true;
}

void KisGpuEngineUi::offerConversion(KisDocument *document, QWidget *parent)
{
    if (!document || !document->image() || !shouldOfferConversion(document->image()->colorSpace())) {
        return;
    }
    KisImageSP image = document->image();
    const KisGpuEngineSettings::ConvertPolicy policy = KisGpuEngineSettings::convertPolicy();
    if (policy == KisGpuEngineSettings::Keep) {
        return;
    }
    if (policy == KisGpuEngineSettings::Convert) {
        convertForGpu(image);
        return;
    }

    const KoColorSpace *target = KisGpuEngineSettings::conversionTarget(image->colorSpace());
    const QString documentName = document->path().isEmpty()
        ? i18n("This document")
        : QStringLiteral("\"%1\"").arg(QFileInfo(document->path()).fileName());
    QMessageBox box(QMessageBox::Question,
                    i18nc("@title:window", "GPU Engine"),
                    i18n("%1 uses the color space %2.\n\n"
                         "The GPU engine only accelerates RGBA float documents. Convert this document to %3? "
                         "The conversion can be undone; saving keeps the new color space.\n\n"
                         "Documents that are not converted work as before, without GPU acceleration.",
                         documentName,
                         image->colorSpace()->name(),
                         target->name()),
                    QMessageBox::NoButton,
                    parent);
    QPushButton *convert = box.addButton(i18n("Convert"), QMessageBox::AcceptRole);
    QPushButton *keep = box.addButton(i18n("Keep"), QMessageBox::RejectRole);
    box.setDefaultButton(convert);
    box.setEscapeButton(keep);
    QCheckBox *remember = new QCheckBox(i18n("Do not ask again"));
    box.setCheckBox(remember);
    box.exec();

    const bool accepted = box.clickedButton() == convert;
    if (remember->isChecked()) {
        KisGpuEngineSettings::setConvertPolicy(accepted ? KisGpuEngineSettings::Convert : KisGpuEngineSettings::Keep);
    }
    if (accepted) {
        convertForGpu(image);
    }
}

bool KisGpuEngineUi::confirmSave(QWidget *parent, bool *saveAs)
{
    quint64 lostTiles = 0;
    if (!gpuEngineFailed(&lostTiles)) {
        return true;
    }
    QMessageBox box(QMessageBox::Warning,
                    i18nc("@title:window", "GPU Engine Stopped"),
                    failureMessage(lostTiles) + QStringLiteral("\n\n")
                        + i18n("Saving now stores the document as it is displayed."),
                    QMessageBox::NoButton,
                    parent);
    QPushButton *newFile = box.addButton(i18n("Save as New File..."), QMessageBox::AcceptRole);
    QPushButton *anyway = box.addButton(i18n("Save Anyway"), QMessageBox::DestructiveRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(newFile);
    box.setEscapeButton(cancel);
    box.exec();

    if (box.clickedButton() == newFile) {
        *saveAs = true;
    } else if (box.clickedButton() != anyway) {
        return false;
    }
    s_acknowledgedLoss.store(lostTiles);
    return true;
}

quint64 KisGpuEngineUi::lostTileCount()
{
#ifdef HAVE_KRITA_GPU_ENGINE
    if (KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance()) {
        return backend->contentLossCount();
    }
#endif
    return 0;
}

bool KisGpuEngineUi::mayFinishExport(const KisDocument *document)
{
    if (document && document->isAutosaving()) {
        return true;
    }
    const quint64 lost = lostTileCount();
    if (lost <= s_acknowledgedLoss.load()) {
        return true;
    }
    warnUI << "GPU engine: image data was lost while saving (" << lost << "lost tiles); the file was not written";
    return false;
}

void KisGpuEngineUi::setAcknowledgedLossCountForTesting(quint64 count)
{
    s_acknowledgedLoss.store(count);
}

KisGpuEngineSettingsWidget::KisGpuEngineSettingsWidget(QWidget *parent)
    : QGroupBox(i18n("GPU Engine (Vulkan)"), parent)
    , m_enabled(new QCheckBox(i18n("Use the GPU engine for RGBA float documents"), this))
    , m_brush(new QCheckBox(i18n("Paint brush strokes on the GPU"), this))
    , m_convertPolicy(new QComboBox(this))
    , m_status(new QLabel(this))
{
    m_convertPolicy->addItem(i18n("Ask"), int(KisGpuEngineSettings::Ask));
    m_convertPolicy->addItem(i18n("Convert to RGBA 32-bit float"), int(KisGpuEngineSettings::Convert));
    m_convertPolicy->addItem(i18n("Keep the color space"), int(KisGpuEngineSettings::Keep));
    m_status->setWordWrap(true);

    QFormLayout *layout = new QFormLayout(this);
    layout->addRow(m_enabled);
    layout->addRow(m_brush);
    // the brush uses the engine
    connect(m_enabled, &QCheckBox::toggled, m_brush, &QWidget::setEnabled);
    layout->addRow(i18n("Opening other documents:"), m_convertPolicy);
    layout->addRow(m_status);

    QString status;
#ifdef HAVE_KRITA_GPU_ENGINE
    KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance();
    if (backend && backend->hasFailed()) {
        status = i18n("Stopped after an error in this session.");
    } else if (backend) {
        status = i18n("Active on %1.", backend->context().deviceInfo().name);
    } else if (KisGpuMergeBatch::isEnabled()) {
        status = i18n("Enabled; starts with the first RGBA float document.");
    } else {
        status = i18n("Not used in this session.");
    }
    status += QLatin1Char(' ') + i18n("Changes take effect after restarting Solstice.");
#else
    status = i18n("Not available: this build of Solstice has no Vulkan support.");
    m_enabled->setEnabled(false);
    m_brush->setEnabled(false);
    m_convertPolicy->setEnabled(false);
#endif
    m_status->setText(status);
}

void KisGpuEngineSettingsWidget::load(bool requestDefault)
{
    // Solstice: the GPU engine is on by default (embedded kritarc defaults).
    m_enabled->setChecked(requestDefault ? true : KisGpuEngineSettings::enabledInConfig());
    m_brush->setChecked(requestDefault ? true : KisGpuEngineSettings::brushEnabledInConfig());
    const int policy = requestDefault ? int(KisGpuEngineSettings::Ask) : int(KisGpuEngineSettings::convertPolicy());
    m_convertPolicy->setCurrentIndex(qMax(0, m_convertPolicy->findData(policy)));
}

void KisGpuEngineSettingsWidget::save()
{
    KisGpuEngineSettings::setEnabledInConfig(m_enabled->isChecked());
    KisGpuEngineSettings::setBrushEnabledInConfig(m_brush->isChecked());
    KisGpuEngineSettings::setConvertPolicy(KisGpuEngineSettings::ConvertPolicy(m_convertPolicy->currentData().toInt()));
}
