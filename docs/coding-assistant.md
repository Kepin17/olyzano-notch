# Coding Assistant — Dev → Agent

The desktop app includes a coding assistant inside **Dev → Agent**, alongside API, SSH, and Database. It is separate from Notch's offline voice assistant. The browser version does not include workspace or command access.

## Setup

1. Open **Dev → Agent**, then choose a workspace folder.
2. Choose Xiaomi MiMo, DeepSeek, Claude or OpenAI and click **Configure…**.
3. Pick a model and paste your API key. The endpoint, protocol and sign-in style of these providers are filled in for you; **Advanced** shows them if you need to change them. Use **Add provider → Custom endpoint** for a separate profile, including subscription endpoints or local OpenAI-compatible servers (those show every field).
4. Save the profile, choose an approval mode, and describe your task. Use **Ctrl+Enter** or **Send**. **Stop** cancels the active request/command and rejects pending operations.

Official references (consulted November 2026):

- [MiMo overview](https://mimo.mi.com/docs/en-US/quick-start/summary/welcome) and [first API call](https://mimo.mi.com/docs/en-US/quick-start/summary/first-api-call).
- [DeepSeek API](https://api-docs.deepseek.com/).

The MiMo preset uses `https://api.xiaomimimo.com/v1`, `mimo-v2.6-pro`, and the `api-key` header. Its token limit uses `max_completion_tokens`. The DeepSeek preset uses `https://api.deepseek.com`, `deepseek-flash`, and bearer authentication. Availability, subscriptions, costs, and rate limits are determined by the provider, not Notch.

## Approval modes

| Mode | Inspect files | Modify files | Shell commands |
| --- | --- | --- | --- |
| Plan | Automatic | Blocked by the backend | Blocked by the backend |
| Manual | Automatic | Review diff, then **Apply** or **Reject** | Review command, then **Run** or **Reject** |
| Auto | Automatic | Automatic | Automatic |

Auto requires an explicit warning confirmation. Saved sessions never restore Auto consent after an application restart or when switching sessions. Finish or stop a task before changing workspace or provider; mode, model and effort can change at any time (see below). Switching Dev tabs does not stop backend work.

Plan is read-only **for the workspace**: conversation history and settings are still saved locally. It uses only Notch's built-in listing, literal text search, and numbered file reading tools, not shell commands disguised as inspections.

## Provider configuration

Profiles separate protocol from provider identity. OpenAI-compatible Chat Completions and Anthropic Messages adapters are included. Base URL can be a versioned API root or complete chat/messages URL. New models on either protocol require only configuration; a fundamentally different API protocol still requires an adapter.

Editable settings include name, base URL, model ID, protocol, authentication style (`Bearer`, `api-key`, `x-api-key`, or `none`), temperature, token limit, token-limit parameter name (`max_tokens` / `max_completion_tokens`). Custom headers and extra request-body JSON are still honored when present in `dev/agent.json`, but have no editor. A temperature of `-1` omits that parameter for models which do not accept it. Extra body parameters can configure capabilities such as `top_p`, `thinking`, or `reasoning_effort`; they cannot override model, conversation, system instructions, streaming, or tool schemas. Provider-specific parameter compatibility remains your responsibility. Basic Anthropic text/tool streaming is supported; extended-thinking signature replay is not implemented.

HTTPS is required except for loopback HTTP endpoints. Redirects are not followed. A pasted key is trimmed of surrounding whitespace. Leave the API key blank when editing to preserve it; use **Clear saved API key on save** to remove it. Model IDs are not restricted to the preset list.

## Privacy and execution boundaries

- API keys use Notch's existing owner-only `SecretsStore`. This is **plaintext storage with file permissions, not an encrypted OS keychain**. Keys are not returned in provider metadata or placed on command-line arguments. Custom headers/body are saved as configuration too; avoid embedding secrets in them.
- Prompts, inspected source code, command output, and conversation history are sent directly to the selected endpoint. Provider billing and data-retention policies apply. Changing providers may send the existing conversation to the newly selected provider; start a new task if you want a clean conversation.
- Sessions and provider configuration live under Notch's config root at `dev/agent.json`; keys live at `dev/secrets.json`. Conversations may contain sensitive source and tool output. Delete a session to remove its locally retained conversation.
- File tools reject traversal and all symlinks and operate only inside the selected workspace. `.git`, `node_modules`, `.ssh`, `.env`/`.env.*`, and `secrets.json` paths are excluded. This is not comprehensive secret detection; other files may contain credentials. Do not select an untrusted or sensitive workspace indiscriminately.
- **Shell commands are not sandboxed.** They run as your desktop user, can access files outside the workspace, inherit your environment, and may use the network. Auto grants this authority without a per-command prompt. Manual commands require approval, including build/test commands.
- Stop and command timeout terminate the shell's process group on Unix and use process-tree termination on Windows. Already completed changes are not undone. Deliberately detached processes may escape cancellation; this is not a security sandbox.
- File writes are atomic and refuse to overwrite a file changed since the proposal was prepared. No automatic Git commits, resets, or rollback occur.

## Bounds and limitations

File reads/writes are limited to 256 KiB; reads show at most 500 lines per call. Search is case-insensitive literal text, capped at 2,000 entries, 100 matches, and approximately 1.5 seconds. Search coverage is explicitly bounded, not proof that no other matches exist. Network responses are capped at 2 MiB and outgoing conversation payloads at 8 MiB. Agent runs stop after 20 model/tool rounds; commands time out after two minutes.

There is no terminal emulator (a `!` command runs once and shows its output), shelf attachment UI, or browser automation. Messages and code are selectable plain text; diffs are review previews, not downloadable patches. Live MiMo/DeepSeek calls require your key and have not been exercised by the offline tests.

## Development validation

```sh
cmake -S . -B build -DNOTCH_VOICE=OFF
cmake --build build --target notch tst_core tst_codingagent -j4
ctest --test-dir build --output-on-failure
```

`tst_codingagent` uses an isolated configuration directory and a loopback HTTP fixture; no provider keys or paid requests are needed. The app's `--ui-check` includes the Agent tab and provider editor.

## Working principles

Every request carries four short principles for the model: think before coding (state assumptions, ask when unclear), simplicity first, surgical changes, and goal-driven execution (checkable goals, verify before saying done). They are adapted from the [karpathy-guidelines skill](https://github.com/multica-ai/andrej-karpathy-skills) (MIT) and paraphrased; see the README credits. The mode (Plan / Manual / Auto) is chosen from the menu on the prompt box.

## Plan Mode: discuss, plan, then decide

Ask for a plan ("make a plan for…", "buatkan rencana…", "let's discuss…") and the agent switches to **Plan** by itself. The switch only ever lowers what the agent may do. In Plan Mode it reads the project, talks the task through with you, and when it has a concrete plan it saves it with a `write_plan` tool. The plan is stored as `plan.md` under Notch's config folder (`dev/plans/<task>/plan.md`), never in your workspace.

A **Plan ready** card appears above the prompt box:

- **Read plan.md** opens the plan (Markdown) in the card; **Hide plan.md** brings the conversation back.
- **Revise**: type what should change; the agent writes a new plan.
- **Reject**: drops the plan and tells the agent not to implement it. Nothing is changed.
- **Accept · Manual**: leaves Plan Mode and starts the work, with approval for every edit and command.
- **Accept · Auto**: the same, but edits and commands run without asking, after the usual Auto warning.

You can also just type a comment in the chat. While a plan waits, the agent gets the plan text and these rules: a request for a change revises the plan (it calls `write_plan` again), a question or a doubt is discussed first, and if it cannot tell which, it asks "discuss first, or revise the plan?". The card stays until you decide. **Revise** is the explicit version: what you type next is sent as "Please revise the plan: …".

After a decision the card stays as "Plan accepted" or "Plan rejected" with **Read plan.md**, so the plan can be read again at any time. A rejected plan has **Reopen**, which puts it back to waiting.

On accept the plan itself is sent to the agent with the instruction to implement it step by step. Deleting a task deletes its plan file.

## The prompt box: @ files, ! commands, / commands

- **`@file`**: type `@` after a space and pick a file from the list (Up/Down, Tab or Enter). It becomes a chip above the box and its content goes with your message (up to 8 files, 100 KB in all, text files under 256 KiB; `.git`, `.env`, `node_modules` and anything outside the workspace are refused). You can also type `@src/main.ts` in full. The chat shows the files as badges, not as pasted text.
- **`!command`**: start the box with `!` and the badge **SHELL** appears. Enter runs the command in the workspace at once, in every mode and without an approval step, because you typed it. The output streams into the chat with a status badge (`exit 0` green, anything else red) and the agent sees it with your next message. Stop kills it. Like the agent's own commands it is not sandboxed. A toast tells you when it ends if the panel is closed.
- **`/command`**: `/fork` (copy this task and carry on in the copy), `/model [id]`, `/effort [level]`, `/mode [plan|manual|auto]` (without an argument they open the menu; Auto still asks first), `/plan [task]`, `/compact [focus]`, `/new`, `/clear` (asks first), `/rename <title>`, `/workspace`, `/copy` (last answer), `/export` (Markdown file) and `/help`.

## To-do list

For a task with three or more steps the agent calls `todo_write` first and keeps the list up to date. The **To-do** card above the prompt box shows `3 / 7`, a progress bar and the items (the current one highlighted, finished ones struck through); click its header to fold it. The list is sent along with every request, so it survives a compact. It is not used in Plan Mode, which has `write_plan`.

## Context meter and compact

The bar at the bottom of the prompt box shows how much of the model's context window the conversation holds. The number comes from the provider's own usage report when it sends one (Claude always; OpenAI and DeepSeek when asked); otherwise it is an estimate from the size. The window itself is guessed from the model name; set the real number under **Advanced → Context window** if it is wrong.

At 85% the agent summarizes the older messages before its next request (a separate request without tools), keeps the last few messages, and shows a **Compacted** divider. The old messages stay readable but are no longer sent; the summary, the plan and the to-do list are. `/compact [focus]` does it on demand. If the summary cannot be made you get a toast and the request goes on without it.

## Folders, tasks and the folder picker

The sidebar groups tasks by workspace folder (like projects). A folder's **+** starts a task in it, so nothing is picked again; the top **+** starts one in the folder you are in, and the folder button adds another folder. Double-click a task or use the pencil to rename it; the pencil on a folder gives it a display name; click a folder to fold it. An empty folder can be removed from the list (files are never touched). A task made before any folder was chosen takes the current folder instead of blanking it.

Folders are chosen in a picker inside the notch, with a path box, Up, Home and **New folder**. System file dialogs open as separate windows that the notch covers, so none is used (export saves into a folder chosen the same way).

## A custom AI

If your model or server is not in the list: type its id in the model menu ("Not in the list → Use … as the model"), or add a custom AI with **+** next to Configure: a name, the address (any OpenAI-compatible server such as Ollama, LM Studio, vLLM or OpenRouter, or an Anthropic-style endpoint), the model id and a key if it needs one. Sign-in style, temperature, token limits and the context window are under Advanced.

## Interrupting

While the agent works, **Send** turns into a red **Stop**, and **Esc** does the same (an open menu or dialog keeps its own Esc). Streaming, a running command and a pending summary stop at once; what the agent had already written stays in the chat, and a thin "Interrupted" line marks the spot (it is never sent to the model).

## Notch notifications

When Dev is not open in front of you, the notch tells you what the agent needs, as a toast: **Agent finished** (with the first words of its answer), **Agent wants to run a command / edit a file** (waiting for Apply or Run), **Plan ready**, **Agent error** (the provider's message), and **Command finished / failed** for a `!` command. Stopping the agent yourself is not announced, and a plan that is waiting for you is announced once, not twice. Toasts follow the notch's usual rules, so gaming mode holds them back.

## Retrying a provider problem

If the provider has a problem that may pass, the same request is sent again, at most **10 times**, waiting 1, 2, 4, 8, 16 and then 30 seconds (a `Retry-After` from the server is respected, up to 2 minutes). The line above the prompt box says "Provider problem (HTTP 503). Retrying 3/10 in 8 s…", and **Stop** or Esc cancels the wait. Retried: HTTP 408, 429 and 5xx (including Anthropic's 529 "overloaded"), a dropped or refused connection, a cut stream, and an error message in the stream that says overloaded, rate limit, timeout or "try again". Not retried: a wrong key (401/403), a bad request (400/422), a full quota, or an answer cut by the token limit; those need you. After the tenth failure the error stays with "gave up after 10 retries" (and the notch tells you). Nothing from the failed tries is kept in the chat; each prompt starts the count again. A request that fails halfway can still be billed by some providers.

## Changing mode, model and effort while it works

The mode, model and effort chips stay usable while the agent works. The request already on its way keeps what it was sent with; the next step of the same task uses the new setting, and a toast says so. Switching to **Plan** drops the edits and commands that were waiting for Apply or Run (the agent is told it was a mode change, so it plans instead of retrying). Switching to **Auto** (after its warning) takes what was waiting. A retry that is waiting also uses the model you pick.

## When the model hits its token limit

An answer that is cut by the token limit is no longer thrown away. If the cut came in the middle of tool calls (or before any text, as happens with reasoning models whose thinking counts toward the limit), nothing is run and the same step is asked again with up to twice and then four times the configured **Max tokens** (at most 65,536), and the bigger limit is kept for the rest of the task. If the provider refuses a bigger limit (HTTP 400 about tokens), the agent goes back to the last limit it accepted. If a plain text answer is cut, the part written so far is kept and the model is asked to "Continue" (up to four times per prompt, then a note says so). The built-in providers now start at 8192 tokens. When the limit still is not enough the message says which number it reached and points to Advanced → Max tokens. HTTP errors now show the provider's own explanation (for example "HTTP 404: Model not found") instead of a generic one.

## Speed

The chat is a virtual list: only the messages in view are built, a message is redrawn only when it changed, the streaming text is drawn as plain text (and about sixteen times a second) until the answer is complete, and the chat, the context estimate and the task list are cached between changes.

## Long command output

The chat shows the last 150 lines (12,000 characters) of a command's output and says how many lines are hidden; **Copy all output** copies the whole of it. Laying out tens of thousands of lines at every update was what froze the panel. An agent's own command result goes to the model in full (up to 64,000 characters); for a `!` command you ran, the model gets the last 8,000 characters.

## One line per action

By default the chat shows what the agent did as one line each: `Read src/loans/service.ts  lines 1–120`, `Searched "lateFee"  2 hits`, `Edited src/late-fee.ts  +12 −3`, `Ran bun test  exit 1`, `Wrote plan`, `To-do updated 3/7`. Click a line to see its diff, command or output; those are only built while open, so a long task stays light. A change or command that waits for Apply / Run opens by itself (with its buttons on the line) so you see it before approving, and folds into a line once decided. The tool results that only the model needs are left out of the chat, and what the model thought is a line ("Thought · 100 words") that opens on a click.

The eye button in the sidebar header, or `/details [headings|full]`, switches to **Full**: every card open as before, tool results included. The choice is remembered.

## What changed, at the end

When a run is over (nothing streaming, no approval waiting) and it changed or ran something, a **What changed** card closes the chat: the files created or edited with `+added −removed` (a file edited several times is one line), how many commands ran and which failed, how many proposals you declined, and the state of the to-do list. It is built from what actually happened, not from what the model says, and a run that only answered a question has none. The agent is also told to end such a task with a short conclusion in plain words (what and why, how it was checked, what is left); a plain question just gets its answer.

