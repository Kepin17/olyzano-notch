# Third-party code in this folder

Pinned copies, from npm (via jsDelivr), so the app works offline. Each keeps its own licence header.

| File | Package | Licence |
|---|---|---|
| `pdf-lib.min.js` | pdf-lib 1.17.1 | MIT |
| `pdf.min.js`, `pdf.worker.min.js` | pdfjs-dist 3.11.174 (Mozilla PDF.js) | Apache-2.0 |
| `fflate.min.js` | fflate 0.8.2 | MIT |
| `qrcode.js` | qrcode-generator 1.4.4 | MIT |
| `jsQR.js` | jsQR 1.4.0 | Apache-2.0 |
| `heic2any.min.js` | heic2any 0.0.4 | MIT |
| `tesseract.min.js` | tesseract.js 5.1.1 | Apache-2.0 |

`ocr/worker.min.js` is tesseract.js 5.1.1's worker and `ocr/tesseract-core-*-lstm.wasm.js` are tesseract.js-core 5.1.1 (both Apache-2.0), served from here. Only the language data is fetched from the tesseract.js CDN the first time OCR is used (data, not code), and then kept by the browser.
