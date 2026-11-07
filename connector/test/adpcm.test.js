import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeAdpcm, upsample16k, voiceBodyToPcm, VOICE_BODY_BYTES, VOICE_DATA_BYTES } from '../src/adpcm.js';

// Reference IMA encoder (same as main/voice_codec.c) to round-trip a tone.
function encode(samples) {
  const STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767];
  const INDEX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8];
  const out = Buffer.alloc(Math.ceil(samples.length / 2)); let pred = 0, index = 0;
  samples.forEach((s, i) => {
    const step = STEP[index]; let diff = s - pred, nib = 0;
    if (diff < 0) { nib = 8; diff = -diff; }
    if (diff >= step) { nib |= 4; diff -= step; }
    if (diff >= step >> 1) { nib |= 2; diff -= step >> 1; }
    if (diff >= step >> 2) nib |= 1;
    let d = step >> 3; if (nib & 4) d += step; if (nib & 2) d += step >> 1; if (nib & 1) d += step >> 2;
    pred = Math.max(-32768, Math.min(32767, pred + (nib & 8 ? -d : d)));
    index = Math.max(0, Math.min(88, index + INDEX[nib]));
    out[i >> 1] |= i & 1 ? nib << 4 : nib;
  });
  return out;
}

test('ADPCM decodes a tone close to the source', () => {
  const tone = Array.from({ length: 8000 }, (_, i) => Math.round(8000 * Math.sin(2 * Math.PI * 440 * i / 8000)));
  const decoded = decodeAdpcm(encode(tone), tone.length);
  let sig = 0, err = 0;
  for (let i = 400; i < tone.length; i++) { sig += tone[i] ** 2; err += (tone[i] - decoded[i]) ** 2; }
  assert.ok(10 * Math.log10(sig / err) > 20);
});

test('upsampling doubles the rate and interpolates', () => {
  const pcm = upsample16k(Int16Array.from([0, 100]));
  assert.equal(pcm.length, 8);
  assert.deepEqual([0, 2, 4, 6].map(o => pcm.readInt16LE(o)), [0, 50, 100, 100]);
});

test('voice body trailer bounds the utterance length', () => {
  const body = Buffer.alloc(VOICE_BODY_BYTES);
  body.writeUInt32LE(8000, VOICE_DATA_BYTES);
  assert.equal(voiceBodyToPcm(body).length, 8000 * 4);
  body.writeUInt32LE(100, VOICE_DATA_BYTES);
  assert.equal(voiceBodyToPcm(body), null);
  body.writeUInt32LE(40001, VOICE_DATA_BYTES);
  assert.equal(voiceBodyToPcm(body), null);
  assert.equal(voiceBodyToPcm(Buffer.alloc(10)), null);
});
