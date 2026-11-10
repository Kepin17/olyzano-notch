#pragma once

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

// The screen recorder puts its recordings together at the end: the screen (or one window, or only the camera) is the picture, the camera is
// laid over it where the person chose, the microphone (inside the screen file) and the sound of the computer (a WAV) are mixed. This file
// makes the ffmpeg arguments and checks the choices; it runs nothing. Pure.
namespace RecordMix {

enum class Shape { Square, Rounded, Circle };

struct Options {
    QString screen;            // the main video (it may carry the microphone as its sound)
    QString camera;            // laid over it; "" = none
    QString system;            // the sound of the computer, a WAV; "" = none
    QString output;            // the MP4 to write
    int screenWidth = 0;       // the main picture's size, to size and place the camera
    int screenHeight = 0;
    double cameraOffset = 0;   // seconds the camera / the system sound started after the screen (never negative)
    double systemOffset = 0;
    double seconds = 0;        // length of the screen recording, 0 = until the shortest ends
    bool screenHasSound = false;
    double micVolume = 1.0;    // 0..2
    double systemVolume = 1.0;
    // the camera
    double x = 1.0;            // 0 = at the left edge .. 1 = at the right edge
    double y = 1.0;            // 0 = at the top .. 1 = at the bottom
    double size = 0.22;        // its width as a part of the picture's width
    Shape shape = Shape::Rounded;
    bool mirror = true;
    double brightness = 0.0;   // -0.5..0.5, the camera's (ffmpeg eq)
    double contrast = 1.0;     // 0.5..1.5
};

constexpr double MinSize = 0.08, MaxSize = 0.5;
constexpr int Margin = 24;     // pixels kept free between the camera and the edge

inline QString validate(const Options &o)
{
    if (o.screen.isEmpty() || o.output.isEmpty())
        return QStringLiteral("There is nothing to put together.");
    if (!o.camera.isEmpty() && (o.screenWidth < 64 || o.screenHeight < 64))
        return QStringLiteral("The size of the picture is not known.");
    if (o.size < MinSize || o.size > MaxSize || o.x < 0 || o.x > 1 || o.y < 0 || o.y > 1)
        return QStringLiteral("The place or size of the camera is out of range.");
    if (o.brightness < -0.5 || o.brightness > 0.5 || o.contrast < 0.5 || o.contrast > 1.5)
        return QStringLiteral("The brightness or contrast of the camera is out of range.");
    if (o.micVolume < 0 || o.micVolume > 2 || o.systemVolume < 0 || o.systemVolume > 2)
        return QStringLiteral("The volume is out of range.");
    return {};
}

// the camera's width in pixels: even, as H.264 needs
inline int cameraWidth(const Options &o) { return std::max(16, int(std::lround(o.screenWidth * o.size / 2.0)) * 2); }

inline QString num(double v) { return QString::number(v, 'f', 3); }

// The picture part of the filter graph: [cam] is shaped and laid over [0:v], giving [v].
inline QString videoGraph(const Options &o)
{
    if (o.camera.isEmpty())
        return QStringLiteral("[0:v]scale=trunc(iw/2)*2:trunc(ih/2)*2,format=yuv420p[v]");
    const int w = cameraWidth(o);
    QString cam = QStringLiteral("[1:v]");
    if (o.mirror)
        cam += QStringLiteral("hflip,");
    if (std::abs(o.brightness) > 0.004 || std::abs(o.contrast - 1.0) > 0.004) // what the sliders showed in the preview
        cam += QStringLiteral("eq=brightness=%1:contrast=%2,").arg(num(o.brightness), num(o.contrast));
    if (o.shape == Shape::Circle)
        cam += QStringLiteral("crop='min(iw,ih)':'min(iw,ih)',scale=%1:%1,").arg(w);
    else
        cam += QStringLiteral("scale=%1:-2,").arg(w);
    cam += QStringLiteral("format=yuva420p");
    if (o.shape == Shape::Circle)
        cam += QStringLiteral(",geq=lum='p(X,Y)':cb='p(X,Y)':cr='p(X,Y)':a='if(lte(hypot(X-W/2,Y-H/2),W/2),255,0)'");
    else if (o.shape == Shape::Rounded) // a corner of about a tenth of the width
        cam += QStringLiteral(",geq=lum='p(X,Y)':cb='p(X,Y)':cr='p(X,Y)':a='if(gt(abs(X-W/2),W/2-%1)*gt(abs(Y-H/2),H/2-%1)*gt(hypot(abs(X-W/2)-(W/2-%1),abs(Y-H/2)-(H/2-%1)),%1),0,255)'").arg(std::max(4, w / 10));
    cam += QStringLiteral("[cam];");
    return cam + QStringLiteral("[0:v][cam]overlay=x='%1+(main_w-overlay_w-%2)*%3':y='%1+(main_h-overlay_h-%2)*%4':eof_action=repeat:format=auto,format=yuv420p[v]")
                     .arg(Margin).arg(2 * Margin).arg(num(o.x), num(o.y));
}

// The arguments of the one ffmpeg run. Input 0 is the screen, 1 the camera (when there is one), then the system sound.
inline QStringList args(const Options &o)
{
    QStringList a = {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-i"), o.screen};
    int next = 1;
    int camIn = -1, sysIn = -1;
    if (!o.camera.isEmpty()) {
        a << QStringLiteral("-itsoffset") << num(std::max(0.0, o.cameraOffset)) << QStringLiteral("-i") << o.camera;
        camIn = next++;
    }
    if (!o.system.isEmpty()) {
        a << QStringLiteral("-itsoffset") << num(std::max(0.0, o.systemOffset)) << QStringLiteral("-i") << o.system;
        sysIn = next++;
    }
    Q_UNUSED(camIn)
    QString graph = videoGraph(o);
    const bool mic = o.screenHasSound, sys = sysIn >= 0;
    if (mic && sys)
        graph += QStringLiteral(";[0:a]volume=%1[m];[%2:a]volume=%3[s];[m][s]amix=inputs=2:duration=longest:normalize=0[a]").arg(num(o.micVolume)).arg(sysIn).arg(num(o.systemVolume));
    else if (mic)
        graph += QStringLiteral(";[0:a]volume=%1[a]").arg(num(o.micVolume));
    else if (sys)
        graph += QStringLiteral(";[%1:a]volume=%2[a]").arg(sysIn).arg(num(o.systemVolume));
    a << QStringLiteral("-filter_complex") << graph << QStringLiteral("-map") << QStringLiteral("[v]");
    if (mic || sys)
        a << QStringLiteral("-map") << QStringLiteral("[a]") << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("160k");
    else
        a << QStringLiteral("-an");
    a << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-preset") << QStringLiteral("veryfast") << QStringLiteral("-crf") << QStringLiteral("23")
      << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    if (o.seconds > 0)
        a << QStringLiteral("-t") << num(o.seconds);
    a << o.output;
    return a;
}

} // namespace RecordMix
