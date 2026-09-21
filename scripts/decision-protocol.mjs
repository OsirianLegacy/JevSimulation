export const MAX_REQUEST_BYTES = 8192;
export const MAX_RESPONSE_BYTES = 16384;
export function validateRequest(r) {
  if (!r || r.version !== 1 || !Number.isSafeInteger(r.revision) || r.revision < 0) throw new Error('Invalid protocol revision');
  for (const key of ['session', 'request'])
    if (typeof r[key] !== 'string' || !r[key].length || r[key].length > 128) throw new Error('Invalid request identity');
  if (!r.self || !r.rules || !r.range || !Array.isArray(r.candidates) || r.candidates.length < 1 || r.candidates.length > 4)
    throw new Error('Invalid context');
  const ids = new Set();
  let wait = false;
  for (const c of r.candidates) {
    if (typeof c.id !== 'string' || !/^c\d+$/.test(c.id) || ids.has(c.id)) throw new Error('Invalid candidate identity');
    ids.add(c.id);
    if (c.action === 'wait') {
      if (!Number.isFinite(c.parameters?.seconds) || c.parameters.seconds <= 0 || c.parameters.seconds > 3600) throw new Error('Invalid wait');
      wait = true;
    } else if (c.action === 'move') {
      const p = c.parameters?.destination;
      if (!p || !Number.isSafeInteger(p.x) || !Number.isSafeInteger(p.y) || p.x < 0 || p.y < 0 || p.x > 2147483647 || p.y > 2147483647)
        throw new Error('Invalid destination');
    } else if (c.action === 'attack') {
      if (typeof r.rules.attack !== 'string' || typeof c.parameters?.target !== 'string' ||
          !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(c.parameters.target)) throw new Error('Invalid attack target');
    } else if (typeof c.action !== 'string' || !/^[a-z][a-z0-9_]{0,63}$/.test(c.action) || typeof r.rules[c.action] !== 'string' || !c.parameters || typeof c.parameters !== 'object' || Array.isArray(c.parameters)) throw new Error('Invalid extension action');
  }
  if (!wait) throw new Error('Wait candidate required');
  return r;
}
export function selection(r, candidate) {
  return { version: 1, session: r.session, request: r.request, revision: r.revision, candidate };
}
export function inquiry(r) {
  validateRequest(r);
  const questions = {};
  for (const c of r.candidates) questions[c.id] = {
    type: 'boolean',
    instructions: `Is candidate ${c.id} a suitable next goal for this entity? Consider current state, previous outcome, nearby entities' dispositions and health, and the supplied action rules. Use the relative enemy flag to distinguish enemies from allies. Neutral wildlife should not initiate combat. Hostile entities may attack non-hostile targets; friendly entities may defend against hostiles. Choose only a supplied candidate.`,
  };
  return { model: 'typesafe-ai/jev', state: JSON.stringify(r), questions };
}
export function choose(r, response) {
  if (response?.model !== 'typesafe-ai/jev') throw new Error('Unexpected provider model');
  let best = null, score = -1;
  for (const c of r.candidates) {
    const a = response?.answers?.[c.id];
    if (a?.type !== 'boolean' || !Number.isFinite(a.probability) || a.probability < 0 || a.probability > 1)
      throw new Error('Invalid provider answer');
    if (a.probability > score) { best = c.id; score = a.probability; }
  }
  return selection(r, best);
}
export async function readBounded(response, limit) {
  const reader = response.body.getReader(); let size = 0; const chunks = [];
  try {
    for (;;) {
      const {done, value} = await reader.read(); if (done) break;
      size += value.byteLength; if (size > limit) throw new Error('Response too large'); chunks.push(value);
    }
  } finally { await reader.cancel(); }
  return Buffer.concat(chunks).toString('utf8');
}
