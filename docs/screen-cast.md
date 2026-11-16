# Phone mirror (Android)

*Camera → Phone.* Show an Android phone in a window of its own, tap and swipe it from the computer, take screenshots and record it. The window is a normal window: put it anywhere on the screen, move it and resize it freely (the picture keeps its shape). Nothing is installed on the phone and nothing is downloaded.

## How it works
- **Needs:** `adb` (the Android platform tools; Debian and Ubuntu: `sudo apt install adb`; Windows: *platform-tools* from developer.android.com, or set `ANDROID_HOME`) and `ffmpeg`. Notch looks for `adb` on the PATH, then in `ANDROID_HOME`, `~/Android/Sdk` and `%LOCALAPPDATA%\Android\Sdk`. Without it the Phone page says so and how to get it.
- **The phone:** turn on *USB debugging* (Settings → Developer options) and allow this computer when the phone asks. On Android 11 and newer you can do it without a cable: *Wireless debugging* on the same WiFi, then *Pair device with pairing code* here (address, port and the 6 digits the phone shows) and *Connect* (the address from the main Wireless debugging screen).
- **The picture:** `adb exec-out screenrecord` (H.264, at most 1280 on the long side, 4 Mbit/s) goes into ffmpeg, which makes 15 pictures a second for the window. `screenrecord` stops by itself every 3 minutes; the picture is started again at once and the last one stays on screen meanwhile.
- **Touch:** a click is a tap, a drag is a swipe, holding is a long press, the wheel scrolls, a right click is Back. The buttons send Back, Home, Recents, the volume and Power. *Type* sends text to the phone (`adb shell input text`).
- **Screenshot and Record** put a PNG or an MP4 (H.264, 15 fps) on the shelf.

## Limits
- Plain letters, digits and symbols only when typing: `input text` cannot do accents, emoji or new lines, and refuses a `%s` of its own. The window says so instead of sending something else.
- Each touch goes through `adb shell input`, which takes about 0.1 to 0.3 s on the phone: tapping and swiping are fine, but a game or a smooth drag is not. A faster way exists (scrcpy's server) but is not used here.
- The picture lags about a quarter of a second to a second, more over WiFi. Apps that block screen capture (banking, some video) show a black picture; that is the phone's choice.
- No sound, no clipboard sharing, no file drag and drop. Android only.
- Tested here with a stand-in `adb` (a script that plays a test video and logs the commands): the picture, the mapping of clicks to the phone's pixels, typing, screenshot, recording and the restart. Not tested here: a real phone, pairing over WiFi, Windows.

## Safety
Everything stays between the phone and this computer. The pairing code is typed once and not kept. What you type for an address is checked (`host:port` only) so it can never become an option of `adb`, and the names of keys are a fixed list.
