# PDF tools: Word ↔ PDF and watermarks

*Documents → Convert* (choose or drop any file; the result goes straight onto the shelf), or right-click a file on the shelf → *Tools*.

## Convert
- **PDF → Word (.docx) / OpenDocument (.odt):** LibreOffice reads the PDF and writes an editable document. Text is kept; columns, tables, formulas and exact layout may not look the same. A PDF made of pictures (a scan) is read with OCR (language packs from *Settings → Languages*) and only its words come over, without layout. Up to 200 pages at once.
- **Word → PDF:** *Tools → Convert to → PDF* (LibreOffice).

## Watermark
Puts a text such as **CONFIDENTIAL DOCUMENT** on the pages of a PDF.
- Text: six presets (*Confidential Document*, *Confidential*, *Draft*, *Copy*, *Internal Use Only*, *Do Not Copy*) or your own, up to 40 characters, Latin letters only (the built-in font).
- Layout: diagonal, straight, or repeated across the page. Colour: grey, red, blue. Strength: 10–60 %. Size: small, fit the page, large. Pages: all, or a range such as `1-3, 5`.
- A Word document is made a PDF first, then marked.
- Made with Ghostscript: the text is vector and see-through, the original words stay selectable. The original file is not changed; the copy is named `<name> (confidential document).pdf`.

## Stamp and signature
*Convert → Stamp* (or right-click a PDF → *Tools → PDF tools → Add stamp or signature…*). Click or drag on a preview of the page to place it.
- **Text stamp:** APPROVED, PAID, RECEIVED, REJECTED, DRAFT, VOID or your own text (up to 24 characters), in a frame, green / red / blue / black. Drawn as vector.
- **Signature picture:** a PNG, JPG or WebP of your signature. The white (and transparent) background is left out, so the words under it stay readable. Dark ink on white paper works best.
- Size small / medium / large; pages: the last (the default), the first, all, or a range such as `1-3, 5`. The result is `<name> (approved).pdf` or `<name> (signed).pdf`.
- It is a picture on the page, **not a digital signature**: it proves nothing by itself and anyone with an editor can remove it.

## Passwords
*Convert → Protect* (or right-click a PDF → *Tools → PDF tools → Lock or unlock with a password…*).
- **Lock:** a password to open the PDF (4–32 letters, digits and symbols, no spaces or accents; typed twice), and whether it may be printed, copied from or changed once open. The copy is named `<name> (locked).pdf`.
- **Open:** type the password of a locked PDF and get `<name> (unlocked).pdf` without a password. A wrong password leaves nothing behind. Notch cannot open a PDF whose password you do not know.
- Passwords are given to Ghostscript in a private file (readable only by your account) that is deleted at once; they are never written on a command line, in a log, in a name or in a message, and Notch does not keep them.
- A locked PDF cannot be converted, watermarked, stamped or summarized until it is opened.

## Limits
- The watermark cannot be taken off the copy: keep the original.
- Ghostscript writes the PDF again: interactive forms, links and bookmarks may not survive. A PDF protected with a password is refused.
- Ghostscript writes the 128-bit RC4 encryption only (it has no AES). That keeps a file from casual readers; it is not meant to stop a determined attacker, and some programs ignore the print and copy limits. For stronger protection use an encrypted archive or disk.
- A watermark is a visible mark, not protection: anyone with an editor can remove it. Use a password or encryption when the contents must be kept from others.
- Needs `ghostscript` and `poppler-utils`; Word and OCR conversions need LibreOffice (recommended by the `.deb`).

## Taking the background off a picture
*Documents → Convert → choose a picture → Remove background*, or right-click a picture → *Tools → Images → Remove background…*. A model finds the main subject and makes everything else transparent; the result is a PNG on your shelf named `<name> (no background).png`. The picture is not changed.
- Two models to choose from: **Fast** (U2-Netp, about 5 MB, about 0.5 GB of memory, good for signatures, logos and simple pictures) and **Best quality** (IS-Net, about 176 MB, about 3 GB of memory; not tried yet on real photos). The best one that is installed is used.
- The model and the program that runs it (ONNX Runtime, about 11 MB on Linux) are downloaded only when you press Download in *Settings → AI → AI models → Remove backgrounds*, are checked, and run on this computer only.
- In *Stamp → Picture*, *Remove the background with AI* cuts a photo of a signature out of its paper first.
- A picture with a transparent background (PNG) is used as it is when stamping; white inside a logo stays.
- This version of Notch contains everything except the piece that talks to ONNX Runtime (`src/services/OnnxRuntime.cpp`), which needs the library's C header. Until that is added the card says the model cannot be run yet.
