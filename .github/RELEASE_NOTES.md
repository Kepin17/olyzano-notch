## Olyzano Notch 0.10.0

The notch can stay on screen, shows what you are listening to, and the open panel is split into sections.

- **How it sits on the screen** (*Settings → General*): **Hidden** (a small black line at the top edge, as before), **Capsule** (a small black capsule always shown, like a Dynamic Island; point at it to open) or **Always open**. The capsule stays up whatever window is in front, a maximised browser included.
- **Music in the capsule.** While music plays the capsule shows the sound wave, the song and where it is (`1:23 / 5:37`); a click, or pointing at it, opens just the Music section. After a pause the song stays for two minutes, then the clock returns.
- **Sections.** A bar under the title, **Files · Music · Timer · Share**, shows one section at a time. **Music** is only the player (no drop area); **Add** opens a file picker and is part of Files. The ⋯ menu and Settings are there in every section.
- **Lyrics** (off by default, *Settings → General → Lyrics*). The Music page splits in two like Spotify's lyrics view: the player on the left, only the words on the right, the line being sung big and the list following the song. The capsule shows the line too. They come from lrclib.net, an open lyrics service: the artist, title, album and length of what plays are sent to it, nothing else, and only when this is on. Not every song is there.
- **Karaoke** (Linux). A mic button on the player plays your voice through the speakers or headphones next to the music. Use headphones.
- **Categories clean up after themselves.** A category with no file in it for 24 hours goes away (locked ones and ones a type rule files into stay; Notch's own, like Screenshots, come back when needed).
- A black edge marker instead of the grey one, a "now playing" capsule that no longer closes by itself, and a window wide enough for the split Music page.

### Downloads
- **Linux:** `notch_0.10.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.10.0_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.10.0-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).
