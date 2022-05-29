#ifndef SOUND_H
#define SOUND_H

void sound_ringbell();
void sound_play_tone(uint16_t frequency, uint16_t duration_ms, uint8_t volume, bool wait);
bool sound_playing();

void sound_init();

#endif
