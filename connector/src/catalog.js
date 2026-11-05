import { readFileSync } from 'node:fs';

// The existing firmware catalog is the only action list maintained by hand.
export const actions = JSON.parse(readFileSync(new URL('../../docs/lynn-tools.json', import.meta.url))).actions;
export const actionById = new Map(actions.map(action => [action.id, action]));
const empty = { type: 'object', properties: {}, additionalProperties: false };
export const tools = [
  ...actions.map(action => ({
    name: `lynn_action_${action.id}`,
    description: `${action.description} Plays for approximately ${action.approx_seconds} seconds. Optional text is shown on Lynn's device.`,
    inputSchema: { type: 'object', properties: { text: { type: 'string', maxLength: 4000 } }, additionalProperties: false },
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false, openWorldHint: false },
  })),
  { name: 'lynn_say', description: 'Deliver your reply as text on Lynn\'s device. Call this tool to send your spoken-conversation response; ordinary chat output does not reach the device. Long replies are split into readable cards.', inputSchema: { type: 'object', properties: { text: { type: 'string', minLength: 1, maxLength: 4000 } }, required: ['text'], additionalProperties: false } },
  { name: 'lynn_list_actions', description: 'List all available Lynn actions.', inputSchema: empty, annotations: { readOnlyHint: true } },
  { name: 'lynn_status', description: 'Show connector delivery queue and recent voice request status. This is not a device playback acknowledgement.', inputSchema: empty, annotations: { readOnlyHint: true } },
];
