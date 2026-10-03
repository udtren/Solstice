/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "AssetLibraryDock.h"

#include <KoDockRegistry.h>
#include <kpluginfactory.h>

class AssetLibraryPlugin : public QObject
{
    Q_OBJECT
public:
    AssetLibraryPlugin(QObject *parent, const QVariantList &)
        : QObject(parent)
    {
        KoDockRegistry::instance()->add(new AssetLibraryDockFactory());
    }
};

K_PLUGIN_FACTORY_WITH_JSON(AssetLibraryPluginFactory,
                           "krita_assetlibrarydocker.json",
                           registerPlugin<AssetLibraryPlugin>();)

#include "AssetLibraryPlugin.moc"
