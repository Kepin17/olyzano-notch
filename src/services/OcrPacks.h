#pragma once

#include <QString>
#include <QStringList>

// The language packs tesseract reads with: the ones the system has (apt: tesseract-ocr-ind ...) and the ones downloaded in Settings → Languages,
// brought together in one folder (tesseract wants all the packs it is asked for in one place) and the `-l` string for the languages wanted.
namespace OcrPacks {

struct Setup {
    QString tessdataDir; // "" = let tesseract use its own
    QString languages;   // "ind+eng"
    QStringList missing; // wanted, but there is no pack for them
};

// The folder tesseract keeps its packs in on this system ("" when tesseract is not there); also what it lists as installed.
QString systemDir();
QStringList systemLanguages();

// `packsDir`: where Notch's downloads are (files <code>.traineddata); `systemDir`: the system's packs; `wanted`: tesseract codes in order of
// preference; `workDir`: where the combined folder is made ("tessdata" inside it). English is always added when it exists (numbers and Latin
// words come out better with it). When none of `wanted` exists, `languages` is what there is (English), and they are all in `missing`.
Setup prepare(const QString &packsDir, const QString &systemDir, const QStringList &wanted, const QString &workDir);

// What the program was told about the languages in use: set by Settings → Languages (wanted codes in order) and by the downloads folder.
void configure(const QString &packsDir, const QString &workDir);
void setWanted(const QStringList &tessCodes);
QStringList wanted();
// prepare() with the configuration (and, first in the order, `first` when it is not empty).
Setup current(const QString &first = {});

} // namespace OcrPacks
