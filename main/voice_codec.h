// main/voice_codec.h —— Push-to-talk audio codec (pure C, host-testable).
//
// The microphone runs at 16 kHz. Voice is downsampled to 8 kHz with a [1 2 1]/4
// low-pass and encoded as 4-bit IMA ADPCM: five seconds fit in 20000 bytes, so
// a whole utterance can be buffered on this no-PSRAM board while the connector
// connection is still being established.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define VOICE_RATE_HZ        8000
#define VOICE_MAX_SECONDS    5
#define VOICE_MAX_SAMPLES    (VOICE_RATE_HZ * VOICE_MAX_SECONDS)
#define VOICE_DATA_BYTES     (VOICE_MAX_SAMPLES / 2)
// Upload body: VOICE_DATA_BYTES of ADPCM (zero-padded) + uint32 LE sample count.
#define VOICE_BODY_BYTES     (VOICE_DATA_BYTES + 4)
#define VOICE_MIN_SAMPLES    (VOICE_RATE_HZ * 3 / 10)   // 0.3 s
#define VOICE_CONTENT_TYPE   "audio/ima-adpcm;rate=8000;channels=1"

typedef struct {
    int16_t predictor;
    uint8_t index;
    int16_t prev;        // last 16 kHz input sample, for the decimation filter
} voice_enc_t;

void voice_enc_init(voice_enc_t *e);
// Encode n (even) 16 kHz samples into n/4 bytes; low nibble first. Returns bytes written.
size_t voice_enc_chunk(voice_enc_t *e, const int16_t *pcm16k, size_t n, uint8_t *out);

typedef struct {
    int16_t predictor;
    uint8_t index;
} voice_dec_t;
void voice_dec_init(voice_dec_t *d);
// Decode `samples` 8 kHz samples from ADPCM bytes.
void voice_dec(voice_dec_t *d, const uint8_t *in, size_t samples, int16_t *out);
