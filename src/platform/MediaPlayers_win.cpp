// C++/WinRT first: its headers must not see Qt's keyword macros.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include "MediaPlayers.h"

#include "models/FileItem.h"
#include "platform/MediaNames.h"
#include "platform/PlayerSelection.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

using namespace winrt::Windows::Media::Control;
using Manager = GlobalSystemMediaTransportControlsSessionManager;
using MediaSession = GlobalSystemMediaTransportControlsSession;
using PlaybackStatus = GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::IRandomAccessStreamReference;

namespace {

enum Command { PlayPause, Next, Previous, Seek };

QString qstr(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}

qint64 msOf(winrt::Windows::Foundation::TimeSpan t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
}

}

// Owns everything WinRT, on its own multithreaded-apartment thread: there the async calls may simply wait
// (.get()), which the GUI thread (single-threaded apartment) must never do. Events from the sessions only
// mark the list dirty; the thread then reads every session and hands a plain copy to the GUI thread.
struct MediaPlayers::Worker {
    QPointer<MediaPlayers> owner;
    QString artDir;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> jobs;
    bool listDirty = true; // sessions came or went
    bool dirty = true;     // something in a session changed
    bool quit = false;

    // The worker thread's own:
    Manager manager{nullptr};
    Manager::SessionsChanged_revoker sessionsChanged;
    struct Watch {
        MediaSession::MediaPropertiesChanged_revoker media;
        MediaSession::PlaybackInfoChanged_revoker playback;
        MediaSession::TimelinePropertiesChanged_revoker timeline;
    };
    std::vector<std::pair<QString, MediaSession>> sessions;
    std::vector<Watch> watches;
    QHash<QString, QPair<QString, QString>> art; // key -> (track, artUrl)
    QHash<QString, Session> last;                // key -> the last complete read, for a read that fails or comes back blank

    void markDirty(bool list = false)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            dirty = true;
            listDirty = listDirty || list;
        }
        wake.notify_one();
    }

    void post(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
    }

    MediaSession find(const QString &key) const
    {
        for (const auto &[k, s] : sessions)
            if (k == key)
                return s;
        return nullptr;
    }

    void run()
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            manager = Manager::RequestAsync().get();
            sessionsChanged = manager.SessionsChanged(winrt::auto_revoke, [this](const Manager &, const auto &) { markDirty(true); });
        } catch (const winrt::hresult_error &e) {
            qCWarning(lcNotch) << "Media sessions unavailable:" << qstr(e.message());
        }
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return quit || dirty || !jobs.empty(); });
            if (quit)
                break;
            std::deque<std::function<void()>> todo;
            todo.swap(jobs);
            lock.unlock();
            for (auto &job : todo) {
                try {
                    job();
                } catch (const winrt::hresult_error &) {
                }
            }
            if (!manager)
                continue;
            std::this_thread::sleep_for(std::chrono::milliseconds(60)); // changes come in bursts
            lock.lock();
            const bool list = listDirty, any = dirty;
            listDirty = dirty = false;
            lock.unlock();
            if (!any)
                continue;
            try {
                if (list)
                    rebuild();
                const QList<Session> read = readAll();
                QMetaObject::invokeMethod(qApp, [o = owner, read] {
                    if (o)
                        o->sessionsRead(read);
                }, Qt::QueuedConnection);
            } catch (const winrt::hresult_error &e) {
                qCWarning(lcNotch) << "Reading media sessions failed:" << qstr(e.message());
            }
        }
        watches.clear();
        sessionsChanged.revoke();
        sessions.clear();
        manager = nullptr;
        winrt::uninit_apartment();
    }

    void rebuild()
    {
        watches.clear();
        sessions.clear();
        QHash<QString, int> seen;
        for (const MediaSession &s : manager.GetSessions()) {
            const QString aumid = qstr(s.SourceAppUserModelId());
            const QString key = aumid + u'#' + QString::number(seen[aumid]++);
            Watch w;
            w.media = s.MediaPropertiesChanged(winrt::auto_revoke, [this](const MediaSession &, const auto &) { markDirty(); });
            w.playback = s.PlaybackInfoChanged(winrt::auto_revoke, [this](const MediaSession &, const auto &) { markDirty(); });
            w.timeline = s.TimelinePropertiesChanged(winrt::auto_revoke, [this](const MediaSession &, const auto &) { markDirty(); });
            watches.push_back(std::move(w));
            sessions.emplace_back(key, s);
        }
    }

    QList<Session> readAll()
    {
        QList<Session> out;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (const auto &[key, s] : sessions) {
            Session r;
            r.key = key;
            r.aumid = qstr(s.SourceAppUserModelId());
            r.status = QStringLiteral("Stopped");
            try {
                const auto info = s.GetPlaybackInfo();
                const PlaybackStatus status = info.PlaybackStatus();
                if (status == PlaybackStatus::Playing)
                    r.status = QStringLiteral("Playing");
                else if (status == PlaybackStatus::Paused)
                    r.status = QStringLiteral("Paused");
                else if ((status == PlaybackStatus::Changing || status == PlaybackStatus::Opened) && last.contains(key))
                    r.status = last.value(key).status; // between two tracks: not a stopped player
                const auto controls = info.Controls();
                r.canNext = controls.IsNextEnabled();
                r.canPrevious = controls.IsPreviousEnabled();
                r.canSeek = controls.IsPlaybackPositionEnabled();

                const auto timeline = s.GetTimelineProperties();
                constexpr qint64 Week = 7LL * 24 * 3600 * 1000;
                const qint64 length = msOf(timeline.EndTime() - timeline.StartTime());
                r.lengthMs = length > 0 && length < Week ? length : 0;
                r.positionMs = msOf(timeline.Position() - timeline.StartTime());
                const qint64 updated = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           winrt::clock::to_sys(timeline.LastUpdatedTime()).time_since_epoch())
                                           .count();
                r.positionAt = updated > 946684800000LL && updated <= now ? updated : now; // unset: 1601

                const auto props = s.TryGetMediaPropertiesAsync().get();
                r.title = qstr(props.Title());
                r.artist = qstr(props.Artist());
                r.album = qstr(props.AlbumTitle());
                if (r.artist.isEmpty())
                    r.artist = qstr(props.AlbumArtist());
                const QString track = r.title + u'\x1f' + r.artist;
                const auto cached = art.constFind(key);
                if (cached != art.cend() && cached->first == track) {
                    r.artUrl = cached->second;
                } else {
                    r.artUrl = saveArt(props.Thumbnail());
                    if (!r.artUrl.isEmpty() || r.title.isEmpty())
                        art.insert(key, {track, r.artUrl}); // a cover that has not arrived yet is asked for again
                }
                // Apps blank the metadata for a moment between tracks: keep the old text and cover instead of flashing empty.
                const auto before = last.constFind(key);
                if (before != last.cend() && r.title.isEmpty() && r.artist.isEmpty() && r.status != QLatin1String("Stopped")) {
                    r.title = before->title;
                    r.artist = before->artist;
                    r.album = before->album;
                    r.artUrl = before->artUrl;
                }
                if (before != last.cend() && r.artUrl.isEmpty() && r.title == before->title && r.artist == before->artist)
                    r.artUrl = before->artUrl;
                last.insert(key, r);
            } catch (const winrt::hresult_error &) {
                // a read that fails (the app is busy or going away) shows what was there, not a stopped, blank player
                const auto before = last.constFind(key);
                if (before != last.cend()) {
                    r = *before;
                    r.positionAt = now;
                }
            }
            out << r;
        }
        for (auto it = last.begin(); it != last.end();)
            it = std::any_of(out.cbegin(), out.cend(), [&](const Session &o) { return o.key == it.key(); }) ? std::next(it) : last.erase(it);
        return out;
    }

    // The cover as a file the card's Image can load; named after its content, so a new cover is a new URL.
    QString saveArt(const IRandomAccessStreamReference &thumbnail) const
    {
        if (!thumbnail)
            return {};
        try {
            const auto stream = thumbnail.OpenReadAsync().get();
            const auto size = uint32_t(stream.Size());
            if (size == 0 || size > (8u << 20))
                return {};
            DataReader reader(stream);
            reader.LoadAsync(size).get();
            QByteArray bytes(qsizetype(size), Qt::Uninitialized);
            reader.ReadBytes(winrt::array_view<uint8_t>(reinterpret_cast<uint8_t *>(bytes.data()),
                                                        reinterpret_cast<uint8_t *>(bytes.data()) + size));
            const char *suffix = bytes.startsWith("\x89PNG") ? ".png" : bytes.startsWith("\xFF\xD8") ? ".jpg" : ".img";
            const QString path = artDir + u'/' + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex())
                                 + QLatin1String(suffix);
            if (!QFileInfo::exists(path)) {
                QFile f(path);
                if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
                    return {};
            }
            return QUrl::fromLocalFile(path).toString();
        } catch (const winrt::hresult_error &) {
            return {};
        }
    }
};

MediaPlayers::MediaPlayers(QObject *parent)
    : QObject(parent)
    , m_worker(std::make_unique<Worker>())
{
    m_clock.start();
    m_worker->owner = this;
    m_worker->artDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/media-art");
    QDir(m_worker->artDir).removeRecursively(); // covers from the last run
    QDir().mkpath(m_worker->artDir);
    Worker *w = m_worker.get();
    w->thread = std::thread([w] { w->run(); });
}

MediaPlayers::~MediaPlayers()
{
    {
        std::lock_guard<std::mutex> lock(m_worker->mutex);
        m_worker->quit = true;
    }
    m_worker->wake.notify_one();
    if (m_worker->thread.joinable())
        m_worker->thread.join();
}

void MediaPlayers::sessionsRead(const QList<Session> &sessions)
{
    QHash<QString, Player> players;
    for (const Session &s : sessions) {
        Player p = m_players.value(s.key);
        if (s.status != p.s.status && (s.status == QLatin1String("Playing") || s.status == QLatin1String("Paused")))
            p.lastActive = m_clock.elapsed() + 1;
        if (s.status == QLatin1String("Playing") && p.s.status != s.status && s.key != m_manual)
            m_manual.clear(); // a player that starts playing takes over from a manual pick
        p.s = s;
        players.insert(s.key, p);
    }
    m_players = players;
    select();
    m_ready = true;
}

void MediaPlayers::select()
{
    QList<PlayerState> states;
    for (const Player &p : std::as_const(m_players))
        states << PlayerState{p.s.key, p.s.status, p.lastActive};
    if (!m_manual.isEmpty() && !m_players.contains(m_manual))
        m_manual.clear();
    m_current = pickPlayer(states, m_manual);

    const QString track = title() + u'\x1f' + artist();
    if (m_ready && playing() && !title().isEmpty() && track != m_lastTrack)
        emit trackChanged(title(), artist(), artUrl());
    if (active())
        m_lastTrack = track;
    emit changed();
}

const MediaPlayers::Session *MediaPlayers::current() const
{
    const auto it = m_players.constFind(m_current);
    return it == m_players.cend() ? nullptr : &it->s;
}

bool MediaPlayers::playing() const
{
    return current() && current()->status == QLatin1String("Playing");
}

QString MediaPlayers::identity() const
{
    return current() ? MediaNames::displayName(current()->aumid) : QString();
}

int MediaPlayers::playerCount() const
{
    QList<PlayerState> states;
    for (const Player &p : std::as_const(m_players))
        states << PlayerState{p.s.key, p.s.status, p.lastActive};
    return visiblePlayers(states).size();
}

qint64 MediaPlayers::positionMs() const
{
    const Session *s = current();
    if (!s)
        return 0;
    qint64 ms = s->positionMs;
    if (s->status == QLatin1String("Playing"))
        ms += QDateTime::currentMSecsSinceEpoch() - s->positionAt;
    return s->lengthMs > 0 ? qBound<qint64>(0, ms, s->lengthMs) : qMax<qint64>(0, ms);
}

void MediaPlayers::command(int which, qint64 arg) const
{
    const Session *s = current();
    if (!s)
        return;
    Worker *w = m_worker.get();
    const QString key = s->key;
    w->post([w, key, which, arg] {
        const MediaSession session = w->find(key);
        if (!session)
            return;
        switch (which) {
        case PlayPause:
            session.TryTogglePlayPauseAsync().get();
            break;
        case Next:
            session.TrySkipNextAsync().get();
            break;
        case Previous:
            session.TrySkipPreviousAsync().get();
            break;
        case Seek: {
            const auto start = session.GetTimelineProperties().StartTime();
            session.TryChangePlaybackPositionAsync((start + std::chrono::milliseconds(arg)).count()).get();
            break;
        }
        }
        w->markDirty();
    });
}

void MediaPlayers::playPause() { command(PlayPause); }
void MediaPlayers::next() { command(Next); }
void MediaPlayers::previous() { command(Previous); }

void MediaPlayers::seek(qint64 ms)
{
    if (canSeek())
        command(Seek, ms);
}

namespace {
struct RaiseSearch {
    QString process; // lower case, no .exe
    QString title;
    HWND best = nullptr;
    HWND any = nullptr;
};

BOOL CALLBACK raiseCandidate(HWND hwnd, LPARAM data)
{
    auto *search = reinterpret_cast<RaiseSearch *>(data);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return TRUE;
    wchar_t path[MAX_PATH];
    DWORD len = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &len);
    CloseHandle(process);
    if (!ok || QFileInfo(QString::fromWCharArray(path, int(len))).completeBaseName().toLower() != search->process)
        return TRUE;
    wchar_t text[512];
    const int n = GetWindowTextW(hwnd, text, 512);
    if (n == 0)
        return TRUE;
    if (!search->any)
        search->any = hwnd;
    // A browser names its window after the tab, which names itself after the track.
    if (!search->title.isEmpty() && QString::fromWCharArray(text, n).contains(search->title, Qt::CaseInsensitive)) {
        search->best = hwnd;
        return FALSE;
    }
    return TRUE;
}
}

void MediaPlayers::raise()
{
    const Session *s = current();
    if (!s)
        return;
    RaiseSearch search{MediaNames::processName(s->aumid), s->title};
    EnumWindows(raiseCandidate, reinterpret_cast<LPARAM>(&search));
    HWND hwnd = search.best ? search.best : search.any;
    if (!hwnd)
        return;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

void MediaPlayers::cyclePlayer()
{
    QList<PlayerState> states;
    for (const Player &p : std::as_const(m_players))
        states << PlayerState{p.s.key, p.s.status, p.lastActive};
    const QList<PlayerState> players = visiblePlayers(states);
    if (players.size() < 2)
        return;
    int at = 0;
    for (int i = 0; i < players.size(); ++i)
        if (players[i].service == m_current)
            at = i;
    m_manual = players[(at + 1) % players.size()].service;
    select();
}
