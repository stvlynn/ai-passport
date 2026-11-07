import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { createConnector, validateConfig } from '../src/server.js';
import { actions } from '../src/catalog.js';
import http from 'node:http';

async function fixture(t, dependencies = {}, overrides = {}) {
  const directory = mkdtempSync(join(tmpdir(), 'lynn-http-test-'));
  const config = { port: 0, publicHost: 'connector.example.com', botId: 'lynn-test', botName: 'Lynn', deviceToken: 'd'.repeat(40), toolToken: 't'.repeat(40), webhookURL: 'https://example.com/hook', webhookKey: 'test-key', stateDir: directory, asr: { command: process.execPath, args: ['{input}'] }, ...overrides };
  const logs = []; const connector = createConnector(config, { transcribe: async () => 'hello', dispatchWebhook: async () => {}, logger: entry => logs.push(entry), ...dependencies });
  const address = await connector.listen();
  t.after(async () => { await connector.close(); rmSync(directory, { recursive: true, force: true }); });
  const base = `http://127.0.0.1:${address.port}`;
  const request = (path, options = {}) => fetch(base + path, options);
  const device = { Authorization: `Bearer ${config.deviceToken}` };
  const rpc = (method, params, token = config.toolToken, extra = {}) => request('/mcp', { method: 'POST', headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream', ...extra }, body: JSON.stringify({ jsonrpc: '2.0', id: 7, method, ...(params ? { params } : {}) }) });
  const voice = () => request('/v1/voice?bot_id=lynn-test', { method: 'POST', headers: { ...device, 'Content-Type': 'audio/pcm;rate=16000;channels=1;format=s16le' }, body: Buffer.alloc(160000) });
  return { ...connector, config, request, device, rpc, voice, logs };
}

test('loopback binding, host/origin validation, independent endpoint tokens and bot allowlist', async t => {
  const { server, request, rpc, device, config } = await fixture(t);
  assert.equal(server.address().address, '127.0.0.1');
  assert.deepEqual(await (await request('/healthz')).json(), { ok: true });
  const invalidHostStatus = await new Promise((resolve, reject) => {
    const call = http.get({ host: '127.0.0.1', port: server.address().port, path: '/healthz', headers: { Host: 'evil.example' } }, response => { response.resume(); resolve(response.statusCode); });
    call.on('error', reject);
  });
  assert.equal(invalidHostStatus, 403);
  assert.equal((await request('/healthz', { headers: { Origin: 'https://evil.example' } })).status, 403);
  assert.equal((await request('/v1/events?bot_id=lynn-test&after=0')).status, 401);
  assert.equal((await rpc('ping', {}, config.deviceToken)).status, 401);
  assert.equal((await request('/v1/events?bot_id=lynn-test&after=0', { headers: { Authorization: `Bearer ${config.toolToken}` } })).status, 401);
  assert.equal((await request('/v1/events?bot_id=other&after=0', { headers: device })).status, 403);
  assert.equal((await request('/v1/events?bot_id=lynn-test&bot_id=other&after=0', { headers: device })).status, 403);
  for (const cursor of ['-1', '1.5', '4294967296', '01', 'abc']) assert.equal((await request(`/v1/events?bot_id=lynn-test&after=${cursor}`, { headers: device })).status, 400);
});

test('MCP initialize/list/each action/say/status/ping and stateless transport', async t => {
  const { rpc, queue, request, config } = await fixture(t);
  const initialize = await (await rpc('initialize', { protocolVersion: '2025-06-18', capabilities: {}, clientInfo: { name: 'test', version: '1' } })).json();
  assert.equal(initialize.result.protocolVersion, '2025-06-18');
  assert.equal((await (await rpc('tools/list')).json()).result.tools.length, 47);
  assert.equal(actions.length, 41);
  for (const action of actions) {
    const reply = await (await rpc('tools/call', { name: `lynn_action_${action.id}`, arguments: { text: `Perform ${action.id}` } })).json();
    assert.equal(reply.result.isError, false);
    assert.equal(queue.state.events.at(-1).event.action, action.id);
  }
  const say = await (await rpc('tools/call', { name: 'lynn_say', arguments: { text: '你好'.repeat(100) } })).json();
  assert.equal(say.result.isError, false);
  assert.ok(queue.state.events.slice(-4).every(entry => Buffer.byteLength(entry.event.text) <= 191));
  const status = await (await rpc('tools/call', { name: 'lynn_status' })).json();
  assert.ok(JSON.parse(status.result.content[0].text).queued_events > 41);
  assert.deepEqual((await (await rpc('ping')).json()).result, {});
  assert.equal((await request('/mcp', { headers: { Authorization: `Bearer ${config.toolToken}` } })).status, 405);
  assert.equal((await rpc('ping', {}, config.toolToken, { 'MCP-Protocol-Version': 'unsupported' })).status, 400);
});

test('MCP validates arguments, body bounds, parse errors and notifications', async t => {
  const { rpc, request, config } = await fixture(t);
  for (const params of [{ name: 'lynn_say', arguments: {} }, { name: 'lynn_action_missing', arguments: {} }, { name: 'lynn_say', arguments: { text: 7 } }, { name: 'lynn_action_wave', arguments: { bot_id: 'other' } }]) {
    assert.equal((await (await rpc('tools/call', params)).json()).error.code, -32602);
  }
  const headers = { Authorization: `Bearer ${config.toolToken}`, 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream' };
  const post = body => request('/mcp', { method: 'POST', headers, body });
  assert.equal((await (await post('{')).json()).error.code, -32700);
  assert.equal((await (await post('[]')).json()).error.code, -32600);
  assert.equal((await post('a'.repeat(32769))).status, 413);
  const notification = await post(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }));
  assert.equal(notification.status, 202); assert.equal(await notification.text(), '');
  assert.equal((await rpc('ping', {}, config.toolToken, { Accept: 'application/json' })).status, 406);
});

test('async voice returns202, dispatches transcript with correlation and waits for bot tool reply', async t => {
  let dispatched;
  const { voice, rpc, request, device, queue, logs } = await fixture(t, { dispatchWebhook: async (...args) => { dispatched = args; } });
  const accepted = await voice(); assert.equal(accepted.status, 202);
  const { request_id } = await accepted.json();
  for (let attempt = 0; !dispatched && attempt < 100; attempt++) await delay(5);
  assert.equal(dispatched[1], 'hello'); assert.equal(dispatched[2], request_id);
  assert.equal(queue.pending, 0);
  assert.ok(logs.some(entry => entry.event === 'voice_dispatched' && entry.request_id === request_id));
  const initial = await (await request('/v1/events?bot_id=lynn-test&after=0', { headers: device })).json();
  await rpc('tools/call', { name: 'lynn_say', arguments: { text: 'Hello from Lynn', request_id } });
  const events = await (await request(`/v1/events?bot_id=lynn-test&after=${initial.cursor}`, { headers: device })).json();
  assert.equal(events.events[0].text, 'Hello from Lynn');
});

test('ASR and webhook failures expose safe errors with request IDs only', async t => {
  const { voice, rpc, queue, logs } = await fixture(t, { dispatchWebhook: async () => { throw new Error('SECRET-UPSTREAM-DETAIL'); } });
  assert.equal((await voice()).status, 202);
  for (let attempt = 0; !queue.pending && attempt < 100; attempt++) await delay(5);
  assert.equal(queue.state.events[0].event.text, 'Voice request failed. Please try again.');
  const status = await (await rpc('tools/call', { name: 'lynn_status' })).json();
  assert.equal(JSON.parse(status.result.content[0].text).recent_voice.status, 'failed');
  assert.ok(!JSON.stringify(logs).includes('SECRET'));
});

test('voice rejects bad format/length, bounds jobs and reports missing webhook', async t => {
  const { request, device, voice } = await fixture(t, { transcribe: async (_pcm, _settings, signal) => new Promise(resolve => signal.addEventListener('abort', resolve)) });
  assert.equal((await request('/v1/voice?bot_id=lynn-test', { method: 'POST', headers: device, body: 'small' })).status, 415);
  assert.equal((await request('/v1/voice?bot_id=lynn-test', { method: 'POST', headers: { ...device, 'Content-Type': 'audio/pcm;rate=16000;channels=1;format=s16le' }, body: Buffer.alloc(4) })).status, 400);
  const adpcm = Buffer.alloc(20004); adpcm.writeUInt32LE(16000, 20000);
  const adpcmHeaders = { ...device, 'Content-Type': 'audio/ima-adpcm;rate=8000;channels=1' };
  assert.equal((await request('/v1/voice?bot_id=lynn-test', { method: 'POST', headers: adpcmHeaders, body: Buffer.alloc(20004) })).status, 400);
  assert.equal((await request('/v1/voice?bot_id=lynn-test', { method: 'POST', headers: adpcmHeaders, body: adpcm })).status, 202);
  for (let n = 0; n < 2; n++) assert.equal((await voice()).status, 202);
  assert.equal((await voice()).status, 429);
  const notReady = await fixture(t, {}, { webhookURL: undefined, webhookKey: undefined });
  assert.equal((await notReady.voice()).status, 503);
});

test('configuration rejects weak/equal tokens, unsafe webhook and ASR command', async t => {
  const { config } = await fixture(t);
  for (const override of [{ toolToken: config.deviceToken }, { deviceToken: 'short' }, { webhookURL: 'http://example.com' }, { asr: { command: 'whisper', args: ['{input}'] } }]) assert.throws(() => validateConfig({ ...config, ...override }));
});

async function until(check) {
  for (let n = 0; n < 100; n++) { const value = await check(); if (value) return value; await delay(5); }
  throw new Error('test wait expired');
}
async function tool(rpc, name, args = {}) {
  const result = (await (await rpc('tools/call', { name, arguments: args })).json()).result;
  if (result.isError) return { error: result.content[0].text };
  return JSON.parse(result.content[0].text);
}

test('voice follow-ups carry completed history; memory survives event initialization', async t => {
  const sent = []; let utterance = '我叫小明';
  const { voice, rpc, request, device } = await fixture(t, { transcribe: async () => utterance, dispatchWebhook: async (...args) => sent.push(args) });
  const first = await (await voice()).json();
  await until(() => sent.length === 1);
  assert.deepEqual(sent[0][4].context, []);
  assert.equal((await tool(rpc, 'lynn_say', { text: '记住了，小明。', request_id: first.request_id })).queued, true);
  assert.equal((await tool(rpc, 'lynn_say', { text: '记住了，小明。', request_id: first.request_id })).duplicate, true);
  await tool(rpc, 'lynn_remember', { key: 'preferred_name', text: '用户说自己叫小明' });
  await request('/v1/events?bot_id=lynn-test&after=0', { headers: device });
  utterance = '我刚才说叫什么？';
  await voice(); await until(() => sent.length === 2);
  const context = sent[1][4];
  assert.equal(context.conversation_id, sent[0][4].conversation_id);
  assert.equal(context.context[0].user, '我叫小明');
  assert.ok(context.context[0].assistant.includes('记住了，小明。'));
  assert.equal(context.memories[0].key, 'preferred_name');
  assert.equal((await tool(rpc, 'lynn_memory_search', { query: '小明' })).memories.length, 1);
});

test('overlapping voice requests reject ambiguous legacy replies and correlate explicit IDs', async t => {
  const sent = [];
  const { voice, rpc } = await fixture(t, { dispatchWebhook: async (...args) => sent.push(args) });
  const first = await (await voice()).json(); await until(() => sent.length === 1);
  const second = await (await voice()).json(); await until(() => sent.length === 2);
  assert.equal((await tool(rpc, 'lynn_say', { text: 'ambiguous' })).error, 'request_id_required');
  assert.equal((await tool(rpc, 'lynn_say', { text: 'first reply', request_id: first.request_id })).queued, true);
  assert.equal((await tool(rpc, 'lynn_say', { text: 'second reply', request_id: second.request_id })).queued, true);
  assert.equal((await tool(rpc, 'lynn_say', { text: 'unknown reply', request_id: 'missing' })).error, 'unknown_request');
  const context = await tool(rpc, 'lynn_context');
  assert.ok(context.context[0].assistant.includes('first reply'));
  assert.ok(context.context[1].assistant.includes('second reply'));
});

test('cached text-only tools route an explicit request tag without displaying it', async t => {
  let dispatched = false;
  const { rpc, voice, queue } = await fixture(t, { dispatchWebhook: async () => { dispatched = true; } });
  const { request_id } = await (await voice()).json(); await until(() => dispatched);
  const result = await tool(rpc, 'lynn_say', { text: `[request_id=${request_id}]你好` });
  assert.equal(result.request_id, request_id);
  assert.equal(queue.state.events.at(-1).event.text, '你好');
  assert.equal((await tool(rpc, 'lynn_context')).context[0].assistant, '你好');
});
