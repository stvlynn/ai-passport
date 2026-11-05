import test from 'node:test';
import assert from 'node:assert/strict';
import { transcribe, wav, dispatchWebhook } from '../src/asr.js';

test('PCM is wrapped as mono 16kHz signed 16-bit WAV', () => {
  const output = wav(Buffer.alloc(160000));
  assert.equal(output.length, 160044);
  assert.equal(output.toString('ascii', 0, 4), 'RIFF');
  assert.equal(output.readUInt32LE(24), 16000);
  assert.equal(output.readUInt16LE(22), 1);
  assert.equal(output.readUInt16LE(34), 16);
  assert.equal(output.readUInt32LE(40), 160000);
});

test('real subprocess supports file transcript and placeholders, removes temporary audio', async () => {
  const code = 'const fs=require("fs"); const input=process.argv[1],output=process.argv[2]; if(fs.readFileSync(input).toString("ascii",0,4)!=="RIFF")process.exit(2); fs.writeFileSync(output,"hello Lynn");';
  const output = await transcribe(Buffer.alloc(160000), { command: process.execPath, args: ['-e', code, '{input}', '{output}'], output: 'file' }, new AbortController().signal);
  assert.equal(output, 'hello Lynn');
});

test('stdout ASR, failure, timeout, empty result and output bounds', async () => {
  const settings = code => ({ command: process.execPath, args: ['-e', code, '{input}'], output: 'stdout', timeoutMs: 1000 });
  const signal = new AbortController().signal;
  assert.equal(await transcribe(Buffer.alloc(16), settings('process.stdout.write("你好 Lynn")'), signal), '你好 Lynn');
  await assert.rejects(transcribe(Buffer.alloc(16), settings('process.exit(1)'), signal), /asr_failed/);
  await assert.rejects(transcribe(Buffer.alloc(16), { ...settings('setTimeout(()=>{},5000)'), timeoutMs: 50 }, signal), /asr_timeout/);
  await assert.rejects(transcribe(Buffer.alloc(16), settings(''), signal), /asr_invalid_transcript/);
  await assert.rejects(transcribe(Buffer.alloc(16), settings('process.stdout.write("a".repeat(70000))'), signal), /asr_output_limit/);
});

test('webhook sends Bearer JSON; accepted 200 does not become device text', async t => {
  const originalFetch = globalThis.fetch; t.after(() => { globalThis.fetch = originalFetch; });
  let call;
  globalThis.fetch = async (url, options) => { call = { url, ...options }; return new Response('accepted', { status: 200 }); };
  const config = { webhookURL: 'https://example.com/webhook', webhookKey: 'private', botId: 'lynn', botName: 'Lynn' };
  await dispatchWebhook(config, 'hello', 'request-test', new AbortController().signal);
  assert.equal(call.headers.Authorization, 'Bearer private');
  assert.equal(call.redirect, 'error');
  assert.deepEqual(JSON.parse(call.body), { text: 'hello', user_text: 'hello', request_id: 'request-test', bot_id: 'lynn', bot_name: 'Lynn' });
  globalThis.fetch = async () => new Response('private upstream detail', { status: 500 });
  await assert.rejects(dispatchWebhook(config, 'hello', 'request-test', new AbortController().signal), /^Error: webhook_failed$/);
});

test('webhook text carries context even for legacy routines reading only text', async t => {
  const originalFetch = globalThis.fetch; t.after(() => { globalThis.fetch = originalFetch; });
  let call;
  globalThis.fetch = async (_url, options) => { call = options; return new Response('', { status: 200 }); };
  const context = { conversation_id: 'stable', context: [{ user: '我叫小明', assistant: '你好小明' }], summary: '', memories: [] };
  await dispatchWebhook({ webhookURL: 'https://example.com', webhookKey: 'private', botId: 'lynn', botName: 'Lynn' }, '我叫什么', 'new-request', new AbortController().signal, context);
  const payload = JSON.parse(call.body);
  assert.equal(payload.user_text, '我叫什么');
  assert.ok(payload.text.includes('我叫小明') && payload.text.includes('你好小明') && payload.text.includes('new-request'));
  assert.equal(payload.conversation_id, 'stable');
  assert.deepEqual(payload.context, context.context);
});
