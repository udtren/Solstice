/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../utils/KisMenuMnemonicFilter.h"

#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

class KisMenuMnemonicFilterTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testLateMenusAndUpdates()
    {
        QMenuBar bar;
        KisMenuMnemonicFilter filter(&bar);
        filter.setSuppressed(true);
        QCoreApplication::processEvents(); // Startup's old single-shot could run here.
        QMenu *edit = bar.addMenu(QStringLiteral("&Edit"));
        QCOMPARE(edit->title(), QStringLiteral("Edit"));
        edit->setTitle(QStringLiteral("&Editing"));
        QCOMPARE(edit->title(), QStringLiteral("Editing"));
        edit->menuAction()->setEnabled(false);
        edit->menuAction()->setEnabled(true);
        filter.setSuppressed(true);
        filter.setSuppressed(false);
        QCOMPARE(edit->title(), QStringLiteral("&Editing"));
        edit->setTitle(QStringLiteral("&Edit"));
        filter.setSuppressed(true);
        QCOMPARE(edit->title(), QStringLiteral("Edit"));
        bar.removeAction(edit->menuAction());
        bar.addAction(edit->menuAction());
        filter.setSuppressed(false);
        QCOMPARE(edit->title(), QStringLiteral("&Edit"));
    }

    void testExistingMenusAndScope()
    {
        QMenuBar bar, otherBar;
        auto *menu = bar.addMenu(QStringLiteral("R&&D &Tools"));
        auto *child = menu->addAction(QStringLiteral("&Copy"));
        auto *other = otherBar.addMenu(QStringLiteral("&Edit"));
        KisMenuMnemonicFilter filter(&bar);
        filter.setSuppressed(true);
        QCOMPARE(menu->title(), QStringLiteral("R&&D Tools"));
        QCOMPARE(child->text(), QStringLiteral("&Copy"));
        QCOMPARE(other->title(), QStringLiteral("&Edit"));
        filter.setSuppressed(false);
        QCOMPARE(menu->title(), QStringLiteral("R&&D &Tools"));
    }

    void testAltShortcutAndRestoration()
    {
        QMainWindow window;
        auto *canvas = new QPushButton(QStringLiteral("Canvas"), &window);
        window.setCentralWidget(canvas);
        auto *bar = window.menuBar();
        bar->setNativeMenuBar(false);
        KisMenuMnemonicFilter filter(bar);
        filter.setSuppressed(true);
        QCoreApplication::processEvents();
        auto *edit = bar->addMenu(QStringLiteral("&Edit"));
        edit->addAction(QStringLiteral("&Copy"));
        QAction shortcut(&window);
        shortcut.setShortcut(QKeySequence(QStringLiteral("Alt+E")));
        window.addAction(&shortcut);
        QSignalSpy activated(&shortcut, &QAction::triggered);
        QSignalSpy opened(edit, &QMenu::aboutToShow);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_E, Qt::AltModifier);
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(opened.count(), 0);
        // XMLGUI may replace the title after first show too.
        edit->setTitle(QStringLiteral("&Edit"));
        QTest::keyClick(canvas, Qt::Key_E, Qt::AltModifier);
        QTRY_COMPARE(activated.count(), 2);
        QCOMPARE(opened.count(), 0);
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->actionGeometry(edit->menuAction()).center());
        QTRY_COMPARE(opened.count(), 1);
        edit->hide();
        shortcut.setEnabled(false);
        filter.setSuppressed(false);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_E, Qt::AltModifier);
        QTRY_COMPARE(opened.count(), 2);
        edit->hide();
    }

    void testMenuBarOwnsFilter()
    {
        auto *bar = new QMenuBar;
        QPointer<KisMenuMnemonicFilter> filter = new KisMenuMnemonicFilter(bar);
        filter->setSuppressed(true);
        bar->addMenu(QStringLiteral("&Edit"));
        delete bar;
        QVERIFY(filter.isNull());
    }
};

QTEST_MAIN(KisMenuMnemonicFilterTest)
#include "KisMenuMnemonicFilterTest.moc"
