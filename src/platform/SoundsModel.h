#pragma once

#include <QString>

// Which sound belongs to which settings group, and whether one may play right now. Pure, for the tests.
namespace SoundsModel {

enum Group { Shelf, Timer, Phone, System, Unknown };

inline Group groupOf(const QString &name)
{
    static const struct { const char *name; Group group; } table[] = {
        {"drop", Shelf},       {"trash", Shelf},     {"pick", Shelf},
        {"timer", Timer},      {"stretch", Timer},   {"break-over", Timer},
        {"ring", Phone},       {"notify", Phone},    {"receive", Phone},  {"send", Phone},
        {"plug-in", System},   {"plug-out", System}, {"device", System},  {"warning", System},
    };
    for (const auto &e : table)
        if (name == QLatin1String(e.name))
            return e.group;
    return Unknown;
}

inline const char *groupKey(Group g)
{
    switch (g) {
    case Shelf: return "shelf";
    case Timer: return "timer";
    case Phone: return "phone";
    case System: return "system";
    case Unknown: break;
    }
    return "";
}

struct State {
    bool enabled = true;
    bool groupOn = true;
    bool quietWhenBusy = true;
    bool micInUse = false;   // a call or a meeting
    bool fullScreen = false; // a video, a game, a presentation
    qint64 sinceSameMs = -1; // since this sound last played, -1 never
};

// A call still rings over a full-screen video; nothing plays over a call or a meeting. The same sound twice
// within 400 ms is one event seen twice (one notch per screen), so it plays once.
inline bool shouldPlay(const QString &name, const State &s)
{
    if (!s.enabled || !s.groupOn || groupOf(name) == Unknown)
        return false;
    if (s.sinceSameMs >= 0 && s.sinceSameMs < 400)
        return false;
    if (s.quietWhenBusy && (s.micInUse || (s.fullScreen && name != QLatin1String("ring"))))
        return false;
    return true;
}

}
