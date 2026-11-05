// IMA ADPCM (8 kHz, mono, low nibble first) from the device's push-to-talk
// upload, converted to the 16 kHz s16le PCM the ASR pipeline expects.
// Mirrors main/voice_codec.c.
const STEP = [
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
  253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
  3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
  12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
];
const INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

export const VOICE_DATA_BYTES = 20000;
export const VOICE_BODY_BYTES = VOICE_DATA_BYTES + 4;
export const VOICE_MIN_SAMPLES = 2400;
export const VOICE_MAX_SAMPLES = VOICE_DATA_BYTES * 2;
export const VOICE_CONTENT_TYPE = 'audio/ima-adpcm;rate=8000;channels=1';

export function decodeAdpcm(data, samples) {
  const out = new Int16Array(samples);
  let predictor = 0, index = 0;
  for (let i = 0; i < samples; i++) {
    const nib = i & 1 ? data[i >> 1] >> 4 : data[i >> 1] & 15;
    const step = STEP[index];
    let diff = step >> 3;
    if (nib & 4) diff += step;
    if (nib & 2) diff += step >> 1;
    if (nib & 1) diff += step >> 2;
    predictor = Math.max(-32768, Math.min(32767, predictor + (nib & 8 ? -diff : diff)));
    index = Math.max(0, Math.min(88, index + INDEX[nib]));
    out[i] = predictor;
  }
  return out;
}

// Linear 2x upsampling to 16 kHz, returned as s16le bytes.
export function upsample16k(pcm8k) {
  const out = Buffer.alloc(pcm8k.length * 4);
  for (let i = 0; i < pcm8k.length; i++) {
    const a = pcm8k[i], b = i + 1 < pcm8k.length ? pcm8k[i + 1] : a;
    out.writeInt16LE(a, i * 4);
    out.writeInt16LE((a + b) >> 1, i * 4 + 2);
  }
  return out;
}

// Body: VOICE_DATA_BYTES of ADPCM (zero padded) + uint32 LE sample count.
export function voiceBodyToPcm(body) {
  if (body.length !== VOICE_BODY_BYTES) return null;
  const samples = body.readUInt32LE(VOICE_DATA_BYTES);
  if (samples < VOICE_MIN_SAMPLES || samples > VOICE_MAX_SAMPLES) return null;
  return upsample16k(decodeAdpcm(body.subarray(0, VOICE_DATA_BYTES), samples));
}
