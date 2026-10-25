#include "AudioLevels.h"

#include "models/FileItem.h"
#include "platform/AudioBands.h"

#include <cmath>
#include <vector>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

namespace {
constexpr int TargetRate = 11025;
constexpr int Window = 512;

template<class T>
void releaseCom(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}
}

// What the default output plays, captured in loopback (no microphone permission: it is the speakers' own
// signal), mixed to mono, thinned to about 11 kHz and turned into band levels, which alone go to the GUI thread.
void AudioLevels::runLoopback()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioClient *client = nullptr;
    IAudioCaptureClient *capture = nullptr;
    WAVEFORMATEX *format = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                         reinterpret_cast<void **>(&enumerator)))
              && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))
              && SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&client)))
              && SUCCEEDED(client->GetMixFormat(&format))
              && SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000 /* 200 ms */, 0,
                                              format, nullptr))
              && SUCCEEDED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&capture)))
              && SUCCEEDED(client->Start());

    bool isFloat = false;
    if (ok) {
        const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
        isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
                  || (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        const bool isPcm16 = format->wBitsPerSample == 16
                             && (format->wFormatTag == WAVE_FORMAT_PCM
                                 || (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM));
        ok = (isFloat && format->wBitsPerSample == 32) || isPcm16;
    }
    if (!ok) {
        qCWarning(lcNotch) << "No loopback capture of the speakers: equalizer uses an animation instead";
        QMetaObject::invokeMethod(this, [this] { setAvailable(false); }, Qt::QueuedConnection);
    } else {
        const int channels = qMax<int>(1, format->nChannels);
        const int factor = qMax(1, int(std::lround(double(format->nSamplesPerSec) / TargetRate)));
        const int rate = int(format->nSamplesPerSec) / factor;
        std::vector<float> window;
        window.reserve(Window);
        double sum = 0;
        int summed = 0;
        int idle = 0;
        while (m_capturing) {
            Sleep(15);
            UINT32 packet = 0;
            bool got = false;
            while (m_capturing && SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                    break;
                got = true;
                const bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
                for (UINT32 i = 0; i < frames; ++i) {
                    double v = 0;
                    if (!silent)
                        for (int c = 0; c < channels; ++c)
                            v += isFloat ? reinterpret_cast<const float *>(data)[i * channels + c]
                                         : reinterpret_cast<const qint16 *>(data)[i * channels + c] / 32768.0;
                    sum += v / channels;
                    if (++summed < factor)
                        continue;
                    window.push_back(float(sum / factor)); // averaging is the low-pass before thinning
                    sum = 0;
                    summed = 0;
                    if (int(window.size()) == Window) {
                        const QList<double> fresh = bandLevels(window.data(), Window, rate);
                        QMetaObject::invokeMethod(this, [this, fresh] { apply(fresh); }, Qt::QueuedConnection);
                        window.clear();
                    }
                }
                capture->ReleaseBuffer(frames);
            }
            // Nothing playing: Windows sends no packets at all, so let the bars fall on their own.
            if (!got && ++idle >= 3) {
                idle = 0;
                QMetaObject::invokeMethod(this, [this] { apply(QList<double>(BandCount, 0.0)); }, Qt::QueuedConnection);
            } else if (got) {
                idle = 0;
            }
        }
        client->Stop();
    }
    releaseCom(capture);
    releaseCom(client);
    releaseCom(device);
    releaseCom(enumerator);
    if (format)
        CoTaskMemFree(format);
    CoUninitialize();
}
