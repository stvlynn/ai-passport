import http from 'node:http';
import { timingSafeEqual, randomUUID } from 'node:crypto';
import { readFileSync, statSync } from 'node:fs';
import { resolve, dirname, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';
import { EventQueue, PublicError } from './events.js';
import { actions, actionById, tools } from './catalog.js';
import { transcribe, dispatchWebhook } from './asr.js';
import { VOICE_BODY_BYTES, VOICE_CONTENT_TYPE, voiceBodyToPcm } from './adpcm.js';

const versions = ['2025-03-26', '2025-06-18', '2025-11-25'];
const record = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const fail = (status, code) => { throw new PublicError(status, code); };

export function validateConfig(value) {
  if (!record(value)) throw new Error('Configuration must be an object');
  for (const key of ['deviceToken', 'toolToken', 'botId', 'botName', 'stateDir']) {
    if (typeof value[key] !== 'string' || !value[key].trim()) throw new Error(`Missing configuration: ${key}`);
  }
  if (value.deviceToken.length < 32 || value.toolToken.length < 32 || value.deviceToken === value.toolToken || /[\s\x00-\x1f]/.test(value.deviceToken + value.toolToken + (value.webhookKey ?? ''))) throw new Error('Use independent device/tool tokens of at least 32 characters and whitespace-free credentials');
  if (!/^[a-zA-Z0-9_-]{1,128}$/.test(value.botId)) throw new Error('Invalid bot ID');
  if (Boolean(value.webhookURL) !== Boolean(value.webhookKey)) throw new Error('Set webhookURL and webhookKey together');
  if (value.webhookURL) {
    if (typeof value.webhookKey !== 'string') throw new Error('Invalid webhook key');
    const url = new URL(value.webhookURL);
    if (url.protocol !== 'https:' || url.username || url.password || url.hash) throw new Error('Webhook must use HTTPS');
  }
  if (!Number.isInteger(value.port) || value.port < 0 || value.port > 65535) throw new Error('Invalid port');
  if (value.publicHost && !/^[a-zA-Z0-9.-]+(?::\d+)?$/.test(value.publicHost)) throw new Error('Invalid public host');
  if (!record(value.asr) || !isAbsolute(value.asr.command ?? '') || !Array.isArray(value.asr.args) || !value.asr.args.every(arg => typeof arg === 'string')) throw new Error('ASR needs an absolute executable and argument array');
  if (!value.asr.args.some(arg => arg.includes('{input}')) || !['file', 'stdout', undefined].includes(value.asr.output)) throw new Error('Invalid ASR input/output configuration');
  if (value.asr.timeoutMs !== undefined && (!Number.isInteger(value.asr.timeoutMs) || value.asr.timeoutMs < 100 || value.asr.timeoutMs > 300000)) throw new Error('Invalid ASR timeout');
  return value;
}

function authorized(request, token) {
  const actual = Buffer.from(request.headers.authorization ?? ''); const expected = Buffer.from(`Bearer ${token}`);
  return actual.length === expected.length && timingSafeEqual(actual, expected);
}

async function body(request, max) {
  const length = request.headers['content-length'];
  if (length !== undefined && (!/^\d+$/.test(length) || Number(length) > max)) fail(413, 'body_too_large');
  const chunks = []; let bytes = 0;
  for await (const chunk of request) {
    bytes += chunk.length; if (bytes > max) fail(413, 'body_too_large'); chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

export function createConnector(config, dependencies = {}) {
  validateConfig(config);
  const queue = new EventQueue(config.stateDir, dependencies.queueOptions);
  const asr = dependencies.transcribe ?? transcribe, webhook = dependencies.dispatchWebhook ?? dispatchWebhook;
  const logger = dependencies.logger ?? (entry => process.stdout.write(`${JSON.stringify(entry)}\n`));
  const controller = new AbortController();
  let active = false, stopping = false, recent = null; const jobs = []; const rates = new Map();
  const rate = (name, limit) => {
    const now = Date.now(); let entry = rates.get(name);
    if (!entry || now - entry.start > 60000) { entry = { start: now, count: 0 }; rates.set(name, entry); }
    if (++entry.count > limit) fail(429, 'rate_limited');
  };
  const run = async () => {
    if (active || stopping) return;
    const job = jobs.shift(); if (!job) return; active = true;
    recent = { request_id: job.id, status: 'transcribing' };
    try {
      const text = await asr(job.pcm, config.asr, controller.signal);
      recent = { request_id: job.id, status: 'dispatching' };
      await webhook(config, text, job.id, controller.signal);
      recent = { request_id: job.id, status: 'accepted_by_grok' };
      logger({ event: 'voice_dispatched', request_id: job.id });
    } catch {
      recent = { request_id: job.id, status: 'failed' };
      logger({ event: 'voice_failed', request_id: job.id });
      if (!stopping) { try { queue.enqueue('text', 'Voice request failed. Please try again.'); } catch { /* bounded queue may be full */ } }
    } finally { active = false; void run(); }
  };
  const rpc = message => {
    if (!record(message) || message.jsonrpc !== '2.0' || typeof message.method !== 'string' || (message.id !== undefined && typeof message.id !== 'string' && !Number.isFinite(message.id))) return { jsonrpc: '2.0', id: null, error: { code: -32600, message: 'Invalid Request' } };
    if (message.id === undefined) {
      if (!message.method.startsWith('notifications/')) fail(400, 'invalid_notification');
      return null;
    }
    const result = payload => ({ jsonrpc: '2.0', id: message.id, result: payload });
    const error = (code, text) => ({ jsonrpc: '2.0', id: message.id, error: { code, message: text } });
    if (message.params !== undefined && !record(message.params)) return error(-32602, 'Invalid params');
    switch (message.method) {
      case 'initialize': return result({ protocolVersion: versions.includes(message.params?.protocolVersion) ? message.params.protocolVersion : versions.at(-1), capabilities: { tools: { listChanged: false } }, serverInfo: { name: 'ai-passport-lynn', version: '1.0.0' }, instructions: 'You are Lynn. Use lynn_say or an action tool with text for every device-facing reply. Routine webhook acceptance does not deliver your normal chat output to the device.' });
      case 'ping': return result({});
      case 'tools/list': return result({ tools });
      case 'tools/call': {
        const name = message.params?.name, args = message.params?.arguments ?? {};
        const tool = tools.find(candidate => candidate.name === name);
        if (!tool) return error(-32602, 'Unknown tool');
        if (!record(args) || Object.keys(args).some(key => key !== 'text') || (args.text !== undefined && (typeof args.text !== 'string' || args.text.length > 4000 || /[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(args.text)))) return error(-32602, 'Invalid tool arguments');
        if ((name === 'lynn_list_actions' || name === 'lynn_status') && Object.keys(args).length) return error(-32602, 'Unexpected arguments');
        let payload;
        try {
          if (name === 'lynn_list_actions') payload = { actions };
          else if (name === 'lynn_status') payload = { bot_id: config.botId, queued_events: queue.pending, voice_jobs: jobs.length + Number(active), recent_voice: recent };
          else {
            if (name === 'lynn_say' && !args.text?.trim()) return error(-32602, 'Text is required');
            const action = name.startsWith('lynn_action_') ? name.slice('lynn_action_'.length) : undefined;
            if (action && !actionById.has(action)) return error(-32602, 'Unknown action');
            payload = { queued: true, event_ids: queue.enqueue(action ? 'action' : 'text', args.text, action), ...(typeof message.params?._meta?.request_id === 'string' ? { request_id: message.params._meta.request_id.slice(0, 128) } : {}) };
          }
          return result({ content: [{ type: 'text', text: JSON.stringify(payload) }], isError: false });
        } catch (caught) {
          if (!(caught instanceof PublicError)) throw caught;
          return result({ content: [{ type: 'text', text: caught.code }], isError: true });
        }
      }
      default: return error(-32601, 'Method not found');
    }
  };
  const server = http.createServer(async (request, response) => {
    const requestId = randomUUID();
    const send = (status, value) => {
      if (response.headersSent || response.destroyed) return;
      response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', 'X-Request-ID': requestId });
      response.end(value === undefined ? undefined : JSON.stringify(value));
    };
    try {
      const port = server.address()?.port ?? config.port;
      const hosts = new Set([`127.0.0.1:${port}`, `localhost:${port}`, ...(config.publicHost ? [config.publicHost] : [])]);
      if (!hosts.has(request.headers.host)) fail(403, 'invalid_host');
      const origin = request.headers.origin;
      if (origin && !new Set([`http://127.0.0.1:${port}`, `http://localhost:${port}`, ...(config.publicHost ? [`https://${config.publicHost}`] : [])]).has(origin)) fail(403, 'invalid_origin');
      const url = new URL(request.url, 'http://127.0.0.1');
      if (url.pathname === '/healthz' && request.method === 'GET') { rate('health', 120); return send(200, { ok: true }); }
      if (url.pathname === '/mcp') {
        if (!authorized(request, config.toolToken)) fail(401, 'unauthorized');
        rate('mcp', 120);
        if (request.headers['mcp-protocol-version'] && !versions.includes(request.headers['mcp-protocol-version'])) fail(400, 'unsupported_protocol_version');
        if (request.method !== 'POST') { response.setHeader('Allow', 'POST'); fail(405, 'method_not_allowed'); }
        if (!/^application\/json(?:\s*;|$)/i.test(request.headers['content-type'] ?? '')) fail(415, 'unsupported_content_type');
        if (!(request.headers.accept ?? '').includes('application/json') || !(request.headers.accept ?? '').includes('text/event-stream')) fail(406, 'mcp_accept_required');
        let message; try { message = JSON.parse((await body(request, 32768)).toString('utf8')); } catch (error) { if (error instanceof PublicError) throw error; return send(200, { jsonrpc: '2.0', id: null, error: { code: -32700, message: 'Parse error' } }); }
        const reply = rpc(message); return send(reply === null ? 202 : 200, reply === null ? undefined : reply);
      }
      if (!['/v1/voice', '/v1/events'].includes(url.pathname)) fail(404, 'not_found');
      if (!authorized(request, config.deviceToken)) fail(401, 'unauthorized');
      if (url.searchParams.getAll('bot_id').length !== 1 || url.searchParams.get('bot_id') !== config.botId) fail(403, 'bot_not_allowed');
      if (url.pathname === '/v1/events') {
        if (request.method !== 'GET') fail(405, 'method_not_allowed');
        rate('events', 300);
        const after = url.searchParams.get('after');
        if (url.searchParams.getAll('after').length !== 1 || !/^(0|[1-9]\d{0,9})$/.test(after ?? '') || Number(after) > 0xffffffff) fail(400, 'invalid_cursor');
        return send(200, queue.poll(Number(after)));
      }
      if (request.method !== 'POST') fail(405, 'method_not_allowed');
      rate('voice', 6);
      if (!config.webhookURL || !config.webhookKey) fail(503, 'not_ready');
      if (stopping || jobs.length + Number(active) >= 3) fail(429, 'voice_queue_full');
      const type = (request.headers['content-type'] ?? '').replaceAll(' ', '').toLowerCase();
      if (request.headers['transfer-encoding']) fail(400, 'invalid_audio_length');
      let pcm;
      if (type === 'audio/pcm;rate=16000;channels=1;format=s16le') {
        // Fixed five-second raw PCM (original protocol).
        if (request.headers['content-length'] !== '160000') fail(400, 'invalid_audio_length');
        pcm = await body(request, 160000);
        if (pcm.length !== 160000) fail(400, 'invalid_audio_length');
      } else if (type === VOICE_CONTENT_TYPE) {
        // Push-to-talk: fixed-size ADPCM body with a sample-count trailer (0.3-5 s).
        if (request.headers['content-length'] !== String(VOICE_BODY_BYTES)) fail(400, 'invalid_audio_length');
        pcm = voiceBodyToPcm(await body(request, VOICE_BODY_BYTES));
        if (!pcm) fail(400, 'invalid_audio_length');
      } else fail(415, 'unsupported_audio');
      if (jobs.length + Number(active) >= 3) fail(429, 'voice_queue_full');
      jobs.push({ id: requestId, pcm }); send(202, { request_id: requestId }); void run();
    } catch (error) {
      const known = error instanceof PublicError;
      if (!known) logger({ event: 'request_failed', request_id: requestId });
      send(known ? error.status : 500, { error: known ? error.code : 'internal_error', request_id: requestId });
    }
  });
  server.requestTimeout = 15000; server.headersTimeout = 5000; server.keepAliveTimeout = 5000; server.maxHeadersCount = 32; server.maxConnections = 32;
  return { server, queue, listen: () => new Promise((resolveListen, reject) => { server.once('error', reject); server.listen(config.port, '127.0.0.1', () => { server.removeListener('error', reject); resolveListen(server.address()); }); }), close: () => new Promise(resolveClose => { stopping = true; jobs.length = 0; controller.abort(); server.close(resolveClose); server.closeIdleConnections(); }) };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const path = resolve(process.argv[2] ?? process.env.AI_PASSPORT_CONFIG ?? 'config.json');
    const stat = statSync(path);
    if (stat.mode & 0o077) throw new Error('Configuration permissions must be 0600');
    const config = JSON.parse(readFileSync(path, 'utf8'));
    config.stateDir = resolve(dirname(path), config.stateDir ?? 'state');
    const connector = createConnector(config); await connector.listen();
    process.stdout.write(`${JSON.stringify({ event: 'listening', address: '127.0.0.1', port: connector.server.address().port })}\n`);
    for (const signal of ['SIGINT', 'SIGTERM']) process.once(signal, () => { void connector.close().then(() => process.exit(0)); });
  } catch { process.stderr.write('Connector startup failed. Check configuration, permissions, catalog, state and port.\n'); process.exitCode = 1; }
}
