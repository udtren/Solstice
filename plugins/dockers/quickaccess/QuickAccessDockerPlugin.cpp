/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickAccessDock.h"
#include "QuickAdjustDock.h"
#include "QuickColorSelectorDock.h"

#include <KoDockRegistry.h>
#include <kconfiggroup.h>
#include <kpluginfactory.h>
#include <ksharedconfig.h>

class QuickAccessDockerPlugin : public QObject
{
    Q_OBJECT
public:
    QuickAccessDockerPlugin(QObject *parent, const QVariantList &)
        : QObject(parent)
    {
        KoDockRegistry::instance()->add(new QuickAccessDockFactory());
        const KConfigGroup config = KSharedConfig::openConfig()->group(QStringLiteral("QuickAccess"));
        if (config.readEntry("QuickAdjustEnabled", true)) {
            KoDockRegistry::instance()->add(new QuickAdjustDockFactory());
        }
        if (config.readEntry("HueSVCEnabled", true))
            KoDockRegistry::instance()->add(new QuickColorSelectorDockFactory());
    }
};

K_PLUGIN_FACTORY_WITH_JSON(QuickAccessDockerPluginFactory,
                           "krita_quickaccessdocker.json",
                           registerPlugin<QuickAccessDockerPlugin>();)

#include "QuickAccessDockerPlugin.moc"
