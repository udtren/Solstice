/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisLazyToolsFastExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <klocalizedstring.h>

namespace
{
class FastExportDialog : public QDialog
{
public:
    FastExportDialog(QWidget *parent,
                     const KisLazyToolsFastExportDialog::Documents &documents,
                     const KisLazyToolsFastExport::WaitForImage &wait)
        : QDialog(parent)
        , m_documents(documents)
        , m_wait(wait)
    {
        setWindowTitle(i18n("Fast Image Export"));
        setAttribute(Qt::WA_DeleteOnClose);

        const auto settings = KisLazyToolsFastExport::Settings::load();
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        m_format = new QComboBox(this);
        m_format->addItem(i18n("PNG"), QStringLiteral("png"));
        m_format->addItem(i18n("JPEG"), QStringLiteral("jpg"));
        m_scope = new QComboBox(this);
        m_scope->addItem(i18n("Active document"), false);
        m_scope->addItem(i18n("All open documents"), true);
        m_destination = new QComboBox(this);
        m_destination->addItem(i18n("Same folder as the document"), 0);
        m_destination->addItem(i18n("Configured export folder"), 1);
        m_destination->addItem(i18n("Choose a folder now"), 2);
        m_folder = new QLineEdit(settings.folder, this);
        auto *browse = new QPushButton(i18n("Browse…"), this);
        auto *folderRow = new QHBoxLayout;
        folderRow->addWidget(m_folder);
        folderRow->addWidget(browse);
        m_pngCompression = new QSpinBox(this);
        m_pngCompression->setRange(0, 9);
        m_pngCompression->setValue(settings.pngCompression);
        m_pngAlpha = new QCheckBox(i18n("Save alpha channel"), this);
        m_pngAlpha->setChecked(settings.pngAlpha);
        m_jpegQuality = new QSpinBox(this);
        m_jpegQuality->setRange(0, 100);
        m_jpegQuality->setValue(settings.jpegQuality);
        form->addRow(i18n("Format:"), m_format);
        form->addRow(i18n("Documents:"), m_scope);
        form->addRow(i18n("Destination:"), m_destination);
        form->addRow(i18n("Export folder:"), folderRow);
        form->addRow(i18n("PNG compression:"), m_pngCompression);
        form->addRow(QString(), m_pngAlpha);
        form->addRow(i18n("JPEG quality:"), m_jpegQuality);
        layout->addLayout(form);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        QPushButton *exportButton = buttons->addButton(i18n("Export"), QDialogButtonBox::AcceptRole);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString folder =
                QFileDialog::getExistingDirectory(this, i18n("Select Export Folder"), m_folder->text());
            if (!folder.isEmpty()) {
                m_folder->setText(folder);
            }
        });
        connect(exportButton, &QPushButton::clicked, this, [this] {
            exportImages();
        });
    }

private:
    void exportImages()
    {
        QString outputFolder;
        const int destination = m_destination->currentData().toInt();
        if (destination == 1) {
            outputFolder = m_folder->text().trimmed();
        } else if (destination == 2) {
            outputFolder = QFileDialog::getExistingDirectory(this, i18n("Select Export Folder"), m_folder->text());
            if (outputFolder.isEmpty()) {
                return;
            }
        }
        if (destination != 0 && outputFolder.isEmpty()) {
            QMessageBox::warning(this, i18n("Fast Image Export"), i18n("Select an export folder first."));
            return;
        }

        KisLazyToolsFastExport::Settings settings;
        settings.folder = m_folder->text().trimmed();
        settings.pngCompression = m_pngCompression->value();
        settings.pngAlpha = m_pngAlpha->isChecked();
        settings.jpegQuality = m_jpegQuality->value();
        settings.save();
        const auto result = KisLazyToolsFastExport::run(m_documents(m_scope->currentData().toBool()),
                                                        m_format->currentData().toString(),
                                                        destination == 0 ? QString() : outputFolder,
                                                        settings,
                                                        m_wait);
        const int exported = result.exported;
        const QStringList &skipped = result.skipped;

        if (!skipped.isEmpty()) {
            QMessageBox::warning(this,
                                 i18n("Fast Image Export"),
                                 i18np("Exported %1 document. Skipped: %2",
                                       "Exported %1 documents. Skipped: %2",
                                       exported,
                                       skipped.join(QStringLiteral(", "))));
        } else {
            accept();
        }
    }

    KisLazyToolsFastExportDialog::Documents m_documents;
    KisLazyToolsFastExport::WaitForImage m_wait;
    QComboBox *m_format{nullptr};
    QComboBox *m_scope{nullptr};
    QComboBox *m_destination{nullptr};
    QLineEdit *m_folder{nullptr};
    QSpinBox *m_pngCompression{nullptr};
    QCheckBox *m_pngAlpha{nullptr};
    QSpinBox *m_jpegQuality{nullptr};
};

} // namespace

QDialog *KisLazyToolsFastExportDialog::create(QWidget *parent,
                                              const Documents &documents,
                                              const KisLazyToolsFastExport::WaitForImage &wait)
{
    return new FastExportDialog(parent, documents, wait);
}
