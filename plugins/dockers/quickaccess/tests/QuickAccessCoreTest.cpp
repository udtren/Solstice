/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QTest>

#include "QuickAccessLayoutEngine.h"
#include "QuickAccessLegacyImporter.h"
#include "QuickAccessProfileRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace QuickAccess;

class QuickAccessCoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void modelRoundTrip();
    void itemNormalization();
    void placementPushesOccupant();
    void compactPreservesVisualOrder();
    void repositoryRoundTrip();
    void rejectsFutureFormat();
    void importsLegacyFiles();
};

static Item makeItem(const QString &id, int row, int column, int columnSpan = 1)
{
    Item item;
    item.id = id;
    item.type = ItemType::Action;
    item.row = row;
    item.column = column;
    item.columnSpan = columnSpan;
    item.payload.insert(QStringLiteral("action_id"), id);
    return item;
}

void QuickAccessCoreTest::modelRoundTrip()
{
    Document document = Document::createDefault();
    document.tabs[0].grids[0].items.append(makeItem(QStringLiteral("undo"), 2, 3, 2));
    document.aliases.insert(QStringLiteral("actions"),
                            QJsonObject{{QStringLiteral("undo"),
                                         QJsonObject{{QStringLiteral("custom_name"), QStringLiteral("Undo now")}}}});
    document.gesturePages.append(QJsonObject{{QStringLiteral("gesture_key"), QStringLiteral("Q")}});

    Document parsed;
    QString error;
    QVERIFY2(Document::fromJson(document.toJson(), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed, document);
}

void QuickAccessCoreTest::itemNormalization()
{
    Item brush = makeItem(QStringLiteral("brush"), 0, 0, 5);
    brush.type = ItemType::Brush;
    brush.rowSpan = 4;
    brush.normalize();
    QCOMPARE(brush.rowSpan, 1);
    QCOMPARE(brush.columnSpan, 1);

    Item separator = makeItem(QStringLiteral("separator"), 0, 0, 4);
    separator.type = ItemType::Separator;
    separator.rowSpan = 3;
    separator.payload.insert(QStringLiteral("orientation"), QStringLiteral("vertical"));
    separator.normalize();
    QCOMPARE(separator.rowSpan, 3);
    QCOMPARE(separator.columnSpan, 1);
}

void QuickAccessCoreTest::placementPushesOccupant()
{
    LayoutEngine engine(4);
    const Item oldItem = makeItem(QStringLiteral("old"), 0, 0);
    const Item newItem = makeItem(QStringLiteral("new"), 0, 0);
    const LayoutResult result = engine.addItem({oldItem}, newItem);
    QVERIFY(result.isValid());
    QCOMPARE(result.items[0].id, QStringLiteral("new"));
    QCOMPARE(result.items[0].column, 0);
    QCOMPARE(result.items[1].id, QStringLiteral("old"));
    QCOMPARE(result.items[1].column, 1);
}

void QuickAccessCoreTest::compactPreservesVisualOrder()
{
    LayoutEngine engine(4);
    const LayoutResult result =
        engine.compact({makeItem(QStringLiteral("b"), 3, 0), makeItem(QStringLiteral("a"), 1, 2)});
    QVERIFY(result.isValid());
    QCOMPARE(result.items[0].id, QStringLiteral("a"));
    QCOMPARE(result.items[0].row, 0);
    QCOMPARE(result.items[0].column, 0);
    QCOMPARE(result.items[1].id, QStringLiteral("b"));
    QCOMPARE(result.items[1].column, 1);
}

void QuickAccessCoreTest::repositoryRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ProfileRepository repository(directory.filePath(QStringLiteral("profiles/default.kqap")));
    Document document = Document::createDefault();
    document.tabs[0].grids[0].items.append(makeItem(QStringLiteral("redo"), 0, 0));
    QString error;
    QVERIFY2(repository.save(document, &error), qPrintable(error));
    Document loaded;
    QVERIFY2(repository.load(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded, document);
}

void QuickAccessCoreTest::rejectsFutureFormat()
{
    QJsonObject object = Document::createDefault().toJson();
    object.insert(QStringLiteral("formatVersion"), CurrentFormatVersion + 1);
    Document parsed;
    QString error;
    QVERIFY(!Document::fromJson(object, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("Unsupported")));
}

static void writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QJsonDocument(object).toJson()), QJsonDocument(object).toJson().size());
}

void QuickAccessCoreTest::importsLegacyFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("gesture/config")));

    QJsonObject legacyPalette = Document::createDefault().toJson();
    legacyPalette.remove(QStringLiteral("formatVersion"));
    legacyPalette.insert(QStringLiteral("version"), 1);
    legacyPalette.insert(QStringLiteral("active_tab_id"), legacyPalette.take(QStringLiteral("activeTabId")));
    writeJson(root.filePath(QStringLiteral("quick_access_palette.json")), legacyPalette);
    writeJson(root.filePath(QStringLiteral("settings.json")),
              QJsonObject{{QStringLiteral("default"), QJsonObject{{QStringLiteral("docker_icon_size"), 42}}}});
    writeJson(root.filePath(QStringLiteral("alias_config.json")),
              QJsonObject{{QStringLiteral("actions"), QJsonObject{{QStringLiteral("undo"), QJsonObject{}}}}});
    writeJson(root.filePath(QStringLiteral("gesture/gesture.json")), QJsonObject{{QStringLiteral("enabled"), true}});
    writeJson(root.filePath(QStringLiteral("gesture/config/1.json")),
              QJsonObject{{QStringLiteral("gesture_key"), QStringLiteral("Q")}});

    LegacyImportResult result;
    QString error;
    QVERIFY2(LegacyImporter::importDirectory(directory.path(), &result, &error), qPrintable(error));
    QCOMPARE(result.document.tabs.size(), 1);
    QVERIFY(result.document.aliases.contains(QStringLiteral("actions")));
    QCOMPARE(result.document.gesturePages.size(), 1);
    QVERIFY(result.settings.contains(QStringLiteral("gesture")));
}

QTEST_MAIN(QuickAccessCoreTest)

#include "QuickAccessCoreTest.moc"
