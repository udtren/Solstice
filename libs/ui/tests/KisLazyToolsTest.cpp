/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lazytools/KisLazyToolsFastExport.h"
#include "lazytools/KisLazyToolsFastExportDialog.h"
#include "lazytools/KisLazyToolsRenameDialog.h"
#include "lazytools/KisLazyToolsRenamePresets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <kconfiggroup.h>
#include <klocalizedstring.h>
#include <KoColorSpaceRegistry.h>
#include "KisDocument.h"
#include "KisPart.h"
#include "kis_paint_layer.h"
#include "kis_paint_device.h"
#include "kis_default_bounds.h"
#include "kis_selection.h"
#include "kis_undo_adapter.h"
#include "KisImageResolutionProxy.h"
#include <commands/kis_set_global_selection_command.h>
#include <testui.h>

namespace
{
namespace Presets = KisLazyToolsRenamePresets;
namespace Export = KisLazyToolsFastExport;

bool write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QPushButton *button(QWidget *widget, const QString &text)
{
    for (auto *candidate : widget->findChildren<QPushButton *>()) {
        if (candidate->text() == text)
            return candidate;
    }
    return nullptr;
}

KisDocument *document(const QString &path)
{
    auto *doc = KisPart::instance()->createDocument();
    // Export filters normally show their own options dialog. Test their output
    // in batch mode; production preserves each document's existing batch mode.
    doc->setFileBatchMode(true);
    const auto *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(doc->createUndoStore(), 16, 12, cs, "export test");
    KisPaintLayerSP layer = new KisPaintLayer(image, "paint", OPACITY_OPAQUE_U8, cs);
    image->addNode(layer, image->root());
    layer->paintDevice()->fill(image->bounds(), KoColor(QColor(80, 140, 200, 127), cs));
    doc->setCurrentImage(image);
    image->waitForDone();
    doc->setLocalFilePath(path);
    return doc;
}
void selectRect(KisDocument *doc, const QRect &rect)
{
    KisImageSP image = doc->image();
    KisSelectionSP selection =
        new KisSelection(new KisDefaultBounds(image), toQShared(new KisImageResolutionProxy(image)));
    selection->pixelSelection()->select(rect);
    selection->updateProjection();
    image->undoAdapter()->addCommand(new KisSetGlobalSelectionCommand(image, selection));
    image->waitForDone();
}
} // namespace

class KisLazyToolsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void presetCompatibility()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("name_color_list.txt");
        bool opened = true;
        QVERIFY(Presets::load(path, &opened).isEmpty());
        QVERIFY(!opened);
        const QByteArray legacy =
            "  Plain  \r\n\r\n Colored , bLuE \r\nGray, gray\r\nGrey, GREY\r\n"
            ", Red\r\nUnknown, cyan\r\nComma, name, Red\r\nPlain\r\n";
        QVERIFY(write(path, legacy));
        const auto presets = Presets::load(path, &opened);
        QVERIFY(opened);
        QCOMPARE(presets.size(), 7);
        const QStringList names{"Plain", "Colored", "Gray", "Grey", "Unknown", "Comma", "Plain"};
        const QList<int> labels{0, 1, 8, 8, 0, 0, 0};
        for (int i = 0; i < presets.size(); ++i) {
            QCOMPARE(presets[i].name, names[i]);
            QCOMPARE(presets[i].label, labels[i]);
        }
        QCOMPARE(read(path), legacy);
        for (int i = 0; i <= 8; ++i)
            QCOMPARE(Presets::colorLabelIndex(Presets::colorLabelName(i)), i);
        QVERIFY(Presets::colorLabelName(-1).isEmpty());
        QVERIFY(Presets::colorLabelName(9).isEmpty());

        Presets::append(path, "New", 6);
        const QByteArray appended = read(path);
        QVERIFY(appended.contains("  Plain  "));
        QVERIFY(appended.contains(" Colored , bLuE "));
        QVERIFY(appended.endsWith("New, Red"));
        Presets::append(path, "New", 6);
        QCOMPARE(read(path), appended); // Exact-line duplicates do not rewrite.
        Presets::append(path, "Colored", 1); // Existing differently spaced line remains distinct.
        QCOMPARE(Presets::load(path).size(), 9);
        const QString nested = dir.filePath("new/config/names.txt");
        Presets::append(nested, QString::fromUtf8("\xe7\xb7\x9a\xe7\x94\xbb"), 0);
        QCOMPARE(Presets::load(nested).first().name, QString::fromUtf8("\xe7\xb7\x9a\xe7\x94\xbb"));
    }

    void presetTextEditor()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("nested/config/names.txt");
        bool opened = true;
        QCOMPARE(Presets::readText(path, &opened), QString());
        QVERIFY(!opened);
        const QString text = QString::fromUtf8("  線画 , blue\n\nPlain\nBad, Teal\n,Red\nLast, gray");
        QVERIFY(Presets::writeText(path, text));
        QCOMPARE(Presets::readText(path, &opened), text);
        QVERIFY(opened);
        const auto presets = Presets::load(path);
        QCOMPARE(presets.size(), 4);
        QCOMPARE(presets[0].name, QString::fromUtf8("線画"));
        QCOMPARE(presets[0].label, 1);
        QCOMPARE(presets[2].label, 0);
        QCOMPARE(presets[3].label, 8);
        // The editor and the dialog's Save checkbox share one file.
        Presets::append(path, "Added", 6);
        QCOMPARE(Presets::readText(path), text + "\nAdded, Red");
        QVERIFY(Presets::writeText(path, QString()));
        QVERIFY(Presets::load(path).isEmpty());
        QVERIFY(!Presets::writeText(dir.path(), "a directory cannot be written"));
    }

    void exportSettingsAndPaths()
    {
        QTemporaryDir dir;
        auto config = KSharedConfig::openConfig(dir.filePath("test.ini"), KConfig::SimpleConfig);
        auto settings = Export::Settings::load(config);
        QCOMPARE(settings.folder, QString());
        QCOMPARE(settings.pngCompression, 6);
        QVERIFY(settings.pngAlpha);
        QCOMPARE(settings.jpegQuality, 90);
        QVERIFY(!QFileInfo::exists(dir.filePath("test.ini")));
        auto group = config->group(QString());
        group.writeEntry("unrelated", 123);
        group.writeEntry("Solstice/FastExportFolder", "old folder");
        group.writeEntry("Solstice/FastExportPngCompression", 8);
        group.writeEntry("Solstice/FastExportPngAlpha", false);
        group.writeEntry("Solstice/FastExportJpegQuality", 71);
        settings = Export::Settings::load(config);
        QCOMPARE(settings.folder, QString("old folder"));
        QCOMPARE(settings.pngCompression, 8);
        QVERIFY(!settings.pngAlpha);
        QCOMPARE(settings.jpegQuality, 71);
        settings.folder = "new folder";
        settings.pngCompression = 3;
        settings.pngAlpha = true;
        settings.jpegQuality = 88;
        settings.save(config);
        config->reparseConfiguration();
        QCOMPARE(group.readEntry("unrelated", 0), 123);
        QCOMPARE(group.readEntry("Solstice/FastExportFolder", QString()), settings.folder);
        QCOMPARE(group.readEntry("Solstice/FastExportPngCompression", 0), 3);
        QVERIFY(group.readEntry("Solstice/FastExportPngAlpha", false));
        QCOMPARE(group.readEntry("Solstice/FastExportJpegQuality", 0), 88);
        const QString source = dir.filePath("paint.v2.kra");
        QCOMPARE(Export::outputPath(source, "png", {}), dir.filePath("paint.v2.png"));
        QCOMPARE(Export::outputPath(source, "jpg", dir.filePath("out")), dir.filePath("out/paint.v2.jpg"));
    }

    void renameDialog()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("names.txt");
        QVERIFY(write(path, "Preset, Red\nPlain"));
        auto group = KSharedConfig::openConfig()->group(QString());
        group.writeEntry("Solstice/RenameGridColumns", 0);
        group.writeEntry("Solstice/RenameDialogWidth", 1);
        group.writeEntry("Solstice/RenameDialogHeight", 1);
        QWidget parent;
        int applied = 0;
        QString name;
        int label = -1;
        auto apply = [&](const QString &value, int color) {
            ++applied;
            name = value;
            label = color;
            return true;
        };
        {
            QScopedPointer<QDialog> dialog(KisLazyToolsRenameDialog::create(&parent, apply, path));
            QCOMPARE(dialog->parentWidget(), &parent);
            QCOMPARE(dialog->size(), QSize(240, 200));
            auto *preset = button(dialog.data(), "Preset");
            QVERIFY(preset);
            preset->click();
            QCOMPARE(dialog->result(), int(QDialog::Accepted));
            QCOMPARE(applied, 1);
            QCOMPARE(name, QString("Preset"));
            QCOMPARE(label, 6);
            dialog->resize(550, 610);
        }
        QCOMPARE(group.readEntry("Solstice/RenameDialogWidth", 0), 550);
        QCOMPARE(group.readEntry("Solstice/RenameDialogHeight", 0), 610);
        QScopedPointer<QDialog> dialog(KisLazyToolsRenameDialog::create(&parent, apply, path));
        auto *input = dialog->findChild<QLineEdit *>();
        auto *combo = dialog->findChild<QComboBox *>();
        auto *save = dialog->findChild<QCheckBox *>();
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(input && combo && save && buttons);
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(applied, 1); // Empty names neither apply nor close.
        input->setText(" Manual ");
        combo->setCurrentIndex(2);
        save->setChecked(true);
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(applied, 2);
        QCOMPARE(name, QString("Manual"));
        QCOMPARE(label, 2);
        QVERIFY(read(path).endsWith("Manual, Green"));
        const auto before = read(path);
        dialog.reset(KisLazyToolsRenameDialog::create(&parent, apply, path));
        dialog->findChild<QLineEdit *>()->setText("Cancelled");
        dialog->findChild<QCheckBox *>()->setChecked(true);
        dialog->reject();
        dialog->resize(560, 620);
        dialog.reset();
        QCOMPARE(read(path), before);
        QCOMPARE(applied, 2);
        QCOMPARE(group.readEntry("Solstice/RenameDialogWidth", 0), 560); // Cancel still remembers geometry.
        dialog.reset(KisLazyToolsRenameDialog::create(
            &parent,
            [](const QString &, int) {
                return false;
            },
            path));
        dialog->findChild<QLineEdit *>()->setText("No active node");
        dialog->findChild<QCheckBox *>()->setChecked(true);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(read(path).endsWith("No active node")); // Legacy save-before-apply order.
    }

    void exportDialog_data()
    {
        QTest::addColumn<bool>("all");
        QTest::addColumn<int>("destination");
        QTest::newRow("active-source") << false << 0;
        QTest::newRow("all-configured") << true << 1;
    }

    void exportDialog()
    {
        QFETCH(bool, all);
        QFETCH(int, destination);
        QTemporaryDir dir;
        Export::Settings baseline;
        baseline.save();
        QWidget parent;
        int resolves = 0;
        bool scope = !all;
        QPointer<QDialog> dialog = KisLazyToolsFastExportDialog::create(
            &parent,
            [&](bool value) {
                ++resolves;
                scope = value;
                return QList<QPointer<KisDocument>>();
            },
            [](KisImageSP) {
                return false;
            });
        QCOMPARE(resolves, 0);
        QCOMPARE(dialog->parentWidget(), &parent);
        QVERIFY(dialog->testAttribute(Qt::WA_DeleteOnClose));
        const auto combos = dialog->findChildren<QComboBox *>();
        QCOMPARE(combos.size(), 4);
        QCOMPARE(combos[0]->currentData().toString(), QString("png"));
        combos[0]->setCurrentIndex(1);
        combos[1]->setCurrentIndex(all ? 1 : 0);
        combos[2]->setCurrentIndex(destination);
        dialog->findChild<QLineEdit *>()->setText(" " + dir.path() + " ");
        const auto spins = dialog->findChildren<QSpinBox *>();
        QCOMPARE(spins.size(), 2);
        spins[0]->setValue(8);
        spins[1]->setValue(75);
        dialog->findChild<QCheckBox *>()->setChecked(false);
        auto *exportButton = button(dialog, "Export");
        QVERIFY(exportButton);
        exportButton->click();
        QCOMPARE(resolves, 1);
        QCOMPARE(scope, all);
        QTRY_VERIFY(dialog.isNull());
        const auto updated = Export::Settings::load();
        QCOMPARE(updated.folder, dir.path());
        QCOMPARE(updated.pngCompression, 8);
        QVERIFY(!updated.pngAlpha);
        QCOMPARE(updated.jpegQuality, 75);
    }

    void exportDialogCancellation()
    {
        Export::Settings baseline;
        baseline.folder = "unchanged";
        baseline.save();
        QWidget parent;
        int resolves = 0;
        auto documents = [&](bool) {
            ++resolves;
            return QList<QPointer<KisDocument>>();
        };
        QPointer<QDialog> dialog = KisLazyToolsFastExportDialog::create(&parent, documents, [](KisImageSP) {
            return false;
        });
        const auto combos = dialog->findChildren<QComboBox *>();
        combos[2]->setCurrentIndex(1);
        dialog->findChild<QLineEdit *>()->clear();
        bool warningSeen = false;
        QTimer::singleShot(0, &parent, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                warningSeen = true;
                box->accept();
            }
        });
        button(dialog, "Export")->click();
        QVERIFY(warningSeen);
        QCOMPARE(resolves, 0);
        QCOMPARE(Export::Settings::load().folder, baseline.folder);
        combos[2]->setCurrentIndex(2);
        bool chooserSeen = false;
        QTimer::singleShot(0, &parent, [&] {
            if (auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                chooserSeen = true;
                chooser->reject();
            }
        });
        button(dialog, "Export")->click();
        QVERIFY(chooserSeen);
        QCOMPARE(resolves, 0);
        QCOMPARE(Export::Settings::load().folder, baseline.folder);
        dialog->findChild<QSpinBox *>()->setValue(0);
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(Export::Settings::load().pngCompression, 6);
    }

    void explorerFolders()
    {
        QTemporaryDir dir;
        auto config = KSharedConfig::openConfig(dir.filePath("test.ini"), KConfig::SimpleConfig);
        QCOMPARE(Export::explorerFolders(config), QStringList());
        Export::setExplorerFolders({" C:\\Art\\Root ", "", "C:/Art/Root", "D:/Work"}, config);
        config->reparseConfiguration();
        QCOMPARE(config->group(QString()).readEntry("Solstice/FastExportExplorerFolders", QStringList()),
                 QStringList({"C:/Art/Root", "D:/Work"}));
        QCOMPARE(Export::explorerFolders(config), QStringList({"C:/Art/Root", "D:/Work"}));
        // The export dialog saves Settings on Export; that must not touch the list.
        Export::Settings().save(config);
        QCOMPARE(Export::explorerFolders(config), QStringList({"C:/Art/Root", "D:/Work"}));
    }

    void registeredFolderBrowse()
    {
        QTemporaryDir dir;
        const QString missing = dir.filePath("missing");
        const QString fallback = dir.filePath("fallback");
        const QString root = dir.filePath("root");
        QVERIFY(QDir().mkpath(fallback));
        QVERIFY(QDir().mkpath(root + "/sub"));
        Export::Settings settings;
        settings.folder = fallback;
        settings.save();
        Export::setExplorerFolders({});
        QWidget parent;
        auto documents = [](bool) {
            return QList<QPointer<KisDocument>>();
        };
        auto wait = [](KisImageSP) {
            return false;
        };
        // Browse starts in the chosen registered folder, or the export folder
        // when there is none; it reports where the chooser opened.
        auto browse = [&](QDialog *dialog, const QString &selection) {
            QString start;
            QTimer::singleShot(0, &parent, [&] {
                if (auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                    start = chooser->directory().absolutePath();
                    if (selection.isEmpty()) {
                        chooser->reject();
                    } else {
                        chooser->setDirectory(selection);
                        QMetaObject::invokeMethod(chooser, "accept");
                    }
                }
            });
            button(dialog, "Browse…")->click();
            return start;
        };

        QPointer<QDialog> empty = KisLazyToolsFastExportDialog::create(&parent, documents, wait);
        QCOMPARE(empty->findChildren<QComboBox *>().size(), 4);
        QVERIFY(!empty->findChildren<QComboBox *>()[3]->isEnabled());
        QVERIFY(button(empty, "Browse…")->isEnabled());
        QVERIFY(!button(empty, "Open in Explorer"));
        QCOMPARE(browse(empty, {}), fallback);
        QCOMPARE(empty->findChild<QLineEdit *>()->text(), fallback);
        QCOMPARE(empty->findChildren<QComboBox *>()[2]->currentData().toInt(), 0);
        empty->reject();
        QTRY_VERIFY(empty.isNull());

        Export::setExplorerFolders({missing, root});
        QPointer<QDialog> dialog = KisLazyToolsFastExportDialog::create(&parent, documents, wait);
        const auto combos = dialog->findChildren<QComboBox *>();
        QComboBox *folders = combos[3];
        auto *folderEdit = dialog->findChild<QLineEdit *>();
        QVERIFY(folders->isEnabled());
        QCOMPARE(folders->count(), 2);
        QCOMPARE(folders->itemData(0).toString(), missing);
        QCOMPARE(folders->itemText(1), QDir::toNativeSeparators(root));
        QCOMPARE(browse(dialog, {}), fallback);
        folders->setCurrentIndex(1);
        QCOMPARE(browse(dialog, root + "/sub"), root);
        QCOMPARE(folderEdit->text(), root + "/sub");
        QCOMPARE(combos[2]->currentData().toInt(), 1);

        // A folder dropped from the file manager becomes the configured export folder.
        combos[2]->setCurrentIndex(0);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(fallback)});
        QDragEnterEvent enter(QPoint(2, 2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(folderEdit, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(2, 2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(folderEdit, &drop);
        QCOMPARE(folderEdit->text(), fallback);
        QCOMPARE(combos[2]->currentData().toInt(), 1);
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        Export::setExplorerFolders({});
    }

    void realExport_data()
    {
        QTest::addColumn<QString>("extension");
        QTest::addColumn<bool>("alpha");
        QTest::addColumn<bool>("modified");
        QTest::addColumn<bool>("sameFolder");
        QTest::newRow("png-alpha-source") << QString("png") << true << true << true;
        QTest::newRow("png-opaque-configured") << QString("png") << false << false << false;
        QTest::newRow("jpeg-source") << QString("jpg") << false << false << true;
        QTest::newRow("jpeg-configured-modified") << QString("jpg") << false << true << false;
    }

    void realExport()
    {
        QFETCH(QString, extension);
        QFETCH(bool, alpha);
        QFETCH(bool, modified);
        QFETCH(bool, sameFolder);
        QTemporaryDir dir;
        const QString source = dir.filePath("original.v2.kra");
        const QString target = dir.filePath("exports");
        QVERIFY(QDir().mkpath(target));
        QScopedPointer<KisDocument> doc(document(source));
        doc->setModified(modified);
        const QString caption = doc->caption();
        Export::Settings settings;
        settings.pngAlpha = alpha;
        settings.pngCompression = 9;
        settings.jpegQuality = 92;
        int waits = 0;
        const QString folder = sameFolder ? QString() : target;
        auto result = Export::run({doc.data()}, extension, folder, settings, [&](KisImageSP image) {
            ++waits;
            image->waitForDone();
            return true;
        });
        QCOMPARE(waits, 1);
        QCOMPARE(result.exported, 1);
        QVERIFY(result.skipped.isEmpty());
        QImage output(Export::outputPath(source, extension, folder));
        QVERIFY(!output.isNull());
        QCOMPARE(output.size(), QSize(16, 12));
        QCOMPARE(output.pixelColor(5, 5).alpha(), extension == "png" && alpha ? 127 : 255);
        QCOMPARE(doc->localFilePath(), source);
        QCOMPARE(doc->caption(), caption);
        QCOMPARE(doc->isModified(), modified);
    }

    void exportNames()
    {
        QCOMPARE(Export::normalizedBaseName("  cover  ", "png"), QString("cover"));
        QCOMPARE(Export::normalizedBaseName("cover.PNG", "png"), QString("cover"));
        QCOMPARE(Export::normalizedBaseName("cover.jpeg", "jpg"), QString("cover"));
        QCOMPARE(Export::normalizedBaseName("cover.v2", "png"), QString("cover.v2"));
        QCOMPARE(Export::normalizedBaseName(".png", "png"), QString(".png"));
        QCOMPARE(Export::normalizedBaseName("   ", "png"), QString());
        QVERIFY(Export::isValidBaseName(QString::fromUtf8("表紙 v2.final")));
        const QStringList invalidNames =
            {"a/b", "a\\b", "a:b", "a*", "a?", "a\"b", "<a>", "a|b", ".", "..", "a.", "a ", "a\tb", QString()};
        for (const QString &invalid : invalidNames) {
            QVERIFY2(!Export::isValidBaseName(invalid), qPrintable(invalid));
        }
        QTemporaryDir dir;
        const QString source = dir.filePath("paint.v2.kra");
        QCOMPARE(Export::outputPath(source, "png", {}, "cover"), dir.filePath("cover.png"));
        QCOMPARE(Export::outputPath(source, "jpg", dir.filePath("out"), "cover.v3"), dir.filePath("out/cover.v3.jpg"));
    }

    void exportDialogName()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("original.kra");
        QScopedPointer<KisDocument> doc(document(source));
        Export::Settings().save();
        QWidget parent;
        bool allDocuments = false;
        QPointer<QDialog> dialog = KisLazyToolsFastExportDialog::create(
            &parent,
            [&](bool all) {
                allDocuments = all;
                return QList<QPointer<KisDocument>>({doc.data()});
            },
            [](KisImageSP image) {
                image->waitForDone();
                return true;
            });
        QLineEdit *name = nullptr;
        for (auto *edit : dialog->findChildren<QLineEdit *>()) {
            if (edit->placeholderText() == "Same as the document")
                name = edit;
        }
        QVERIFY(name);
        QVERIFY(dialog->findChild<QLineEdit *>() != name);
        auto *clear = button(dialog, "Clear");
        QVERIFY(clear);
        QVERIFY(!clear->isEnabled());
        name->setText("cover");
        QVERIFY(clear->isEnabled());
        clear->click();
        QVERIFY(name->text().isEmpty());
        QVERIFY(!clear->isEnabled());

        // The name applies only to the active document.
        const auto combos = dialog->findChildren<QComboBox *>();
        name->setText("cover");
        combos[1]->setCurrentIndex(1);
        QVERIFY(!name->isEnabled());
        QVERIFY(!clear->isEnabled());
        combos[1]->setCurrentIndex(0);
        QVERIFY(name->isEnabled());
        QVERIFY(clear->isEnabled());

        name->setText("bad:name");
        bool warningSeen = false;
        QTimer::singleShot(0, &parent, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                warningSeen = true;
                box->accept();
            }
        });
        button(dialog, "Export")->click();
        QVERIFY(warningSeen);
        QVERIFY(!QFileInfo::exists(dir.filePath("bad.png")));

        name->setText(QString::fromUtf8(" 表紙.png "));
        button(dialog, "Export")->click();
        QVERIFY(!allDocuments);
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(!QImage(dir.filePath(QString::fromUtf8("表紙.png"))).isNull());
        QVERIFY(!QFileInfo::exists(dir.filePath("original.png")));
        QCOMPARE(doc->localFilePath(), source);
    }

    void exportRegion()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("region.kra");
        const QString target = dir.filePath("out");
        QVERIFY(QDir().mkpath(target));
        QScopedPointer<KisDocument> doc(document(source));
        doc->setModified(false);
        auto wait = [](KisImageSP image) {
            image->waitForDone();
            return true;
        };
        QScopedPointer<KisDocument> unnamed(document({}));
        selectRect(unnamed.data(), QRect(1, 1, 2, 2));
        QCOMPARE(Export::exportRegion(unnamed.data(), "png", target, {}, wait), Export::RegionResult::Unnamed);
        QCOMPARE(Export::exportRegion(nullptr, "png", target, {}, wait), Export::RegionResult::Unnamed);
        QCOMPARE(Export::exportRegion(doc.data(), "png", target, {}, wait), Export::RegionResult::NoSelection);
        QVERIFY(QDir(target).isEmpty());

        selectRect(doc.data(), QRect(2, 3, 5, 4));
        doc->setModified(false);
        QCOMPARE(Export::exportRegion(doc.data(), "png", target, {}, wait, "part"), Export::RegionResult::Exported);
        QImage png(QDir(target).filePath("part.png"));
        QCOMPARE(png.size(), QSize(5, 4));
        QCOMPARE(png.pixelColor(0, 0).alpha(), 127);
        QCOMPARE(png.pixelColor(4, 3).alpha(), 127);
        QCOMPARE(Export::exportRegion(doc.data(), "jpg", {}, {}, wait), Export::RegionResult::Exported);
        QCOMPARE(QImage(dir.filePath("region.jpg")).size(), QSize(5, 4));
        QCOMPARE(doc->localFilePath(), source);
        QVERIFY(!doc->isModified());
        QCOMPARE(doc->image()->size(), QSize(16, 12));
        QVERIFY(doc->image()->globalSelection());
        QCOMPARE(Export::exportRegion(doc.data(), "png", dir.filePath("missing/folder"), {}, wait),
                 Export::RegionResult::Failed);
    }

    void exportDialogRegion()
    {
        QTemporaryDir dir;
        QScopedPointer<KisDocument> doc(document(dir.filePath("original.kra")));
        Export::Settings().save();
        QWidget parent;
        QList<bool> scopes;
        QPointer<QDialog> dialog = KisLazyToolsFastExportDialog::create(
            &parent,
            [&](bool all) {
                scopes << all;
                return QList<QPointer<KisDocument>>({doc.data()});
            },
            [](KisImageSP image) {
                image->waitForDone();
                return true;
            });
        auto *region = button(dialog, "Export Region");
        QVERIFY(region);
        const auto combos = dialog->findChildren<QComboBox *>();
        combos[1]->setCurrentIndex(1);
        QVERIFY(!region->isEnabled());
        combos[1]->setCurrentIndex(0);
        QVERIFY(region->isEnabled());

        bool warningSeen = false;
        QTimer::singleShot(0, &parent, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                warningSeen = true;
                box->accept();
            }
        });
        region->click();
        QVERIFY(warningSeen);
        QVERIFY(!dialog.isNull());

        selectRect(doc.data(), QRect(0, 0, 3, 2));
        for (auto *edit : dialog->findChildren<QLineEdit *>()) {
            if (edit->placeholderText() == "Same as the document")
                edit->setText("crop");
        }
        region->click();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(scopes, QList<bool>({false, false}));
        QCOMPARE(QImage(dir.filePath("crop.png")).size(), QSize(3, 2));
        QVERIFY(!QFileInfo::exists(dir.filePath("original.png")));
    }

    void exportSkippedDocuments()
    {
        QTemporaryDir dir;
        QScopedPointer<KisDocument> named(document(dir.filePath("named.kra")));
        QScopedPointer<KisDocument> unnamed(document({}));
        int waits = 0;
        auto result = Export::run({nullptr, unnamed.data(), named.data()}, "png", {}, {}, [&](KisImageSP) {
            ++waits;
            return false;
        });
        QCOMPARE(waits, 1);
        QCOMPARE(result.exported, 0);
        QCOMPARE(result.skipped, QStringList({"Unknown document", unnamed->caption(), named->caption()}));
        QVERIFY(!QFileInfo::exists(dir.filePath("named.png")));
        result = Export::run({named.data()}, "png", dir.filePath("missing/folder"), {}, [](KisImageSP) {
            return true;
        });
        QCOMPARE(result.exported, 0);
        QCOMPARE(result.skipped, QStringList({named->caption()}));
    }
};

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir profile;
    if (!profile.isValid())
        return 1;
    qputenv("SOLSTICE_PROFILE_ROOT", profile.path().toUtf8());
    qputenv("LANGUAGE", "en");
    qputenv("EXTRA_RESOURCE_DIRS", QByteArray(KRITA_RESOURCE_DIRS_FOR_TESTS));
    qputenv("KRITA_PLUGIN_PATH", QByteArray(KRITA_PLUGINS_DIR_FOR_TESTS));
    kisTestSetupFontconfig();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    KConfig::setMainConfigName(profile.filePath("test-kritarc"));
    KLocalizedString::setApplicationDomain("krita");
    registerResources();
    KisLazyToolsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "KisLazyToolsTest.moc"
