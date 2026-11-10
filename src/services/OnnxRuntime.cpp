#include "OnnxRuntime.h"

#if __has_include("onnxruntime/onnxruntime_c_api.h")
#include "onnxruntime/onnxruntime_c_api.h"

#include <QLibrary>
#include <QThread>

#include <memory>

namespace OnnxRuntime {

namespace {

// The library, the session and what they need freed, in the order they must go.
struct State {
    QLibrary lib;
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    OrtSessionOptions *options = nullptr;
    OrtSession *session = nullptr;
    OrtMemoryInfo *memory = nullptr;
    char *inName = nullptr, *outName = nullptr;
    OrtAllocator *allocator = nullptr;

    ~State()
    {
        if (!api)
            return;
        if (allocator && inName) api->AllocatorFree(allocator, inName);
        if (allocator && outName) api->AllocatorFree(allocator, outName);
        if (memory) api->ReleaseMemoryInfo(memory);
        if (session) api->ReleaseSession(session);
        if (options) api->ReleaseSessionOptions(options);
        if (env) api->ReleaseEnv(env);
    }
    // "" when `status` is null (success), else its message; the status is freed.
    QString take(OrtStatus *status) const
    {
        if (!status)
            return {};
        const QString message = QString::fromUtf8(api->GetErrorMessage(status));
        api->ReleaseStatus(status);
        return message.isEmpty() ? QStringLiteral("unknown error") : message;
    }
};

using GetBase = const OrtApiBase *(ORT_API_CALL *)();

} // namespace

bool supported() { return true; }

Matting::Runner makeRunner(const QString &library, const QString &model, QString *error)
{
    auto fail = [error](const QString &why) { if (error) *error = why; return Matting::Runner(); };
    auto s = std::make_shared<State>();
    s->lib.setFileName(library);
    if (!s->lib.load())
        return fail(QStringLiteral("The runtime library could not be loaded: %1").arg(s->lib.errorString()));
    const auto getBase = reinterpret_cast<GetBase>(s->lib.resolve("OrtGetApiBase"));
    if (!getBase)
        return fail(QStringLiteral("This is not an ONNX Runtime library."));
    // ONNX Runtime 1.N speaks API N (asking for a newer one makes it print a complaint): use the newest both know. The functions used here sit
    // at the start of the table, which never changes order.
    const int minor = QString::fromUtf8(getBase()->GetVersionString()).section('.', 1, 1).toInt();
    if (minor >= 17)
        s->api = getBase()->GetApi(uint32_t(std::min(minor, int(ORT_API_VERSION))));
    if (!s->api)
        return fail(QStringLiteral("This ONNX Runtime library is too old."));
    const OrtApi *a = s->api;
    QString e;
    if (!(e = s->take(a->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "notch", &s->env))).isEmpty()
        || !(e = s->take(a->CreateSessionOptions(&s->options))).isEmpty()
        || !(e = s->take(a->SetIntraOpNumThreads(s->options, std::clamp(QThread::idealThreadCount(), 1, 4)))).isEmpty()
        || !(e = s->take(a->SetSessionGraphOptimizationLevel(s->options, ORT_ENABLE_ALL))).isEmpty()
#ifdef _WIN32
        || !(e = s->take(a->CreateSession(s->env, model.toStdWString().c_str(), s->options, &s->session))).isEmpty()
#else
        || !(e = s->take(a->CreateSession(s->env, model.toUtf8().constData(), s->options, &s->session))).isEmpty()
#endif
        || !(e = s->take(a->GetAllocatorWithDefaultOptions(&s->allocator))).isEmpty()
        || !(e = s->take(a->SessionGetInputName(s->session, 0, s->allocator, &s->inName))).isEmpty()
        || !(e = s->take(a->SessionGetOutputName(s->session, 0, s->allocator, &s->outName))).isEmpty()
        || !(e = s->take(a->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &s->memory))).isEmpty())
        return fail(QStringLiteral("The model could not be opened: %1").arg(e));

    return [s](const QVector<float> &chw, int h, int w, QVector<float> *out, int *oh, int *ow, QString *err) {
        const OrtApi *a = s->api;
        const int64_t shape[4] = {1, 3, h, w};
        OrtValue *input = nullptr, *output = nullptr;
        auto done = [&](const QString &why) {
            if (input) a->ReleaseValue(input);
            if (output) a->ReleaseValue(output);
            if (err) *err = why;
            return why.isEmpty();
        };
        QString e = s->take(a->CreateTensorWithDataAsOrtValue(s->memory, const_cast<float *>(chw.constData()), size_t(chw.size()) * sizeof(float), shape, 4,
                                                              ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
        if (!e.isEmpty())
            return done(e);
        // Only the first output is asked for: U2-Net gives seven maps, the first is the finest.
        e = s->take(a->Run(s->session, nullptr, &s->inName, &input, 1, &s->outName, 1, &output));
        if (!e.isEmpty())
            return done(e);
        float *data = nullptr;
        OrtTensorTypeAndShapeInfo *info = nullptr;
        size_t dims = 0;
        if (!(e = s->take(a->GetTensorMutableData(output, reinterpret_cast<void **>(&data)))).isEmpty()
            || !(e = s->take(a->GetTensorTypeAndShape(output, &info))).isEmpty())
            return done(e);
        e = s->take(a->GetDimensionsCount(info, &dims));
        int64_t d[8] = {};
        if (e.isEmpty() && dims >= 2 && dims <= 8)
            e = s->take(a->GetDimensions(info, d, dims));
        else if (e.isEmpty())
            e = QStringLiteral("the model gave an unexpected answer");
        a->ReleaseTensorTypeAndShapeInfo(info);
        if (!e.isEmpty())
            return done(e);
        *oh = int(d[dims - 2]);
        *ow = int(d[dims - 1]);
        if (*oh < 1 || *ow < 1 || *oh > 8192 || *ow > 8192)
            return done(QStringLiteral("the model gave an unexpected answer"));
        out->resize(*oh * *ow);
        std::copy(data, data + *oh * *ow, out->begin());
        return done({});
    };
}

}

#else // no header: nothing can be run

namespace OnnxRuntime {

bool supported() { return false; }

Matting::Runner makeRunner(const QString &, const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("This version of Notch cannot run the background-removal model yet.");
    return {};
}

}

#endif
