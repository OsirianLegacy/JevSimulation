import { MAX_REQUEST_BYTES, MAX_RESPONSE_BYTES, validateRequest, readBounded } from './decision-protocol.mjs';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const url = new URL(process.argv[2] ?? 'http://127.0.0.1:8787/decision');
const timeout = Number(process.argv[3] ?? 2000);
if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || !Number.isFinite(timeout) || timeout < 1 || timeout > 30000)
  throw new Error('Bridge requires a loopback proxy and bounded timeout');
// A managed proxy uses an ephemeral loopback port, so independent game instances
// cannot accidentally share credentials or collide on a fixed port.
let proxy;
const endpoint = process.argv[4] ? new Promise((resolve,reject)=>{
  proxy=spawn(process.execPath,[fileURLToPath(new URL('./decision-proxy.mjs',import.meta.url)),
    '--config',process.argv[4],'--port','0','--announce'],{stdio:['ignore','pipe','inherit'],windowsHide:true});
  const timer=setTimeout(()=>{proxy.kill();reject(new Error('Proxy startup timeout'));},timeout);
  let data='';
  proxy.stdout.on('data',chunk=>{
    data+=chunk;
    if(data.length>1024){clearTimeout(timer);proxy.kill();reject(new Error('Invalid readiness'));return;}
    if(data.includes('\n')) {
      clearTimeout(timer);
      try { const ready=new URL(JSON.parse(data.slice(0,data.indexOf('\n'))).url);
        if(ready.hostname!=='127.0.0.1'||ready.protocol!=='http:')throw new Error('Invalid proxy endpoint');resolve(ready);
      }catch(error){reject(error);}
    }
  });
  proxy.once('error',error=>{clearTimeout(timer);reject(error);});
  proxy.once('exit',()=>{clearTimeout(timer);reject(new Error('Proxy exited'));});
}) : Promise.resolve(url);
endpoint.catch(()=>{}); // Failure is returned through the next request, never an unhandled rejection.
process.on('exit',()=>proxy?.kill());
process.on('SIGTERM',()=>{proxy?.kill();process.exit();});
process.stdin.on('end',()=>{proxy?.kill();process.exit();});
let buffer = Buffer.alloc(0), busy = false;
function emit(value) { process.stdout.write(JSON.stringify(value)+'\n'); }
async function handle(line) {
  let request;
  try {
    request = validateRequest(JSON.parse(line));
    if (busy) throw new Error('busy');
    busy = true;
    try {
      const response = await fetch(await endpoint,{method:'POST',redirect:'error',headers:{'Content-Type':'application/json'},
        body:JSON.stringify(request),signal:AbortSignal.timeout(timeout)});
      if (!response.ok) throw new Error('proxy unavailable');
      const result = JSON.parse(await readBounded(response,MAX_RESPONSE_BYTES));
      if (result.version !== 1 || result.session !== request.session || result.request !== request.request ||
          result.revision !== request.revision || !request.candidates.some(c => c.id === result.candidate)) throw new Error('invalid selection');
      emit(result);
    } finally { busy = false; }
  } catch { emit({version:1,session:request?.session,request:request?.request,revision:request?.revision,error:'decision_unavailable'}); }
}
process.stdin.on('data',chunk => {
  buffer = Buffer.concat([buffer,chunk]);
  let end;
  while ((end=buffer.indexOf(10)) >= 0) {
    if (end > MAX_REQUEST_BYTES) { process.exitCode=1; process.stdin.destroy(); return; }
    const line=buffer.subarray(0,end).toString('utf8'); buffer=buffer.subarray(end+1);
    void handle(line);
  }
  if (buffer.length > MAX_REQUEST_BYTES) { process.exitCode=1; process.stdin.destroy(); }
});
