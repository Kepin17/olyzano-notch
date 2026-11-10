# Screen recorder

*Camera → Record screen.* Record the screen, one window, or only the camera; lay the camera over the picture where you want it; choose the microphone and the sound of the computer. The finished MP4 goes onto your shelf.

## How it works
- **Source:** *Screen* (pick the monitor), *Window*, or *Camera only*. *Preview* shows what will be recorded; nothing is captured before you press Preview or Record. On Wayland (KDE, GNOME) the system asks which screen or window to share, once per recording or preview.
- **Camera:** *Show the camera* lays it over the picture. Pick the camera, mirror it, choose its shape (rounded, square, circle) and size, and put it in a corner or at the bottom middle, or drag it in the preview.
- **Sound:** the microphone (choose the input and its volume) and the sound of the computer (choose which output, and its volume). Each can be switched off.
- **Record / Stop:** a 3 second countdown, then the clock runs. Stop puts the recording together (a few seconds for a short one, longer for an hour) and adds `Screen recording <date>.mp4` to the shelf.

The screen (with the microphone) and the camera are recorded as two files, and the sound of the computer as a third; they are put together with ffmpeg when you stop, and the temporary files are deleted. If putting them together fails, the files are kept and the message says where.

## Needs
`ffmpeg` and `qml6-module-qtmultimedia` (the Camera page already needs it), and `parec` (package `pulseaudio-utils`) for the sound of the computer.

## Limits
- One screen or window and one camera; no scenes, transitions or text like OBS.
- The camera is laid over when you stop, so the preview shows the place and shape but is not the file itself (the corner margin is a little different).
- The camera and the sound of the computer start a moment after the screen; the offset is measured and corrected, but check the sync of a first long recording.
- The sound of the computer is silent while nothing is playing, and then starts when something does; a long silence at the start can shift it, so try it first.
- Linux only for now; Windows is not done.
- Tested here: the putting together (ffmpeg, all three shapes, mixed sound). Not tested here: the capture itself on your screen, since it needs the system's sharing dialog.

## While it records
- The recording **goes on when the panel closes or you open another page**: the recorder page is kept loaded, out of sight, while it records, previews or puts the file together.
- With the notch closed, a pill shows a red dot, *Recording screen* and the time; pointing at it or clicking it opens *Camera → Record screen*.

## Why Preview used to crash
The core dumps show `pw_thread_loop_stop(NULL)` inside Qt's PipeWire capture (`QPipeWireCapture::setActiveInternal(false)` and the destructor of `QScreenCapture`): Qt stops a thread loop that does not exist yet when the capture is stopped, or deleted, before the system's sharing window has been answered (or after it failed). So now:
- the screen capture is made by `RecordController::newScreenCapture()`, belongs to no page and is **never deleted** (one object is left behind at exit, on purpose);
- it is stopped only after the first picture has come from it; a stop asked earlier waits for that picture;
- after an error that capture is left alone and a new one is made next time;
- nothing sets `active` from a binding or from a page being destroyed.

Not tested here (needs Wayland and the system's sharing window): the capture itself, and whether the first picture comes at once on a still screen; the fix follows the core dumps, not a reproduction.

## Recording one application
- *Window* shows the open applications and their windows (KDE Plasma tells Notch; other desktops list windows through the window capture). A *Record <app>* button next to Preview picks the application that was in front before the notch was opened.
- The recording is named after the application (`Screen recording Firefox 2026-11-10 11.40.mp4`) and the closed notch says *Recording Firefox*.
- **On Wayland the system, not Notch, decides what is shared**: Notch brings the chosen window to the front and then the system's window asks which one to share; choose that one. Notch cannot answer it for you, and does not crop the screen to a window (a window that moves would leave the crop).
- Not on Wayland (X11, Windows): the window of the chosen application is selected in the list of windows by its name.

## The camera
*Adjust the camera* (Camera page and recorder): quality *Light 480p / Balanced 720p / Full 1080p*, zoom (when the camera has it), brightness and contrast.
- Qt takes the camera's biggest size by default; now the size nearest to the chosen one that runs at 24 fps or more is set before the camera starts (Balanced by default). A change counts the next time the page opens, because changing the size of a running camera restarts it, and stopping a V4L2 camera can crash Qt's ffmpeg backend.
- Brightness and contrast are drawn over the picture, and for a recording ffmpeg applies the same to the camera when the files are put together (`eq`). Zoom is the camera's own.
- Remembered in `notch/camera.conf`. The camera is still not switched off while the page is open, for the same crash reason.
