/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QCoreApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QTest>
#include <QVBoxLayout>

#include "KisDockerBorrowRegistry.h"
#include "kis_utility_title_bar.h"

namespace
{
class TestBorrower : public QWidget, public KisDockerBorrower
{
public:
    TestBorrower(const QString &name, QStringList *log)
        : m_name(name)
        , m_log(log)
    {
        new QVBoxLayout(this);
    }

    QString dockerBorrowerName() const override
    {
        return m_name;
    }

    void dockerContentGranted(const QString &dockerId, QWidget *content, QWidget *titleArea) override
    {
        *m_log << QStringLiteral("%1 granted %2").arg(m_name, dockerId);
        if (titleArea) {
            layout()->addWidget(titleArea);
        }
        layout()->addWidget(content);
        content->show();
        m_content = content;
        m_titleArea = titleArea;
    }

    void dockerContentHeldElsewhere(const QString &dockerId, const QString &holderName) override
    {
        *m_log << QStringLiteral("%1 waits for %2 in %3").arg(m_name, dockerId, holderName);
        if (m_content) {
            layout()->removeWidget(m_content);
        }
        if (m_titleArea) {
            layout()->removeWidget(m_titleArea);
        }
        m_content = nullptr;
        m_titleArea = nullptr;
    }

    QPointer<QWidget> m_content;
    QPointer<QWidget> m_titleArea;

private:
    QString m_name;
    QStringList *m_log;
};

struct Fixture {
    Fixture()
    {
        window.setCentralWidget(new QWidget(&window));
        dock = new QDockWidget(QStringLiteral("Layers"), &window);
        dock->setObjectName(QStringLiteral("KisLayerBox"));
        content = new QLabel(QStringLiteral("layers content"));
        dock->setWidget(content);
        window.addDockWidget(Qt::RightDockWidgetArea, dock);
        registry = KisDockerBorrowRegistry::forWindow(&window);
    }

    TestBorrower *borrower(const QString &name)
    {
        return new TestBorrower(name, &log);
    }

    QMainWindow window;
    QDockWidget *dock = nullptr;
    QPointer<QWidget> content;
    KisDockerBorrowRegistry *registry = nullptr;
    QStringList log;
};

void deferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
} // namespace

class KisDockerBorrowRegistryTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void registryPerWindow()
    {
        Fixture f;
        QMainWindow other;
        QCOMPARE(KisDockerBorrowRegistry::forWindow(&f.window), f.registry);
        QVERIFY(KisDockerBorrowRegistry::forWindow(&other) != f.registry);
        QCOMPARE(KisDockerBorrowRegistry::forWindow(nullptr), nullptr);
    }

    void borrowAndReturn_data()
    {
        QTest::addColumn<bool>("dockVisible");
        QTest::newRow("visible") << true;
        QTest::newRow("hidden") << false;
    }

    void borrowAndReturn()
    {
        QFETCH(bool, dockVisible);
        Fixture f;
        f.window.show();
        f.dock->setVisible(dockVisible);
        QScopedPointer<TestBorrower> a(f.borrower(QStringLiteral("Tray")));

        QVERIFY(f.registry->request(QStringLiteral("KisLayerBox"), a.data()));
        QCOMPARE(f.content->parentWidget(), a.data());
        QCOMPARE(a->m_content.data(), f.content.data());
        QCOMPARE(f.registry->holder(QStringLiteral("KisLayerBox")), a.data());
        QPointer<QLabel> placeholder = qobject_cast<QLabel *>(f.dock->widget());
        QVERIFY(placeholder);
        QVERIFY(placeholder->text().contains(QStringLiteral("Tray")));
        QCOMPARE(f.dock->isVisible(), dockVisible);

        // Asking again changes nothing.
        QVERIFY(f.registry->request(QStringLiteral("KisLayerBox"), a.data()));
        QCOMPARE(f.log, QStringList({QStringLiteral("Tray granted KisLayerBox")}));

        f.registry->release(QStringLiteral("KisLayerBox"), a.data());
        QCOMPARE(f.dock->widget(), f.content.data());
        QCOMPARE(f.content->parentWidget(), f.dock);
        QCOMPARE(f.dock->isVisible(), dockVisible);
        QCOMPARE(f.registry->holder(QStringLiteral("KisLayerBox")), nullptr);
        deferredDeletes();
        QVERIFY(placeholder.isNull());

        // It can be borrowed again.
        QVERIFY(f.registry->request(QStringLiteral("KisLayerBox"), a.data()));
        QCOMPARE(f.content->parentWidget(), a.data());
    }

    void unknownDocker()
    {
        Fixture f;
        QScopedPointer<TestBorrower> a(f.borrower(QStringLiteral("Tray")));
        QVERIFY(!f.registry->request(QStringLiteral("NoSuchDocker"), a.data()));

        auto *empty = new QDockWidget(QStringLiteral("Empty"), &f.window);
        empty->setObjectName(QStringLiteral("EmptyDocker"));
        QVERIFY(!f.registry->request(QStringLiteral("EmptyDocker"), a.data()));
        QVERIFY(f.log.isEmpty());
        f.registry->release(QStringLiteral("NoSuchDocker"), a.data());
    }

    void lastRequesterHolds()
    {
        Fixture f;
        QScopedPointer<TestBorrower> a(f.borrower(QStringLiteral("A")));
        QScopedPointer<TestBorrower> b(f.borrower(QStringLiteral("B")));
        QScopedPointer<TestBorrower> c(f.borrower(QStringLiteral("C")));
        const QString id = QStringLiteral("KisLayerBox");

        f.registry->request(id, a.data());
        f.registry->request(id, b.data());
        QCOMPARE(f.content->parentWidget(), b.data());
        QVERIFY(qobject_cast<QLabel *>(f.dock->widget())->text().contains(QStringLiteral("B")));
        f.registry->request(id, c.data());
        QCOMPARE(f.content->parentWidget(), c.data());
        QCOMPARE(f.registry->borrowers(id), QList<KisDockerBorrower *>({a.data(), b.data(), c.data()}));
        QCOMPARE(f.log,
                 QStringList({QStringLiteral("A granted KisLayerBox"),
                              QStringLiteral("A waits for KisLayerBox in B"),
                              QStringLiteral("B granted KisLayerBox"),
                              QStringLiteral("B waits for KisLayerBox in C"),
                              QStringLiteral("C granted KisLayerBox"),
                              QStringLiteral("A waits for KisLayerBox in C")}));
        f.log.clear();

        // The holder gives it to the newest waiting borrower.
        f.registry->release(id, c.data());
        QCOMPARE(f.content->parentWidget(), b.data());
        QCOMPARE(
            f.log,
            QStringList({QStringLiteral("B granted KisLayerBox"), QStringLiteral("A waits for KisLayerBox in B")}));
        f.log.clear();

        // A waiting borrower leaves without disturbing the holder.
        f.registry->release(id, a.data());
        QCOMPARE(f.content->parentWidget(), b.data());
        QVERIFY(f.log.isEmpty());

        f.registry->release(id, b.data());
        QCOMPARE(f.dock->widget(), f.content.data());
    }

    void waitingBorrowerRequestsAgain()
    {
        Fixture f;
        QScopedPointer<TestBorrower> a(f.borrower(QStringLiteral("A")));
        QScopedPointer<TestBorrower> b(f.borrower(QStringLiteral("B")));
        const QString id = QStringLiteral("KisLayerBox");

        f.registry->request(id, a.data());
        f.registry->request(id, b.data());
        f.registry->request(id, a.data());
        QCOMPARE(f.content->parentWidget(), a.data());
        QCOMPARE(f.registry->borrowers(id), QList<KisDockerBorrower *>({b.data(), a.data()}));
        f.registry->release(id, a.data());
        QCOMPARE(f.content->parentWidget(), b.data());
    }

    void borrowerDestroyed()
    {
        Fixture f;
        const QString id = QStringLiteral("KisLayerBox");
        TestBorrower *a = f.borrower(QStringLiteral("A"));
        TestBorrower *b = f.borrower(QStringLiteral("B"));
        f.registry->request(id, a);
        f.registry->request(id, b);

        // A waiting borrower disappears.
        delete a;
        QCOMPARE(f.registry->borrowers(id), QList<KisDockerBorrower *>({b}));

        // The holder disappears with the content in its layout.
        delete b;
        QVERIFY(f.content);
        QCOMPARE(f.dock->widget(), f.content.data());
        QCOMPARE(f.content->parentWidget(), f.dock);
        QCOMPARE(f.registry->holder(id), nullptr);
    }

    void registryDestroyed()
    {
        Fixture f;
        QScopedPointer<TestBorrower> a(f.borrower(QStringLiteral("A")));
        f.registry->request(QStringLiteral("KisLayerBox"), a.data());
        delete f.registry;
        QCOMPARE(f.dock->widget(), f.content.data());
        QCOMPARE(f.content->parentWidget(), f.dock);
    }

    void titleBarControls()
    {
        Fixture f;
        auto *timeline = new QDockWidget(QStringLiteral("Timeline"), &f.window);
        timeline->setObjectName(QStringLiteral("TimelineDocker"));
        auto *titleBar = new KisUtilityTitleBar(new QLabel(QStringLiteral("Timeline")), timeline);
        timeline->setTitleBarWidget(titleBar);
        QPointer<QWidget> frames = new QLabel(QStringLiteral("frames"));
        timeline->setWidget(frames);
        f.window.addDockWidget(Qt::BottomDockWidgetArea, timeline);
        QPointer<QWidget> controls = titleBar->widgetArea();
        QVERIFY(controls);

        QScopedPointer<TestBorrower> a(f.borrower(QStringLiteral("A")));
        QVERIFY(f.registry->request(QStringLiteral("TimelineDocker"), a.data()));
        QCOMPARE(a->m_titleArea.data(), controls.data());
        QCOMPARE(controls->parentWidget(), a.data());
        QCOMPARE(frames->parentWidget(), a.data());

        // Other dockers lend no title-bar controls.
        QScopedPointer<TestBorrower> b(f.borrower(QStringLiteral("B")));
        f.registry->request(QStringLiteral("KisLayerBox"), b.data());
        QVERIFY(!b->m_titleArea);

        f.registry->release(QStringLiteral("TimelineDocker"), a.data());
        QCOMPARE(controls->parentWidget(), titleBar);
        QCOMPARE(titleBar->widgetArea(), controls.data());
        QCOMPARE(timeline->widget(), frames.data());
    }
};

QTEST_MAIN(KisDockerBorrowRegistryTest)

#include "KisDockerBorrowRegistryTest.moc"
