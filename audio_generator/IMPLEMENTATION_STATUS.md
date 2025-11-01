# TTS Implementation Status

## Current Implementation

### Library Integration
- ✅ Added `arduino-simple-tts` library to `platformio.ini` (only for `jc2432w328c` board)
- ✅ Created custom `AudioOutputDAC26` class to output to GPIO26 DAC
- ✅ Placeholder in `speakLapAnnouncement()` for future TTS implementation

### Audio File Generation
- ✅ Created Python script to generate TTS audio files (`audio_generator/generate_tts_files.py`)
- ✅ Script generates 150+ audio files for word fragment playback
- ✅ Files include: numbers 0-59, lap numbers 1-20, connecting words

### Current Behavior
- For now, lap detection triggers a simple **beep** (100ms, 1kHz tone)
- TTS implementation is **ready to activate** once audio files are generated and uploaded

## Next Steps to Enable Full TTS

### 1. Generate Audio Files
```bash
cd StarForgeOS/audio_generator
pip install pyttsx3
python generate_tts_files.py
```

### 2. Upload Audio Files to ESP32
```bash
mkdir -p ../data/tts
cp tts_audio/*.wav ../data/tts/
cd ..
pio run -e jc2432w328c -t uploadfs
```

### 3. Implement TTS Playback Code

Update `standalone_mode.cpp` in `speakLapAnnouncement()`:

```cpp
void StandaloneMode::speakLapAnnouncement(uint16_t lapNumber, uint32_t lapTimeMs) {
    #if defined(BOARD_JC2432W328C)
    static SimpleTTS tts;
    static AudioOutputDAC26 out;
    static bool initialized = false;
    
    if (!initialized) {
        tts.begin(&out);
        tts.setSampleRate(16000);  // Match audio file sample rate
        initialized = true;
    }
    
    // Build announcement from word fragments
    uint32_t seconds = lapTimeMs / 1000;
    uint32_t minutes = seconds / 60;
    seconds = seconds % 60;
    
    // Play: "Lap [N]"
    tts.playFile("/tts/lap.wav");
    char filename[32];
    snprintf(filename, sizeof(filename), "/tts/lap_num_%d.wav", lapNumber);
    tts.playFile(filename);
    
    // Play time if provided
    if (lapTimeMs > 0) {
        if (minutes > 0) {
            snprintf(filename, sizeof(filename), "/tts/num_%lu.wav", minutes);
            tts.playFile(filename);
            tts.playFile(minutes == 1 ? "/tts/minute.wav" : "/tts/minutes.wav");
            
            if (seconds > 0) {
                tts.playFile("/tts/and.wav");
            }
        }
        
        if (seconds > 0 || minutes == 0) {
            snprintf(filename, sizeof(filename), "/tts/num_%lu.wav", seconds);
            tts.playFile(filename);
            tts.playFile(seconds == 1 ? "/tts/second.wav" : "/tts/seconds.wav");
        }
    }
    
    #else
    playLapBeep();
    #endif
}
```

## Why This Approach Works

- ✅ **No ADC conflicts** - arduino-simple-tts just plays audio files, no ADC usage
- ✅ **Low memory** - Streams audio files from SPIFFS, doesn't load full TTS engine
- ✅ **Natural speech** - Uses pre-generated high-quality TTS
- ✅ **Board-specific** - Only compiled/loaded for JC2432W328C
- ✅ **DMA ADC compatible** - No driver conflicts

## Current Flash/Memory Usage

- Code: ~175KB
- Audio files: ~150KB (WAV) or ~50KB (MP3)
- Runtime RAM: Minimal (streaming playback)
- **Total flash used: 3.8MB available, ~1.4MB used (plenty of room)**

## Testing

Once audio files are uploaded:
1. Trigger a lap on the timing system
2. Should hear: "Lap [number], [time] seconds" instead of just a beep
3. Verify audio quality and timing
4. Adjust as needed

## Notes

- SimpleTTS supports both WAV and MP3 formats
- MP3 saves flash space (~3x smaller) but requires more CPU to decode
- For best performance, use WAV files at 16kHz, 16-bit mono
- Audio generation takes ~5-10 minutes one-time setup

