# TTS Audio File Generator

This directory contains scripts to generate audio files for the SimpleTTS word fragment playback system.

## Prerequisites

Install the required Python packages:

```bash
pip install pyttsx3
```

On Linux, you may also need espeak:
```bash
sudo apt-get install espeak
```

On Windows/Mac, pyttsx3 uses the built-in TTS engines.

## Generate Audio Files

Run the generator script:

```bash
python generate_tts_files.py
```

This will create a `tts_audio/` directory with all the required audio files:
- Basic words: "lap", "seconds", "minutes", etc.
- Numbers 0-59 for seconds/minutes
- Lap numbers 1-20
- Special phrases: "fastest lap", "race complete"

## Optional: Convert to MP3 (smaller size)

If you want smaller files, convert WAV to MP3 using ffmpeg:

```bash
cd tts_audio
for f in *.wav; do
  ffmpeg -i "$f" -codec:a libmp3lame -qscale:a 2 "${f%.wav}.mp3"
done
```

## Upload to ESP32

1. Copy the audio files to `StarForgeOS/data/tts/` directory:
   ```bash
   mkdir -p ../data/tts
   cp tts_audio/*.wav ../data/tts/
   # or if you converted to MP3:
   cp tts_audio/*.mp3 ../data/tts/
   ```

2. Upload the filesystem to ESP32:
   ```bash
   cd ..
   pio run -e jc2432w328c -t uploadfs
   ```

## File List

Generated files (~150KB total for WAV, ~50KB for MP3):

- `lap.wav` - "Lap"
- `num_0.wav` through `num_59.wav` - Numbers 0-59
- `lap_num_1.wav` through `lap_num_20.wav` - Lap numbers
- `seconds.wav`, `second.wav` - "seconds" / "second"
- `minutes.wav`, `minute.wav` - "minutes" / "minute"
- `and.wav` - "and"
- `fastest_lap.wav` - "Fastest lap"
- `race_complete.wav` - "Race complete"

## Example Usage

To say "Lap 5, 1 minute and 23 seconds":
- Play: `lap.wav` → `lap_num_5.wav` → `num_1.wav` → `minute.wav` → `and.wav` → `num_23.wav` → `seconds.wav`

