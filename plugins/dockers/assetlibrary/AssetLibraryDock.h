/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ASSETLIBRARYDOCK_H
#define ASSETLIBRARYDOCK_H

#include <KoDockFactoryBase.h>
#include <kis_mainwindow_observer.h>

#include <QCache>
#include <QDockWidget>
#include <QJsonObject>
#include <QPixmap>
#include <QPointer>
#include <QStringList>
#include <QVector>

class KisViewManager;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QScrollArea;
class QSplitter;
class QTimer;
class QVBoxLayout;
class QWidget;

struct AssetPathEntry {
    QString alias;
    QString path;
    bool includeSubfolders{false};
    QString extensions{QStringLiteral("kra,jpg,jpeg,png,svg")};
};

struct AssetLibrarySettings {
    QVector<AssetPathEntry> paths;
    int windowWidth{720};
    int windowHeight{520};
    QList<int> splitterSizes{150, 570};
    bool rightPanelHidden{false};
    int expandedWindowWidth{720};
    int collapsedWindowWidth{170};
    bool autoColumns{true};
    int columns{3};
    int thumbnailSize{140};
    int uiFontSize{10};
    int headerFontSize{10};
    int assetNameFontSize{10};

    static QString filePath();
    void load();
    bool save() const;
};

class AssetLibraryDock : public QDockWidget, public KisMainwindowObserver
{
    Q_OBJECT
public:
    explicit AssetLibraryDock(QWidget *parent = nullptr);
    ~AssetLibraryDock() override;

    QString observerName() override;
    void setViewManager(KisViewManager *viewManager) override;
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    struct Section {
        QString title;
        QStringList files;
    };

    void buildUi();
    void applySettings();
    void loadFolders();
    void folderChanged(QListWidgetItem *current);
    void refreshAssets();
    QVector<Section> collectSections() const;
    QStringList extensions() const;
    void clearAssets(const QString &message = {});
    void populateSections();
    int thumbnailColumns() const;
    QPixmap thumbnailForPath(const QString &path);
    void openAsset(const QString &path);
    void insertAsLayer(const QString &path);
    void insertAsFileLayer(const QString &path);
    void duplicateAsset(const QString &path);
    void renameAsset(const QString &path);
    void deleteAsset(const QString &path);
    QString requestAssetPath(const QString &path, const QString &title, bool allowCurrent = false);
    void showSettings();
    void setAssetPanelHidden(bool hidden, bool save);
    void saveRuntimeSettings();

    AssetLibrarySettings m_settings;
    QPointer<KisViewManager> m_viewManager;
    QString m_currentPath;
    bool m_includeSubfolders{false};
    QString m_extensionText{QStringLiteral("kra,jpg,jpeg,png,svg")};
    QVector<Section> m_sections;
    bool m_restoringLayout{false};
    QWidget *m_root{nullptr};
    QSplitter *m_splitter{nullptr};
    QWidget *m_folderPanel{nullptr};
    QListWidget *m_folderList{nullptr};
    QPushButton *m_hideButton{nullptr};
    QWidget *m_assetPanel{nullptr};
    QLabel *m_statusLabel{nullptr};
    QScrollArea *m_scroll{nullptr};
    QWidget *m_assetHost{nullptr};
    QVBoxLayout *m_assetLayout{nullptr};
    QTimer *m_saveTimer{nullptr};
    QTimer *m_layoutTimer{nullptr};
    QCache<QString, QPixmap> m_thumbnailCache;
    int m_currentColumns{0};
};

class AssetLibraryDockFactory : public KoDockFactoryBase
{
public:
    QString id() const override;
    QDockWidget *createDockWidget() override;
    DockPosition defaultDockPosition() const override;
};

#endif
