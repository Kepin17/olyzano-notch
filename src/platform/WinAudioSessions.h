#pragma once

#include <QByteArray>
#include <QHash>

struct IAudioSessionManager2;

// What Discord's audio sessions look like right now. A session is "active" while Discord has the stream
// running; the peak (0..1) is how loud it is at this moment.
struct DiscordAudio {
    bool captureActive = false; // its microphone stream
    bool renderActive = false;  // what it plays to you (other people's voices)
    double capturePeak = 0;
    double renderPeak = 0;
};

// Windows audio sessions of the default microphone and speakers, looking for Discord. Polled by Activities;
// used on one thread only.
class WinAudioSessions
{
public:
    WinAudioSessions() = default;
    WinAudioSessions(const WinAudioSessions &) = delete;
    WinAudioSessions &operator=(const WinAudioSessions &) = delete;
    ~WinAudioSessions();

    DiscordAudio read();
    // One line per Discord session (flow, state, peak), for `notch --dump-discord`.
    QByteArray dump();

private:
    struct Flow {
        IAudioSessionManager2 *manager = nullptr;
        int idle = 0;
    };
    bool open(Flow &flow, bool capture);
    void close(Flow &flow);
    // Calls `each(state, peak)` for every Discord session of the flow; false if the flow could not be read.
    template<class F>
    bool eachDiscord(Flow &flow, bool capture, F each);
    bool isDiscord(quint32 pid);

    Flow m_capture, m_render;
    QHash<quint32, bool> m_isDiscord; // process id -> is it a Discord build (cleared now and then, so ids can't pile up)
};
