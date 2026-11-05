#pragma once

#include "app/FileFilter.h"
#include "models/FileItemModel.h"
#include "repositories/FileRepository.h"
#include "services/FolderWatcher.h"
#include "services/Vault.h"

#include <QObject>
#include <functional>
#include <QThreadPool>
#include <atomic>
#include <memory>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;

// Bridge between QML and the services. Owns the repository (main thread), dispatches copies
// to the global thread pool and downloads to QNetworkAccessManager.
// Settings live in ~/.config/notch/notch.conf.
class NotchController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *items READ items CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int copying READ copying NOTIFY copyingChanged)
    // A big copy or zip: "Copying 12 of 340", "38 GB of 100 GB · 14 min left", how far it is (0-1, -1 when unknown), and a moment before
    // that, while the files are being listed. All "" / -1 when nothing is being copied.
    Q_PROPERTY(QString copyLabel READ copyLabel NOTIFY copyProgressChanged)
    Q_PROPERTY(QString copyDetail READ copyDetail NOTIFY copyProgressChanged)
    Q_PROPERTY(double copyFraction READ copyFraction NOTIFY copyProgressChanged)
    Q_PROPERTY(QString batchLabel READ batchLabel NOTIFY batchLabelChanged) // "Removing 340 of 1000" while a big remove/restore/delete runs, else ""
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged) // [{id, name, count}]
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY categoryFilterChanged)
    Q_PROPERTY(int retentionDays READ retentionDays WRITE setRetentionDays NOTIFY retentionDaysChanged)
    Q_PROPERTY(QString storageDir READ storageDir NOTIFY storageDirChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
    Q_PROPERTY(bool alerts READ alerts WRITE setAlerts NOTIFY alertsChanged) // charger/Bluetooth toasts
    Q_PROPERTY(int undoCount READ undoCount NOTIFY undoCountChanged) // files the Undo toast can restore
    Q_PROPERTY(int removedCount READ removedCount NOTIFY countChanged) // in Recently removed
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged) // date|name|size|type
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged) // dark|light|system
    Q_PROPERTY(QString uiStyle READ uiStyle WRITE setUiStyle NOTIFY uiStyleChanged)         // classic|washi
    Q_PROPERTY(QStringList recentColors READ recentColors NOTIFY recentColorsChanged) // picked, newest first
    // Automatic category per file type (FileService::typeGroup): {group: categoryId}.
    Q_PROPERTY(QVariantMap typeRules READ typeRules NOTIFY typeRulesChanged)
    // Encrypted vault for locked categories (gocryptfs). Unlocked = mounted in $XDG_RUNTIME_DIR.
    Q_PROPERTY(bool vaultSupported READ vaultSupported CONSTANT)   // locked categories (Vault: gocryptfs on Linux)
    Q_PROPERTY(bool linuxDesktop READ linuxDesktop CONSTANT)       // Spectacle, KDE Connect, … live here
    Q_PROPERTY(QString platform READ platform CONSTANT)            // linux | windows | other
    Q_PROPERTY(bool vaultExists READ vaultExists NOTIFY vaultChanged)
    Q_PROPERTY(bool vaultUnlocked READ vaultUnlocked NOTIFY vaultChanged)
    Q_PROPERTY(bool autoScreenshots READ autoScreenshots WRITE setAutoScreenshots NOTIFY autoScreenshotsChanged)
    Q_PROPERTY(bool autoDownloads READ autoDownloads WRITE setAutoDownloads NOTIFY autoDownloadsChanged)
    Q_PROPERTY(bool autoKdeConnect READ autoKdeConnect WRITE setAutoKdeConnect NOTIFY autoKdeConnectChanged)
    // Spectacle only writes a file (which Notch picks up) when its auto-save is on; off by default.
    Q_PROPERTY(bool spectacleAutoSave READ spectacleAutoSave NOTIFY spectacleAutoSaveChanged) // removals that can still be undone

public:
    explicit NotchController(const QString &dataDir, QObject *parent = nullptr);

    bool start();

    QAbstractItemModel *items() { return &m_proxy; }
    int count() const { return m_model.shelfCount(); }
    int removedCount() const { return m_model.removedCount(); }
    int copying() const { return m_copying; }
    QString errorMessage() const { return m_error; }
    QString filter() const { return m_filter; }
    QString batchLabel() const { return m_batchLabel; }
    QString copyLabel() const { return m_copyLabel; }
    QString copyDetail() const { return m_copyDetail; }
    double copyFraction() const { return m_copyFraction; }
    void setFilter(const QString &text);
    QVariantList categories() const { return m_categories; }
    QString categoryFilter() const { return m_categoryFilter; }
    void setCategoryFilter(const QString &categoryId);
    int retentionDays() const { return m_retentionDays; }
    void setRetentionDays(int days);
    QString storageDir() const { return m_filesDir; }
    bool autostart() const;
    void setAutostart(bool enabled);
    int undoCount() const { return m_lastRemoved.size(); }
    bool alerts() const { return m_settings.value(QStringLiteral("alerts"), true).toBool(); }
    void setAlerts(bool enabled);
    QString sortMode() const { return m_settings.value(QStringLiteral("sort"), QStringLiteral("date")).toString(); }
    void setSortMode(const QString &mode);
    QString themeMode() const { return m_settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString(); }
    void setThemeMode(const QString &mode);
    QString uiStyle() const { return m_settings.value(QStringLiteral("style"), QStringLiteral("classic")).toString(); }
    void setUiStyle(const QString &style);
    QStringList recentColors() const { return m_settings.value(QStringLiteral("colors")).toStringList(); }
    QVariantMap typeRules() const;
    bool vaultSupported() const { return Vault::supported(); }
    QString platform() const
    {
#if defined(Q_OS_WIN)
        return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
        return QStringLiteral("linux");
#else
        return QStringLiteral("other");
#endif
    }
    bool linuxDesktop() const
    {
#ifdef Q_OS_LINUX
        return true;
#else
        return false;
#endif
    }
    bool vaultExists() const { return m_vault.exists(); }
    bool vaultUnlocked() const { return m_vault.isUnlocked(); }
    bool autoScreenshots() const { return m_settings.value(QStringLiteral("autoScreenshots"), true).toBool(); }
    void setAutoScreenshots(bool enabled);
    bool autoDownloads() const { return m_settings.value(QStringLiteral("autoDownloads"), false).toBool(); }
    void setAutoDownloads(bool enabled);
    bool autoKdeConnect() const { return m_settings.value(QStringLiteral("autoKdeConnect"), true).toBool(); }
    void setAutoKdeConnect(bool enabled);
    bool spectacleAutoSave() const;

    // Local files, local folders (one category per parent folder), http(s)/data URLs
    // (downloaded; web pages become link shortcuts). Files land in categoryId when given.
    Q_INVOKABLE void addUrls(const QList<QUrl> &urls, const QString &categoryId = {});
    // A drop as QML sees it. Browsers add the page title as a bogus relative "URL" and, for an
    // image inside a link, list only the link: the image itself is in the HTML.
    Q_INVOKABLE void addDrop(const QList<QUrl> &urls, const QString &html, const QString &categoryId = {});
    // Stops the copies and zips in progress (and the listing of a folder); what was half done is removed.
    Q_INVOKABLE void cancelCopies();
    Q_INVOKABLE void cancelCopy(const QString &id);
    // The files of a category as one .zip on the shelf, made in the background.
    Q_INVOKABLE void zipCategory(const QString &categoryId);
    Q_INVOKABLE void open(const QString &id);
    // Remove = move to Recently removed (deleted for good after TrashHours). Undo restores the
    // last batch; restoreItems/deleteForever act on Recently removed.
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void removeItems(const QStringList &ids);
    Q_INVOKABLE void undoRemove();
    Q_INVOKABLE void restoreItems(const QStringList &ids);
    Q_INVOKABLE void deleteForever(const QStringList &ids);
    Q_INVOKABLE void emptyRecentlyRemoved();
    Q_INVOKABLE void pasteClipboard(const QString &categoryId = {});
    void keepBytes(const QString &name, const QString &mimeType, const QByteArray &bytes, const QString &source); // into the category being viewed
    QString keepBytesId(const QString &name, const QString &mimeType, const QByteArray &bytes, const QString &source); // the same, and the new item's id ("" when it failed)
    // A finished file already on disk becomes a shelf item right away (moved, Available); returns its id ("" when it failed).
    QString adoptLocalFile(const QString &path, const QString &name, const QString &source, const QString &categoryId = {});
    // Text dropped on Notch (no file): saved as a note named after its first line.
    Q_INVOKABLE void addTextNote(const QString &text, const QString &categoryId = {});
    Q_INVOKABLE void copyFiles(const QStringList &ids);
    Q_INVOKABLE void copyContents(const QString &id); // text files: their text
    Q_INVOKABLE void copyPaths(const QStringList &ids);
    Q_INVOKABLE void setPinned(const QStringList &ids, bool pinned); // kept items never expire
    Q_INVOKABLE void renameItem(const QString &id, const QString &name);
    Q_INVOKABLE void saveTo(const QStringList &ids, const QUrl &folder); // copies out, never overwrites
    Q_INVOKABLE void zipItems(const QStringList &ids);                   // new .zip item in Notch
    Q_INVOKABLE QStringList categoryItemIds(const QString &categoryId) const;
    Q_INVOKABLE QStringList fileUrls(const QStringList &ids) const; // file:// of the stored copies
    Q_INVOKABLE void copyText(const QString &text);
    // Windows: RivaTuner (RTSS / MSI Afterburner) is hooked into this app and may draw its overlay on it. False elsewhere.
    Q_INVOKABLE bool rtssHooked() const;
    Q_INVOKABLE QString exePath() const; // this program, for the guide that tells RTSS to ignore it
    Q_INVOKABLE void setTypeRule(const QString &group, const QString &categoryId); // empty = no rule
    // Vault. Passwords go straight to gocryptfs and are not kept. Return "" or a reason.
    Q_INVOKABLE QString createVault(const QString &password);
    Q_INVOKABLE QString unlockVault(const QString &password);
    Q_INVOKABLE void lockVault();
    // Locks (moves the category's files into the vault) or unlocks it for good. Vault must be open.
    Q_INVOKABLE void setCategoryLocked(const QString &categoryId, bool locked);
    Q_INVOKABLE int applyRulesToExisting(); // uncategorized files only; returns how many moved
    Q_INVOKABLE void removeColor(const QString &hex);
    // User-confirmed (Settings button): sets Spectacle's [General] autoSaveImage=true via kwriteconfig6.
    Q_INVOKABLE void enableSpectacleAutoSave();
    // Notes: full text of a text item (up to 1 MiB; empty + error if larger or unreadable), and
    // create (id empty) or overwrite/rename one. Returns false on failure.
    Q_INVOKABLE QString readText(const QString &id);
    Q_INVOKABLE bool saveNote(const QString &id, const QString &name, const QString &text);
    // Document/image tools (DocTools) on stored files; results become new items in the current
    // category, originals stay. kind: merge | images-pdf | compress | split | extract (option: page
    // ranges) | ocr-copy | ocr-note | convert (option: png/jpg/webp) | resize (option: max width).
    Q_INVOKABLE void runTool(const QString &kind, const QStringList &ids, const QString &option = {});
    Q_INVOKABLE QStringList mimeTypes(const QStringList &ids) const;
    // What the selected files can be converted to: [{id, label, group, available, hint}], empty for a mixed selection.
    Q_INVOKABLE QVariantList convertTargets(const QStringList &ids) const;
    Q_INVOKABLE int pageCount(const QString &id) const;
    // Note editor toolkit (see TextTools.h): {ok, text, error}.
    Q_INVOKABLE QVariantMap transformText(const QString &kind, const QString &text) const;

public slots:
    // A file another device sent through KDE Connect (url of the saved file).
    void addReceived(const QString &url, const QString &deviceName);
    // A color from the picker: copied to the clipboard and kept in recentColors.
    void addColor(const QString &hex);

public:
    Q_INVOKABLE void setDragging(const QStringList &ids, bool dragging);
    Q_INVOKABLE QString uriList(const QStringList &ids) const; // text/uri-list for drag-out
    Q_INVOKABLE QStringList visibleIds() const;                // ids in current view order
    Q_INVOKABLE void clearError();

    Q_INVOKABLE QString createCategory(const QString &name); // returns id (existing one if name taken)
    Q_INVOKABLE void renameCategory(const QString &id, const QString &name);
    Q_INVOKABLE void deleteCategory(const QString &id);       // its files stay, uncategorized
    Q_INVOKABLE void setCategory(const QStringList &ids, const QString &categoryId); // empty = none

    // Only allowed while the shelf is empty, so no stored file is ever orphaned.
    Q_INVOKABLE bool setStorageDir(const QUrl &folder);
    // First 64 KiB of a text item; empty if unavailable or binary.
    Q_INVOKABLE QString previewText(const QString &id) const;
    // Quick Look picture: the image itself, a PDF's first page (rendered once, cached), else "".
    Q_INVOKABLE QString quickLookImage(const QString &id);
    // {name, sizeText, addedText, iconName, isText} of one item, for overlays like Quick Look.
    Q_INVOKABLE QVariantMap itemInfo(const QString &id) const;

    // For sharing (ShareController): what to send for the given items. Locked items are skipped
    // while the vault is closed; `vault` files live on the decrypted mount.
    struct ShareSource {
        QString path, name, mimeType;
        bool vault = false;
    };
    QList<ShareSource> shareSources(const QStringList &ids) const;
    // Files that another Notch computer just sent (Nearby share): moved from `folder` into the "Nearby" category,
    // with a toast "From <device>". Returns how many were added.
    int addReceivedFiles(const QString &folder, const QStringList &names, const QString &fromDevice);
    QString settingsFolder() const; // where notch.conf lives
    QString scratchPath(bool forVault) const { return scratchDir(forVault); }

signals:
    void countChanged();
    void copyingChanged();
    void batchLabelChanged();
    void copyProgressChanged();
    void errorMessageChanged();
    void filterChanged();
    void categoriesChanged();
    void categoryFilterChanged();
    void retentionDaysChanged();
    void storageDirChanged();
    void autostartChanged();
    void undoCountChanged();
    void alertsChanged();
    void sortModeChanged();
    void themeModeChanged();
    void uiStyleChanged();
    void recentColorsChanged();
    void typeRulesChanged();
    void vaultChanged();
    void autoScreenshotsChanged();
    void autoDownloadsChanged();
    void autoKdeConnectChanged();
    void spectacleAutoSaveChanged();
    // Something finished that deserves a toast (file auto-added, files saved, zip ready).
    void notice(const QString &title, const QString &detail, const QString &icon, const QString &art);
    void sound(const QString &name); // "drop" (dropped or pasted onto the shelf), "receive" (a phone or computer sent files)

private:
    void addLocalFile(const QString &path, const QString &categoryId, QStringList &errors);
    void startCopy(const FileItem &item);
    void startDownload(const QUrl &url, const QString &categoryId);
    void addLink(const QUrl &url, const QString &categoryId);
    bool addBytes(const QString &name, const QString &mimeType, const QByteArray &bytes, const QString &source,
                  const QString &categoryId, QString *idOut = nullptr);
    // Moves a finished file (e.g. a tool's output) into managed storage as a new item.
    bool adoptFile(const QString &path, const QString &name, const QString &source, const QString &categoryId, QString *idOut = nullptr);
    void updateWatchers();
    void onWatchedFile(const QString &path, const QString &settingKey, const QString &categoryName, const QString &title);
    QString autoCategory(const QString &settingKey, const QString &name); // remembered by id, recreated if deleted
    QString screenshotDir() const;
    bool claimAutoAdd(const QString &path); // false if another source just added this path
    FileItem newItem(const QString &name, const QString &categoryId) const;
    QString freeName(const QString &name, const QString &exceptId = {}) const;
    const FileItem *available(const QString &id) const; // the item if it is ready to use, else nullptr
    QString baseDir(const FileItem &item) const; // files dir or vault mount
    QString pathOf(const FileItem &item) const;
    QString scratchDir(bool forVault) const;
    bool isLockedCategory(const QString &categoryId) const;
    bool assignStorage(FileItem &item, QString *why); // vault or files dir, by category
    bool relocate(const FileItem &item, bool toVault);
    void vaultStateChanged();
    void reloadCategories();  // after a category is created/renamed/deleted
    void refreshCategories(); // recount; emits categoriesChanged only if something changed
    bool storeSetting(const QString &key, const QVariant &value); // true if it changed
    // A new file's category: the explicit one if any (open category, folder, auto source),
    // else the rule for its type, else none.
    QString ruledCategory(const QString &categoryId, const QString &mimeType);
    // Big removes, restores and deletes run in small steps (kBatch items, a short pause between, one transaction each), so the panel
    // keeps drawing and answering; a short list is done at once.
    bool runBatches(const QString &verb, const QStringList &ids, std::function<void(const QStringList &)> step, std::function<void()> finish);
    void nextBatch();
    void trashBatch(const QStringList &chunk);
    void restoreBatch(const QStringList &chunk);
    void deleteBatch(const QStringList &chunk);
    // Search inside text files: the first 64 KB of each goes to the FTS index a few files at a time, in the background.
    void scheduleIndexing();
    void indexStep();
    bool indexable(const FileItem &item);
    void runSearch();
    void runCleanup();
    void expireEmptyCategories();
    void setError(const QString &message);

    // A drop is listed and looked at on a worker (a folder can hold thousands of files, a mime lookup reads the file), then put on
    // the shelf a few at a time, each copied by a worker with its own progress.
public: // (the worker that lists a drop is a free function)
    struct Prep {
        QString path, name, mime, folder; // folder: the category a file of a dropped folder goes in
        bool fromFolder = false;
        qint64 size = 0;
    };
    struct Prepared {
        QList<Prep> items;
        QStringList errors;
    };
    struct Ingest {
        QList<Prep> items;
        QString categoryId;
        qsizetype next = 0;
    };

private:
    void ingestStep();
    void ingestOne(const Prep &p, const QString &categoryId, QStringList &errors);
public:
    // One running copy, zip or cloud transfer: how far it is, and the switch that stops it. `verb` ("Uploading", "Downloading") makes it a
    // job named in the pill instead of one of "Copying 12 of 340"; the cloud shelf uses it.
    struct Transfer {
        std::shared_ptr<std::atomic<qint64>> done;
        std::shared_ptr<std::atomic_bool> cancel;
        qint64 total = 0;
        bool zip = false;
        QString title, verb;
    };
    void beginTransfer(const QString &id, const Transfer &t);
    void endTransfer(const QString &id);
    // For the cloud shelf: a stored file, where it is on disk.
    const FileItem *itemFor(const QString &id) const { return m_model.find(id); }
    QString storedPath(const QString &id) const;

private:
    void progressTick();
    void startZip(const QStringList &ids, const QString &name, const QString &categoryId);

    QSettings m_settings;
    QString m_runtimeDir; // tmpfs: vault mount + decrypted scratch
    Vault m_vault;
    QString m_dataDir;
    QString m_filesDir;
    int m_retentionDays;
    FileRepository m_repo;
    FileItemModel m_model;
    FileFilter m_proxy;
    QString m_filter;
    QString m_categoryFilter;
    QNetworkAccessManager *m_net = nullptr;
    QSet<QString> m_dragging;
    QHash<QString, QDateTime> m_fresh; // categories just made: left alone for a moment so the files meant for them can arrive
    QTimer m_clock;   // refreshes "expires in" labels
    QTimer m_cleanup;
    QStringList m_lastRemoved; // what the Undo toast restores
    QList<Category> m_categoryList; // from the database, see reloadCategories()
    QVariantList m_categories;      // what QML sees: [{id, name, count, special}]
    QSet<QString> m_indexed; // ids whose text is in the index
    QHash<QString, bool> m_textMime;
    QTimer m_indexTimer, m_searchTimer, m_batchTimer, m_countsTimer;
    struct Job {
        QString verb;
        QStringList ids;
        qsizetype done = 0;
        std::function<void(const QStringList &)> step;
        std::function<void()> finish;
    } m_job;
    QString m_batchLabel;
    QThreadPool m_ingestPool, m_copyPool;
    std::atomic_bool m_ingestCancel{false};
    int m_preparing = 0;
    QList<Ingest> m_ingest;
    QTimer m_ingestTimer, m_progressTimer;
    QHash<QString, Transfer> m_transfers;
    qint64 m_xferBase = 0, m_xferTotal = 0, m_lastDone = 0, m_lastTickMs = 0;
    double m_rate = 0;
    int m_xferFiles = 0, m_xferFinished = 0;
    QString m_copyLabel, m_copyDetail;
    double m_copyFraction = -1;
    QStringList m_trashed; // what the running remove has moved so far (for one Undo at the end)
    FolderWatcher m_screenshots;
    FolderWatcher m_downloads;
    QHash<QString, qint64> m_autoAdded; // path -> ms since epoch, de-dupes Downloads vs KDE Connect
    QTimer m_trashTimer;
    int m_copying = 0;
    bool m_ready = false;
    QString m_error;
};
