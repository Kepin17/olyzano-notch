// OCR with tesseract.js. The worker and the WebAssembly engine are our own files (vendor/ocr), so no code is loaded from a third party at run
// time; only the language data (plain data, not code) comes from the tesseract.js CDN the first time, then it stays cached in the browser.
const here = p => new URL(p, document.baseURI).href;
import { lazyScript } from "../util.js";
import { decodable } from "./image.js";
import { openPdf, renderPage } from "./pdf.js";

export const LANGS = [["eng", "English"], ["ind", "Indonesian"], ["jpn", "Japanese"], ["kor", "Korean"], ["chi_sim", "Chinese (simplified)"], ["spa", "Spanish"], ["fra", "French"], ["deu", "German"], ["por", "Portuguese"], ["ara", "Arabic"], ["rus", "Russian"]];

export async function recognize(file, lang, onProgress = () => {}) {
  await lazyScript("vendor/tesseract.min.js");
  const T = window.Tesseract;
  const worker = await T.createWorker(lang, 1, { workerPath: here("vendor/ocr/worker.min.js"), corePath: here("vendor/ocr/"), logger: m => { if (m.status === "recognizing text") onProgress(m.progress); else if (m.status) onProgress(null, m.status); } });
  try {
    let text = "";
    if (file.type === "application/pdf" || file.name.toLowerCase().endsWith(".pdf")) {
      const doc = await openPdf(file);
      for (let i = 1; i <= doc.numPages; i++) {
        const c = await renderPage(doc, i, 300 / 72);
        const r = await worker.recognize(c);
        text += (i > 1 ? "\n\n" : "") + r.data.text.trim();
        onProgress(i / doc.numPages);
      }
    } else {
      text = (await worker.recognize(await decodable(file))).data.text.trim();
    }
    return text;
  } finally { await worker.terminate(); }
}
