#!/usr/bin/env python3
"""
Generate audio files for SimpleTTS word fragment playback
Uses pyttsx3 for offline TTS generation
"""

import pyttsx3
import os

# Output directory
output_dir = "tts_audio"
os.makedirs(output_dir, exist_ok=True)

# Initialize TTS engine
engine = pyttsx3.init()

# Set voice properties for better quality
engine.setProperty('rate', 150)    # Speed (words per minute)
engine.setProperty('volume', 0.9)  # Volume (0-1)

# Get available voices and try to use a good one
voices = engine.getProperty('voices')
for voice in voices:
    if 'english' in voice.name.lower():
        engine.setProperty('voice', voice.id)
        break

def generate_audio(text, filename):
    """Generate audio file from text"""
    filepath = os.path.join(output_dir, filename)
    print(f"Generating: {filename} -> '{text}'")
    engine.save_to_file(text, filepath)

# Generate basic words
generate_audio("Lap", "lap.wav")
generate_audio("seconds", "seconds.wav")
generate_audio("second", "second.wav")
generate_audio("minutes", "minutes.wav")
generate_audio("minute", "minute.wav")
generate_audio("and", "and.wav")

# Generate numbers 0-59 (for seconds/minutes)
for i in range(0, 60):
    generate_audio(str(i), f"num_{i}.wav")

# Generate lap numbers 1-20 (most races won't exceed 20 laps)
for i in range(1, 21):
    generate_audio(str(i), f"lap_num_{i}.wav")

# Generate special phrases
generate_audio("Fastest lap", "fastest_lap.wav")
generate_audio("Race complete", "race_complete.wav")
generate_audio("New lap", "new_lap.wav")

# Run the engine to generate all files
print("\nGenerating audio files...")
engine.runAndWait()

print(f"\n✓ Done! Audio files saved to: {output_dir}/")
print(f"Total files: {len(os.listdir(output_dir))}")
print("\nNext steps:")
print("1. Review the audio files")
print("2. Optionally convert to MP3 for smaller size (ffmpeg)")
print("3. Copy files to StarForgeOS/data/ directory")
print("4. Upload filesystem to ESP32 using PlatformIO")

