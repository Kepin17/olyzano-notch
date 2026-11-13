## Olyzano Notch 0.18.0

Notch lives at the top of your screen and keeps your files, recordings, tasks and forms one click away. This version teaches it to **listen, read, translate and answer**, and everything the AI does runs on your own computer.

### Highlights
- 🎙️ **Recordings become notes.** Record or import a recording and get a transcript, who said what, a summary, decisions and tasks.
- 💬 **Talk to Notch.** Ask about your tasks, recordings and files, or tell it what to do. It always asks before it changes anything.
- 📝 **Forms that feel like Google Forms.** Page-by-page forms, your own thank-you message, opening and closing dates, star ratings, signatures, and answers you can search.
- 🔍 **Search finds what is inside.** Press Shift + Space to look inside your recordings, files and form answers, not just app names.
- 🌍 **Translate.** Turn a transcript into subtitles in two languages, or translate a PDF, page by page.

---

### Talk to Notch
- **Assistant** (action bar → *Assistant*). Ask "What is due this week?" or "Summarize my last recording", or say "Add a task: send the report tomorrow 9". Anything that changes something shows an **Add** button first, and text inside your files can never give it orders.
- **Voice commands.** Press the microphone and say "open Firefox" or "timer 5 minutes". Nothing is recorded or sent anywhere.
- **Ask your files** (*Docs → Ask your files*). Ask a question about a PDF, a recording, or everything. Each answer shows where it came from, and Notch says "not found" instead of guessing.

### Recordings & meetings
- **Record or import** (action bar → *Audio*). Record your microphone and the sound of your computer, or drop in a file.
- **Transcript with times.** Click a time to play from there, fix any line, and find words. Indonesian, English, or automatic.
- **Who said what.** Notch separates voices; rename a speaker once and every line changes.
- **Notes you can trust.** Summary, key points, decisions, tasks and open questions. Each decision and task must quote the transcript, and anything Notch cannot back up is marked *Needs review*.
- **Follow-up in one click** (a recording → *Follow-up*). Get a ready-to-send email, an agenda for the next meeting, and your tasks added to the Tasks list.
- **Translate** (a recording → *Transcript* → globe button). Save subtitles with two languages at once, or the translation alone.
- **Live meetings.** Watch the transcript appear while the meeting happens, with optional translation and three suggested replies.
- **Cleaner sound.** Optional noise reduction, music detection (shown as "♪ Music" instead of invented words), and punctuation for English.
- **Export.** Save a transcript as subtitles or text, and get chapters for a video description.
- **About thirty languages** (*Settings → Languages*). Choose what is spoken in a recording and the language of your notes. Reading text in pictures needs one small pack per language, downloaded only when you ask.
- **Jobs pick up where they stopped.** If a job fails or you stop it, it continues from the last good step.

### Documents & files
- **Study notes from a PDF** (*Docs*). Get an overview, key points, terms and practice questions, each checked against the page it came from.
- **Translate a PDF** (*Docs → Convert → Translate*). You get a new PDF and a text file, page by page.
- **Convert and mark** (*Docs → Convert*). PDF to Word, Word to PDF, watermarks, stamps and your signature, and password protection. Your original is never changed.
- **Remove backgrounds** from pictures, and have a picture described or its text read.
- **Smart search in Files.** Find a file by describing it: "last month's electricity receipt".
- **Suggested tasks.** Notch spots tasks in your recordings and PDFs and offers them on the Tasks page. Nothing is added until you press Add.

### Forms
- **Make a form** (*Docs → Forms*). Use text, numbers, choices, dates, uploads, **star ratings**, **scales** and **signatures**.
- **One page or one question at a time.** Group questions into sections, and show or skip questions based on earlier answers.
- **Your own ending.** Write a thank-you message with a button, for example to join a group.
- **Open between two dates.** Pick the days on a calendar.
- **One answer or many.** Choose one answer per device, or let people answer as often as they like.
- **Change an answer later.** If you allow it, people get a personal link to edit what they sent.
- **Limits per choice.** For example, "Saturday: 10 places". This works on your WiFi.
- **See the results.** Charts and averages, a table for your spreadsheet, and a report with pictures. Files people send go to Files in a folder named after the form.
- **Share it your way.** Use a QR code on your WiFi, or your own server.

### Sharing
- **Share by QR code** with limits: how many downloads, and how many people. Set them with a slider or by typing.
- **Notices.** Notch tells you when a phone takes your file or when a limit is reached.

### Search
- **Shift + Space.** Find apps, and also passages inside your recordings (with their notes), your files and your form answers. The words you typed are in bold, and **Enter** opens the right place: the recording at that second, the file, or the answers of that form.
- **Not over full-screen apps.** Search stays out of the way of a game or video that fills the screen.

### Look & feel
- **Hidden in full screen.** With *Hide Notch when an app is full screen* on, Notch goes into **Hidden mode** while a game or video fills the screen. You can still open it from the top edge, and it goes back to your usual mode afterwards. *Gaming mode* stays the stricter option that also mutes sounds.
- **Screen recorder** (*Camera → Record screen*). Record the screen, one window or just the camera. Place the camera in a corner, add your microphone and the computer's sound, and get an MP4 in Files. Linux first.
- **A cleaner design.** Calmer buttons, cards and switches, with one clear main action on each page.
- **AI models in one place** (*Settings → AI*). See everything Notch can download, grouped by use, and install a whole group with one click.

### Good to know
- **Everything runs on your computer.** Nothing is sent to a cloud service, and models are downloaded only when you press **Download**.
- **Bigger models for a good graphics card.** Whisper Large v3, Qwen3 8B and Qwen3 14B are optional. If your computer cannot run one, its Download button is turned off and tells you why.
- **Forms on your own server need its files updated.** Copy the new `server/` folder and restart it. Without that, "one answer per device", "change my answer" and opening dates will not work there.
- **Limits per choice only work on your WiFi.** Through your own server the answers are sealed, so Notch cannot count them.
- **Small AI, check the details.** The standard AI is good for Indonesian and English. For other languages, and for dates and numbers, read what it writes against your own data.
- **Your graphics card is used automatically** to make speech recognition and the AI much quicker. You can switch to "processor only" in *Settings → AI*, and Notch falls back by itself if the card does not work.
- Notch needs `ffmpeg` for audio (on Windows it must be on PATH).

### Optional downloads
| What | Good for | Size (about) |
| --- | --- | --- |
| Speech recognition (Small) | Transcripts | 0.3 GB |
| Speech recognition (more accurate) | Better transcripts | 0.9 GB |
| Notes AI (Qwen3 4B) | Notes, Ask, translation, assistant | 2.5 GB |
| Picture reader (Qwen3-VL) | Describing pictures and charts | 2.9 GB |
| Search by meaning | Finding files by what they mean | 0.6 GB |
| Speaker separation | Who said what | 0.1 GB |
| Noise reduction, music detection, background removal | Cleaner sound and pictures | 0.5 to 190 MB each |
| Whisper Large v3, Qwen3 8B, Qwen3 14B | Best quality on a graphics card | 3 GB, 5 GB, 9 GB |

More details: `docs/forms.md`, `docs/audio-intelligence.md`, `docs/ai-models.md`, `docs/assistant-chat.md`, `docs/ask-your-files.md`, `docs/pdf-tools.md`, `docs/transcript-export.md`, `docs/screen-recorder.md`.

### Downloads
- **Linux:** `notch_0.18.0_amd64.deb` for Debian 13 or Ubuntu 25.04 and newer. Install it with `sudo apt install ./notch_0.18.0_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.18.0-windows-x64-setup.exe`, or the portable `.zip`.
