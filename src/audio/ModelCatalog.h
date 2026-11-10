#pragma once

#include "services/Languages.h"

#include <QList>
#include <QString>

// What Audio Intelligence can download: the speech models, the language model, and the two helper programs (llama.cpp's llama-server
// and sherpa-onnx's speaker diarization), each with the address it comes from, its size and its SHA-256. The sizes and checksums are
// those the publishers list (Hugging Face LFS, GitHub release assets) at the time of pinning; a file that does not match is thrown away.
// Nothing here is downloaded without the person clicking Download.
namespace Audio {

enum class Kind { Whisper, Llm, Segmentation, Embedding, Helper, Ocr, Vision, Embedder, Matting, Sound };

struct CatalogItem {
    QString id;
    Kind kind = Kind::Whisper;
    QString title, note;
    QString file;      // the file name on disk (for a helper: the archive's name)
    QString url;
    qint64 size = 0;   // bytes of the download
    QString sha256;
    QString platform;  // "linux" | "windows" | "" (both)
    QString exe;       // a helper: the program inside the archive, searched by name once extracted
    qint64 memory = 0; // bytes of RAM it needs while it runs (rough: for the warning before a job)
};

inline QList<CatalogItem> fullCatalog()
{
    const QString hf = QStringLiteral("https://huggingface.co/");
    const QString gh = QStringLiteral("https://github.com/");
    return {
        {QStringLiteral("whisper-small"), Kind::Whisper, QStringLiteral("Whisper Small (standard)"),
         QStringLiteral("Speech to text for Indonesian and English. Int8, about 1 GB of memory while it runs."), QStringLiteral("ggml-small-q8_0.bin"),
         hf + QStringLiteral("ggerganov/whisper.cpp/resolve/main/ggml-small-q8_0.bin"), 264464607,
         QStringLiteral("49c8fb02b65e6049d5fa6c04f81f53b867b5ec9540406812c643f177317f779f"), {}, {}, Q_INT64_C(1) << 30},
        {QStringLiteral("whisper-turbo"), Kind::Whisper, QStringLiteral("Whisper Large v3 Turbo (high quality)"),
         QStringLiteral("More accurate, slower. Int8, about 2.5 GB of memory while it runs."), QStringLiteral("ggml-large-v3-turbo-q8_0.bin"),
         hf + QStringLiteral("ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"), 874188075,
         QStringLiteral("317eb69c11673c9de1e1f0d459b253999804ec71ac4c23c17ecf5fbe24e259a1"), {}, {}, Q_INT64_C(5) << 29},
        {QStringLiteral("qwen3-4b"), Kind::Llm, QStringLiteral("Qwen3 4B (summaries)"),
         QStringLiteral("The language model that writes the notes. Q4, about 3.5 GB of memory while it runs."), QStringLiteral("Qwen3-4B-Q4_K_M.gguf"),
         hf + QStringLiteral("Qwen/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf"), 2497280256,
         QStringLiteral("7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5"), {}, {}, Q_INT64_C(7) << 29},
        {QStringLiteral("embed-qwen3"), Kind::Embedder, QStringLiteral("Qwen3 Embedding 0.6B (finds by meaning)"),
         QStringLiteral("Lets the search follow what a sentence means, not only its words (it also finds a file written in another language). Optional. Q8, about 1 GB of memory while it runs; Apache-2.0 licence."),
         QStringLiteral("Qwen3-Embedding-0.6B-Q8_0.gguf"), hf + QStringLiteral("Qwen/Qwen3-Embedding-0.6B-GGUF/resolve/main/Qwen3-Embedding-0.6B-Q8_0.gguf"), 639150592,
         QStringLiteral("06507c7b42688469c4e7298b0a1e16deff06caf291cf0a5b278c308249c3e439"), {}, {}, Q_INT64_C(1) << 30},
        {QStringLiteral("vision-qwen3vl"), Kind::Vision, QStringLiteral("Qwen3-VL 4B (reads pictures)"),
         QStringLiteral("Looks at a picture and says what is in it, reads the writing in it, tables and charts. Optional. Q4, about 4.5 GB of memory while it runs; Apache-2.0 licence."),
         QStringLiteral("Qwen3VL-4B-Instruct-Q4_K_M.gguf"), hf + QStringLiteral("Qwen/Qwen3-VL-4B-Instruct-GGUF/resolve/main/Qwen3VL-4B-Instruct-Q4_K_M.gguf"), 2497281664,
         QStringLiteral("66358cb18bb6b3b1b6675aa412c7a88ef01d228f481184d13668e5201c730a0a"), {}, {}, Q_INT64_C(9) << 29},
        {QStringLiteral("vision-mmproj"), Kind::Vision, QStringLiteral("Qwen3-VL 4B picture reader (mmproj)"),
         QStringLiteral("The part of Qwen3-VL that turns a picture into something the model can read. Needed with it."),
         QStringLiteral("mmproj-Qwen3VL-4B-Instruct-Q8_0.gguf"), hf + QStringLiteral("Qwen/Qwen3-VL-4B-Instruct-GGUF/resolve/main/mmproj-Qwen3VL-4B-Instruct-Q8_0.gguf"), 453974304,
         QStringLiteral("30ba2c7dd3127a4561b6cba9d13d0f711c91bdb38742e2f56d73c8cb596bd06d"), {}, {}, 0},
        {QStringLiteral("matting-u2netp"), Kind::Matting, QStringLiteral("U2-Netp (fast, takes the background off)"),
         QStringLiteral("A small model that cuts the main subject out of a picture in a second or two. Good for signatures, logos and simple pictures. Optional. About 0.5 GB of memory while it runs; Apache-2.0 licence."),
         QStringLiteral("u2netp.onnx"), gh + QStringLiteral("danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx"), 4574861,
         QStringLiteral("309c8469258dda742793dce0ebea8e6dd393174f89934733ecc8b14c76f4ddd8"), {}, {}, Q_INT64_C(1) << 29},
        {QStringLiteral("matting-isnet"), Kind::Matting, QStringLiteral("IS-Net general use (takes the background off)"),
         QStringLiteral("Cuts the main subject out of a picture: a signature, a logo, a product, a person. Optional. About 3 GB of memory while it runs; MIT licence (IS-Net: Apache-2.0)."),
         QStringLiteral("isnet-general-use.onnx"), hf + QStringLiteral("imgly/isnet-general-onnx/resolve/main/onnx/model.onnx"), 176149806,
         QStringLiteral("cc2c9f5c1751b9737cb81e708ff0c5e9542c2205daed22418a4fd2ab5d4c481a"), {}, {}, Q_INT64_C(3) << 30},
        {QStringLiteral("onnxruntime-linux"), Kind::Helper, QStringLiteral("ONNX Runtime (1.31.1)"),
         QStringLiteral("Runs the background-removal model on this computer only. MIT licence."), QStringLiteral("onnxruntime-linux-x64-1.31.1.tgz"),
         gh + QStringLiteral("microsoft/onnxruntime/releases/download/v1.31.1/onnxruntime-linux-x64-1.31.1.tgz"), 11413655,
         QStringLiteral("c849e36a1ef3e22d4ff217b9d1ce4a3cf5b9467b975610913ae959ad5b16bdc9"), QStringLiteral("linux"), QStringLiteral("libonnxruntime.so"), 0},
        {QStringLiteral("onnxruntime-windows"), Kind::Helper, QStringLiteral("ONNX Runtime (1.31.1)"),
         QStringLiteral("Runs the background-removal model on this computer only. MIT licence."), QStringLiteral("onnxruntime-win-x64-1.31.1.zip"),
         gh + QStringLiteral("microsoft/onnxruntime/releases/download/v1.31.1/onnxruntime-win-x64-1.31.1.zip"), 82929944,
         QStringLiteral("5869e26bb25314375f2d5ef1877e3ea06b1543d6c710e6e4a1ec2f7177ab4b2c"), QStringLiteral("windows"), QStringLiteral("onnxruntime.dll"), 0},
        {QStringLiteral("llama-server-linux"), Kind::Helper, QStringLiteral("llama.cpp server (b12202)"),
         QStringLiteral("Runs the language model on this computer only (127.0.0.1). MIT licence."), QStringLiteral("llama-b12202-bin-ubuntu-x64.tar.gz"),
         gh + QStringLiteral("ggml-org/llama.cpp/releases/download/b12202/llama-b12202-bin-ubuntu-x64.tar.gz"), 23066943,
         QStringLiteral("9076cf9b2ce5b849384711e85b0c37d18c36b1475106c236941de2b6f0899a88"), QStringLiteral("linux"), QStringLiteral("llama-server"), 0},
        {QStringLiteral("llama-server-windows"), Kind::Helper, QStringLiteral("llama.cpp server (b12202)"),
         QStringLiteral("Runs the language model on this computer only (127.0.0.1). MIT licence."), QStringLiteral("llama-b12202-bin-win-cpu-x64.zip"),
         gh + QStringLiteral("ggml-org/llama.cpp/releases/download/b12202/llama-b12202-bin-win-cpu-x64.zip"), 24759273,
         QStringLiteral("75d108e6f91907638fef0cbdf4cc230e56bd9eac71bbffb209ae8da1f932590e"), QStringLiteral("windows"), QStringLiteral("llama-server.exe"), 0},
        {QStringLiteral("gpu-llama-linux"), Kind::Helper, QStringLiteral("llama.cpp server for graphics cards (Vulkan, b12202)"),
         QStringLiteral("The same program built to run the language model on a graphics card (NVIDIA, AMD or Intel). Only useful if this computer has one. MIT licence."),
         QStringLiteral("llama-b12202-bin-ubuntu-vulkan-x64.tar.gz"),
         gh + QStringLiteral("ggml-org/llama.cpp/releases/download/b12202/llama-b12202-bin-ubuntu-vulkan-x64.tar.gz"), 35880958,
         QStringLiteral("c1eaf8fcaf0b17c1fe9097f7ff08bad484cd451533e7f2075381d18d220694e7"), QStringLiteral("linux"), QStringLiteral("llama-server"), 0},
        {QStringLiteral("gpu-llama-windows"), Kind::Helper, QStringLiteral("llama.cpp server for graphics cards (Vulkan, b12202)"),
         QStringLiteral("The same program built to run the language model on a graphics card (NVIDIA, AMD or Intel). Only useful if this computer has one. MIT licence."),
         QStringLiteral("llama-b12202-bin-win-vulkan-x64.zip"),
         gh + QStringLiteral("ggml-org/llama.cpp/releases/download/b12202/llama-b12202-bin-win-vulkan-x64.zip"), 37715166,
         QStringLiteral("ab526ddd4cacca463b73a385ede310c9888818d084efb57f3d113b4d3e79c404"), QStringLiteral("windows"), QStringLiteral("llama-server.exe"), 0},
        {QStringLiteral("sherpa-linux"), Kind::Helper, QStringLiteral("sherpa-onnx speaker diarization (2.0.1)"),
         QStringLiteral("Tells the speakers of a recording apart. Apache-2.0 licence."), QStringLiteral("sherpa-onnx-v2.0.1-linux-x64-shared-no-tts.tar.bz2"),
         gh + QStringLiteral("k2-fsa/sherpa-onnx/releases/download/v2.0.1/sherpa-onnx-v2.0.1-linux-x64-shared-no-tts.tar.bz2"), 25289904,
         QStringLiteral("b3b095f619059d54914027ff38f8a0a132ea3bbc5df32a212a9743a8bd1467b2"), QStringLiteral("linux"),
         QStringLiteral("sherpa-onnx-offline-speaker-diarization"), 0},
        {QStringLiteral("sherpa-windows"), Kind::Helper, QStringLiteral("sherpa-onnx speaker diarization (2.0.1)"),
         QStringLiteral("Tells the speakers of a recording apart. Apache-2.0 licence."), QStringLiteral("sherpa-onnx-v2.0.1-win-x64-shared-MD-Release-no-tts.tar.bz2"),
         gh + QStringLiteral("k2-fsa/sherpa-onnx/releases/download/v2.0.1/sherpa-onnx-v2.0.1-win-x64-shared-MD-Release-no-tts.tar.bz2"), 20962760,
         QStringLiteral("11fd6a50b97eed38b54fb16d8219f49b16aa7f6a8e43a042316469ef47652e82"), QStringLiteral("windows"),
         QStringLiteral("sherpa-onnx-offline-speaker-diarization.exe"), 0},
        {QStringLiteral("sound-ced-tiny"), Kind::Helper, QStringLiteral("Sound detection (CED tiny)"),
         QStringLiteral("Tells music, singing, applause, crowds and other sounds from speech, so the transcript says where they are instead of making words up. Runs on this computer only. Apache-2.0 licence."),
         QStringLiteral("sherpa-onnx-ced-tiny-audio-tagging-2024-04-19.tar.bz2"),
         gh + QStringLiteral("k2-fsa/sherpa-onnx/releases/download/audio-tagging-models/sherpa-onnx-ced-tiny-audio-tagging-2024-04-19.tar.bz2"), 28531989,
         QStringLiteral("84baf315b57d61aa69480c4fee878dab54cbc7be3e877db334e65d8b087e23c3"), {}, QStringLiteral("model.int8.onnx"), 0},
        {QStringLiteral("clean-gtcrn"), Kind::Sound, QStringLiteral("Noise reduction (GTCRN)"),
         QStringLiteral("A very small model that lowers hum, fans and background noise before a recording is turned into text. Runs on this computer only. MIT licence."),
         QStringLiteral("gtcrn_simple.onnx"), gh + QStringLiteral("k2-fsa/sherpa-onnx/releases/download/speech-enhancement-models/gtcrn_simple.onnx"), 535638,
         QStringLiteral("e77603ac0c23dac3227dd2d7135b3a585cbee2679048aecfa886657d3ae1b534"), {}, {}, Q_INT64_C(1) << 28},
        {QStringLiteral("clean-spleeter"), Kind::Helper, QStringLiteral("Voice and music separation (Spleeter)"),
         QStringLiteral("Splits the voice from the music, so a song with a singer is written without words made up for the band. Slower than the rest; runs on this computer only. MIT licence."),
         QStringLiteral("sherpa-onnx-spleeter-2stems-fp16.tar.bz2"),
         gh + QStringLiteral("k2-fsa/sherpa-onnx/releases/download/source-separation-models/sherpa-onnx-spleeter-2stems-fp16.tar.bz2"), 35271738,
         QStringLiteral("c6c5c4307673bc6813ddf58d4efdff57c26d2dfc3f25b05c7a32db453d70aca6"), {}, QStringLiteral("vocals.fp16.onnx"), Q_INT64_C(1) << 30},
        {QStringLiteral("punct-en"), Kind::Helper, QStringLiteral("Punctuation, English (CNN-BiLSTM)"),
         QStringLiteral("Adds full stops, commas and capital letters to English text that has none. English only. Apache-2.0 licence."),
         QStringLiteral("sherpa-onnx-online-punct-en-2024-08-06.tar.bz2"),
         gh + QStringLiteral("k2-fsa/sherpa-onnx/releases/download/punctuation-models/sherpa-onnx-online-punct-en-2024-08-06.tar.bz2"), 30667839,
         QStringLiteral("9f5e5a72c7d2829635bd074fce92b6bbd5b78da8a52e7ad8ed1be933f366b99d"), {}, QStringLiteral("model.onnx"), Q_INT64_C(1) << 27},
        {QStringLiteral("diar-segmentation"), Kind::Segmentation, QStringLiteral("Speaker segmentation (pyannote 3.0)"),
         QStringLiteral("Finds where someone speaks. MIT licence."), QStringLiteral("pyannote-segmentation-3-0.onnx"),
         hf + QStringLiteral("csukuangfj/sherpa-onnx-pyannote-segmentation-3-0/resolve/main/model.onnx"), 5992913,
         QStringLiteral("220ad67ca923bef2fa91f2390c786097bf305bceb5e261d4af67b38e938e1079"), {}, {}, 0},
        {QStringLiteral("diar-embedding"), Kind::Embedding, QStringLiteral("Speaker embedding (WeSpeaker ResNet34)"),
         QStringLiteral("Tells whose voice it is, so the same person gets the same label. Apache-2.0 licence."), QStringLiteral("wespeaker_en_voxceleb_resnet34_LM.onnx"),
         gh + QStringLiteral("k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/wespeaker_en_voxceleb_resnet34_LM.onnx"), 26530550,
         QStringLiteral("e9848563da86f263117134dfd7ad63c92355b37de492b55e325400c9d9c39012"), {}, {}, 0},
    };
}

// The OCR language packs (tesseract's "fast" models, Apache-2.0), one per language of Languages.h, pinned to one commit of the repository they
// come from. A pack is the file <code>.traineddata; with the speech and language models these are the only things Languages asks to download.
inline QList<CatalogItem> ocrCatalog()
{
    struct Pack { const char *tess; qint64 size; const char *sha256; };
    static const Pack packs[] = {
        {"ara", 1432056, "e3206d3dc87fd50c24a0fb9f01838615911d25168f4e64415244b67d2bb3e729"},
        {"ben", 855841, "31163084c279aaebd376216f0c3d5c17ad4b5fee8db49dae79c20000b5de5964"},
        {"ces", 3795684, "934bcaf97ef3348413263331131c9fa7f55f30db333c711929c124fb635f7e1b"},
        {"chi_sim", 2469156, "a5fcb6f0db1e1d6d8522f39db4e848f05984669172e584e8d76b6b3141e1f730"},
        {"chi_tra", 2366642, "529c5b5797d64b126065cd55f2bb4c7fd7b15790798091b1ff259941a829330b"},
        {"deu", 1525436, "19d219bbb6672c869d20a9636c6816a81eb9a71796cb93ebe0cb1530e2cdb22d"},
        {"ell", 1419514, "4fba8a0b461038d51f1c20d043d4f2ac38c4e778f1b90830847f7bd8fa3ba726"},
        {"eng", 4113088, "7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2"},
        {"fas", 431500, "db1c0a91208aff00d3cf1ed2c1d23f76419afd5f024688b4f71adc3f2ce4a505"},
        {"fil", 1841715, "2b17f68014ab44b88b5e2334ebcd0ce0090d9af7c6fbef6fab22e0bbd9a934dd"},
        {"fra", 1130365, "ced037562e8c80c13122dece28dd477d399af80911a28791a66a63ac1e3445ca"},
        {"heb", 961404, "11f9e43ab227f786352a50f75c94c2e9906f1baba86d93276da19da7ce0904db"},
        {"hin", 1122751, "4c73ffc59d497c186b19d1e90f5d721d678ea6b2e277b719bee4e2af12271825"},
        {"ind", 1122661, "69786901da87ab8766c1ea7fbb10b28f2110c14da3f6c8f2735df131fba95d88"},
        {"ita", 2701314, "b8f89e1e785118dac4d51ae042c029a64edb5c3ee42ef73027a6d412748d8827"},
        {"jpn", 2471260, "1f5de9236d2e85f5fdf4b3c500f2d4926f8d9449f28f5394472d9e8d83b91b4d"},
        {"kor", 1677415, "6b85e11d9bbf07863b97b3523b1b112844c43e713df8b66418a081fd1060b3b2"},
        {"msa", 1747801, "e41a3e5febfec50c90371eb1cbb17a48b10cad387900e3420b1f134c1b766cba"},
        {"nld", 6050296, "ced0e5e046a84c908a6aa7accbef9a232c4a5d9a8276691b81c6ee64d02963f6"},
        {"pol", 4765518, "c4476cdbc0e33d898d32345122b7be1cbf85ace15f920f06c7714756e1ef79b2"},
        {"por", 1982756, "c4932b937207a9514b7514d518b931a99938c02a28a5a5a553f8599ed58b7deb"},
        {"rus", 3861738, "e16e5e036cce1d9ec2b00063cf8b54472625b9e14d893a169e2b0dedeb4df225"},
        {"spa", 2294433, "6f2e04d02774a18f01bed44b1111f2cd7f3ba7ac9dc4373cd3f898a40ea6b464"},
        {"swe", 4167034, "f7304988d41f833efebcc2d529df54b1903ecebbc3da1faabd19a0fddd4fe586"},
        {"tam", 3237963, "d02fbec24be4b07e32e80d0ccfc3b6b67a3c5d61c9d0a7c8532677990912c6ec"},
        {"tha", 1072600, "294227cc2d1292b0acb28d61d4115c88252b96d466ca90b417cf4cf0c67bf07c"},
        {"tur", 4550554, "7393381111e1152420fc4092cb44eef4237580d21b92bf30d7d221aad192c6b7"},
        {"ukr", 3825102, "d59e53e2bded32f4445f124b4b00240fcac7e8044c003ab822ccb94f0b3db59b"},
        {"urd", 1398718, "62e8250ce2a994106e313a82e26a516a39e2cf159d0ce3c5b5008387fd0d555f"},
        {"vie", 531275, "79df64caf7bcfb2a27df5042ecb6121e196eada34da774956995747636d5bfa1"},
    };
    const QString base = QStringLiteral("https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/87416418657359cb625c412a48b6e1d6d41c29bd/");
    QList<CatalogItem> out;
    for (const Pack &p : packs) {
        QString name = QString::fromLatin1(p.tess);
        for (const Lang::Language &l : Lang::all())
            if (l.tess == name)
                name = l.english;
        const QString tess = QString::fromLatin1(p.tess);
        out.append({QStringLiteral("ocr-") + tess, Kind::Ocr, QStringLiteral("%1 text recognition (OCR)").arg(name),
                    QStringLiteral("Reads %1 in pictures and scanned pages. Apache-2.0 licence.").arg(name), tess + QStringLiteral(".traineddata"), base + tess + QStringLiteral(".traineddata"),
                    p.size, QString::fromLatin1(p.sha256), {}, {}, 0});
    }
    return out;
}

// The items for this computer.
inline QList<CatalogItem> catalog()
{
#ifdef Q_OS_WIN
    const QString here = QStringLiteral("windows");
#else
    const QString here = QStringLiteral("linux");
#endif
    QList<CatalogItem> out;
    for (const CatalogItem &i : fullCatalog() + ocrCatalog())
        if (i.platform.isEmpty() || i.platform == here)
            out.append(i);
    return out;
}

} // namespace Audio
