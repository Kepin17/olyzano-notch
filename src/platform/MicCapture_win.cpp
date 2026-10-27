#include "MicCapture.h"

#include <cmath>
#include <vector>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

namespace {
constexpr int TargetRate = 16000;
constexpr int ChunkFrames = TargetRate * 60 / 1000; // about 60 ms

template<class T>
void releaseCom(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}
}

MicCapture::MicCapture(QObject *parent) : QObject(parent) {}

MicCapture::~MicCapture() { stop(); }

bool MicCapture::running() const { return m_run.load(); }

bool MicCapture::start()
{
    if (m_run.exchange(true))
        return true;
    if (m_thread.joinable())
        m_thread.join();
    m_thread = std::thread([this] { run(); });
    return true;
}

void MicCapture::stop()
{
    m_run = false;
    if (m_thread.joinable())
        m_thread.join();
}

// The default microphone in shared mode, in the format Windows mixes it in; mixed to mono and resampled to 16 kHz
// (linear), cut into chunks that go to the GUI thread.
void MicCapture::run()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioClient *client = nullptr;
    IAudioCaptureClient *capture = nullptr;
    WAVEFORMATEX *format = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                         reinterpret_cast<void **>(&enumerator)))
              && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device))
              && SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&client)))
              && SUCCEEDED(client->GetMixFormat(&format))
              && SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000 /* 200 ms */, 0, format, nullptr))
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
        m_run = false;
        QMetaObject::invokeMethod(this, [this] { emit failed(tr("The microphone could not be opened.")); }, Qt::QueuedConnection);
    } else {
        const int channels = qMax<int>(1, format->nChannels);
        const double step = double(format->nSamplesPerSec) / TargetRate; // input frames per output sample
        double pos = 0;       // where the next output sample falls, in input frames from the start of `mono`
        std::vector<float> mono;
        std::vector<qint16> out;
        out.reserve(ChunkFrames);
        while (m_run) {
            Sleep(15);
            UINT32 packet = 0;
            while (m_run && SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                    break;
                const bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
                for (UINT32 i = 0; i < frames; ++i) {
                    double v = 0;
                    if (!silent)
                        for (int c = 0; c < channels; ++c)
                            v += isFloat ? reinterpret_cast<const float *>(data)[i * channels + c]
                                         : reinterpret_cast<const qint16 *>(data)[i * channels + c] / 32768.0;
                    mono.push_back(float(v / channels));
                }
                capture->ReleaseBuffer(frames);
                while (pos + 1 < double(mono.size())) { // a sample between two input frames
                    const size_t i = size_t(pos);
                    const double f = pos - double(i);
                    const double v = mono[i] * (1 - f) + mono[i + 1] * f;
                    out.push_back(qint16(std::lround(qBound(-1.0, v, 1.0) * 32767)));
                    pos += step;
                    if (int(out.size()) == ChunkFrames) {
                        QByteArray bytes(reinterpret_cast<const char *>(out.data()), int(out.size() * sizeof(qint16)));
                        out.clear();
                        QMetaObject::invokeMethod(this, [this, bytes] { emit chunk(bytes); }, Qt::QueuedConnection);
                    }
                }
                const size_t used = size_t(pos);
                mono.erase(mono.begin(), mono.begin() + qMin(used, mono.size()));
                pos -= double(used);
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
