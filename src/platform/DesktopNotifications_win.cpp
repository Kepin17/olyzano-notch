// C++/WinRT first: its headers must not see Qt's keyword macros.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

#include "platform/DesktopNotifications.h"

#include "models/FileItem.h"

#include <QCoreApplication>
#include <QPointer>
#include <QSet>

#include <atomic>
#include <chrono>
#include <thread>

#include <windows.h>
#include <shellapi.h>

using namespace winrt::Windows::UI::Notifications;
using winrt::Windows::UI::Notifications::Management::UserNotificationListener;
using winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus;

namespace {
QString qstr(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}
}

// On its own multithreaded-apartment thread, where the async calls may wait. The toasts in the action centre
// are listed every 1.5 s and new ones reported: polling works for any desktop app that is let in at all,
// where the NotificationChanged event needs more.
struct DesktopNotifications::Listener {
    QPointer<DesktopNotifications> owner;
    std::atomic<bool> quit{false};
    std::thread thread;

    template<class F>
    void post(F f)
    {
        const QPointer<DesktopNotifications> o = owner;
        QMetaObject::invokeMethod(qApp, [o, f] {
            if (o)
                f(o.data());
        }, Qt::QueuedConnection);
    }

    void run()
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            UserNotificationListener listener = UserNotificationListener::Current();
            UserNotificationListenerAccessStatus access = listener.GetAccessStatus();
            if (access == UserNotificationListenerAccessStatus::Unspecified)
                access = listener.RequestAccessAsync().get(); // asks the user once
            if (access != UserNotificationListenerAccessStatus::Allowed) {
                qCInfo(lcNotch) << "Desktop notifications: Windows did not allow reading them";
            } else {
                post([](DesktopNotifications *d) { d->setAvailable(true); });
                QSet<uint32_t> seen;
                bool primed = false; // the ones already there when Notch starts are not new
                while (!quit) {
                    QSet<uint32_t> now;
                    for (const UserNotification &n : listener.GetNotificationsAsync(NotificationKinds::Toast).get()) {
                        now.insert(n.Id());
                        if (!primed || seen.contains(n.Id()))
                            continue;
                        QString app, title;
                        try {
                            app = qstr(n.AppInfo().DisplayInfo().DisplayName());
                        } catch (const winrt::hresult_error &) {
                        }
                        if (const auto binding = n.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric())) {
                            const auto texts = binding.GetTextElements();
                            if (texts.Size() > 0)
                                title = qstr(texts.GetAt(0).Text()); // the first line is the title; the rest stays private
                        }
                        post([app, title](DesktopNotifications *d) { d->received(app, title, QString()); });
                    }
                    seen = now;
                    primed = true;
                    for (int i = 0; i < 15 && !quit; ++i)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        } catch (const winrt::hresult_error &e) {
            // Typically: no package identity, which this API may require.
            qCInfo(lcNotch) << "Desktop notifications unavailable:" << qstr(e.message());
        }
        winrt::uninit_apartment();
    }
};

void DesktopNotifications::start()
{
    if (m_listener)
        return;
    m_listener = std::make_unique<Listener>();
    m_listener->owner = this;
    Listener *l = m_listener.get();
    l->thread = std::thread([l] { l->run(); });
}

void DesktopNotifications::stop()
{
    if (!m_listener)
        return;
    m_listener->quit = true;
    if (m_listener->thread.joinable())
        m_listener->thread.join();
    m_listener.reset();
    setAvailable(false);
}

// Focus assist ("Do not disturb") on.
bool DesktopNotifications::doNotDisturb() const
{
    QUERY_USER_NOTIFICATION_STATE state;
    return SUCCEEDED(SHQueryUserNotificationState(&state)) && state == QUNS_QUIET_TIME;
}
