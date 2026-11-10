# Music and sounds in transcripts

A speech model given music or a noisy room makes words up. With the sound model installed, Notch first listens to the whole recording in clips of ten seconds, says what each holds, and writes the stretches that are only music or only another sound as lines of their own, without giving them to the speech model.

## What you see
- `♪ Music` (or `♪ Musik`): music and nobody talking. These stretches are not transcribed.
- `Ambient sound: applause` (or `Suara sekitar: tepuk tangan`): a crowd, applause, laughter, rain, an animal… and nobody talking.
- `♪ Background music`: somebody talks over music. This is still transcribed; the line only tells you the music is there.
- These lines have no speaker, are not counted as speakers, and are left out of the notes, of Ask and of search. They can be played from, and deleted if the model was wrong.
- The language of the lines follows the recording (Indonesian when it is not known).

## Setting it up
*Settings → AI → AI models → Music and sounds*: Download (the model, about 28 MB, and the speaker program if it is not installed yet; both run on this computer only). It applies to the next recording or import; *Tell music and sounds apart* switches it off.

Without the model nothing is lost: when the speech model itself writes `[Music]`, `(applause)` or `♪`, Notch now keeps that as a line like the ones above instead of dropping it.

## Limits
- Ten-second resolution: music or noise shorter than five seconds is not told.
- Not used by live transcripts, only by recordings and imports.
- A song with singing may be taken for speech over music and be written down as words, as well as the speech model can.
- The model is CED-tiny, small and fast (a fraction of a second per ten seconds on the processor). It can be wrong on unusual recordings.
