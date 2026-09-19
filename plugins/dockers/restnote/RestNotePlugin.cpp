/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "RestNoteDock.h"

#include <KoDockRegistry.h>
#include <kpluginfactory.h>

class RestNotePlugin : public QObject
{
    Q_OBJECT
public:
    RestNotePlugin(QObject *parent, const QVariantList &)
        : QObject(parent)
    {
        KoDockRegistry::instance()->add(new RestNoteDockFactory());
    }
};

K_PLUGIN_FACTORY_WITH_JSON(RestNotePluginFactory, "krita_restnotedocker.json", registerPlugin<RestNotePlugin>();)

#include "RestNotePlugin.moc"
