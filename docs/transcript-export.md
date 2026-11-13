# Export a transcript, and chapters

*Audio → a recording → Transcript → ⤓* saves the transcript as **SRT** or **WebVTT** (subtitles for a video) or **plain text**. *Include music and sounds* keeps the music, applause and other sound lines as `[♪ Music]`-style markers; switched off they are left out.

- Subtitles: one cue per line of the transcript, with the speaker's name in front when there is one; long lines break at about 42 characters; a cue with no length lasts one second; empty lines are dropped. VTT escapes `&`, `<` and `>`.
- Plain text: `[mm:ss] Name: text`.

## Chapters
On the **Summary** tab, a *chapters* button appears when the notes give at least three topics with a time. *Copy chapters (YouTube)* gives `00:00 Title` lines for a video description (YouTube needs the first at 00:00, at least three chapters and ten seconds each, and Notch makes the list follow those rules); *Copy chapters (list)* gives `- 12:34  Title`; *Save chapters as text* writes a file.

Chapters come from the topics the notes model (Qwen3) already wrote; nothing new is asked of it. Topics without a time are left out; if the notes do not give enough, there are no chapters. The text is made in `src/audio/TranscriptExport.h` and tested in `tests/tst_core.cpp`.

## Translating the transcript

The globe button on the **Transcript** tab translates the transcript into another language on this computer, with the notes model. The text goes in small numbered batches; an answer is believed only when it has the same count and keeps every number of the source, otherwise that batch is asked again line by line, and a line that still fails stays as it was, marked, and counted in a warning. Music and sound markers are not translated. The translation is kept (until the transcript is edited), so it can be saved several ways without translating again:

- **Subtitles with both (SRT / WebVTT):** each cue has the words as said, then the translation under them.
- **Translation only (SRT)** and **Translation (text)**.

The standard notes model is good for Indonesian and English; for other languages choose the Larger notes model (Settings → AI → Large models).
