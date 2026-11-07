import { mkdirSync, readFileSync, openSync, writeFileSync, fsyncSync, closeSync, renameSync, chmodSync } from 'node:fs';
import { join } from 'node:path';
import { actionById } from './catalog.js';

export class PublicError extends Error {
  constructor(status, code) { super(code); this.status = status; this.code = code; }
}

export function splitText(text, maxBytes = 191) {
  const parts = []; let current = '';
  // Code-point iteration never cuts a UTF-8 sequence or UTF-16 surrogate pair.
  for (const char of text.normalize('NFC').replace(/[\t\r\n]+/g, ' ')) {
    if (Buffer.byteLength(current + char) > maxBytes) { parts.push(current); current = ''; }
    current += char;
  }
  if (current) parts.push(current);
  return parts;
}

export class EventQueue {
  constructor(directory, { now = Date.now, maxEvents = 64, ttlMs = 300000 } = {}) {
    mkdirSync(directory, { recursive: true, mode: 0o700 }); chmodSync(directory, 0o700);
    this.file = join(directory, 'events.json'); this.now = now; this.maxEvents = maxEvents; this.ttlMs = ttlMs;
    // Zero is the protocol's initialize sentinel, so reserve cursor 1 at creation.
    this.state = { cursor: 1, events: [], nextAt: 0 };
    try { this.state = JSON.parse(readFileSync(this.file, 'utf8')); }
    catch (error) { if (error.code !== 'ENOENT') throw new Error('Invalid connector state; restore state instead of resetting the cursor'); }
    if (!Number.isInteger(this.state.cursor) || this.state.cursor < 1 || this.state.cursor > 0xffffffff || !Array.isArray(this.state.events) || this.state.events.length > maxEvents || !Number.isFinite(this.state.nextAt)) throw new Error('Invalid connector state');
    let previousId = 1;
    for (const entry of this.state.events) {
      const event = entry?.event;
      if (!event || !Number.isInteger(event.id) || event.id <= previousId || event.id > this.state.cursor || !Number.isFinite(entry.expiresAt) || (entry.deliveredAt !== null && !Number.isFinite(entry.deliveredAt)) || !['text', 'action'].includes(event.type) || (event.type === 'action' && !actionById.has(event.action)) || (event.type === 'text' && typeof event.text !== 'string') || (event.text !== undefined && (typeof event.text !== 'string' || Buffer.byteLength(event.text) > 191))) throw new Error('Invalid connector event state');
      previousId = event.id;
    }
    this.prune(); this.save();
  }
  save() {
    const temporary = `${this.file}.tmp`;
    const fd = openSync(temporary, 'w', 0o600);
    try { writeFileSync(fd, JSON.stringify(this.state)); fsyncSync(fd); } finally { closeSync(fd); }
    renameSync(temporary, this.file);
  }
  prune() {
    const previous = this.state.events.length;
    this.state.events = this.state.events.filter(event => event.expiresAt > this.now());
    return previous !== this.state.events.length;
  }
  enqueue(type, text, action) {
    this.prune();
    const chunks = text ? splitText(text) : [];
    const outgoing = type === 'action' ? [{ type, action, ...(chunks[0] ? { text: chunks.shift() } : {}) }] : [];
    outgoing.push(...chunks.map(chunk => ({ type: 'text', text: chunk })));
    if (!outgoing.length) throw new PublicError(400, 'empty_text');
    if (this.state.events.length + outgoing.length > this.maxEvents) throw new PublicError(429, 'event_queue_full');
    if (this.state.cursor + outgoing.length > 0xffffffff) throw new PublicError(503, 'cursor_exhausted');
    const ids = [];
    for (const event of outgoing) {
      event.id = ++this.state.cursor; ids.push(event.id);
      this.state.events.push({ event, expiresAt: this.now() + this.ttlMs, deliveredAt: null });
    }
    this.save(); return ids;
  }
  poll(after) {
    let changed = this.prune();
    if (after > this.state.cursor) throw new PublicError(400, 'invalid_cursor');
    if (after === 0) {
      // Boot/reconnect starts at the current head, deliberately skipping old work.
      changed ||= this.state.events.length > 0 || this.state.nextAt !== 0;
      this.state.events = []; this.state.nextAt = 0; if (changed) this.save();
      return { cursor: this.state.cursor, events: [] };
    }
    const previous = this.state.events.length;
    this.state.events = this.state.events.filter(entry => entry.event.id > after);
    changed ||= previous !== this.state.events.length;
    const entry = this.state.events[0];
    if (!entry) { if (changed) this.save(); return { cursor: this.state.cursor, events: [] }; }
    if (entry.deliveredAt === null && this.now() < this.state.nextAt) { if (changed) this.save(); return { cursor: after, events: [] }; }
    if (entry.deliveredAt === null) {
      entry.deliveredAt = this.now();
      const textHold = entry.event.text ? Math.max(5000, [...entry.event.text].length * 250) : 0;
      const actionHold = entry.event.type === 'action' ? actionById.get(entry.event.action).approx_seconds * 1000 + 300 : 0;
      const hold = Math.max(textHold, actionHold);
      this.state.nextAt = this.now() + hold; this.save();
    } else if (changed) this.save();
    return { cursor: entry.event.id, events: [entry.event] };
  }
  get pending() { this.prune(); return this.state.events.length; }
}
