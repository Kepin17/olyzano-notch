#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QThread>
#include <QTimer>

#include <functional>
#include <memory>

// Image providers that ask the icon theme (QIcon::fromTheme, KIconLoader) must not run on Qt Quick's image-reading thread: the
// theme is not safe to use from two threads, and a race with the GUI thread crashed Notch. These providers make every image on
// the GUI thread, whichever thread asked, and hand it over through an asynchronous response.
class GuiImageResponse : public QQuickImageResponse
{
public:
    explicit GuiImageResponse(const std::function<QImage()> &make) : m_state(std::make_shared<State>())
    {
        m_state->owner = this;
        const auto state = m_state;
        auto run = [state, make] { // on the GUI thread, after the caller has connected to finished()
            const QImage image = make();
            QMutexLocker lock(&state->lock);
            state->image = image;
            if (state->owner)
                emit state->owner->finished();
        };
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
            QTimer::singleShot(0, QCoreApplication::instance(), run);
        else
            QMetaObject::invokeMethod(QCoreApplication::instance(), run, Qt::QueuedConnection);
    }
    ~GuiImageResponse() override
    {
        QMutexLocker lock(&m_state->lock);
        m_state->owner = nullptr; // a late result is dropped
    }
    QQuickTextureFactory *textureFactory() const override
    {
        QMutexLocker lock(&m_state->lock);
        return QQuickTextureFactory::textureFactoryForImage(m_state->image);
    }

private:
    struct State {
        QRecursiveMutex lock;
        QImage image;
        GuiImageResponse *owner = nullptr;
    };
    std::shared_ptr<State> m_state;
};

class GuiImageProvider : public QQuickAsyncImageProvider
{
public:
    using Maker = std::function<QImage(const QString &id, const QSize &size)>;
    GuiImageProvider(Maker make, const QSize &fallback) : m_make(std::move(make)), m_fallback(fallback) {}
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requested) override
    {
        const QSize size = requested.isValid() ? requested : m_fallback;
        const Maker make = m_make;
        return new GuiImageResponse([make, id, size] { return make(id, size); });
    }

private:
    Maker m_make;
    QSize m_fallback;
};
