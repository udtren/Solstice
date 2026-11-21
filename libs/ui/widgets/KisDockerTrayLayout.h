/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KIS_DOCKER_TRAY_LAYOUT_H
#define KIS_DOCKER_TRAY_LAYOUT_H

#include <QByteArray>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <ksharedconfig.h>

#include "kritaui_export.h"

/// One docker on the Docker Tray and its stored size.
struct KRITAUI_EXPORT KisDockerTrayCell {
    QString dockerId;
    QSize size;

    bool operator==(const KisDockerTrayCell &other) const
    {
        return dockerId == other.dockerId && size == other.size;
    }
};

/**
 * The Docker Tray's arrangement: columns of stacked dockers
 * (docs/agent/docker-tray.md). Stored as JSON in kritarc, group
 * `DockerTray`, key `Layout`:
 *
 *   {"version": 1, "trays": [{"name": "Default",
 *     "columns": [{"items": [{"id": "KisLayerBox", "w": 320, "h": 400}]}]}]}
 *
 * Only the first tray is used.
 */
struct KRITAUI_EXPORT KisDockerTrayLayout {
    static constexpr int MinimumExtent = 50;
    static constexpr int MaximumExtent = 4000;
    static const QSize DefaultCellSize;

    QVector<QVector<KisDockerTrayCell>> columns;

    /// True if no column holds a docker.
    bool isEmpty() const;
    QStringList dockerIds() const;

    // Editing (the configuration dialog). Columns may be empty while
    // editing; fromJson() drops empty columns when the layout is read back.

    /// Column (x) and row (y) of \p dockerId, or (-1, -1).
    QPoint find(const QString &dockerId) const;
    /**
     * Appends \p dockerId to \p column; \p column equal to the number of
     * columns appends a new column. Fails if the docker is already there or
     * the column does not exist.
     */
    bool addDocker(const QString &dockerId, int column, const QSize &size = DefaultCellSize);
    /// Removes the docker; its column stays, even if empty.
    bool removeDocker(const QString &dockerId);
    /**
     * Moves the docker one row up (\p delta -1) or down (+1). Past the top
     * or bottom of its column it goes to the end of the previous column or
     * the start of the next one.
     */
    bool moveDocker(const QString &dockerId, int delta);
    /// Moves a whole column one place left (-1) or right (+1).
    bool moveColumn(int column, int delta);
    void addColumn();
    /// Removes the column with its dockers.
    bool removeColumn(int column);
    /// Sets the stored size, clamped to [MinimumExtent, MaximumExtent].
    bool setSize(const QString &dockerId, const QSize &size);

    /// One column with the Layers docker.
    static KisDockerTrayLayout defaultLayout();

    /**
     * Reads the first tray of \p json. Cells without an id, repeated
     * dockers and empty columns are dropped; sizes are clamped to
     * [MinimumExtent, MaximumExtent], missing ones use DefaultCellSize.
     * Returns the default layout and sets \p ok to false if \p json is
     * not a valid layout.
     */
    static KisDockerTrayLayout fromJson(const QByteArray &json, bool *ok = nullptr);
    QByteArray toJson() const;

    static KisDockerTrayLayout load(KSharedConfigPtr config = KSharedConfig::openConfig());
    void save(KSharedConfigPtr config = KSharedConfig::openConfig()) const;

    bool operator==(const KisDockerTrayLayout &other) const
    {
        return columns == other.columns;
    }
};

namespace KisDockerTrayGeometry
{
/**
 * Sizes for each cell of \p wanted (columns of cells). A column is as wide
 * as its widest cell; cells are at least their \p minimum. If the columns
 * (with \p spacing between them) are wider than \p available, every
 * column is narrowed by the same factor, never below its minimum; a column
 * taller than \p available is shortened the same way. An invalid
 * \p available leaves the sizes as wanted.
 */
KRITAUI_EXPORT QVector<QVector<QSize>>
fit(const QVector<QVector<QSize>> &wanted, const QVector<QVector<QSize>> &minimum, const QSize &available, int spacing);
} // namespace KisDockerTrayGeometry

#endif // KIS_DOCKER_TRAY_LAYOUT_H
