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
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <kconfiggroup.h>
#include <klocalizedstring.h>
#include <KoColorSpaceRegistry.h>
#include "KisDocument.h"
#include "KisPart.h"
#include "kis_paint_layer.h"
#include "kis_paint_device.h"
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
        QCOMPARE(combos.size(), 3);
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
