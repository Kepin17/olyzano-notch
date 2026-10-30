#include "FileFilter.h"

void FileFilter::setText(const QString &text)
{
    changeFilter([&] { m_text = text; });
}

void FileFilter::setCategory(const QString &categoryId)
{
    changeFilter([&] { m_category = categoryId; });
}

void FileFilter::setVaultUnlocked(bool unlocked)
{
    changeFilter([&] { m_vaultUnlocked = unlocked; });
}

void FileFilter::setSortMode(const QString &mode)
{
    m_sort = mode;
    invalidate();
    sort(0);
}

bool FileFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    static const QCollator natural = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    const auto name = [](const QModelIndex &i) { return i.data(FileItemModel::NameRole).toString(); };
    if (m_sort == QLatin1String("name"))
        return natural.compare(name(left), name(right)) < 0;
    if (m_sort == QLatin1String("size"))
        return left.data(FileItemModel::SizeRole).toLongLong() > right.data(FileItemModel::SizeRole).toLongLong();
    if (m_sort == QLatin1String("type")) {
        const int byType = QString::compare(left.data(FileItemModel::MimeTypeRole).toString(),
                                            right.data(FileItemModel::MimeTypeRole).toString());
        return byType != 0 ? byType < 0 : natural.compare(name(left), name(right)) < 0;
    }
    return left.data(FileItemModel::AddedAtRole).toDateTime() > right.data(FileItemModel::AddedAtRole).toDateTime();
}

bool FileFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    const bool match = i.data(FileItemModel::NameRole).toString().contains(m_text, Qt::CaseInsensitive)
                       || (m_contents && !m_text.isEmpty()
                           && m_contents->value(i.data(FileItemModel::IdRole).toString()).contains(m_text, Qt::CaseInsensitive));
    if (!match)
        return false;
    // Vault files: only inside their own (locked) category, and only while the vault is open.
    if (i.data(FileItemModel::VaultRole).toBool())
        return m_vaultUnlocked && m_category == i.data(FileItemModel::CategoryIdRole).toString();
    const bool removed = i.data(FileItemModel::RemovedRole).toBool();
    if (m_category == QLatin1String(RemovedView))
        return removed;
    return !removed && (m_category.isEmpty() || i.data(FileItemModel::CategoryIdRole).toString() == m_category);
}
