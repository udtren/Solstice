/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <simpletest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QScopedPointer>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyleFactory>
#include <QTabBar>
#include <QToolButton>

#include <KisSolsticeStyle.h>

namespace
{
QPalette darkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(40, 40, 43));
    palette.setColor(QPalette::WindowText, QColor(224, 224, 228));
    palette.setColor(QPalette::Base, QColor(30, 30, 33));
    palette.setColor(QPalette::Text, QColor(224, 224, 228));
    palette.setColor(QPalette::Button, QColor(50, 50, 54));
    palette.setColor(QPalette::ButtonText, QColor(224, 224, 228));
    palette.setColor(QPalette::Highlight, QColor(70, 108, 150));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    return palette;
}

void applyStyle(QWidget *widget, QStyle *style)
{
    widget->setStyle(style);
    for (QWidget *child : widget->findChildren<QWidget *>())
        child->setStyle(style);
}

/// One of each control the Solstice style draws, in a grid.
QWidget *createGallery()
{
    QWidget *gallery = new QWidget();
    QGridLayout *layout = new QGridLayout(gallery);
    QPushButton *button = new QPushButton(QStringLiteral("Apply"));
    QPushButton *toggled = new QPushButton(QStringLiteral("Toggled"));
    toggled->setCheckable(true);
    toggled->setChecked(true);
    QToolButton *tool = new QToolButton();
    tool->setText(QStringLiteral("Tool"));
    tool->setCheckable(true);
    tool->setChecked(true);
    tool->setAutoRaise(true);
    QCheckBox *check = new QCheckBox(QStringLiteral("Checked"));
    check->setChecked(true);
    QCheckBox *uncheck = new QCheckBox(QStringLiteral("Unchecked"));
    QCheckBox *partial = new QCheckBox(QStringLiteral("Partial"));
    partial->setTristate(true);
    partial->setCheckState(Qt::PartiallyChecked);
    QRadioButton *radio = new QRadioButton(QStringLiteral("Radio"));
    radio->setChecked(true);
    QLineEdit *edit = new QLineEdit(QStringLiteral("Line edit"));
    QComboBox *combo = new QComboBox();
    combo->addItems({QStringLiteral("Combo box"), QStringLiteral("Second")});
    QSpinBox *spin = new QSpinBox();
    spin->setValue(42);
    QScrollBar *scroll = new QScrollBar(Qt::Horizontal);
    scroll->setRange(0, 100);
    scroll->setPageStep(30);
    scroll->setValue(20);
    QTabBar *tabs = new QTabBar();
    tabs->addTab(QStringLiteral("General"));
    tabs->addTab(QStringLiteral("Window"));
    layout->addWidget(button, 0, 0);
    layout->addWidget(toggled, 0, 1);
    layout->addWidget(tool, 0, 2);
    layout->addWidget(check, 1, 0);
    layout->addWidget(uncheck, 1, 1);
    layout->addWidget(partial, 1, 2);
    layout->addWidget(radio, 2, 0);
    layout->addWidget(edit, 2, 1);
    layout->addWidget(combo, 2, 2);
    layout->addWidget(spin, 3, 0);
    layout->addWidget(scroll, 3, 1, 1, 2);
    layout->addWidget(tabs, 4, 0, 1, 3);
    return gallery;
}
} // namespace

class KisSolsticeStyleTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testStyleKey()
    {
        QVERIFY(KisSolsticeStyle::isStyleKey(QStringLiteral("Solstice")));
        QVERIFY(KisSolsticeStyle::isStyleKey(QStringLiteral("solstice")));
        QVERIFY(!KisSolsticeStyle::isStyleKey(QStringLiteral("fusion")));
        KisSolsticeStyle style;
        // Widget-local proxy styles clone the application style by name.
        QCOMPARE(style.objectName(), QStringLiteral("fusion"));
    }

    void testCreateStyleFollowsApplication()
    {
        // Widget-local proxy styles clone the application style by name.
        QStyle *previous = QApplication::style();
        const QString previousName = previous->objectName();
        QApplication::setStyle(QStringLiteral("fusion"));
        QScopedPointer<QStyle> plain(KisSolsticeStyle::createStyle(QStringLiteral("fusion")));
        QVERIFY(plain);
        QVERIFY(!qobject_cast<KisSolsticeStyle *>(plain.data()));
        QApplication::setStyle(new KisSolsticeStyle());
        QScopedPointer<QStyle> solstice(KisSolsticeStyle::createStyle(QStringLiteral("fusion")));
        QVERIFY(qobject_cast<KisSolsticeStyle *>(solstice.data()));
        QScopedPointer<QStyle> windows(KisSolsticeStyle::createStyle(QStringLiteral("windows")));
        QVERIFY(!qobject_cast<KisSolsticeStyle *>(windows.data()));
        QApplication::setStyle(previousName);
    }

    void testSizesMatchFusion()
    {
        // The style changes only drawing: every control keeps Fusion's size.
        QScopedPointer<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
        KisSolsticeStyle solstice;
        QScopedPointer<QWidget> a(createGallery());
        QScopedPointer<QWidget> b(createGallery());
        applyStyle(a.data(), fusion.data());
        applyStyle(b.data(), &solstice);
        const QList<QWidget *> as = a->findChildren<QWidget *>();
        const QList<QWidget *> bs = b->findChildren<QWidget *>();
        QCOMPARE(as.size(), bs.size());
        for (int i = 0; i < as.size(); ++i) {
            QCOMPARE(bs[i]->sizeHint(), as[i]->sizeHint());
            QCOMPARE(bs[i]->minimumSizeHint(), as[i]->minimumSizeHint());
        }
        QMenu menuA;
        QMenu menuB;
        for (QMenu *menu : {&menuA, &menuB}) {
            menu->addAction(QStringLiteral("Configure Solstice..."));
            menu->addSeparator();
            menu->addAction(QStringLiteral("Styles"))->setCheckable(true);
        }
        menuA.setStyle(fusion.data());
        menuB.setStyle(&solstice);
        QCOMPARE(menuB.sizeHint(), menuA.sizeHint());
    }

    void testRendersControls()
    {
        // Renders the gallery with both styles; SOLSTICE_STYLE_DUMP=<dir>
        // saves fusion.png and solstice.png for review.
        QScopedPointer<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
        KisSolsticeStyle solstice;
        for (QStyle *style : {fusion.data(), static_cast<QStyle *>(&solstice)}) {
            QScopedPointer<QWidget> gallery(createGallery());
            applyStyle(gallery.data(), style);
            gallery->setPalette(darkPalette());
            for (QWidget *child : gallery->findChildren<QWidget *>())
                child->setPalette(darkPalette());
            gallery->setAutoFillBackground(true);
            gallery->resize(gallery->sizeHint().expandedTo(QSize(420, 0)));
            const QImage image = gallery->grab().toImage();
            QVERIFY(!image.isNull());
            if (qEnvironmentVariableIsSet("SOLSTICE_STYLE_DUMP")) {
                const QString name = style == &solstice ? QStringLiteral("solstice") : QStringLiteral("fusion");
                image.save(QString::fromLocal8Bit(qgetenv("SOLSTICE_STYLE_DUMP")) + QLatin1Char('/') + name
                           + QStringLiteral(".png"));
            }
        }
    }
};

SIMPLE_TEST_MAIN(KisSolsticeStyleTest)

#include "KisSolsticeStyleTest.moc"
