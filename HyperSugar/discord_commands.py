import asyncio
import os
import subprocess
import tempfile
from pathlib import Path

import discord

from .romaji import convert_sheet_romaji, romaji_to_hiragana


PRESETS = ("vibri", "mojibri", "mojiko", "osorezan")
BASE_DIR = Path(__file__).resolve().parent
ASSET_DIR = BASE_DIR / "mojib"


class TtsError(RuntimeError):
    pass


def _native_tts_path() -> Path:
    override = os.environ.get("VIBRIBOT_MOJIB_TTS")
    if override:
        path = Path(override)
    else:
        path = BASE_DIR / "bin" / ("mojib_tts.exe" if os.name == "nt" else "mojib_tts")
    if not path.is_file():
        raise TtsError(
            f"TTS executable was not found at '{path}' !!"
            "Build HyperSugar/native first."
        )
    return path


def _run_native(mode: str, output: Path, *, preset: str, bpm: int,
                time_den: int | None = None, notesheet: str | None = None,
                source_note: str | None = None, japanese_phrase: str | None = None,
                romaji: bool = False) -> None:
    command = [
        str(_native_tts_path()),
        mode,
        "--assets", str(ASSET_DIR),
        "--preset", preset,
        "--bpm", str(bpm),
        "--out", str(output),
    ]
    input_path = output.with_suffix(".txt")
    try:
        if mode == "sheet":
            sheet_text = notesheet or ""
            if romaji:
                try:
                    sheet_text = convert_sheet_romaji(sheet_text)
                except ValueError as error:
                    raise TtsError(str(error)) from error
            input_path.write_text(sheet_text, encoding="utf-8", newline="")
            command += ["--time-den", str(time_den), "--sheet-file", str(input_path)]
        elif mode == "quick":
            phrase = japanese_phrase or ""
            if romaji:
                phrase = romaji_to_hiragana(phrase)
            input_path.write_text(phrase, encoding="utf-8", newline="")
            command += ["--note", source_note or "", "--text-file", str(input_path)]
        else:
            raise TtsError(f"Unknown native TTS mode '{mode}'.")

        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120,
            check=False,
        )
    except OSError as error:
        raise TtsError(f"Could not start TTS: {error}") from error
    except subprocess.TimeoutExpired as error:
        raise TtsError("TTS timed out.") from error
    finally:
        input_path.unlink(missing_ok=True)

    if result.returncode != 0:
        # Keep subprocess output as bytes. I had issues with python not liking my console`s encoding
        stderr = result.stderr.decode("utf-8", errors="replace").strip()
        stdout = result.stdout.decode("utf-8", errors="replace").strip()
        message = stderr or stdout or "TTS failed."
        if message.lower().startswith("error:"):
            message = message[6:].strip()
        raise TtsError(message)
    if not output.is_file() or output.stat().st_size <= 44:
        raise TtsError("TTS did not produce a valid WAV file !!")


TTS_INFO = """**HyperVoice TTS**

**Voices**
`vibri`, `mojibri`, `mojiko`, and `osorezan` use their original settings.

**Text**
Hiragana and katakana are supported. Romaji conversion is **on by default** for `/tts`, `/msheet`, and `/ttsquick`.
Set `romaji:false` to use the ASCII letter-spelling instead.
Raw VRS symbols can be written in brackets, e.g. `[MI]`, `[Q]`.

**Music sheets**
`/tts` and `/msheet` use:
`time_den 4` = quarter-note unit `8` = eighth `16` = sixteenth.

```text
F4: な(2.5)
F4: ん(1.5)
A4-20~A4~A4: な(2)
C5-15~C5~C5: ら(6)
C5: あん(1)
rest(3)
```

**Pitch**
`F4:` sets the pitch for following lyrics.
You can also attach it directly: `C4:su(2)`.
Use cents like `A4-40` or `C5+25`.

You can algo pitch bend:
`>` = linear bend · `~` = smooth bend.
e.g.
`C5>C6 ka(10){vib=35,5.0}`

**Timing**
`(length)` is measured in `time_den` units, so `su(2.5)` lasts 2.5 units.
Use `rest(length)` for silence.

**Vibrato**
Add it to an individual note:
`{vib}` → 30 cents, 5.5 Hz, 120 ms fade-in
`{vib=35,5.0}` → depth/rate
`{vib=40,5.2,180}` → depth/rate/fade
Example: `A4~C5: a(8){vib=40,5.2,180}`

The fade smoothly grows the vibrato depth.

**Quick TTS**
`/ttsquick` gives every analyzed mora one eighth note at the chosen BPM and pitch.
`source_note` accepts `C5`, `F#4`, `A4-25`, etc."""


async def send_tts_file(interaction, render_call):
    await interaction.response.defer(thinking=True)
    temp_path = Path(tempfile.gettempdir()) / f"vibri-{interaction.id}.wav"
    try:
        await asyncio.to_thread(render_call, temp_path)
        await interaction.followup.send(file=discord.File(temp_path, filename="vibri.wav"))
    except TtsError as error:
        await interaction.followup.send(f"Game over! {error}", ephemeral=True)
    except Exception as error:
        print(f"TTS failed: {error}")
        await interaction.followup.send(
            "Game over! Vibri could not synthesize that sheet.", ephemeral=True)
    finally:
        temp_path.unlink(missing_ok=True)


async def send_music_sheet(interaction, preset, bpm, time_den, notesheet, romaji=True):
    preset = preset.lower()
    if preset not in PRESETS:
        await interaction.response.send_message(
            f"Game over! Unknown preset. Use: {', '.join(PRESETS)}", ephemeral=True)
        return
    await send_tts_file(
        interaction,
        lambda output: _run_native(
            "sheet", output, preset=preset, bpm=bpm,
            time_den=time_den, notesheet=notesheet, romaji=romaji))


def register_tts_commands(tree):
    preset_choices = [
        discord.app_commands.Choice(name=name.title(), value=name) for name in PRESETS
    ]

    @tree.command(name="tts", description="Make the HyperVoice engine sing a music sheet")
    @discord.app_commands.choices(preset=preset_choices)
    @discord.app_commands.describe(
        preset="vibri, mojibri, mojiko, or osorezan",
        bpm="Tempo in beats per minute",
        time_den="Length unit: 4=quarter note, 8=eighth note, etc.",
        notesheet="Example: C4: su D4~F4: ki(2){vib=35,5.5,120}",
        romaji="Convert romaji lyrics such as su, kyo, gakkou to hiragana")
    async def tts(interaction: discord.Interaction, preset: str, bpm: int, time_den: int, notesheet: str, romaji: bool = True):
        await send_music_sheet(interaction, preset, bpm, time_den, notesheet, romaji)

    @tree.command(name="msheet", description="Make the HyperVoice engine sing a music sheet")
    @discord.app_commands.choices(preset=preset_choices)
    @discord.app_commands.describe(
        preset="vibri, mojibri, mojiko, or osorezan",
        bpm="Tempo in beats per minute",
        time_den="Length unit: 4=quarter note, 8=eighth note, etc.",
        notesheet="Example: C4: su D4~F4: ki(2){vib=35,5.5,120}",
        romaji="Convert romaji lyrics such as su, kyo, gakkou to hiragana")
    async def msheet(interaction: discord.Interaction, preset: str, bpm: int, time_den: int, notesheet: str, romaji: bool = True):
        await send_music_sheet(interaction, preset, bpm, time_den, notesheet, romaji)

    @tree.command(name="ttsquick", description="Make a HyperVoice preset say a phrase")
    @discord.app_commands.choices(preset=preset_choices)
    @discord.app_commands.describe(
        preset="vibri, mojibri, mojiko, or osorezan",
        bpm="Tempo in beats per minute",
        source_note="Pitch for every mora, such as C5, F#4, or A4-25",
        japanese_phrase="Japanese text or romaji such as sushi or gakkou",
        romaji="Convert romaji runs to hiragana before synthesis")
    async def ttsquick(
            interaction: discord.Interaction, preset: str, bpm: int,
            source_note: str, japanese_phrase: str, romaji: bool = True):
        preset = preset.lower()
        if preset not in PRESETS:
            await interaction.response.send_message(
                f"Game over! Unknown preset. Use: {', '.join(PRESETS)}", ephemeral=True)
            return
        await send_tts_file(
            interaction,
            lambda output: _run_native(
                "quick", output, preset=preset, bpm=bpm,
                source_note=source_note, japanese_phrase=japanese_phrase, romaji=romaji))

    @tree.command(name="ttsinfo", description="Explain VibriBot's native hypervoice TTS")
    async def ttsinfo(interaction: discord.Interaction):
        await interaction.response.send_message(TTS_INFO, ephemeral=True)
