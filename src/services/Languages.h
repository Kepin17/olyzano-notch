#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

// The languages Notch can work in, in one list that Audio (what is said, the language of the notes), the PDF notes and OCR share. Pure and
// tested. For each: its code (the one Whisper uses), its names, the code of its tesseract language pack, the way it is written, and the words
// that give it away in a text. What is used is chosen in Settings → Languages; what may need a download there is the OCR pack.
namespace Lang {

struct Language {
    QString code;        // "id", "en", "zh", "zh-tw" (Whisper takes "zh" for both Chinese)
    QString english;     // "Indonesian"
    QString native;      // "Bahasa Indonesia"
    QString tess;        // the tesseract language pack: "ind"
    QString script;      // latin | cyrillic | arabic | han | kana | hangul | devanagari | thai | greek | hebrew | bengali | tamil
    bool rtl = false;
    QString stopWords;   // the commonest words, space separated (for the Latin and Cyrillic scripts, and Arabic script)
    QString whisper() const { return code == QLatin1String("zh-tw") ? QStringLiteral("zh") : code; }
};

inline const QList<Language> &all()
{
    static const QList<Language> list = {
        {QStringLiteral("id"), QStringLiteral("Indonesian"), QStringLiteral("Bahasa Indonesia"), QStringLiteral("ind"), QStringLiteral("latin"), false,
         QStringLiteral("yang dan di ke dari untuk dengan pada adalah ini itu dalam tidak akan juga sebagai oleh atau karena dapat")},
        {QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("English"), QStringLiteral("eng"), QStringLiteral("latin"), false,
         QStringLiteral("the and of to in is that for with as on are by this be or from which it was not have")},
        {QStringLiteral("ms"), QStringLiteral("Malay"), QStringLiteral("Bahasa Melayu"), QStringLiteral("msa"), QStringLiteral("latin"), false,
         QStringLiteral("yang dan di ke dari untuk dengan pada ialah ini itu dalam tidak akan juga sebagai oleh atau kerana boleh telah")},
        {QStringLiteral("ar"), QStringLiteral("Arabic"), QStringLiteral("العربية"), QStringLiteral("ara"), QStringLiteral("arabic"), true,
         QStringLiteral("في من على إلى هذا هذه التي الذي عن مع كان أن لا ما هو هي")},
        {QStringLiteral("zh"), QStringLiteral("Chinese (Simplified)"), QStringLiteral("简体中文"), QStringLiteral("chi_sim"), QStringLiteral("han"), false, {}},
        {QStringLiteral("zh-tw"), QStringLiteral("Chinese (Traditional)"), QStringLiteral("繁體中文"), QStringLiteral("chi_tra"), QStringLiteral("han"), false, {}},
        {QStringLiteral("ja"), QStringLiteral("Japanese"), QStringLiteral("日本語"), QStringLiteral("jpn"), QStringLiteral("kana"), false, {}},
        {QStringLiteral("ko"), QStringLiteral("Korean"), QStringLiteral("한국어"), QStringLiteral("kor"), QStringLiteral("hangul"), false, {}},
        {QStringLiteral("hi"), QStringLiteral("Hindi"), QStringLiteral("हिन्दी"), QStringLiteral("hin"), QStringLiteral("devanagari"), false, {}},
        {QStringLiteral("th"), QStringLiteral("Thai"), QStringLiteral("ไทย"), QStringLiteral("tha"), QStringLiteral("thai"), false, {}},
        {QStringLiteral("vi"), QStringLiteral("Vietnamese"), QStringLiteral("Tiếng Việt"), QStringLiteral("vie"), QStringLiteral("latin"), false,
         QStringLiteral("và của là có không được cho trong một những này các đã với để")},
        {QStringLiteral("tl"), QStringLiteral("Filipino"), QStringLiteral("Filipino"), QStringLiteral("fil"), QStringLiteral("latin"), false,
         QStringLiteral("ang ng sa na at ay mga ito para kanyang hindi ni siya may")},
        {QStringLiteral("es"), QStringLiteral("Spanish"), QStringLiteral("Español"), QStringLiteral("spa"), QStringLiteral("latin"), false,
         QStringLiteral("el la de que y en los las del se por con para una es no su al lo como más pero")},
        {QStringLiteral("fr"), QStringLiteral("French"), QStringLiteral("Français"), QStringLiteral("fra"), QStringLiteral("latin"), false,
         QStringLiteral("le la les de des du et en un une est que pour dans qui pas sur au avec ce il elle")},
        {QStringLiteral("de"), QStringLiteral("German"), QStringLiteral("Deutsch"), QStringLiteral("deu"), QStringLiteral("latin"), false,
         QStringLiteral("der die das und ist nicht ein eine zu den von mit sich auf für im dem auch es wird")},
        {QStringLiteral("pt"), QStringLiteral("Portuguese"), QStringLiteral("Português"), QStringLiteral("por"), QStringLiteral("latin"), false,
         QStringLiteral("o a os as de do da em um uma que e para com não é por se mais como foi")},
        {QStringLiteral("it"), QStringLiteral("Italian"), QStringLiteral("Italiano"), QStringLiteral("ita"), QStringLiteral("latin"), false,
         QStringLiteral("il lo la i gli le di che e un una è per con non in del della si come più")},
        {QStringLiteral("nl"), QStringLiteral("Dutch"), QStringLiteral("Nederlands"), QStringLiteral("nld"), QStringLiteral("latin"), false,
         QStringLiteral("de het een en van in is dat op te zijn voor met niet aan er maar om ook als")},
        {QStringLiteral("ru"), QStringLiteral("Russian"), QStringLiteral("Русский"), QStringLiteral("rus"), QStringLiteral("cyrillic"), false,
         QStringLiteral("и в не на что я с он как это по но они к у из за от то все так его")},
        {QStringLiteral("uk"), QStringLiteral("Ukrainian"), QStringLiteral("Українська"), QStringLiteral("ukr"), QStringLiteral("cyrillic"), false,
         QStringLiteral("і в не на що я з він як це по але вони до у із за від то все так його")},
        {QStringLiteral("pl"), QStringLiteral("Polish"), QStringLiteral("Polski"), QStringLiteral("pol"), QStringLiteral("latin"), false,
         QStringLiteral("i w nie na z do się to że jest jak po co za od ale przez dla")},
        {QStringLiteral("tr"), QStringLiteral("Turkish"), QStringLiteral("Türkçe"), QStringLiteral("tur"), QStringLiteral("latin"), false,
         QStringLiteral("ve bir bu da de için ile ne çok olarak daha gibi ama en var olan")},
        {QStringLiteral("cs"), QStringLiteral("Czech"), QStringLiteral("Čeština"), QStringLiteral("ces"), QStringLiteral("latin"), false,
         QStringLiteral("a je se na v že to s z do jako ale pro nebo který byl tak")},
        {QStringLiteral("sv"), QStringLiteral("Swedish"), QStringLiteral("Svenska"), QStringLiteral("swe"), QStringLiteral("latin"), false,
         QStringLiteral("och att det är en som på av för med den har inte ett till om")},
        {QStringLiteral("el"), QStringLiteral("Greek"), QStringLiteral("Ελληνικά"), QStringLiteral("ell"), QStringLiteral("greek"), false, {}},
        {QStringLiteral("he"), QStringLiteral("Hebrew"), QStringLiteral("עברית"), QStringLiteral("heb"), QStringLiteral("hebrew"), true, {}},
        {QStringLiteral("fa"), QStringLiteral("Persian"), QStringLiteral("فارسی"), QStringLiteral("fas"), QStringLiteral("arabic"), true,
         QStringLiteral("که و در از این را با به است برای آن یک هم کرد شد")},
        {QStringLiteral("bn"), QStringLiteral("Bengali"), QStringLiteral("বাংলা"), QStringLiteral("ben"), QStringLiteral("bengali"), false, {}},
        {QStringLiteral("ta"), QStringLiteral("Tamil"), QStringLiteral("தமிழ்"), QStringLiteral("tam"), QStringLiteral("tamil"), false, {}},
        {QStringLiteral("ur"), QStringLiteral("Urdu"), QStringLiteral("اردو"), QStringLiteral("urd"), QStringLiteral("arabic"), true,
         QStringLiteral("کے میں ہے کی کا کو سے اور پر یہ نے ایک بھی")},
    };
    return list;
}

inline const Language *find(const QString &code)
{
    for (const Language &l : all())
        if (l.code == code)
            return &l;
    return nullptr;
}
inline bool known(const QString &code) { return find(code) != nullptr; }
inline QString englishName(const QString &code) { const Language *l = find(code); return l ? l->english : QString(); }
inline QString tessCode(const QString &code) { const Language *l = find(code); return l ? l->tess : QString(); }
inline QString whisperCode(const QString &code) { const Language *l = find(code); return l ? l->whisper() : QString(); }
inline bool isRtl(const QString &code) { const Language *l = find(code); return l && l->rtl; }
inline QStringList defaults() { return {QStringLiteral("id"), QStringLiteral("en")}; }

namespace detail {
inline bool inRange(char32_t c, char32_t a, char32_t b) { return c >= a && c <= b; }
inline const QSet<QString> &words(const Language &l)
{
    static QHash<QString, QSet<QString>> cache;
    auto it = cache.find(l.code);
    if (it == cache.end()) {
        QSet<QString> set;
        for (const QString &w : l.stopWords.split(u' ', Qt::SkipEmptyParts))
            set.insert(w);
        it = cache.insert(l.code, set);
    }
    return it.value();
}
// the best of `candidates` (codes) for the words of the text, "" when it cannot tell. Indonesian and Malay share most of their commonest words: when
// those two are the best and close, the one that comes first in `candidates` (the one the person has in use first) is taken.
inline QString byWords(const QStringList &tokens, const QStringList &candidates, int minHits = 6)
{
    QList<QPair<int, QString>> scored;
    for (const QString &code : candidates) {
        const Language *l = find(code);
        if (!l || l->stopWords.isEmpty())
            continue;
        int hits = 0;
        for (const QString &w : tokens)
            if (words(*l).contains(w))
                ++hits;
        scored.append({hits, code});
    }
    if (scored.isEmpty())
        return {};
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    const int best = scored[0].first, second = scored.size() > 1 ? scored[1].first : 0;
    if (best < minHits)
        return {};
    if (best >= second * 1.2)
        return scored[0].second;
    const QSet<QString> pair = {scored[0].second, scored[1].second};
    if (pair == QSet<QString>{QStringLiteral("id"), QStringLiteral("ms")}) // siblings: the first in use
        return candidates.indexOf(scored[0].second) < candidates.indexOf(scored[1].second) ? scored[0].second : scored[1].second;
    return {};
}
}

// What language a text is in, among the languages in `enabled` (codes, in the order of preference: a tie goes to the first). "" when it cannot
// tell. The script says most of it (Hangul is Korean, Kana is Japanese, ...); the commonest words tell the languages that share a script.
inline QString detect(const QString &text, const QStringList &enabled)
{
    int han = 0, kana = 0, hangul = 0, arabic = 0, cyrillic = 0, devanagari = 0, thai = 0, greek = 0, hebrew = 0, bengali = 0, tamil = 0, latin = 0, total = 0;
    int persianLetters = 0, urduLetters = 0, ukrainianLetters = 0, russianLetters = 0, vietnamese = 0;
    for (const char32_t c : text.toStdU32String()) {
        if (!QChar::isLetter(c))
            continue;
        ++total;
        if (detail::inRange(c, 0x4E00, 0x9FFF) || detail::inRange(c, 0x3400, 0x4DBF)) ++han;
        else if (detail::inRange(c, 0x3040, 0x30FF)) ++kana;
        else if (detail::inRange(c, 0xAC00, 0xD7AF) || detail::inRange(c, 0x1100, 0x11FF)) ++hangul;
        else if (detail::inRange(c, 0x0600, 0x06FF) || detail::inRange(c, 0x0750, 0x077F)) {
            ++arabic;
            if (c == 0x067E || c == 0x0686 || c == 0x0698 || c == 0x06AF) ++persianLetters; // پ چ ژ گ
            if (c == 0x0679 || c == 0x0688 || c == 0x0691 || c == 0x06BA || c == 0x06D2 || c == 0x06BE) ++urduLetters; // ٹ ڈ ڑ ں ے ھ
        } else if (detail::inRange(c, 0x0400, 0x04FF)) {
            ++cyrillic;
            if (c == 0x0456 || c == 0x0457 || c == 0x0454 || c == 0x0491) ++ukrainianLetters; // і ї є ґ
            if (c == 0x044B || c == 0x044D || c == 0x044A) ++russianLetters;                  // ы э ъ
        } else if (detail::inRange(c, 0x0900, 0x097F)) ++devanagari;
        else if (detail::inRange(c, 0x0E00, 0x0E7F)) ++thai;
        else if (detail::inRange(c, 0x0370, 0x03FF)) ++greek;
        else if (detail::inRange(c, 0x0590, 0x05FF)) ++hebrew;
        else if (detail::inRange(c, 0x0980, 0x09FF)) ++bengali;
        else if (detail::inRange(c, 0x0B80, 0x0BFF)) ++tamil;
        else {
            ++latin;
            if (detail::inRange(c, 0x1EA0, 0x1EF9) || c == 0x0111 || c == 0x01A1 || c == 0x01B0) ++vietnamese; // ạ ế ộ ... đ ơ ư
        }
    }
    if (total < 12)
        return {};
    const auto allowed = [&](const QStringList &codes) { QStringList out; for (const QString &c : enabled) if (codes.contains(c)) out << c; return out; };
    const auto first = [&](const QStringList &codes) { const QStringList a = allowed(codes); return a.isEmpty() ? QString() : a.first(); };
    struct Script { int count; QString name; };
    const QList<Script> scripts = {{han, QStringLiteral("han")}, {kana, QStringLiteral("kana")}, {hangul, QStringLiteral("hangul")}, {arabic, QStringLiteral("arabic")}, {cyrillic, QStringLiteral("cyrillic")},
                                   {devanagari, QStringLiteral("devanagari")}, {thai, QStringLiteral("thai")}, {greek, QStringLiteral("greek")}, {hebrew, QStringLiteral("hebrew")},
                                   {bengali, QStringLiteral("bengali")}, {tamil, QStringLiteral("tamil")}, {latin, QStringLiteral("latin")}};
    QString dominant;
    int most = 0;
    for (const Script &s : scripts)
        if (s.count > most) {
            most = s.count;
            dominant = s.name;
        }
    if (most < total * 0.3 && !(kana > 0 && han + kana >= total * 0.3))
        return {};
    if (kana > 0 && han + kana >= total * 0.3) // Japanese text has kana among the Han characters
        return first({QStringLiteral("ja")});
    if (dominant == QLatin1String("han")) return first({QStringLiteral("zh"), QStringLiteral("zh-tw")});
    if (dominant == QLatin1String("kana")) return first({QStringLiteral("ja")});
    if (dominant == QLatin1String("hangul")) return first({QStringLiteral("ko")});
    if (dominant == QLatin1String("devanagari")) return first({QStringLiteral("hi")});
    if (dominant == QLatin1String("thai")) return first({QStringLiteral("th")});
    if (dominant == QLatin1String("greek")) return first({QStringLiteral("el")});
    if (dominant == QLatin1String("hebrew")) return first({QStringLiteral("he")});
    if (dominant == QLatin1String("bengali")) return first({QStringLiteral("bn")});
    if (dominant == QLatin1String("tamil")) return first({QStringLiteral("ta")});
    const QStringList words = text.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}]+")), Qt::SkipEmptyParts);
    if (dominant == QLatin1String("arabic")) {
        const QStringList cands = allowed({QStringLiteral("ar"), QStringLiteral("fa"), QStringLiteral("ur")});
        if (persianLetters > 2 && cands.contains(QLatin1String("fa"))) return QStringLiteral("fa");
        if (urduLetters > 2 && cands.contains(QLatin1String("ur"))) return QStringLiteral("ur");
        const QString byWords = detail::byWords(words, cands, 3);
        return byWords.isEmpty() ? (cands.isEmpty() ? QString() : cands.first()) : byWords;
    }
    if (dominant == QLatin1String("cyrillic")) {
        const QStringList cands = allowed({QStringLiteral("ru"), QStringLiteral("uk")});
        if (ukrainianLetters > 1 && cands.contains(QLatin1String("uk"))) return QStringLiteral("uk");
        if (russianLetters > 1 && cands.contains(QLatin1String("ru"))) return QStringLiteral("ru");
        const QString byWords = detail::byWords(words, cands, 3);
        return byWords.isEmpty() ? (cands.isEmpty() ? QString() : cands.first()) : byWords;
    }
    // Latin letters: Vietnamese has its own marks; the others are told by their commonest words, among the languages in use
    const QStringList cands = allowed({QStringLiteral("id"), QStringLiteral("en"), QStringLiteral("ms"), QStringLiteral("vi"), QStringLiteral("tl"), QStringLiteral("es"), QStringLiteral("fr"),
                                       QStringLiteral("de"), QStringLiteral("pt"), QStringLiteral("it"), QStringLiteral("nl"), QStringLiteral("pl"), QStringLiteral("tr"), QStringLiteral("cs"), QStringLiteral("sv")});
    if (vietnamese > total * 0.04 && cands.contains(QLatin1String("vi")))
        return QStringLiteral("vi");
    return detail::byWords(words, cands, 6);
}

// ---- the words of the notes in each language ---------------------------------------------------------------------

struct Words {
    QString overview, objectives, outline, points, terms, formulas, questions, cards, answer;
    QString page;      // "hal. %1", for one page
    QString pages;     // "hal. %1–%2", for several
};

// The headings and the "page" words of the study notes, for the languages that have them; others get English (the text of the notes is in their
// language all the same).
inline const Words &words(const QString &code)
{
    static const QHash<QString, Words> table = {
        {QStringLiteral("id"), {QStringLiteral("Gambaran umum"), QStringLiteral("Tujuan belajar"), QStringLiteral("Kerangka materi"), QStringLiteral("Poin penting"), QStringLiteral("Istilah dan definisi"),
                                QStringLiteral("Rumus, aturan, dan contoh"), QStringLiteral("Soal latihan"), QStringLiteral("Kartu belajar"), QStringLiteral("Jawaban"), QStringLiteral("hal. %1"), QStringLiteral("hal. %1–%2")}},
        {QStringLiteral("en"), {QStringLiteral("Overview"), QStringLiteral("Learning objectives"), QStringLiteral("Outline"), QStringLiteral("Key points"), QStringLiteral("Terms and definitions"),
                                QStringLiteral("Formulas, rules and examples"), QStringLiteral("Practice questions"), QStringLiteral("Flash cards"), QStringLiteral("Answer"), QStringLiteral("p. %1"), QStringLiteral("pp. %1–%2")}},
        {QStringLiteral("es"), {QStringLiteral("Resumen general"), QStringLiteral("Objetivos de aprendizaje"), QStringLiteral("Esquema"), QStringLiteral("Puntos clave"), QStringLiteral("Términos y definiciones"),
                                QStringLiteral("Fórmulas, reglas y ejemplos"), QStringLiteral("Preguntas de práctica"), QStringLiteral("Tarjetas de estudio"), QStringLiteral("Respuesta"), QStringLiteral("pág. %1"), QStringLiteral("págs. %1–%2")}},
        {QStringLiteral("fr"), {QStringLiteral("Vue d'ensemble"), QStringLiteral("Objectifs d'apprentissage"), QStringLiteral("Plan"), QStringLiteral("Points clés"), QStringLiteral("Termes et définitions"),
                                QStringLiteral("Formules, règles et exemples"), QStringLiteral("Questions d'entraînement"), QStringLiteral("Fiches de révision"), QStringLiteral("Réponse"), QStringLiteral("p. %1"), QStringLiteral("p. %1–%2")}},
        {QStringLiteral("de"), {QStringLiteral("Überblick"), QStringLiteral("Lernziele"), QStringLiteral("Gliederung"), QStringLiteral("Kernpunkte"), QStringLiteral("Begriffe und Definitionen"),
                                QStringLiteral("Formeln, Regeln und Beispiele"), QStringLiteral("Übungsfragen"), QStringLiteral("Lernkarten"), QStringLiteral("Antwort"), QStringLiteral("S. %1"), QStringLiteral("S. %1–%2")}},
        {QStringLiteral("pt"), {QStringLiteral("Visão geral"), QStringLiteral("Objetivos de aprendizagem"), QStringLiteral("Estrutura"), QStringLiteral("Pontos principais"), QStringLiteral("Termos e definições"),
                                QStringLiteral("Fórmulas, regras e exemplos"), QStringLiteral("Questões de prática"), QStringLiteral("Cartões de estudo"), QStringLiteral("Resposta"), QStringLiteral("p. %1"), QStringLiteral("p. %1–%2")}},
        {QStringLiteral("it"), {QStringLiteral("Panoramica"), QStringLiteral("Obiettivi di apprendimento"), QStringLiteral("Struttura"), QStringLiteral("Punti chiave"), QStringLiteral("Termini e definizioni"),
                                QStringLiteral("Formule, regole ed esempi"), QStringLiteral("Domande di esercizio"), QStringLiteral("Schede di studio"), QStringLiteral("Risposta"), QStringLiteral("p. %1"), QStringLiteral("pp. %1–%2")}},
        {QStringLiteral("nl"), {QStringLiteral("Overzicht"), QStringLiteral("Leerdoelen"), QStringLiteral("Opbouw"), QStringLiteral("Belangrijkste punten"), QStringLiteral("Begrippen en definities"),
                                QStringLiteral("Formules, regels en voorbeelden"), QStringLiteral("Oefenvragen"), QStringLiteral("Leerkaartjes"), QStringLiteral("Antwoord"), QStringLiteral("p. %1"), QStringLiteral("p. %1–%2")}},
        {QStringLiteral("ru"), {QStringLiteral("Обзор"), QStringLiteral("Цели обучения"), QStringLiteral("Структура"), QStringLiteral("Ключевые моменты"), QStringLiteral("Термины и определения"),
                                QStringLiteral("Формулы, правила и примеры"), QStringLiteral("Практические вопросы"), QStringLiteral("Карточки"), QStringLiteral("Ответ"), QStringLiteral("стр. %1"), QStringLiteral("стр. %1–%2")}},
        {QStringLiteral("tr"), {QStringLiteral("Genel bakış"), QStringLiteral("Öğrenme hedefleri"), QStringLiteral("Ana hatlar"), QStringLiteral("Önemli noktalar"), QStringLiteral("Terimler ve tanımlar"),
                                QStringLiteral("Formüller, kurallar ve örnekler"), QStringLiteral("Alıştırma soruları"), QStringLiteral("Çalışma kartları"), QStringLiteral("Cevap"), QStringLiteral("s. %1"), QStringLiteral("s. %1–%2")}},
        {QStringLiteral("vi"), {QStringLiteral("Tổng quan"), QStringLiteral("Mục tiêu học tập"), QStringLiteral("Dàn ý"), QStringLiteral("Điểm chính"), QStringLiteral("Thuật ngữ và định nghĩa"),
                                QStringLiteral("Công thức, quy tắc và ví dụ"), QStringLiteral("Câu hỏi luyện tập"), QStringLiteral("Thẻ ghi nhớ"), QStringLiteral("Đáp án"), QStringLiteral("tr. %1"), QStringLiteral("tr. %1–%2")}},
        {QStringLiteral("zh"), {QStringLiteral("概述"), QStringLiteral("学习目标"), QStringLiteral("提纲"), QStringLiteral("要点"), QStringLiteral("术语与定义"), QStringLiteral("公式、规则与示例"), QStringLiteral("练习题"),
                                QStringLiteral("学习卡片"), QStringLiteral("答案"), QStringLiteral("第%1页"), QStringLiteral("第%1–%2页")}},
        {QStringLiteral("zh-tw"), {QStringLiteral("概述"), QStringLiteral("學習目標"), QStringLiteral("大綱"), QStringLiteral("重點"), QStringLiteral("術語與定義"), QStringLiteral("公式、規則與範例"), QStringLiteral("練習題"),
                                   QStringLiteral("學習卡片"), QStringLiteral("答案"), QStringLiteral("第%1頁"), QStringLiteral("第%1–%2頁")}},
        {QStringLiteral("ar"), {QStringLiteral("نظرة عامة"), QStringLiteral("أهداف التعلم"), QStringLiteral("المخطط"), QStringLiteral("النقاط الرئيسية"), QStringLiteral("المصطلحات والتعريفات"),
                                QStringLiteral("الصيغ والقواعد والأمثلة"), QStringLiteral("أسئلة تدريبية"), QStringLiteral("بطاقات تعليمية"), QStringLiteral("الإجابة"), QStringLiteral("ص %1"), QStringLiteral("ص %1–%2")}},
        {QStringLiteral("ja"), {QStringLiteral("概要"), QStringLiteral("学習目標"), QStringLiteral("アウトライン"), QStringLiteral("重要ポイント"), QStringLiteral("用語と定義"), QStringLiteral("公式・ルール・例"),
                                QStringLiteral("練習問題"), QStringLiteral("フラッシュカード"), QStringLiteral("答え"), QStringLiteral("%1ページ"), QStringLiteral("%1–%2ページ")}},
        {QStringLiteral("ko"), {QStringLiteral("개요"), QStringLiteral("학습 목표"), QStringLiteral("목차"), QStringLiteral("핵심 요점"), QStringLiteral("용어와 정의"), QStringLiteral("공식, 규칙, 예시"), QStringLiteral("연습 문제"),
                                QStringLiteral("플래시카드"), QStringLiteral("정답"), QStringLiteral("%1쪽"), QStringLiteral("%1–%2쪽")}},
    };
    // (a const QHash's operator[] gives a copy: a reference to it would dangle)
    const auto it = table.constFind(code);
    return it != table.constEnd() ? it.value() : table.constFind(QStringLiteral("en")).value();
}
inline bool hasWords(const QString &code)
{
    static const QSet<QString> have = {QStringLiteral("id"), QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("fr"), QStringLiteral("de"), QStringLiteral("pt"), QStringLiteral("it"), QStringLiteral("nl"),
                                       QStringLiteral("ru"), QStringLiteral("tr"), QStringLiteral("vi"), QStringLiteral("ar"), QStringLiteral("zh"), QStringLiteral("zh-tw"), QStringLiteral("ja"), QStringLiteral("ko")};
    return have.contains(code);
}

} // namespace Lang
