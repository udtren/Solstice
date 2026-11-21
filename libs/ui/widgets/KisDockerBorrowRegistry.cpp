/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisDockerBorrowRegistry.h"

#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>

#include <klocalizedstring.h>

#include "kis_assert.h"
#include "kis_utility_title_bar.h"

namespace
{
QList<KisDockerBorrowRegistry *> &liveRegistries()
{
    static QList<KisDockerBorrowRegistry *> registries;
    return registries;
}

bool hasTitleBarControls(const QString &dockerId)
{
    // Animation dockers keep playback controls in the title bar; they are
    // borrowed with the content (KisDockerHud did the same).
    return dockerId == QLatin1String("AnimationCurvesDocker") || dockerId == QLatin1String("TimelineDocker");
}
} // namespace

KisDockerBorrower::KisDockerBorrower() = default;

KisDockerBorrower::~KisDockerBorrower()
{
    const QList<KisDockerBorrowRegistry *> registries = liveRegistries();
    for (KisDockerBorrowRegistry *registry : registries) {
        registry->forget(this);
    }
}

QString KisDockerBorrower::dockerPlaceholderText() const
{
    return i18nc("%1 is the name of the widget", "Docker is open in %1. Close %1 to show here.", dockerBorrowerName());
}

KisDockerBorrowRegistry *KisDockerBorrowRegistry::forWindow(QMainWindow *window)
{
    if (!window) {
        return nullptr;
    }
    KisDockerBorrowRegistry *registry =
        window->findChild<KisDockerBorrowRegistry *>(QString(), Qt::FindDirectChildrenOnly);
    return registry ? registry : new KisDockerBorrowRegistry(window);
}

KisDockerBorrowRegistry::KisDockerBorrowRegistry(QMainWindow *window)
    : QObject(window)
    , m_window(window)
{
    setObjectName(QStringLiteral("dockerBorrowRegistry"));
    liveRegistries().append(this);
}

KisDockerBorrowRegistry::~KisDockerBorrowRegistry()
{
    liveRegistries().removeAll(this);
    const QList<Entry> entries = m_entries.values();
    m_entries.clear();
    for (Entry entry : entries) {
        returnToDocker(entry);
    }
}

bool KisDockerBorrowRegistry::request(const QString &dockerId, KisDockerBorrower *borrower)
{
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(borrower, false);
    if (!m_window) {
        return false;
    }

    auto it = m_entries.find(dockerId);
    if (it == m_entries.end()) {
        QDockWidget *dock = docker(dockerId);
        if (!dock || !dock->widget()) {
            return false;
        }

        Entry entry;
        entry.docker = dock;
        entry.content = dock->widget();
        if (hasTitleBarControls(dockerId)) {
            KisUtilityTitleBar *titleBar = dynamic_cast<KisUtilityTitleBar *>(dock->titleBarWidget());
            KIS_SAFE_ASSERT_RECOVER_NOOP(titleBar);
            if (titleBar) {
                entry.titleArea = titleBar->widgetArea();
            }
        }
        entry.placeholder = new QLabel();
        entry.placeholder->setWordWrap(true);
        entry.placeholder->setAlignment(Qt::AlignCenter);
        entry.borrowers.append(borrower);
        it = m_entries.insert(dockerId, entry);
        hand(dockerId, it.value());
        return true;
    }

    Entry &entry = it.value();
    KisDockerBorrower *previous = entry.borrowers.isEmpty() ? nullptr : entry.borrowers.last();
    if (previous == borrower) {
        return true;
    }

    entry.borrowers.removeAll(borrower);
    entry.borrowers.append(borrower);
    if (previous) {
        previous->dockerContentHeldElsewhere(dockerId, borrower->dockerBorrowerName());
    }
    hand(dockerId, entry);
    for (KisDockerBorrower *waiting : entry.borrowers) {
        if (waiting != borrower && waiting != previous) {
            waiting->dockerContentHeldElsewhere(dockerId, borrower->dockerBorrowerName());
        }
    }
    return true;
}

void KisDockerBorrowRegistry::release(const QString &dockerId, KisDockerBorrower *borrower)
{
    auto it = m_entries.find(dockerId);
    if (it == m_entries.end()) {
        return;
    }

    Entry &entry = it.value();
    const int index = entry.borrowers.indexOf(borrower);
    if (index < 0) {
        return;
    }
    const bool wasHolder = index == entry.borrowers.size() - 1;
    entry.borrowers.removeAt(index);
    if (!wasHolder) {
        return;
    }

    if (entry.borrowers.isEmpty()) {
        Entry returned = entry;
        m_entries.erase(it);
        returnToDocker(returned);
        return;
    }

    hand(dockerId, entry);
    notifyWaiting(dockerId, entry);
}

void KisDockerBorrowRegistry::releaseAll(KisDockerBorrower *borrower)
{
    const QStringList ids = m_entries.keys();
    for (const QString &id : ids) {
        release(id, borrower);
    }
}

KisDockerBorrower *KisDockerBorrowRegistry::holder(const QString &dockerId) const
{
    const auto it = m_entries.constFind(dockerId);
    return it == m_entries.constEnd() || it->borrowers.isEmpty() ? nullptr : it->borrowers.last();
}

QList<KisDockerBorrower *> KisDockerBorrowRegistry::borrowers(const QString &dockerId) const
{
    return m_entries.value(dockerId).borrowers;
}

QDockWidget *KisDockerBorrowRegistry::docker(const QString &dockerId) const
{
    return m_window ? m_window->findChild<QDockWidget *>(dockerId) : nullptr;
}

void KisDockerBorrowRegistry::hand(const QString &dockerId, Entry &entry)
{
    KisDockerBorrower *holder = entry.borrowers.last();
    if (entry.placeholder) {
        entry.placeholder->setText(holder->dockerPlaceholderText());
    }
    KIS_SAFE_ASSERT_RECOVER_NOOP(entry.content);
    holder->dockerContentGranted(dockerId, entry.content, entry.titleArea);

    // The borrower has re-parented the content, which removed it from the
    // docker's layout; fill the docker with the placeholder.
    if (entry.docker && entry.placeholder && entry.docker->widget() != entry.placeholder) {
        entry.docker->setWidget(entry.placeholder);
    }
}

void KisDockerBorrowRegistry::returnToDocker(Entry &entry)
{
    if (!entry.docker) {
        return;
    }

    if (entry.titleArea) {
        KisUtilityTitleBar *titleBar = dynamic_cast<KisUtilityTitleBar *>(entry.docker->titleBarWidget());
        KIS_SAFE_ASSERT_RECOVER_NOOP(titleBar);
        if (titleBar) {
            titleBar->setWidgetArea(entry.titleArea);
        }
    }

    if (entry.content) {
        entry.docker->setWidget(entry.content);
        entry.content->setParent(entry.docker);
        if (entry.placeholder) {
            entry.placeholder->deleteLater();
        }
    }
}

void KisDockerBorrowRegistry::notifyWaiting(const QString &dockerId, const Entry &entry)
{
    const QList<KisDockerBorrower *> borrowers = entry.borrowers;
    const QString holderName = borrowers.last()->dockerBorrowerName();
    for (int i = 0; i < borrowers.size() - 1; ++i) {
        borrowers[i]->dockerContentHeldElsewhere(dockerId, holderName);
    }
}

void KisDockerBorrowRegistry::forget(KisDockerBorrower *borrower)
{
    // Called from ~KisDockerBorrower: release() never calls the releasing
    // borrower back, so its virtual methods are not reached.
    releaseAll(borrower);
}
