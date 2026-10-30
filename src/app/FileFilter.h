#pragma once

#include "models/FileItemModel.h"

#include <QCollator>
#include <QHash>
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
    void setContents(const QHash<QString, QString> *contents) { m_contents = contents; }
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
    const QHash<QString, QString> *m_contents = nullptr; // id -> text of text files, for search
    bool m_vaultUnlocked = false;
};
