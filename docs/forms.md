# Forms

*Documents → Forms* lets you ask people questions and collect their answers and files, like a small form builder. You share a link or a QR code on your WiFi; they fill it in on their phone or computer; the answers stay on this computer.

## Make one
1. Press a template (**Send me files**, **Contact**, **RSVP**) or **Blank**, then edit it.
2. Questions can be short or long text, a number, an email, a date, one choice, many choices, or files. Each has a label, an optional hint, and can be required. Mark one short-text question **It is the name**: notices then say "Budi answered …".
3. Set the limits: how many **answers** it takes (it closes by itself after the last), how many **people** (devices) may answer, how long the **link works**, and the size of **each file**.
4. **Share…** → pick the network, optionally a PIN (4–12 digits), **Start sharing**. Scan the code or copy the link.

## Layout, ending and dates

- **Layout**: *One page*, *Sections* (each **Section** you add starts a new slide) or *One per slide* (like Google Forms). Slides have a progress bar and Back / Next; Next checks only that slide, and answers are kept when going back.
- **When it is sent**: your own title, message and one optional button (a link such as a group invitation; only `http://` or `https://` is shown as a button).
- **Link works**: 1 hour, 1 day, 7 days, or **Dates…**: pick the first and last day on the calendar and the times. Before the start time visitors see "This form is not open yet" with a countdown. A range is at most 90 days. A server of your own needs the updated `server/` to hold a form until its start time.

## Rating, scale, signature and limits per choice

- **Rating**: stars from 1 up to 3, 5, 7 or 10 (keyboard: arrow keys). **Scale**: numbers on a line from 1 to 3–10, with words for the low and the high end ("Not at all", "Very much"). Both show on the Summary as a bar for each number with the average.
- **Signature**: the person draws it on the screen (mouse or finger) and it is kept as a small PNG picture (`signature.png`, up to 300 KB), shown as a thumbnail with the answer and kept in Files like any other file. It works on this WiFi and through your own server.
- **Limits per choice**: on a *Pick one* or *Pick many* question each choice can have a **Max** ("Saturday: 10"). When a choice has taken all it may it is switched off on the page ("Full") and the server turns away a late answer for it ("Saturday is full. Choose another."). Changing an answer does not count it against itself. Limits are counted on **this WiFi only**: through your own server the answers are sealed and cannot be counted, so a form with limits is not put on a server (remove the limits or share on this WiFi).

## Rules, summary and changing answers

- **Rules**: on a question or section, *More* → "Ask this only when…" shows it only for chosen answers of an earlier question with choices. On a *Pick one* question, "End the form when the answer is…" sends people straight to Send. A section that is not asked takes its questions with it.
- **Summary** (on the Answers screen): bars for each choice question, average, total, smallest and largest for numbers, and answers per day.
- **Files**: with "Keep the files in Files" (on for new forms) every file that comes in goes to the Files shelf by itself, in a category named like the form, named "<who sent it> - <file>". Pictures show as thumbnails on the Answers screen (tap one to see it large), the table (.csv) lists the names they have in Files, and **Report (.html)** makes one page with the answers and the pictures inside it, to open or send without the app.
- **One answer or many**: *Settings* → "How many answers from one person?". With "One each", a device that already answered is turned away (counted by network address, so people sharing one address count as one) and its browser shows "You already answered" (with "Change my answer" when changes are allowed). The default is as many as they like. Through your own server this needs the updated `server/` files.
- **Change their answer**: *Settings* → "People can change their answer". After sending, a person gets a personal link; opening it fills the form with what they sent and saves the changes to the same answer (it does not count as a new answer or a new person). Files stay unless new ones are added to that question. Through your own server this needs the updated `server/` files; the server then keeps a small sealed copy of each answer (it cannot read it) until the form ends.

## See what came in
**Answers** shows each answer as a card. **Table to Files (.csv)** puts the table on your shelf (opens in a spreadsheet; text that begins like a formula is written as text), **Files to Files** puts the files people sent on your shelf. Files are only kept; nothing opens them. A notice appears for each new answer, and when the form closes.

## Good to know
- The link works only while Olyzano Notch is open and the form is open; **Close** ends it, and it ends by itself when the answers or the time are used up. A form that was open when Notch quit is closed on the next start (make a new link with **Share**).
- It is plain HTTP on your own network, like *Share via QR → This WiFi*: not encrypted, so do not ask for secrets. A PIN keeps strangers on the same network out.
- A person is a device (its network address). Over the same WiFi each device has its own; others are turned away once the people allowed have started, and those who started can finish. 30 answers per device in 10 minutes at most.
- File names are cleaned (no folders), each file is held until its answer is sent, and a visitor who gives up halfway leaves nothing behind.
- A form that is open cannot be edited (people are filling it in): close it first.
- Planned next: the same through *My server* with the answers encrypted in the browser.

## Through your own server (My server)
With a server of your own (see `server/README.md`; update it to the version with forms), *Share…* has **This WiFi** and **My server**. Through the server the link works from anywhere and keeps working when Notch is closed:
- The form is sealed on this computer and the server holds it; the key is only in the link after the `#`. The page opens it in the browser, and the answers and files are **sealed in the browser** before they go up, so the server never sees them.
- Notch fetches the answers while it is open (every few seconds), opens them, checks them against the form (an answer that does not follow it is thrown away, and you get a notice), keeps them and tells the server to let them go.
- The same limits apply (answers, people, time, size); the server also keeps to its own (the largest answer is its file limit; the answers waiting to be fetched count in the member's space).
- **Close** deletes the form from the server. A form on the server is picked up again when Notch starts.
- Answers are limited by what a browser can seal in memory: for big files use *This WiFi*, or a lower limit per file.

## Searching the answers

The answers of your forms are in the search from the notch (Shift + Space): type a word someone wrote and the form comes up with the answer, and Enter opens its Answers screen. Turn it off in *Settings → Files → Include the answers of forms*.
