import http from 'node:http';
import { readFileSync, existsSync } from 'node:fs';
import { loadEnvFile } from 'node:process';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { MAX_REQUEST_BYTES, MAX_RESPONSE_BYTES, validateRequest, selection, inquiry, choose, readBounded } from './decision-protocol.mjs';

export function createDecisionServer({mode = 'fake', apiKey = '', maxConcurrency = 8, requestsPerSecond = 40, timeoutMs = 2000, fetchImpl = fetch, now = () => performance.now()} = {}) {
  if (!['fake','live'].includes(mode) || !Number.isInteger(maxConcurrency) || maxConcurrency < 1 || maxConcurrency > 1024 ||
      !Number.isFinite(requestsPerSecond) || requestsPerSecond <= 0 || requestsPerSecond > 10000 ||
      !Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 30000 || (mode === 'live' && !apiKey.trim()))
    throw new Error('Invalid proxy configuration or missing provider key');
  let active = 0, tokens = requestsPerSecond, last = now(); const sessions = new Set();
  return http.createServer(async (req, res) => {
    const reply = (status, value) => { if (!res.destroyed) { res.writeHead(status, {'Content-Type':'application/json'}); res.end(JSON.stringify(value)); } };
    if (req.method !== 'POST' || req.url !== '/decision') return reply(404, {error:'not_found'});
    if (!(req.headers['content-type'] ?? '').startsWith('application/json')) return reply(415, {error:'json_required'});
    let body = '', bytes = 0; const chunks = [];
    try {
      for await (const chunk of req) {
        bytes += chunk.length; if (bytes > MAX_REQUEST_BYTES) { reply(413, {error:'payload_limit'}); return; }
        chunks.push(chunk);
      }
      body = Buffer.concat(chunks).toString('utf8');
    } catch { return; }
    let r;
    try { r = validateRequest(JSON.parse(body)); } catch { return reply(400,{error:'invalid_request'}); }
    const at = now(); tokens = Math.min(requestsPerSecond,tokens+(at-last)*requestsPerSecond/1000); last = at;
    if (sessions.has(r.session)) return reply(409,{error:'session_busy'});
    if (active >= maxConcurrency || tokens < 1) return reply(429,{error:'capacity'});
    tokens--; active++; sessions.add(r.session);
    const start = now();
    try {
      let result;
      if (mode === 'fake') result = selection(r,r.candidates[0].id);
      else {
        const response = await fetchImpl('https://ai-gateway.vercel.sh/v1/evaluate', {
          method:'POST', redirect:'error', signal:AbortSignal.timeout(timeoutMs),
          headers:{Authorization:`Bearer ${apiKey.trim()}`,'Content-Type':'application/json'}, body:JSON.stringify(inquiry(r)),
        });
        if (!response.ok) throw new Error('Provider rejected request');
        result = choose(r,JSON.parse(await readBounded(response,MAX_RESPONSE_BYTES)));
      }
      reply(200,result);
      console.error(JSON.stringify({event:'decision',request:r.request,bytes,latencyMs:Math.round(now()-start)}));
    } catch { reply(502,{error:'provider_unavailable'}); }
    finally { active--; sessions.delete(r.session); }
  });
}
async function main() {
  const args = process.argv.slice(2); const configIndex = args.indexOf('--config');
  for (let i=0;i<args.length;i++) { if (args[i]==='--live') continue; if(args[i]==='--config' && args[i+1]) { i++; continue; } throw new Error('Usage: decision-proxy.mjs [--config path] [--live]'); }
  const path = configIndex >= 0 ? args[configIndex+1] : fileURLToPath(new URL('../ai-config.json',import.meta.url));
  const config = JSON.parse(readFileSync(path,'utf8')); if (config.version !== 1) throw new Error('Unsupported config version');
  const live = args.includes('--live');
  if (live) for (const name of ['.env.local','.env']) if (existsSync(name)) loadEnvFile(name);
  const port = config.proxy?.port ?? 8787;
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid proxy port');
  const server = createDecisionServer({mode:live ? 'live' : 'fake', apiKey:live ? (process.env.AI_GATEWAY_API_KEY || process.env.API_KEY || '') : '',
    maxConcurrency:config.proxy?.maxConcurrency,requestsPerSecond:config.proxy?.requestsPerSecond,timeoutMs:config.timeout*1000});
  server.requestTimeout = 5000; server.headersTimeout = 5000;
  server.listen(port,'127.0.0.1',() => console.error(`Decision proxy listening on 127.0.0.1:${port} (${live?'live':'fake'})`));
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch(() => { console.error('Proxy startup failed; check config and server-side credentials.'); process.exitCode=1; });
