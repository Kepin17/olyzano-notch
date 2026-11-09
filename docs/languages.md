# Languages

*Settings → Languages* is where you choose the languages you use. They appear in the language choices of **Audio** (what is said, and the language of the notes) and **Documents** (the language of the study notes), and *Automatic* picks among them. By default Indonesian and English are ticked; about thirty are listed (Arabic, Chinese (Simplified and Traditional), Japanese, Korean, Hindi, Thai, Vietnamese, Filipino, Spanish, French, German, Portuguese, Italian, Dutch, Russian, Ukrainian, Polish, Turkish, Czech, Swedish, Greek, Hebrew, Persian, Bengali, Tamil, Urdu, Malay).

## What needs a download
- **Speech** (Audio): the Whisper models Notch downloads already understand about a hundred languages: nothing more to download. Automatic picks the likeliest of the languages you ticked.
- **Notes** (recordings and PDFs): the language model writes in all of them; the quality is best in the big languages and weaker in rare ones.
- **Reading pictures (OCR)**: one small pack per language (1 to 6 MB, tesseract's "fast" models, Apache-2.0), downloaded here only when you press *Download*, checked against a checksum, and removable. Packs installed on the system (for example `tesseract-ocr-ind` from apt) are used too and show as *installed on this computer*. Pictures are read with the languages you ticked that have a pack, plus English; when you choose a language for a document, that one is read first.

## Things to know
- The headings of the study notes (Overview, Key points, …) are translated for Indonesian, English, Spanish, French, German, Portuguese, Italian, Dutch, Russian, Turkish, Vietnamese, Arabic, Chinese, Japanese and Korean; other languages get English headings with the notes themselves in their language.
- Text in Chinese, Japanese, Korean or Arabic in the PDF made from the notes needs a font for that script on the computer (for example `fonts-noto-cjk`); Arabic, Hebrew, Persian and Urdu notes are laid out right to left.
- Transcribing and translating a recording in a language other than Indonesian or English has not been tried on real recordings yet.
