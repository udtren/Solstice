/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisLazyToolsFastExportDialog.h"

#include "KisDocument.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <klocalizedstring.h>

namespace
{
// Accepts a folder dragged from the system file manager, so a subfolder found
// from a registered folder can become the export folder.
class FolderLineEdit : public QLineEdit
{
public:
    FolderLineEdit(const QString &text, QWidget *parent, const std::function<void()> &dropped)
        : QLineEdit(text, parent)
        , m_dropped(dropped)
    {
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (!droppedFolder(event->mimeData()).isEmpty()) {
            event->acceptProposedAction();
        } else {
            QLineEdit::dragEnterEvent(event);
        }
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (!droppedFolder(event->mimeData()).isEmpty()) {
            event->acceptProposedAction();
        } else {
            QLineEdit::dragMoveEvent(event);
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        const QString folder = droppedFolder(event->mimeData());
        if (folder.isEmpty()) {
            QLineEdit::dropEvent(event);
            return;
        }
        setText(folder);
        event->acceptProposedAction();
        m_dropped();
    }

private:
    static QString droppedFolder(const QMimeData *mimeData)
    {
        if (!mimeData || !mimeData->hasUrls()) {
            return QString();
        }
        for (const QUrl &url : mimeData->urls()) {
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
                return url.toLocalFile();
            }
        }
        return QString();
    }

    std::function<void()> m_dropped;
};

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
        m_registeredFolder = new QComboBox(this);
        m_registeredFolder->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_registeredFolder->setMinimumContentsLength(30);
        auto *browse = new QPushButton(i18n("Browse…"), this);
        const QStringList explorerFolders = KisLazyToolsFastExport::explorerFolders();
        for (const QString &folder : explorerFolders) {
            m_registeredFolder->addItem(QDir::toNativeSeparators(folder), folder);
            m_registeredFolder->setItemData(m_registeredFolder->count() - 1,
                                            QDir::toNativeSeparators(folder),
                                            Qt::ToolTipRole);
        }
        if (explorerFolders.isEmpty()) {
            m_registeredFolder->addItem(i18n("No registered folders"));
            m_registeredFolder->setEnabled(false);
        }
        m_registeredFolder->setToolTip(
            i18n("Browse… starts in this folder. Register folders in Settings > Configure Solstice > General > "
                 "Custom."));
        browse->setToolTip(i18n("Choose the export folder, starting in the registered folder."));
        auto *registeredRow = new QHBoxLayout;
        registeredRow->addWidget(m_registeredFolder, 1);
        registeredRow->addWidget(browse);
        m_folder = new FolderLineEdit(settings.folder, this, [this] {
            m_destination->setCurrentIndex(m_destination->findData(1));
        });
        m_folder->setToolTip(i18n("Drop a folder here from the file manager to export into it."));
        m_name = new QLineEdit(this);
        m_name->setPlaceholderText(i18n("Same as the document"));
        m_name->setToolTip(i18n("Optional file name without the extension. Used only for the active document."));
        auto *clearName = new QPushButton(i18n("Clear"), this);
        clearName->setEnabled(false);
        auto *nameRow = new QHBoxLayout;
        nameRow->addWidget(m_name, 1);
        nameRow->addWidget(clearName);
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
        form->addRow(i18n("Registered folder:"), registeredRow);
        form->addRow(i18n("Export folder:"), m_folder);
        form->addRow(i18n("Name:"), nameRow);
        form->addRow(i18n("PNG compression:"), m_pngCompression);
        form->addRow(QString(), m_pngAlpha);
        form->addRow(i18n("JPEG quality:"), m_jpegQuality);
        layout->addLayout(form);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        QPushButton *exportButton = buttons->addButton(i18n("Export"), QDialogButtonBox::AcceptRole);
        QPushButton *regionButton = buttons->addButton(i18n("Export Region"), QDialogButtonBox::ActionRole);
        regionButton->setToolTip(i18n("Export the selected area of the active document."));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(browse, &QPushButton::clicked, this, [this] {
            browseExportFolder();
        });
        connect(exportButton, &QPushButton::clicked, this, [this] {
            exportImages();
        });
        connect(regionButton, &QPushButton::clicked, this, [this] {
            exportRegion();
        });
        connect(clearName, &QPushButton::clicked, m_name, &QLineEdit::clear);
        // One name or selection cannot serve several documents, so both apply
        // only to the active document.
        auto updateScope = [this, clearName, regionButton] {
            const bool single = !m_scope->currentData().toBool();
            m_name->setEnabled(single);
            clearName->setEnabled(single && !m_name->text().isEmpty());
            regionButton->setEnabled(single);
        };
        connect(m_name, &QLineEdit::textChanged, this, updateScope);
        connect(m_scope, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateScope);
    }

private:
    void browseExportFolder()
    {
        // The registered folder is only where the chooser starts; the folder
        // picked in it becomes the export folder.
        QString start = m_registeredFolder->currentData().toString();
        if (start.isEmpty() || !QFileInfo(start).isDir()) {
            start = m_folder->text().trimmed();
        }
        const QString folder = QFileDialog::getExistingDirectory(this, i18n("Select Export Folder"), start);
        if (!folder.isEmpty()) {
            m_folder->setText(folder);
            m_destination->setCurrentIndex(m_destination->findData(1));
        }
    }

    struct Request {
        QString outputFolder;
        QString name;
        KisLazyToolsFastExport::Settings settings;
    };

    // Resolves the destination and name shared by both export buttons. Returns
    // false, before saving preferences, when they are invalid or cancelled.
    bool prepare(Request *request)
    {
        const int destination = m_destination->currentData().toInt();
        if (destination == 1) {
            request->outputFolder = m_folder->text().trimmed();
        } else if (destination == 2) {
            request->outputFolder =
                QFileDialog::getExistingDirectory(this, i18n("Select Export Folder"), m_folder->text());
            if (request->outputFolder.isEmpty()) {
                return false;
            }
        }
        if (destination != 0 && request->outputFolder.isEmpty()) {
            QMessageBox::warning(this, i18n("Fast Image Export"), i18n("Select an export folder first."));
            return false;
        }

        request->name = m_scope->currentData().toBool()
            ? QString()
            : KisLazyToolsFastExport::normalizedBaseName(m_name->text(), m_format->currentData().toString());
        if (!request->name.isEmpty() && !KisLazyToolsFastExport::isValidBaseName(request->name)) {
            QMessageBox::warning(this,
                                 i18n("Fast Image Export"),
                                 i18n("The name cannot contain \\ / : * ? \" < > | or end with a dot or a space."));
            return false;
        }

        request->settings.folder = m_folder->text().trimmed();
        request->settings.pngCompression = m_pngCompression->value();
        request->settings.pngAlpha = m_pngAlpha->isChecked();
        request->settings.jpegQuality = m_jpegQuality->value();
        request->settings.save();
        return true;
    }

    void exportRegion()
    {
        Request request;
        if (!prepare(&request)) {
            return;
        }
        const QList<QPointer<KisDocument>> documents = m_documents(false);
        const auto result =
            KisLazyToolsFastExport::exportRegion(documents.isEmpty() ? nullptr : documents.first().data(),
                                                 m_format->currentData().toString(),
                                                 request.outputFolder,
                                                 request.settings,
                                                 m_wait,
                                                 request.name);
        switch (result) {
        case KisLazyToolsFastExport::RegionResult::Exported:
            accept();
            return;
        case KisLazyToolsFastExport::RegionResult::Unnamed:
            QMessageBox::warning(this,
                                 i18n("Fast Image Export"),
                                 i18n("Save the active document before exporting a region."));
            return;
        case KisLazyToolsFastExport::RegionResult::NoSelection:
            QMessageBox::warning(this, i18n("Fast Image Export"), i18n("Select an area to export first."));
            return;
        case KisLazyToolsFastExport::RegionResult::Failed:
            QMessageBox::warning(this, i18n("Fast Image Export"), i18n("Could not export the selected area."));
            return;
        }
    }

    void exportImages()
    {
        Request request;
        if (!prepare(&request)) {
            return;
        }
        const auto result = KisLazyToolsFastExport::run(m_documents(m_scope->currentData().toBool()),
                                                        m_format->currentData().toString(),
                                                        request.outputFolder,
                                                        request.settings,
                                                        m_wait,
                                                        request.name);
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
    QComboBox *m_registeredFolder{nullptr};
    QLineEdit *m_folder{nullptr};
    QLineEdit *m_name{nullptr};
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
