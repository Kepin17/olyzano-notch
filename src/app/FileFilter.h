#pragma once

#include "models/FileItemModel.h"

#include <QCollator>
#include <QSet>
#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QString>

// The category filter value that shows Recently removed instead of the shelf.
inline constexpr char RemovedView[] = "__removed__";

// Shelf view: name contains `text` (case-insensitive) and, if set, item is in `category`
// (or Recently removed for RemovedView); removed items never show elsewhere;
// ordered by `sort`: date (newest first), name (natural A-Z), size (largest first), type.
class FileFilter : public QSortFilterProxyModel
{
public:
    void setText(const QString &text);
    void setCategory(const QString &categoryId);
    void setSortMode(const QString &mode);
    // Items whose text matches the search (from the FTS index): shown besides the ones whose name matches.
    void setContentIds(const QSet<QString> &ids);
    void setVaultUnlocked(bool unlocked);

protected:
    // Changes something filterAcceptsRow reads and re-filters. Qt 6.9 added begin/endFilterChange for this;
    // older Qt (the Windows build uses 6.8) re-filters with invalidateFilter() after the change.
    template <typename Apply>
    void changeFilter(Apply apply)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        beginFilterChange();
        apply();
        endFilterChange(Direction::Rows);
#else
        apply();
        invalidateFilter();
#endif
    }

    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_text;
    QString m_category;
    QString m_sort = QStringLiteral("date");
    QSet<QString> m_contentIds;
    bool m_vaultUnlocked = false;
};

// The first `limit` rows of another model (the shelf's grid builds a page of tiles at a time, and asks for the next page when the
// pointer reaches the end). The rows stay in the source's order; searching or changing the category starts over at one page.
class PageLimit : public QSortFilterProxyModel
{
public:
    static constexpr int PageSize = 16;

    void setPagedSource(QAbstractItemModel *source)
    {
        setSourceModel(source);
        // Rows arriving or leaving shift the ones after them across the limit: look again once the burst is over.
        connect(source, &QAbstractItemModel::rowsInserted, this, [this] { refreshSoon(); });
        connect(source, &QAbstractItemModel::rowsRemoved, this, [this] { refreshSoon(); });
        connect(source, &QAbstractItemModel::rowsMoved, this, [this] { refreshSoon(); });
        connect(source, &QAbstractItemModel::layoutChanged, this, [this] { refreshSoon(); });
        connect(source, &QAbstractItemModel::modelReset, this, [this] { refreshSoon(); });
    }

    bool hasMore() const { return sourceModel() && sourceModel()->rowCount() > m_limit; }

    // The next page; false when there is nothing more to show.
    bool loadMore()
    {
        if (!hasMore())
            return false;
        changeLimit(m_limit + PageSize);
        return true;
    }

    void reset()
    {
        if (m_limit != PageSize)
            changeLimit(PageSize);
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &) const override { return row < m_limit; }

private:
    void changeLimit(int limit)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        beginFilterChange();
        m_limit = limit;
        endFilterChange(Direction::Rows);
#else
        m_limit = limit;
        invalidateFilter();
#endif
    }

    void refreshSoon()
    {
        if (m_pending)
            return;
        m_pending = true;
        QMetaObject::invokeMethod(this, [this] {
            m_pending = false;
            changeLimit(m_limit);
        }, Qt::QueuedConnection);
    }

    int m_limit = PageSize;
    bool m_pending = false;
};
