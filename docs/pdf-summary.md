# Summarize a PDF as study notes

Open the **Docs** page of the action bar and choose a PDF (one from your shelf, any file, or drop it here), or right-click a PDF in Files → **Tools → PDF tools → Summarize as study notes…** (it opens the Docs page with that file chosen). What was made stays in the *Recent notes* list there. Notch reads the PDF, a local language model writes the notes, and two files go onto your shelf: `Name (summary).pdf` (laid out for reading or printing) and `Name (summary).md` (to copy into a document). Everything runs on this computer; nothing is sent anywhere.

## How it works
1. **Pages:** the text each page has is read with `pdftotext`. A page with no text (a scan or a photo) is drawn at 300 dpi and read with `tesseract` (OCR), using Indonesian and English when the `ind` language pack is installed. Choose *Read them* (pages with no text), *Read all*, or *Skip*.
2. **Cleaning:** running headers and footers, page numbers, hyphenated line ends and wrapped lines are fixed.
3. **Reading:** the pages go to the model in pieces of about 6000 characters. For each piece it gives the section title, key points, terms with their definitions, and formulas/rules/examples, each with a short exact quote and its page.
4. **Checking:** a point is kept only when its quote is really on that page (most of its words, because OCR makes mistakes); the page number is corrected to where the words are, and what the pages do not support is dropped. The document is data: instructions written inside a PDF are not obeyed.
5. **Writing:** one more pass writes the overview, learning objectives and practice questions with answers, from the checked notes only.

Styles: **Study notes** (overview, objectives, outline with pages, key points, terms, formulas, practice questions), **Brief**, **Flash cards** (questions and answers), **Cheat sheet**. Length: Short / Medium / Detailed. Language: automatic (from the text), Indonesian or English.

## Needs
The language model and its program (the same two downloads as the notes of recordings, offered in the window when missing), `poppler-utils`, and for pictures `tesseract-ocr` (and `tesseract-ocr-ind` for Indonesian). Up to 150 pages at once: choose a range of pages for more.

## Time and limits
About a minute a page of text on a CPU (measured: 2 pages in 3½ minutes with the Qwen3-4B model), plus about 3 seconds a page read by OCR; the window shows an estimate before it starts. A job that is stopped goes on from where it was when started again. Pictures, tables and formulas are not understood (only the text that OCR finds), and the quality of the notes is that of a 4 B model: read them against the document.

## Figures, charts and diagrams (optional)
The words of a PDF are read as text, and scanned pages with OCR. A chart, a diagram or a table that is a picture has no words to read, so by default it is skipped. With the optional **picture model** (Qwen3-VL 4B, about 2.9 GB, *Settings → AI → Reading images*, downloaded only when you press Download) the option **Figures: Describe (AI)** on the Documents page shows each page that holds a figure to the model first (at most 20 pages, drawn at 120 dpi): what it says about the figure (what it shows, the values it can read, the writing in it) is added to that page as `[Figure: …]`, and the notes are then made from the page as usual, so a claim in the notes still has to be supported by the page. The picture model and the notes model are not in memory together. On the processor this takes about a minute a page; on a graphics card much less. Pages already read as pictures by OCR are not shown again.

## Pictures on the shelf
Right-click a picture → **Tools → Image (AI)**: *Describe image* and *Read text* (each copied, or saved as a note). The writing in a picture is data to report, never an instruction to the model.
