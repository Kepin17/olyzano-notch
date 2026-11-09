# Audio Intelligence

Record or import a recording (meeting, lecture, interview) and get a transcript with timestamps, who said what, and notes with decisions and action items. Everything runs on your computer; nothing is uploaded. Open it from the **Audio** button on the action bar.

## How it works
`preprocess` (ffmpeg → 16 kHz mono WAV) → `vad` (finds speech, cuts ≤25 s windows at quiet points) → `asr` (whisper.cpp, built into Notch) → `diarize` (speakers) → `align` (who said each line) → `summarize` (local language model). A recording is worked on one stage at a time; what is done is kept in `~/.local/share/notch/audio/meetings/<id>/` (`work.wav`, `asr.json`, `diarization.json`, `summary.json`), so a stopped or failed job **continues** from the last good stage.

| Stage | Engine | Needs |
|---|---|---|
| Convert | `ffmpeg` from PATH | Linux: `sudo apt install ffmpeg`. Windows: ffmpeg on PATH (not bundled yet) |
| Transcript | whisper.cpp (in Notch) | model `whisper-small` (Standard) or `whisper-large-v3-turbo` (High quality) |
| Speakers | `sherpa-onnx` offline speaker diarization (pyannote-segmentation-3.0 + WeSpeaker) | helper + 2 small models |
| Notes | `llama-server` (llama.cpp) on `127.0.0.1` with a one-time token, Qwen3-4B Q4 | helper + model |

Models and helpers are listed in **Settings → Audio AI**. They are downloaded **only when you press Download**, resumed if interrupted, checked against a pinned SHA-256, and stored under `~/.local/share/notch/models` and `…/helpers`. Without the notes model you still get the transcript and speakers.

## Recording
**Record** captures your microphone (left channel, "You") and the sound the computer plays (right channel, the other people), then offers to process it. Because the two sides are separate channels, "you" and "them" are told apart reliably; several remote people are separated by voice. Nothing records by itself, and only record when everyone heard agrees. Limit: 60 minutes per recording.

## Live
**Live** (next to Record) records the same way and, while it records, shows what is said as it is said: you on the right, the others on the left. Each side is cut into utterances at short silences and transcribed on this computer with the *Whisper Small* model (so Live needs only that model; it is the standard one, not the high-quality one). A line appears about two seconds after the speaker stops.
- **Languages:** pick the language *you* speak and the one the *others* speak (or Automatic, which chooses among the languages ticked in *Settings → Languages*), and a language to translate into.
- **Translate:** *Translate what they say* puts a translation under each line of the others; *Translate what I say* translates your lines into their language. Translation uses the local language model (Qwen3 4B, the notes model) and takes about 2–3 s a line; if it falls behind, the original stays untranslated. Nothing is sent anywhere.
- **Smart answer:** press **Suggest answer**: from the last part of the conversation (and an optional note about who you are and what you want) the model writes up to three things you could say next — short, detailed and polite — in the language you translate into. They are drafts: read them before you use one, they can contain details you must check. Copy puts one on the clipboard; nothing is sent.
- **Falling behind:** if the computer cannot keep up, the oldest speech is left out of the live view (the recording has everything) and a note says so.
- **Afterwards:** **Stop and save** keeps the recording with the live lines as its first transcript (You / Others, without voices told apart); **Improve** then makes the full transcript, speakers and notes as for any recording and replaces the live one. Translations are shown live only and are not saved.
- Live and processing a recording use the same processor, so Live does not start while a recording is being processed.

## Speakers
Speakers are **grouped by voice**, not identified. Tap a name to rename it (it applies to every line). Voices that overlap, or very similar voices, can be merged or split wrongly; lines that cross a change of speaker are marked "approx.". This is sherpa-onnx's diarization, which can differ from pyannote's Community-1.
- **How many people:** when you know it, say so when importing (Speakers: Auto / 2 / 3 / 4 / 5) or later with **Detect again** on the Transcript tab: the recognized text is kept and only the speakers are found again. Otherwise the number comes from the voices, steered by *Settings → Audio AI → Telling speakers apart* (Fewer / Balanced / More). A voice heard for only a moment is not counted as a person.
- **Recordings made by Notch** are split by channel: the microphone (you, and anyone else sitting at it, as "Room 2", "Room 3") and the computer's sound (the people far away, "Others 1", "Others 2").
- **Names from the conversation:** "my name is Budi" names the one who says it; "Halo Budi" or "Pak Budi" suggests the name for the person who answers. They appear as suggestions above the transcript; nothing is renamed until you press **Apply**.

## Playing
Linux plays through the computer's own default sound output (ffmpeg and `paplay`), because QtMultimedia did not list every output (a Bluetooth headset was missing). The transcript follows the audio as it plays; scroll it yourself to stop that, and press *Follow the audio* to go on.

## The notes
A professional, structured document per style. **Meeting:** overview, topics (in order, with times), decisions (with the reason and who, when said), action items as a table (task, owner, due, priority, time), risks, open questions, next steps. **Lecture:** overview, topics, concepts and definitions, key points, questions. **Interview:** overview, topics, questions and answers, notable quotes, key points. **General:** overview, topics, key points, decisions, tasks. Long recordings are read in pieces and then written once more as a whole. **Copy** puts the notes on the clipboard as Markdown.

## Notes you can trust
The transcript is handed to the model as **data** (fenced; "ignore the above" inside a recording is not obeyed). Every decision and task must carry a quote that really appears in the transcript near its timestamp, checked by code, not by the model: invented items are dropped, items without a matching quote are marked **Needs review**, and a person or deadline that was not said stays empty ("not said"). Long recordings are read in pieces and merged. Styles: Meeting, Lecture, Interview, General.

## Editing
Double-click a line to change it (the recognized text is kept as the original); delete a line; click a time to play from there; the current line follows the player.

## Not yet
Export (Markdown/PDF/JSON/TXT), search across recordings, "ask the recording", a speaker-model choice, speeding up Live with the graphics card or NPU.

## Troubleshooting
- *"ffmpeg not found"*: install it and restart Notch.
- *Out of memory*: lower Quality to Standard, or set a memory limit in Settings; High quality needs several GB free.
- *Recording is silent on Linux*: the system-sound side uses PulseAudio/PipeWire's default monitor (`@DEFAULT_MONITOR@`); check it in `pavucontrol`.
- A job stuck in "Working" after a crash is marked stopped on the next start; press **Continue**.
