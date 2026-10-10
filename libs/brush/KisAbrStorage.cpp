/*
 * SPDX-FileCopyrightText: 2018 Boudewijn Rempt <boud@valdyas.org>
 * SPDX-FileCopyrightText: 2019 Agata Cacko <cacko.azh@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KisAbrStorage.h"
#include "KisResourceStorage.h"

#include <QFileInfo>
#include <KisStaticInitializer.h>

KIS_DECLARE_STATIC_INITIALIZER {
    KisStoragePluginRegistry::instance()->addStoragePluginFactory(KisResourceStorage::StorageType::AdobeBrushLibrary, new KisStoragePluginFactory<KisAbrStorage>());
}

class AbrTagIterator : public KisResourceStorage::TagIterator
{
public:
    AbrTagIterator(KisAbrBrushCollectionSP brushCollection, const QString &location, const QString &resourceType)
        : m_brushCollection(brushCollection)
        , m_location(location)
        , m_resourceType(resourceType)
    {}

    bool hasNext() const override {
        if (m_resourceType != ResourceType::Brushes && m_resourceType != ResourceType::Patterns)
            return false;
        return !m_taggingDone;
    }

    void next() override { m_taggingDone = true; }

    KisTagSP tag() const override
    {
        KisTagSP abrTag(new KisTag());
        abrTag->setUrl(QFileInfo(m_location).fileName());
        abrTag->setName(QFileInfo(m_location).fileName());
        abrTag->setComment(QFileInfo(m_location).fileName());
        abrTag->setFilename(QFileInfo(m_location).fileName());
        abrTag->setResourceType(m_resourceType);
        abrTag->setValid(true);
        QStringList brushes;
        if (m_resourceType == ResourceType::Patterns) {
            // Solstice: the file's patterns (docs/agent/abr-import-plan.md)
            brushes = m_brushCollection->patternsMap()->keys();
        } else {
            Q_FOREACH (const KisAbrBrushSP brush, m_brushCollection->brushes()) {
                brushes << brush->filename();
            }
        }
        abrTag->setDefaultResources(brushes);

        return abrTag;
    }

private:

    bool m_taggingDone {false};
    KisAbrBrushCollectionSP m_brushCollection;
    QString m_location;
    QString m_resourceType;
};

class AbrIterator : public KisResourceStorage::ResourceIterator
{
public:
    KisAbrBrushCollectionSP m_brushCollection;
    /// Solstice: the brush tips or the patterns (docs/agent/abr-import-plan.md)
    QVector<QPair<QString, KoResourceSP>> m_items;
    int m_nextItem{0};
    KoResourceSP m_currentResource;
    bool isLoaded;
    QString m_currentUrl;
    QString m_resourceType;


    AbrIterator(KisAbrBrushCollectionSP brushCollection, const QString& resourceType)
        : m_brushCollection(brushCollection)
        , isLoaded(false)
        , m_resourceType(resourceType)
    {
    }

    bool hasNext() const override
    {
        if (m_resourceType != ResourceType::Brushes && m_resourceType != ResourceType::Patterns) {
            return false;
        }

        if (!isLoaded) {
            AbrIterator *self = const_cast<AbrIterator *>(this);
            if (!m_brushCollection->isLoaded()) {
                bool success = m_brushCollection->load();
                Q_UNUSED(success); // brush collection will be empty
            }
            if (m_resourceType == ResourceType::Brushes) {
                const auto brushes = m_brushCollection->brushesMap();
                for (auto it = brushes->constBegin(); it != brushes->constEnd(); ++it) {
                    self->m_items.append({it.key(), it.value()});
                }
            } else {
                const auto patterns = m_brushCollection->patternsMap();
                for (auto it = patterns->constBegin(); it != patterns->constEnd(); ++it) {
                    self->m_items.append({it.key(), it.value()});
                }
            }
            self->isLoaded = true;
        }

        return m_nextItem < m_items.size();
    }

    void next() override
    {
        KIS_SAFE_ASSERT_RECOVER_RETURN(m_nextItem < m_items.size());
        m_currentUrl = m_items[m_nextItem].first;
        m_currentResource = m_items[m_nextItem].second;
        m_nextItem++;
    }

    QString url() const override { return m_currentUrl; }
    QString type() const override
    {
        return m_resourceType;
    }
    QDateTime lastModified() const override { return m_brushCollection->lastModified(); }

    KoResourceSP resourceImpl() const override
    {
        return m_currentResource;
    }
};

KisAbrStorage::KisAbrStorage(const QString &location)
    : KisStoragePlugin(location)
    , m_brushCollection(new KisAbrBrushCollection(location))
{
}

KisAbrStorage::~KisAbrStorage()
{

}

KisResourceStorage::ResourceItem KisAbrStorage::resourceItem(const QString &url)
{
    KisResourceStorage::ResourceItem item;
    item.url = url;
    // last "_" with index is the suffix added by abr_collection
    int indexOfUnderscore = url.lastIndexOf("_");
    QString filenameUrl = url;
    // filenameUrl contains the name of the collection (filename without .abr, brush name without index)
    filenameUrl.remove(indexOfUnderscore, url.length() - indexOfUnderscore);
    item.folder = filenameUrl;
    item.resourceType = ResourceType::Brushes;
    // Solstice: the file's patterns are <identifier>.pat
    if (url.endsWith(QStringLiteral(".pat"))) {
        item.folder = QFileInfo(m_brushCollection->filename()).completeBaseName();
        item.resourceType = ResourceType::Patterns;
    }
    item.lastModified = QFileInfo(m_brushCollection->filename()).lastModified();
    return item;
}


KoResourceSP KisAbrStorage::resource(const QString &url)
{
    if (!m_brushCollection->isLoaded()) {
        m_brushCollection->load();
    }
    const QString name = QFileInfo(url).fileName();
    if (name.endsWith(QStringLiteral(".pat"))) {
        return m_brushCollection->patternByName(name);
    }
    return m_brushCollection->brushByName(name);
}

bool KisAbrStorage::loadVersionedResource(KoResourceSP /*resource*/)
{
    return false;
}

bool KisAbrStorage::supportsVersioning() const
{
    return false;
}

QSharedPointer<KisResourceStorage::ResourceIterator> KisAbrStorage::resources(const QString &resourceType)
{
    return QSharedPointer<KisResourceStorage::ResourceIterator>(new AbrIterator(m_brushCollection, resourceType));
}

QSharedPointer<KisResourceStorage::TagIterator> KisAbrStorage::tags(const QString &resourceType)
{
    return QSharedPointer<KisResourceStorage::TagIterator>(new AbrTagIterator(m_brushCollection, location(), resourceType));
}

QImage KisAbrStorage::thumbnail() const
{
    return m_brushCollection->image();
}
