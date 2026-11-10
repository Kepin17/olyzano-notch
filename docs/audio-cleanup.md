# Clean up sound, and punctuation

Both are optional steps of a recording's job (Settings → AI → *Recordings*) and need a small download each (Settings → AI → AI models). They use programs that already come with the sherpa-onnx helper.

## Clean up sound before writing
`Off` (the default), `Reduce noise` or `Voice from music`. The job gains a stage, **Cleaning up the sound**, between preparing the audio and finding the speech.

- **Reduce noise:** GTCRN, a 0.5 MB model, lowers hum, fans and background noise. Fast.
- **Voice from music:** Spleeter (2 stems, 35 MB) splits the voice from the music, so a song with a singer is written down without words made up for the band. Slow, as long as the recording or more; there is a time limit of twice the length plus twenty minutes.
- Only the **speech recognizer** (and the search for speech) hears the cleaned sound (`clean.wav` in the recording's folder, kept so a stopped job goes on). The sound model (music and applause) and the speaker model still read the original: music has to stay audible to be found, and voices are told apart better unchanged. The cleaned file is made exactly as long as the original so the times of the transcript do not move.
- With *Voice from music* the music stretches are **not** taken away from the recognizer (the singing is what you want); they are still marked as «♪ Music» lines.
- If the model is missing or the program fails, the job goes on with the original and a warning says so. A saved result is used again only for the same mode.

## Punctuation for English
Adds full stops, commas and capitals to English text that has none (the CNN-BiLSTM model of sherpa-onnx, 30 MB). Off by default.

- **English only.** The programs of the helper have no Indonesian model (the other one, `sherpa-onnx-offline-punctuation`, knows Chinese and English). Indonesian and other languages are left exactly as Whisper wrote them; Whisper already punctuates Indonesian and English, so this helps only where its output is bare.
- Only the text of a line changes: its times and its speaker never do, a line that already has a capital or a mark is not sent, and an answer that has other words than the line is refused.
- It runs after the transcript is made and before the speakers are put in.

## Not tested here
The programs `sherpa-onnx-offline-denoiser`, `…source-separation` and `…online-punctuation` and their models were not run on a real recording (no download without your yes). The command lines follow each program's own `--help`; the output of the punctuation program is read as "Output text: …" or its last line, which was not seen from a real run. Test with the fakes only: the order of the stages, the use of the cleaned file, a failing or missing model, and the rules of the punctuation step.
