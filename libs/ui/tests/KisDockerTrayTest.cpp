/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <kconfiggroup.h>
#include <ksharedconfig.h>

#include "KisDockerBorrowRegistry.h"
#include "KisDockerTray.h"
#include "KisDockerTrayLayout.h"
#include "dialogs/KisDlgDockerTray.h"

namespace
{
class Content : public QLabel
{
public:
    explicit Content(const QString &text, QSize minimum = QSize(40, 30))
        : QLabel(text)
        , m_minimum(minimum)
    {
    }
    QSize minimumSizeHint() const override
    {
        return m_minimum;
    }

private:
    QSize m_minimum;
};

class OtherBorrower : public QWidget, public KisDockerBorrower
{
public:
    OtherBorrower()
    {
        new QVBoxLayout(this);
    }
    QString dockerBorrowerName() const override
    {
        return QStringLiteral("Toolbar Docker Box");
    }
    void dockerContentGranted(const QString &, QWidget *content, QWidget *) override
    {
        layout()->addWidget(content);
    }
    void dockerContentHeldElsewhere(const QString &, const QString &) override
    {
    }
};

void drag(QWidget *grip, const QPoint &delta)
{
    const QPointF local = grip->rect().center();
    const QPointF global = grip->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(grip, &press);
    QMouseEvent move(QEvent::MouseMove, local + delta, global + delta, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(grip, &move);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        local + delta,
                        global + delta,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(grip, &release);
    QCoreApplication::processEvents();
}

QByteArray layoutJson(const QString &columns)
{
    return QStringLiteral("{\"version\":1,\"trays\":[{\"name\":\"Default\",\"columns\":%1}]}").arg(columns).toUtf8();
}

struct Fixture {
    QTemporaryDir directory;
    KSharedConfigPtr config{KSharedConfig::openConfig(directory.filePath("kritarc"), KConfig::SimpleConfig)};
    QMainWindow window;
    QDockWidget *layers{new QDockWidget(QStringLiteral("Layers"), &window)};
    QDockWidget *presets{new QDockWidget(QStringLiteral("Brush Presets"), &window)};
    QDockWidget *history{new QDockWidget(QStringLiteral("Undo History"), &window)};
    QPointer<QWidget> layersContent{new Content(QStringLiteral("layers"))};
    QPointer<QWidget> presetsContent{new Content(QStringLiteral("presets"))};
    QPointer<QWidget> historyContent{new Content(QStringLiteral("history"), QSize(250, 30))};

    Fixture()
    {
        window.setCentralWidget(new QWidget);
        window.resize(800, 600);
        layers->setObjectName(QStringLiteral("KisLayerBox"));
        presets->setObjectName(QStringLiteral("PresetDocker"));
        history->setObjectName(QStringLiteral("History"));
        layers->setWidget(layersContent);
        presets->setWidget(presetsContent);
        history->setWidget(historyContent);
        window.addDockWidget(Qt::RightDockWidgetArea, layers);
        window.addDockWidget(Qt::RightDockWidgetArea, presets);
        window.addDockWidget(Qt::RightDockWidgetArea, history);
        window.show();
        presets->hide();
        QCoreApplication::processEvents();
    }

    void setLayout(const QByteArray &json)
    {
        config->group("DockerTray").writeEntry("Layout", QString::fromUtf8(json));
    }

    KisDockerTray *tray()
    {
        return KisDockerTray::forWindow(&window, config);
    }

    QFrame *cell(const QString &id)
    {
        return tray()->findChild<QFrame *>(QStringLiteral("dockerTrayCell_") + id);
    }

    QPoint center() const
    {
        return window.geometry().center();
    }
};
} // namespace

class KisDockerTrayTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void layoutJsonRoundTrip()
    {
        KisDockerTrayLayout layout;
        layout.columns = {
            {{QStringLiteral("KisLayerBox"), QSize(320, 260)}, {QStringLiteral("History"), QSize(320, 200)}},
            {{QStringLiteral("PresetDocker"), QSize(280, 520)}}};
        bool ok = false;
        QCOMPARE(KisDockerTrayLayout::fromJson(layout.toJson(), &ok), layout);
        QVERIFY(ok);
        QCOMPARE(
            layout.dockerIds(),
            QStringList({QStringLiteral("KisLayerBox"), QStringLiteral("History"), QStringLiteral("PresetDocker")}));
    }

    void layoutJsonCleanup()
    {
        bool ok = false;
        const KisDockerTrayLayout layout = KisDockerTrayLayout::fromJson(
            layoutJson(QStringLiteral("[{\"items\":[{\"id\":\"A\",\"w\":10,\"h\":99999},{\"w\":5},{\"id\":\"A\"}]},"
                                      "{\"items\":[]},{\"items\":[{\"id\":\"B\",\"w\":\"x\"}]}]")),
            &ok);
        QVERIFY(ok);
        QCOMPARE(layout.columns.size(), 2);
        QCOMPARE(layout.columns[0],
                 QVector<KisDockerTrayCell>(
                     {{QStringLiteral("A"),
                       QSize(KisDockerTrayLayout::MinimumExtent, KisDockerTrayLayout::MaximumExtent)}}));
        QCOMPARE(layout.columns[1],
                 QVector<KisDockerTrayCell>({{QStringLiteral("B"), KisDockerTrayLayout::DefaultCellSize}}));
    }

    void layoutJsonInvalid_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("garbage") << QByteArray("not json");
        QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("no trays") << QByteArray("{\"version\":1}");
    }

    void layoutJsonInvalid()
    {
        QFETCH(QByteArray, json);
        bool ok = true;
        QCOMPARE(KisDockerTrayLayout::fromJson(json, &ok), KisDockerTrayLayout::defaultLayout());
        QVERIFY(!ok);
    }

    void layoutLoadAndSave()
    {
        QTemporaryDir directory;
        KSharedConfigPtr config = KSharedConfig::openConfig(directory.filePath("kritarc"), KConfig::SimpleConfig);
        QCOMPARE(KisDockerTrayLayout::load(config), KisDockerTrayLayout::defaultLayout());
        QCOMPARE(KisDockerTrayLayout::defaultLayout().dockerIds(), QStringList({QStringLiteral("KisLayerBox")}));

        KisDockerTrayLayout layout;
        layout.columns = {{{QStringLiteral("History"), QSize(200, 300)}}};
        layout.save(config);
        QCOMPARE(KisDockerTrayLayout::load(config), layout);
        QVERIFY(config->group("DockerTray").readEntry("Layout", QString()).contains(QStringLiteral("\"History\"")));
    }

    void layoutEditing()
    {
        KisDockerTrayLayout layout;
        QVERIFY(layout.isEmpty());
        QVERIFY(!layout.addDocker(QStringLiteral("A"), 1));
        QVERIFY(layout.addDocker(QStringLiteral("A"), 0, QSize(10, 99999)));
        QCOMPARE(layout.columns[0][0].size,
                 QSize(KisDockerTrayLayout::MinimumExtent, KisDockerTrayLayout::MaximumExtent));
        QVERIFY(!layout.addDocker(QStringLiteral("A"), 0));
        QVERIFY(layout.addDocker(QStringLiteral("B"), 0));
        QVERIFY(layout.addDocker(QStringLiteral("C"), 1));
        QCOMPARE(layout.find(QStringLiteral("B")), QPoint(0, 1));
        QCOMPARE(layout.find(QStringLiteral("X")), QPoint(-1, -1));

        // Within a column, then across the column boundary.
        QVERIFY(layout.moveDocker(QStringLiteral("B"), -1));
        QCOMPARE(layout.dockerIds(), QStringList({QStringLiteral("B"), QStringLiteral("A"), QStringLiteral("C")}));
        QVERIFY(layout.moveDocker(QStringLiteral("A"), 1));
        QCOMPARE(layout.find(QStringLiteral("A")), QPoint(1, 0));
        QVERIFY(layout.moveDocker(QStringLiteral("A"), -1));
        QCOMPARE(layout.find(QStringLiteral("A")), QPoint(0, 1));
        QVERIFY(!layout.moveDocker(QStringLiteral("B"), -1));
        QVERIFY(!layout.moveDocker(QStringLiteral("C"), 1));

        QVERIFY(layout.moveColumn(1, -1));
        QCOMPARE(layout.dockerIds(), QStringList({QStringLiteral("C"), QStringLiteral("B"), QStringLiteral("A")}));
        QVERIFY(!layout.moveColumn(0, -1));

        QVERIFY(layout.setSize(QStringLiteral("C"), QSize(123, 456)));
        QCOMPARE(layout.columns[0][0].size, QSize(123, 456));

        // An emptied column stays while editing and is dropped when read back.
        QVERIFY(layout.removeDocker(QStringLiteral("C")));
        QCOMPARE(layout.columns.size(), 2);
        layout.addColumn();
        QCOMPARE(layout.columns.size(), 3);
        QCOMPARE(KisDockerTrayLayout::fromJson(layout.toJson()).columns.size(), 1);
        QVERIFY(layout.removeColumn(1));
        QCOMPARE(layout.dockerIds(), QStringList());
        QVERIFY(layout.isEmpty());
        QVERIFY(!layout.removeColumn(5));
    }

    void fitKeepsWantedSizes()
    {
        const QVector<QVector<QSize>> wanted = {{QSize(200, 100), QSize(300, 150)}, {QSize(250, 400)}};
        const QVector<QVector<QSize>> minimum = {{QSize(50, 50), QSize(50, 50)}, {QSize(50, 50)}};
        const QVector<QVector<QSize>> expected = {{QSize(300, 100), QSize(300, 150)}, {QSize(250, 400)}};
        QCOMPARE(KisDockerTrayGeometry::fit(wanted, minimum, QSize(2000, 2000), 6), expected);
        QCOMPARE(KisDockerTrayGeometry::fit(wanted, minimum, QSize(), 6), expected);
        // The minimum wins over a smaller wanted size.
        QCOMPARE(KisDockerTrayGeometry::fit({{QSize(60, 60)}}, {{QSize(100, 80)}}, QSize(), 6),
                 QVector<QVector<QSize>>({{QSize(100, 80)}}));
    }

    void fitShrinks()
    {
        // 600 + 400 + 10 = 1010 wide, 510 available: half each.
        const QVector<QVector<QSize>> wanted = {{QSize(600, 300), QSize(600, 300)}, {QSize(400, 200)}};
        const QVector<QVector<QSize>> minimum = {{QSize(10, 10), QSize(10, 10)}, {QSize(10, 10)}};
        const QVector<QVector<QSize>> sizes = KisDockerTrayGeometry::fit(wanted, minimum, QSize(510, 310), 10);
        QCOMPARE(sizes, QVector<QVector<QSize>>({{QSize(300, 150), QSize(300, 150)}, {QSize(200, 200)}}));
    }

    void fitShrinkRespectsMinimum()
    {
        // The first column cannot go below 500; the second takes the rest.
        const QVector<QVector<QSize>> sizes = KisDockerTrayGeometry::fit({{QSize(600, 100)}, {QSize(600, 100)}},
                                                                         {{QSize(500, 10)}, {QSize(10, 10)}},
                                                                         QSize(700, 1000),
                                                                         0);
        QCOMPARE(sizes, QVector<QVector<QSize>>({{QSize(500, 100)}, {QSize(200, 100)}}));
    }

    void trayPerWindow()
    {
        Fixture f;
        QCOMPARE(f.tray(), KisDockerTray::forWindow(&f.window));
        QCOMPARE(KisDockerTray::forWindow(nullptr), nullptr);
        QVERIFY(f.tray()->windowFlags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(f.tray()->windowType(), Qt::Tool);
    }

    void openAndClose()
    {
        Fixture f;
        f.setLayout(
            layoutJson(QStringLiteral("[{\"items\":[{\"id\":\"KisLayerBox\",\"w\":200,\"h\":150},"
                                      "{\"id\":\"History\",\"w\":120,\"h\":100}]},"
                                      "{\"items\":[{\"id\":\"PresetDocker\",\"w\":180,\"h\":300}]}]")));
        KisDockerTray *tray = f.tray();
        QSignalSpy finished(tray, SIGNAL(finished()));

        tray->popupAt(f.center());
        QVERIFY(tray->onScreen());
        QVERIFY(tray->isAncestorOf(f.layersContent));
        QVERIFY(tray->isAncestorOf(f.historyContent));
        // A hidden docker is usable on the tray and stays hidden.
        QVERIFY(tray->isAncestorOf(f.presetsContent));
        QVERIFY(!f.presets->isVisible());
        QVERIFY(f.layers->isVisible());

        // A column is as wide as its widest cell, and the minimum wins.
        QCOMPARE(f.cell(QStringLiteral("KisLayerBox"))->size(), QSize(250, 150));
        QCOMPARE(f.cell(QStringLiteral("History"))->width(), 250);
        QCOMPARE(f.cell(QStringLiteral("PresetDocker"))->size(), QSize(180, 300));

        // The tray is centered at the position and its size follows the cells.
        QCoreApplication::processEvents();
        QVERIFY(tray->width() >= 250 + 180);
        QVERIFY(tray->height() >= 300);
        QVERIFY(qAbs(tray->frameGeometry().center().x() - f.center().x()) <= 2);

        tray->closeTray();
        QVERIFY(!tray->onScreen());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(f.layers->widget(), f.layersContent.data());
        QCOMPARE(f.presets->widget(), f.presetsContent.data());
        QCOMPARE(f.history->widget(), f.historyContent.data());
        QVERIFY(!f.presets->isVisible());

        // dismiss() is the input manager's: no finished().
        tray->popupAt(f.center());
        QVERIFY(tray->onScreen());
        tray->dismiss();
        QVERIFY(!tray->onScreen());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(f.layers->widget(), f.layersContent.data());
    }

    void escapeCloses()
    {
        Fixture f;
        KisDockerTray *tray = f.tray();
        QSignalSpy finished(tray, SIGNAL(finished()));
        tray->popupAt(f.center());
        QVERIFY(tray->onScreen());
        QTest::keyClick(tray, Qt::Key_Escape);
        QVERIFY(!tray->onScreen());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(f.layers->widget(), f.layersContent.data());
    }

    void missingDockers()
    {
        Fixture f;
        f.setLayout(
            layoutJson(QStringLiteral("[{\"items\":[{\"id\":\"NoSuchDocker\"}]},"
                                      "{\"items\":[{\"id\":\"History\",\"w\":260,\"h\":100}]}]")));
        KisDockerTray *tray = f.tray();
        tray->popupAt(f.center());
        QVERIFY(tray->onScreen());
        QVERIFY(!f.cell(QStringLiteral("NoSuchDocker"))->isVisible());
        QVERIFY(f.cell(QStringLiteral("History"))->isVisible());
        tray->closeTray();

        // Nothing to show: the tray opens with a hint, so the configure
        // button can be reached.
        f.setLayout(layoutJson(QStringLiteral("[{\"items\":[{\"id\":\"NoSuchDocker\"}]}]")));
        QSignalSpy finished(tray, SIGNAL(finished()));
        tray->popupAt(f.center());
        QVERIFY(tray->onScreen());
        QCOMPARE(finished.count(), 0);
        bool hint = false;
        for (QLabel *label : tray->findChildren<QLabel *>()) {
            hint |= label->isVisible() && label->text().contains(QStringLiteral("No dockers"));
        }
        QVERIFY(hint);
        QVERIFY(tray->findChild<QToolButton *>(QStringLiteral("dockerTrayConfigure"))->isVisible());
        tray->closeTray();
    }

    void staysOnScreen()
    {
        Fixture f;
        f.setLayout(layoutJson(QStringLiteral("[{\"items\":[{\"id\":\"KisLayerBox\",\"w\":300,\"h\":300}]}]")));
        QScreen *screen = QGuiApplication::screenAt(f.center());
        QVERIFY(screen);
        const QRect available = screen->availableGeometry();
        KisDockerTray *tray = f.tray();
        tray->popupAt(available.topLeft());
        QVERIFY(tray->onScreen());
        QVERIFY(available.contains(tray->geometry()));
        tray->closeTray();
        tray->popupAt(available.bottomRight());
        QVERIFY(available.contains(tray->geometry()));
        tray->closeTray();
    }

    void anotherBorrowerTakesAndReturns()
    {
        Fixture f;
        KisDockerTray *tray = f.tray();
        tray->popupAt(f.center());
        QVERIFY(tray->isAncestorOf(f.layersContent));

        KisDockerBorrowRegistry *registry = KisDockerBorrowRegistry::forWindow(&f.window);
        QVERIFY(qobject_cast<QLabel *>(f.layers->widget())->text().contains(QStringLiteral("Docker Tray")));
        QScopedPointer<OtherBorrower> other(new OtherBorrower);
        registry->request(QStringLiteral("KisLayerBox"), other.data());
        QCOMPARE(f.layersContent->parentWidget(), other.data());
        bool waiting = false;
        for (QLabel *label : f.cell(QStringLiteral("KisLayerBox"))->findChildren<QLabel *>()) {
            waiting |= label->isVisible() && label->text().contains(QStringLiteral("Toolbar Docker Box"));
        }
        QVERIFY(waiting);

        registry->release(QStringLiteral("KisLayerBox"), other.data());
        QVERIFY(tray->isAncestorOf(f.layersContent));
        tray->closeTray();
        QCOMPARE(f.layers->widget(), f.layersContent.data());
    }

    void dialogEditsACopy()
    {
        KisDockerTrayLayout layout;
        layout.columns = {{{QStringLiteral("KisLayerBox"), QSize(300, 300)}},
                          {{QStringLiteral("Gone"), QSize(100, 100)}}};
        const QVector<KisDlgDockerTray::DockerInfo> dockers = {
            {QStringLiteral("KisLayerBox"), QStringLiteral("Layers")},
            {QStringLiteral("History"), QStringLiteral("Undo History")},
            {QStringLiteral("PresetDocker"), QStringLiteral("Brush Presets")}};
        KisDlgDockerTray dialog(layout, dockers);
        auto *available = dialog.findChild<QListWidget *>(QStringLiteral("availableDockers"));
        auto *tray = dialog.findChild<QTreeWidget *>(QStringLiteral("trayColumns"));
        auto button = [&dialog](const char *name) {
            return dialog.findChild<QPushButton *>(QString::fromLatin1(name));
        };
        auto *width = dialog.findChild<QSpinBox *>(QStringLiteral("widthSpin"));
        auto *height = dialog.findChild<QSpinBox *>(QStringLiteral("heightSpin"));
        QVERIFY(available && tray && width && height);

        // Available: the window's dockers not on the tray, sorted by title.
        QCOMPARE(available->count(), 2);
        QCOMPARE(available->item(0)->text(), QStringLiteral("Brush Presets"));
        QCOMPARE(tray->topLevelItemCount(), 2);
        QVERIFY(tray->topLevelItem(1)->child(0)->text(0).contains(QStringLiteral("Gone")));

        // Add Undo History to the first column.
        tray->setCurrentItem(tray->topLevelItem(0));
        available->setCurrentRow(1);
        QVERIFY(button("addButton")->isEnabled());
        button("addButton")->click();
        QCOMPARE(dialog.trayLayout().columns[0].size(), 2);
        QCOMPARE(dialog.trayLayout().find(QStringLiteral("History")), QPoint(0, 1));
        QCOMPARE(available->count(), 1);

        // The selected docker's size.
        QVERIFY(width->isEnabled());
        width->setValue(222);
        height->setValue(111);
        QCOMPARE(dialog.trayLayout().columns[0][1].size, QSize(222, 111));
        QCOMPARE(tray->currentItem()->text(1), QStringLiteral("222"));

        // Move it up, then down into the next column.
        button("upButton")->click();
        QCOMPARE(dialog.trayLayout().find(QStringLiteral("History")), QPoint(0, 0));
        button("downButton")->click();
        button("downButton")->click();
        QCOMPARE(dialog.trayLayout().find(QStringLiteral("History")), QPoint(1, 0));

        // Remove a docker, add and remove columns.
        button("removeButton")->click();
        QCOMPARE(dialog.trayLayout().find(QStringLiteral("History")), QPoint(-1, -1));
        button("addColumnButton")->click();
        QCOMPARE(tray->topLevelItemCount(), 3);
        QVERIFY(button("removeColumnButton")->isEnabled());
        button("removeColumnButton")->click();
        QCOMPARE(tray->topLevelItemCount(), 2);
        // Empty columns are dropped from the result.
        tray->setCurrentItem(tray->topLevelItem(1)->child(0));
        button("removeButton")->click();
        QCOMPARE(dialog.trayLayout().columns.size(), 1);
        QCOMPARE(layout.columns.size(), 2);

        dialog.findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"))
            ->button(QDialogButtonBox::RestoreDefaults)
            ->click();
        QCOMPARE(dialog.trayLayout(), KisDockerTrayLayout::defaultLayout());
    }

    void reloadKeepsTheTrayOpen()
    {
        Fixture f;
        KisDockerTray *tray = f.tray();
        QSignalSpy finished(tray, SIGNAL(finished()));
        tray->popupAt(f.center());
        QVERIFY(tray->isAncestorOf(f.layersContent));
        f.setLayout(layoutJson(QStringLiteral("[{\"items\":[{\"id\":\"History\",\"w\":260,\"h\":100}]}]")));
        tray->reload();
        QVERIFY(tray->onScreen());
        QCOMPARE(finished.count(), 0);
        QVERIFY(tray->isAncestorOf(f.historyContent));
        QCOMPARE(f.layers->widget(), f.layersContent.data());
        tray->closeTray();
    }

    void dragResizes()
    {
        Fixture f;
        f.setLayout(
            layoutJson(QStringLiteral("[{\"items\":[{\"id\":\"KisLayerBox\",\"w\":200,\"h\":150},"
                                      "{\"id\":\"History\",\"w\":200,\"h\":100}]},"
                                      "{\"items\":[{\"id\":\"PresetDocker\",\"w\":180,\"h\":300}]}]")));
        KisDockerTray *tray = f.tray();
        tray->popupAt(f.center());
        QCoreApplication::processEvents();
        auto *widthGrip = tray->findChild<QWidget *>(QStringLiteral("dockerTrayWidthGrip_0"));
        auto *heightGrip = tray->findChild<QWidget *>(QStringLiteral("dockerTrayHeightGrip_KisLayerBox"));
        QVERIFY(widthGrip && heightGrip);
        QVERIFY(widthGrip->isVisible() && heightGrip->isVisible());
        QCOMPARE(f.cell(QStringLiteral("KisLayerBox"))->width(), 250);
        const int trayWidth = tray->width();

        // Wider: every docker of the column, saved at release.
        drag(widthGrip, QPoint(40, 0));
        QCOMPARE(f.cell(QStringLiteral("KisLayerBox"))->width(), 290);
        QCOMPARE(f.cell(QStringLiteral("History"))->width(), 290);
        QCOMPARE(tray->width(), trayWidth + 40);
        KisDockerTrayLayout saved = KisDockerTrayLayout::load(f.config);
        QCOMPARE(saved.columns[0][0].size, QSize(290, 150));
        QCOMPARE(saved.columns[0][1].size, QSize(290, 100));
        QCOMPARE(saved.columns[1][0].size, QSize(180, 300));
        QCOMPARE(tray->trayLayout(), saved);

        // Not below the widest minimum of the column (History: 250).
        drag(widthGrip, QPoint(-1000, 0));
        QCOMPARE(f.cell(QStringLiteral("KisLayerBox"))->width(), 250);
        QCOMPARE(KisDockerTrayLayout::load(f.config).columns[0][0].size.width(), 250);

        // Height: only that docker.
        drag(heightGrip, QPoint(0, 50));
        QCOMPARE(f.cell(QStringLiteral("KisLayerBox"))->height(), 200);
        QCOMPARE(f.cell(QStringLiteral("History"))->height(), 100);
        saved = KisDockerTrayLayout::load(f.config);
        QCOMPARE(saved.columns[0][0].size, QSize(250, 200));
        QCOMPARE(saved.columns[0][1].size, QSize(250, 100));

        drag(heightGrip, QPoint(0, -1000));
        const int minimumHeight = f.cell(QStringLiteral("KisLayerBox"))->minimumSizeHint().height();
        QCOMPARE(f.cell(QStringLiteral("KisLayerBox"))->height(),
                 qMax(minimumHeight, int(KisDockerTrayLayout::MinimumExtent)));

        // The tray stays inside its screen.
        QScreen *screen = QGuiApplication::screenAt(tray->geometry().center());
        QVERIFY(screen);
        QVERIFY(screen->availableGeometry().contains(tray->geometry()));

        // Reopening uses the saved sizes.
        tray->closeTray();
        tray->popupAt(f.center());
        QCOMPARE(f.cell(QStringLiteral("KisLayerBox"))->size(), KisDockerTrayLayout::load(f.config).columns[0][0].size);
        tray->closeTray();
    }

    void layoutChangeRebuilds()
    {
        Fixture f;
        KisDockerTray *tray = f.tray();
        tray->popupAt(f.center());
        QVERIFY(f.cell(QStringLiteral("KisLayerBox")));
        tray->closeTray();

        f.setLayout(layoutJson(QStringLiteral("[{\"items\":[{\"id\":\"History\",\"w\":260,\"h\":100}]}]")));
        tray->popupAt(f.center());
        QVERIFY(!f.cell(QStringLiteral("KisLayerBox")));
        QVERIFY(tray->isAncestorOf(f.historyContent));
        QCOMPARE(f.layers->widget(), f.layersContent.data());
        tray->closeTray();
    }
};

QTEST_MAIN(KisDockerTrayTest)

#include "KisDockerTrayTest.moc"
