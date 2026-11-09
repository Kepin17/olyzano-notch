#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QString>
#include <QTextDocument>

namespace DocSummary {

// The notes' HTML as an A4 PDF (text layout: on the GUI thread).
inline QByteArray renderPdf(const QString &html, const QString &title)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QPdfWriter writer(&buffer);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(18, 16, 18, 18), QPageLayout::Millimeter);
    writer.setResolution(120);
    writer.setTitle(title);
    writer.setCreator(QStringLiteral("Olyzano Notch"));
    QTextDocument doc;
    doc.setHtml(html);
    doc.setPageSize(QSizeF(writer.width(), writer.height()));
    doc.print(&writer);
    buffer.close();
    return bytes;
}

} // namespace DocSummary
