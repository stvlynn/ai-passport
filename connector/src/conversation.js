import { mkdirSync, chmodSync, statSync, readFileSync, openSync, writeFileSync, fsyncSync, closeSync, renameSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import { join } from 'node:path';
import { PublicError } from './events.js';

const MAX_TURNS = 48, MAX_CONTEXT = 12, MAX_SUMMARY = 4000, MAX_SNAPSHOT = 20000;
const REQUEST_AGE = 10 * 60 * 1000;
const statuses = new Set(['pending', 'dispatched', 'failed', 'uncertain', 'replied']);
const unresolved = turn => ['pending', 'dispatched', 'uncertain'].includes(turn.status);
const idValid = value => typeof value === 'string' && /^[a-zA-Z0-9_-]{1,128}$/.test(value);
const keyValid = value => typeof value === 'string' && /^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}$/.test(value);
const textValid = (value, max) => typeof value === 'string' && value.length <= max && value.isWellFormed() && Boolean(value.trim()) && !/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(value);
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const keys = (value, names) => object(value) && Object.keys(value).length === names.length && names.every(name => Object.hasOwn(value, name));
const fail = (status, code) => { throw new PublicError(status, code); };
// Do not split surrogate pairs when shortening context or the rolling extract.
const head = (text, size) => text.length <= size ? text : text.slice(0, size).replace(/[\ud800-\udbff]$/, '') + '…';
const extract = (turn, expired = false) => {
  const label = expired ? ' Status: expired, no reply\n' : (unresolved(turn) ? ` Status: ${turn.status}, no reply\n` : ' ');
  return `[${turn.requestId}]${label}User: ${head(turn.user.replace(/[\r\n]+/g, ' '), 256)}\nAssistant: ${head(turn.assistant.join(' ').replace(/[\r\n]+/g, ' '), 512)}`;
};
const rollSummary = (summary, addition) => {
  const entries = summary ? summary.split('\n\n') : [];
  entries.push(addition);
  while (entries.join('\n\n').length > MAX_SUMMARY) entries.shift();
  return entries.join('\n\n');
};

export class ConversationStore {
  constructor(directory, botId, { now = Date.now } = {}) {
    if (!idValid(botId)) throw new Error('Invalid conversation bot ID');
    mkdirSync(directory, { recursive: true, mode: 0o700 }); chmodSync(directory, 0o700);
    this.file = join(directory, 'conversation.json'); this.now = now;
    this.state = { version: 1, botId, conversationId: randomUUID(), summary: '', turns: [], memories: [] };
    try {
      // Valid state is below 2 MiB even with maximum-length three-byte UTF-8
      // characters. Reject oversized files before allocating their contents.
      if (statSync(this.file).size > 2 * 1024 * 1024) throw new Error('Oversized conversation state');
      this.state = JSON.parse(readFileSync(this.file, 'utf8'));
    }
    catch (error) { if (error.code !== 'ENOENT') throw new Error('Invalid conversation state; restore state instead of resetting it'); }
    const state = this.state;
    if (!keys(state, ['version', 'botId', 'conversationId', 'summary', 'turns', 'memories']) || state.version !== 1 || state.botId !== botId || typeof state.conversationId !== 'string' || !/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(state.conversationId) || typeof state.summary !== 'string' || !state.summary.isWellFormed() || state.summary.length > MAX_SUMMARY || !Array.isArray(state.turns) || state.turns.length > MAX_TURNS || !Array.isArray(state.memories) || state.memories.length > 32) throw new Error('Invalid conversation state');
    const ids = new Set(), memoryKeys = new Set();
    for (const turn of state.turns) {
      if (!keys(turn, ['requestId', 'user', 'assistant', 'status', 'createdAt']) || !idValid(turn.requestId) || ids.has(turn.requestId) || !textValid(turn.user, 4000) || !statuses.has(turn.status) || !Number.isFinite(turn.createdAt) || turn.createdAt < 0 || !Array.isArray(turn.assistant) || turn.assistant.length > 8 || !turn.assistant.every(text => textValid(text, 8000)) || turn.assistant.join('\n').length > 8000 || new Set(turn.assistant).size !== turn.assistant.length || (turn.assistant.length > 0) !== (turn.status === 'replied')) throw new Error('Invalid conversation turn state');
      ids.add(turn.requestId);
    }
    for (const memory of state.memories) {
      if (!keys(memory, ['key', 'text']) || !keyValid(memory.key) || !textValid(memory.text, 1000) || memoryKeys.has(memory.key)) throw new Error('Invalid conversation memory state');
      memoryKeys.add(memory.key);
    }
    this.save();
  }
  save() {
    const temporary = `${this.file}.tmp`;
    const fd = openSync(temporary, 'w', 0o600);
    try { chmodSync(temporary, 0o600); writeFileSync(fd, JSON.stringify(this.state)); fsyncSync(fd); } finally { closeSync(fd); }
    renameSync(temporary, this.file);
  }
  turn(requestId) {
    if (!idValid(requestId)) fail(400, 'invalid_request_id');
    const turn = this.state.turns.find(entry => entry.requestId === requestId);
    if (!turn) fail(404, 'unknown_request');
    return turn;
  }
  begin(requestId, text) {
    if (!idValid(requestId)) fail(400, 'invalid_request_id');
    if (!textValid(text, 4000)) fail(400, 'invalid_conversation_text');
    if (this.state.turns.some(turn => turn.requestId === requestId)) fail(409, 'duplicate_request');
    if (this.state.turns.length === MAX_TURNS && unresolved(this.state.turns[0]) && this.now() - this.state.turns[0].createdAt < REQUEST_AGE) fail(409, 'conversation_full');
    const snapshot = this.context();
    if (this.state.turns.length === MAX_TURNS) {
      const archived = this.state.turns.shift();
      if (archived.status !== 'failed') {
        // This is an extract, never an inferred fact or an LLM-generated summary.
        this.state.summary = rollSummary(this.state.summary, extract(archived, unresolved(archived)));
      }
    }
    this.state.turns.push({ requestId, user: text, assistant: [], status: 'pending', createdAt: this.now() });
    this.save(); return snapshot;
  }
  status(requestId, status) {
    if (!statuses.has(status)) fail(400, 'invalid_conversation_status');
    const turn = this.turn(requestId);
    // A synchronous tool callback may finish before webhook acceptance returns.
    if (turn.status === 'replied') return;
    if (status === 'replied') fail(400, 'reply_text_required');
    turn.status = status; this.save();
  }
  resolveRequest(requestId) {
    if (requestId !== undefined && requestId !== null) {
      const turn = this.turn(requestId);
      if (turn.status === 'failed') fail(409, 'failed_request');
      if (unresolved(turn) && this.now() - turn.createdAt >= REQUEST_AGE) fail(409, 'expired_request');
      return requestId;
    }
    // Expired outstanding requests remain ambiguous; never silently assign an old
    // callback to the newest request, even after a restart.
    const pending = this.state.turns.filter(unresolved);
    if (pending.length > 1) fail(409, 'request_id_required');
    return pending.length ? this.resolveRequest(pending[0].requestId) : null;
  }
  replyAlreadyRecorded(requestId, text) { return this.turn(requestId).assistant.includes(text); }
  hasReply(requestId, text) { return this.replyAlreadyRecorded(requestId, text); }
  validateReply(requestId, text) {
    this.resolveRequest(requestId);
    if (!textValid(text, 8000)) fail(400, 'invalid_reply_text');
    const turn = this.turn(requestId);
    if (turn.assistant.includes(text)) return false;
    if (turn.assistant.length >= 8 || [...turn.assistant, text].join('\n').length > 8000) fail(409, 'reply_limit');
    return true;
  }
  appendReply(requestId, text) {
    if (!this.validateReply(requestId, text)) return false;
    const turn = this.turn(requestId);
    turn.assistant.push(text); turn.status = 'replied'; this.save(); return true;
  }
  context(requestId) {
    const end = requestId === undefined ? this.state.turns.length : this.state.turns.indexOf(this.turn(requestId));
    const turns = this.state.turns.slice(0, end).filter(turn => turn.status !== 'failed');
    let summary = this.state.summary;
    for (const turn of turns.slice(0, -MAX_CONTEXT)) summary = rollSummary(summary, extract(turn, unresolved(turn) && this.now() - turn.createdAt >= REQUEST_AGE));
    const snapshot = {
      conversation_id: this.state.conversationId,
      context: turns.slice(-MAX_CONTEXT).map(turn => ({ request_id: turn.requestId, user: turn.user, assistant: turn.assistant.join('\n'), status: turn.status })),
      summary,
      memories: this.getMemory(),
    };
    // Retain the newest turn, then earlier context, as space permits. Full replies
    // remain on disk; the webhook receives a bounded projection.
    while (JSON.stringify(snapshot).length > MAX_SNAPSHOT && snapshot.memories.length) snapshot.memories.pop();
    while (JSON.stringify(snapshot).length > MAX_SNAPSHOT && snapshot.context.length > 1) {
      const removed = snapshot.context.shift();
      snapshot.summary = rollSummary(snapshot.summary, extract(this.turn(removed.request_id)));
    }
    while (JSON.stringify(snapshot).length > MAX_SNAPSHOT && snapshot.summary) snapshot.summary = snapshot.summary.split('\n\n').slice(1).join('\n\n');
    while (JSON.stringify(snapshot).length > MAX_SNAPSHOT) {
      const turn = snapshot.context[0], field = turn.assistant.length >= turn.user.length ? 'assistant' : 'user';
      turn[field] = head(turn[field], Math.max(0, turn[field].length - 500));
    }
    return snapshot;
  }
  getMemory(query = '') {
    if (typeof query !== 'string' || query.length > 1000) fail(400, 'invalid_memory_query');
    const needle = query.toLocaleLowerCase();
    return this.state.memories.filter(memory => !needle || `${memory.key}\n${memory.text}`.toLocaleLowerCase().includes(needle)).map(memory => ({ ...memory }));
  }
  remember(key, text) {
    if (!keyValid(key) || !textValid(text, 1000)) fail(400, 'invalid_memory');
    const existing = this.state.memories.find(memory => memory.key === key);
    if (!existing && this.state.memories.length >= 32) fail(409, 'memory_full');
    if (existing) existing.text = text;
    else this.state.memories.push({ key, text });
    this.save(); return { key, text };
  }
}
