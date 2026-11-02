#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QString>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextTable>

// Markdown -> HTML with the look of a document, for the plan.md reader in Dev → Agent: roomy lines, headings with room above,
// inline code as a tinted chip in an accent colour, links coloured, code blocks on a tint, tables with thin borders.
// QML's plain Markdown mode cannot colour or tint inline code, so the text is parsed by QTextDocument, restyled, and handed
// to a rich-text view. No sockets, no QObject.
namespace MarkdownHtml {

struct Style {
    QString family;       // the app's font, so the document does not bring its own
    int px = 14;
    QColor code = QColor(QStringLiteral("#ff8a80"));   // inline code text
    QColor codeBg = QColor(255, 255, 255, 22);          // inline code chip and code blocks
    QColor link = QColor(QStringLiteral("#4da3ff"));
    QColor muted = QColor(QStringLiteral("#8e8e93"));   // what looked like a link but is not one
    QColor border = QColor(255, 255, 255, 50);          // table borders
};

inline QString render(const QString &markdown, const Style &st)
{
    QTextDocument doc;
    doc.setDocumentMargin(0);
    QFont base;
    if (!st.family.isEmpty()) base.setFamily(st.family);
    base.setPixelSize(st.px);
    doc.setDefaultFont(base);
    // No raw HTML (an <img> or <a> written by the model), and below: no images, links only to the web and mail.
    doc.setMarkdown(markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub | QTextDocument::MarkdownNoHTML));

    struct Edit { int from, to; QTextCharFormat fmt; };
    QList<Edit> edits;
    QList<QPair<int, QTextBlockFormat>> blocks;
    QList<QPair<int, QString>> images;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        const int level = bf.headingLevel();
        const bool inList = b.textList() != nullptr;
        const bool codeBlock = b.length() > 1 && b.charFormat().fontFixedPitch() && b.begin() != b.end();
        bool wholeBlockMono = codeBlock;
        if (wholeBlockMono) // a code block is one fixed-pitch fragment (a line of inline code sits among other fragments)
            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) wholeBlockMono = wholeBlockMono && it.fragment().charFormat().fontFixedPitch();
        bf.setLineHeight(level > 0 ? 122 : wholeBlockMono ? 140 : 155, QTextBlockFormat::ProportionalHeight);
        if (level > 0) {
            const int px = level == 1 ? st.px + 9 : level == 2 ? st.px + 4 : st.px + 1;
            bf.setTopMargin(level == 1 ? 2 : level == 2 ? 20 : 14);
            bf.setBottomMargin(level == 1 ? 12 : 6);
            QTextCharFormat cf;
            QFont f = base; f.setPixelSize(px); f.setWeight(QFont::DemiBold);
            cf.setFont(f);
            edits.append({b.position(), b.position() + b.length() - 1, cf});
        } else if (wholeBlockMono) {
            bf.setBackground(st.codeBg);
            bf.setTopMargin(4); bf.setBottomMargin(10); bf.setLeftMargin(10); bf.setRightMargin(10);
        } else if (inList) {
            bf.setTopMargin(0); bf.setBottomMargin(4);
        } else {
            bf.setTopMargin(0); bf.setBottomMargin(10);
        }
        blocks.append({b.position(), bf});
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            QTextCharFormat extra;
            if (cf.isImageFormat()) { // a picture would be fetched by the view: any address in it is a way out for data. Say that it was there instead.
                const QString alt = cf.toolTip().simplified().left(60);
                images.append({fr.position(), QStringLiteral("[image") + (alt.isEmpty() ? QString() : QStringLiteral(": ") + alt) + QStringLiteral("]")});
                continue;
            }
            if (cf.isAnchor()) {
                const QString scheme = QUrl(cf.anchorHref()).scheme().toLower();
                if (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("mailto")) { extra.setForeground(st.link); extra.setFontUnderline(true); }
                else { extra.setAnchor(false); extra.setAnchorHref(QString()); extra.setForeground(st.muted); extra.setFontUnderline(false); } // file:, smb:, an app's own scheme: text, not a link
            }
            else if (cf.fontFixedPitch() && !wholeBlockMono) {
                extra.setForeground(st.code); extra.setBackground(st.codeBg);
                QFont mono(QStringLiteral("monospace")); mono.setStyleHint(QFont::Monospace); mono.setPixelSize(qMax(10, st.px - 1));
                extra.setFont(mono);
            }
            if (extra.properties().size() > 0) edits.append({fr.position(), fr.position() + fr.length(), extra});
        }
    }
    for (const Edit &e : edits) { QTextCursor c(&doc); c.setPosition(e.from); c.setPosition(e.to, QTextCursor::KeepAnchor); c.mergeCharFormat(e.fmt); }
    for (int i = int(images.size()) - 1; i >= 0; --i) { // last first: positions before it stay valid
        QTextCursor c(&doc); c.setPosition(images.at(i).first); c.setPosition(images.at(i).first + 1, QTextCursor::KeepAnchor);
        c.removeSelectedText(); c.insertText(images.at(i).second, QTextCharFormat());
    }
    for (const auto &b : blocks) { QTextCursor c(&doc); c.setPosition(b.first); c.setBlockFormat(b.second); }
    // tables: thin borders, air inside the cells
    for (QTextFrame *frame : doc.rootFrame()->childFrames())
        if (auto *table = qobject_cast<QTextTable *>(frame)) {
            QTextTableFormat tf = table->format();
            tf.setBorder(1); tf.setBorderBrush(st.border); tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            tf.setCellPadding(7); tf.setCellSpacing(0); tf.setTopMargin(6); tf.setBottomMargin(12);
            table->setFormat(tf);
        }
    QString html = doc.toHtml();
    html.replace(QRegularExpression(QStringLiteral("<body style=\"[^\"]*\">")), QStringLiteral("<body>")); // the view's own font and colour apply
    return html;
}

} // namespace MarkdownHtml
