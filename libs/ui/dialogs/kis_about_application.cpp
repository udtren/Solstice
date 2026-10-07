/*
 *  SPDX-FileCopyrightText: 2014 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2022 L. E. Segovia <amy@amyspark.me>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_about_application.h"

#include <KAboutData>
#include <KLocalizedString>
#include <QFile>
#include <QStandardPaths>

#include <kis_debug.h>
#include <kis_global.h>

#include "kis_splash_screen.h"
#include "ui_wdgaboutapplication.h"
#include <KisPortingUtils.h>

class Q_DECL_HIDDEN WdgAboutApplication : public QWidget, public Ui::WdgAboutApplication
{
public:
    WdgAboutApplication(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setupUi(this);
    }
};

KisAboutApplication::KisAboutApplication(QWidget *parent)
    : KoDialog(parent)
{
    setWindowTitle(i18n("About Solstice"));
    setButtons(KoDialog::Close);

    WdgAboutApplication *wdgTab = new WdgAboutApplication(this);

    KisSplashScreen *splash = new KisSplashScreen(true);
    splash->setWindowFlags(Qt::Widget);
    splash->displayLinks(true);

    wdgTab->aboutTab->layout()->addWidget(splash);

    // Solstice: the upstream Krita authors, translators, sponsors and credits
    // tabs are removed (docs/agent/solstice-visual-branding-todo.md); the License tab keeps the
    // attribution to Krita and its contributors.
    for (QWidget *tab : {wdgTab->authorsTab, wdgTab->translatorsTab, wdgTab->kickstarterTab, wdgTab->creditsTab}) {
        delete tab; // also removes its tab
    }

    QString license = i18n(
        "<html>"
        "<head/>"
        "<body>"
        "<h1 align=\"center\"><b>Your Rights</b></h1>"
        "<p>Solstice is an independent distribution based on Krita. It is not affiliated with or endorsed by the Krita "
        "Foundation.</p>"
        "<p>Solstice is released under the GNU General Public License (version 3 or any later version).</p>"
        "<p>This license grants people a number of freedoms:</p>"
        "<ul>"
        "<li>You are free to use Solstice, for any purpose</li>"
        "<li>You are free to distribute Solstice</li>"
        "<li>You can study how Solstice works and change it</li>"
        "<li>You can distribute changed versions of Solstice</li>"
        "</ul>"
        "<p>The upstream Krita project and its contributors retain their respective copyrights and credits.</p>"
        "<h1 align=\"center\">Your artwork</h1>"
        "<p>What you create with Solstice is your sole property. All your artwork is free for you to use as you "
        "like.</p>"
        "<p>That means that Solstice can be used commercially, for any purpose. There are no restrictions "
        "whatsoever.</p>"
        "<p>Solstice’s GNU GPL license guarantees you this freedom. Nobody is ever permitted to take it away, in "
        "contrast "
        "to trial or educational versions of commercial software that will forbid your work in commercial "
        "situations.</p>"
        "<br/><hr/><pre>");

    QFile licenseFile(":/LICENSE");
    Q_ASSERT(licenseFile.exists());
    if (licenseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream licenseText(&licenseFile);
        KisPortingUtils::setUtf8OnStream(licenseText);
        license.append(licenseText.readAll());
    }
    license.append("</pre></body></html>");
    wdgTab->lblLicense->setText(license);

    QFile thirdPartyFile(":/libraries.txt");
    if (thirdPartyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream thirdPartyText(&thirdPartyFile);
        KisPortingUtils::setUtf8OnStream(thirdPartyText);

        QString thirdPartyHtml = i18n(
            "<html>"
            "<head/>"
            "<body>"
            "<h1 align=\"center\"><b>Third-party Libraries used by Solstice</b></h1>"
            "<p>Solstice, through its Krita base, is built on the following free software libraries:</p><p><ul>");

        Q_FOREACH (const QString &lib, thirdPartyText.readAll().split('\n', Qt::SkipEmptyParts)) {
            if (!lib.startsWith("#")) {
                QStringList parts = lib.split(',');
                if (parts.size() >= 3) {
                    thirdPartyHtml.append(
                        QString("<li><a href=\"%2\">%1</a>: %3</li>").arg(parts[0], parts[1], parts[2]));
                }
            }
        }
        thirdPartyHtml.append("<ul></p></body></html>");
        wdgTab->lblThirdParty->setText(thirdPartyHtml);
    }

    setMainWidget(wdgTab);
    setMinimumSize(sizeHint());
    Q_ASSERT(layout());
    layout()->setSizeConstraint(QLayout::SetFixedSize);
}
