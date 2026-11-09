# Search, ask, and tasks from your files

Everything here runs on this computer. The index is a file of its own (`knowledge.db`, under Notch's data folder) that can be cleared and made again at any time.

## The index
Notch keeps an index of the words in your **PDFs**, **text files** and **recordings** (their transcripts), cut into passages of about 900 characters (a recording: about a minute of talk, with who spoke and when). It is kept up to date in the background: a little after start, every few minutes, and when a recording's notes are made. It waits while a recording is being processed or recorded. Files in the vault and in *Recently removed* are never indexed.

*Settings → AI → Search & Ask*: switch the index off, leave PDFs out, **Read scanned PDFs (OCR)** (off: slow), **Check now**, **Rebuild**, **Clear**, and the **find by meaning** model.

## Smart search (the ✦ button in Files)
Describe what you look for in a sentence ("kuitansi listrik bulan lalu") and press ✦ (or Ctrl+Enter in the search field). The language model turns the sentence into words and synonyms; the time you say ("last month", "bulan Oktober", "kemarin") is worked out by the program itself, never by the model (a model that invents "last month" would hide the right file). The shelf then shows only the files that match, as chips you can take away one by one. The time is a preference: files added then come first and alone, and when none was added then, everything that matches the words is shown with a note. Without the language model the words and the time of your sentence are used as they are.

## Ask your files
*Documents → Ask your files*, the **Ask** tab of a recording, and the same box scoped to the PDF you picked. Ask a question; the passages that fit are found (words, and meaning when the optional model is installed), and the model answers **only from them**:
- every claim is tied to a source number and a quote, and the quote must really be in that passage; one that is not is thrown away, and an answer left without a single supported quote is "I could not find this" (with the closest passages), never a guess;
- a number in the answer that is in none of the sources it cites is flagged ("check the numbers"), and a quote with a wrong number is shown as the passage's own sentence;
- the sources are listed under the answer: a file opens (the page is in the label), a recording opens at that minute;
- what the files say is data, never instructions.

Measured on this computer (Qwen3 4B, six PDFs and one recording made for the test): six of seven questions answered correctly with a valid citation, and a question that nothing answers ("flight to Bali") was not answered; 7–48 s per question on the processor (the first one starts the model).

## Find by meaning (optional)
*Qwen3 Embedding 0.6B* (639 MB, Apache-2.0, downloaded only when you press Download) gives each passage a vector. A search then joins two orders, the words (BM25) and the meaning (cosine of the vectors) by rank. It helps when the words differ: a sentence in Indonesian finding an English report. Without it only the words are used.

## Tasks from your files
- When a recording's notes are made, its **action items** are offered on the **Tasks** page (*Suggested*: title, deadline, "Recording · name · minute", the quote). Switch off in *Settings → AI → Search & Ask*.
- In *Documents*, **Find tasks** reads a PDF for things to do and deadlines. A task must come with a quote that is on the page, or it is not offered.
- Deadlines are understood in Indonesian and English ("Jumat, 14 November 2026 pukul 17.00", "besok jam 9", "Jumat minggu depan", "3 hari lagi").
- **Nothing is added until you press Add.** A task already on the list (same words), or one you dismissed, is not offered again.

## Not tested here
Your own documents and languages (only generated ones), collections of tens of thousands of passages, NVIDIA/Intel/Windows, and the search by meaning on real files.
