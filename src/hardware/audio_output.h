#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include "platform/hal.h"
#include "config/config.h"

#if ENABLE_AUDIO

// Audio output manager for beeps and TTS announcements
// Uses DAC for simple beep tones and word fragment TTS
class AudioOutput {
public:
    AudioOutput();

    // Initialize audio hardware (DAC)
    void begin();

    // Play a simple beep for lap detection
    void playLapBeep();

    // Speak lap announcement using word fragment TTS
    // NOTE: Only announces lap number and time - NO comparisons to previous laps
    // Skips leading zero time units (e.g., "3 minutes 5 seconds" not "0 hours 3 minutes")
    void speakLapAnnouncement(uint16_t lapNumber, uint32_t lapTimeMs);

private:
    // Note: BEEP_DURATION_MS is defined in config.h
};

#endif // ENABLE_AUDIO

#endif // AUDIO_OUTPUT_H
