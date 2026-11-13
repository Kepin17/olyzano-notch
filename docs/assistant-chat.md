# Assistant (talk to Notch)

The **Assistant** page (the *Assistant* button of the action bar) is a conversation with the language model of this computer. Nothing is sent anywhere. It needs the notes model (*Settings → AI*; Qwen3 4B is enough, the Larger or Largest one answers better). A simple command such as "open Firefox" or "timer 5 minutes" works without it.

## What to ask
- **About your tasks:** "What is due this week?"
- **About your recordings:** "Summarize my last recording", "What did we decide about the budget in the meeting?" (answers cite the recording as [1]; a click opens it).
- **About your files:** "Where is my electricity bill?" goes to Ask your files, with its sources (file and page, recording and minute).
- **To do something:** "Add a task: send the report tomorrow 9", "Write a note: …", "Timer 10 minutes", "Open Firefox".
- **Anything else:** talk, ask, rewrite a text.

## Nothing happens behind your back
- What changes something (a task, a note, a timer) is shown as a card with **Add** and **Cancel**. Only Add does it.
- Opening an app and reading are done at once. Nothing is ever deleted.
- What is written inside your files, notes or recordings can never give the assistant an order: the step that chooses what to do sees only what you said, and the step that reads your data has no tools at all, so all it can do is write words.
- The talk is kept on this computer (`chat.json`) so that it is there next time; **Clear** removes it. It holds your messages and the replies, not the contents of your files.

## Seeing it work
You never wait in front of a blank screen. While Notch works, a small card lists the steps ("Understanding your message ✓ → Reading your tasks ✓ → Writing the answer"), the step in progress pulses, and a counter shows how many seconds it has taken. The answer then appears word by word as it is written, with a blinking cursor; **Stop** ends it at once and keeps what had been written.

## Good to know
The model that chooses and answers is a small one: read what it says against your data, especially dates and numbers. Asking about your files loads a second model run for a moment, so a first question about files takes longer.
