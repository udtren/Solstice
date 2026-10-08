/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2019 Wolthera van Hövell tot Westerflier <griffinvalley@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KISRESOURCEITEMLISTVIEW_H
#define KISRESOURCEITEMLISTVIEW_H

#include <QListView>
#include <QScopedPointer>

#include <KisKineticScroller.h>

#include "kritaresourcewidgets_export.h"
#include "ResourceListViewModes.h"

#include <functional>

class KRITARESOURCEWIDGETS_EXPORT KisResourceItemListView : public QListView
{
    Q_OBJECT

public:
    KisResourceItemListView(QWidget *parent = nullptr);
    ~KisResourceItemListView() override;

    void setListViewMode(ListViewMode layout);

    /**
     * @brief setItemSize
     * convenience function which sets both the icon and the grid size
     * to the same value.
     * @param size - the size you wish either to be.
     */
    void setItemSize(QSize size);

    /**
     * @brief setStrictSelectionMode sets additional restrictions on the selection.
     *
     * When in QAbstractItemView::SingleSelection mode, this ensures that the
     * selection never gets transferred to another item. Instead, the selection
     * is cleared if the current item gets removed (filtered) from the model.
     * Furthermore, it prevents users from deselecting the current item with Ctrl+click.
     * This behavior is important for resource selectors.
     * @param enable Determines if strict mode is enabled.
     */
    void setStrictSelectionMode(bool enable);

    void setFixedToolTipThumbnailSize(const QSize &size);
    void setToolTipShouldRenderCheckers(bool value);

    /// (sort key, header label) of the group an index belongs to.
    using GroupFunction = std::function<QPair<QString, QString>(const QModelIndex &)>;
    /**
     * Solstice (docs/agent/brush-preset-grouping.md): shows the icon grid in
     * groups, each under a header line. Groups are ordered by sort key; the
     * model order is kept within a group. An empty function turns grouping
     * off. Only the IconGrid mode groups.
     */
    void setGrouping(const GroupFunction &groupOf);
    void doItemsLayout() override;

    /**
     * Solstice (docs/agent/brush-preset-scroll.md): when false, the view keeps
     * its scroll position when the program sets the current item (see
     * KisResourceItemChooser::setCurrentResource()) and when it is resized.
     * Selecting an item with the mouse or the keyboard still scrolls to it.
     * True (default) is Krita's behavior.
     */
    void setFollowCurrentItem(bool follow);
    bool followCurrentItem() const;

public Q_SLOTS:
    void slotScrollerStateChange(QScroller::State state){ KisKineticScroller::updateCursor(this, state); }

Q_SIGNALS:

    void sigSizeChanged();

    void currentResourceChanged(const QModelIndex &);
    void currentResourceClicked(const QModelIndex &);

    void contextMenuRequested(const QPoint &);

protected Q_SLOTS:
    void rowsAboutToBeRemoved(const QModelIndex &parent, int start, int end) override;
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;

protected:
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex &index, const QEvent *event = nullptr) const override;
    void contextMenuEvent(QContextMenuEvent *event) override;

    bool viewportEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Private;
    const QScopedPointer<Private> m_d;
};

#endif // KISRESOURCEITEMLISTVIEW_H
