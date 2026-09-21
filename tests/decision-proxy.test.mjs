import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn, execFile } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { createDecisionServer } from '../scripts/decision-proxy.mjs';
import { choose, inquiry, validateRequest } from '../scripts/decision-protocol.mjs';
const request = () => ({version:1,session:'s',request:'r',revision:1,self:{health:{current:100,maximum:100}},rules:{move:'Move'},range:{radius:8},previousGoal:null,
  candidates:[{id:'c0',action:'move',parameters:{destination:{x:1,y:2}},pathSteps:3},{id:'c1',action:'wait',parameters:{seconds:1}}]});
async function server(t,options={}) { const s=createDecisionServer(options); await new Promise(resolve=>s.listen(0,'127.0.0.1',resolve));
  t.after(()=>{s.closeAllConnections();s.close();}); return `http://127.0.0.1:${s.address().port}/decision`; }
const post=(url,r=request())=>fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(r)});
test('strict candidate contract, deterministic ties, malformed scores',()=>{
  assert.equal(inquiry(request()).model,'typesafe-ai/jev');
  const response={model:'typesafe-ai/jev',answers:{c0:{type:'boolean',probability:0.5},c1:{type:'boolean',probability:0.5}}};
  assert.equal(choose(request(),response).candidate,'c0'); response.answers.c1.probability=0.9; assert.equal(choose(request(),response).candidate,'c1');
  response.answers.c0.probability=NaN; assert.throws(()=>choose(request(),response));
  const bad=request(); bad.candidates[0].id='c1'; assert.throws(()=>validateRequest(bad));
});
test('fake proxy and bounded invalid bodies',async t=>{
  const url=await server(t); const r=await post(url); assert.equal(r.status,200); assert.equal((await r.json()).candidate,'c0');
  assert.equal((await post(url,{...request(),version:2})).status,400);
  assert.equal((await post(url,{...request(),padding:'x'.repeat(9000)})).status,413);
});
test('injected live provider returns selection and keeps key off output',async t=>{
  const url=await server(t,{mode:'live',apiKey:'test-secret',fetchImpl:async (url,options)=>{
    assert.equal(options.headers.Authorization,'Bearer test-secret');
    const body=JSON.parse(options.body); assert.equal(Object.keys(body.questions).length,2);
    return Response.json({model:'typesafe-ai/jev',answers:{c0:{type:'boolean',probability:0.1},c1:{type:'boolean',probability:0.9}}});
  }});
  const text=await (await post(url)).text(); assert.equal(JSON.parse(text).candidate,'c1'); assert.ok(!text.includes('test-secret'));
});
test('concurrency, session deduplication, and rate limits',async t=>{
  let release; const gate=new Promise(r=>release=r); const url=await server(t,{mode:'live',apiKey:'test',maxConcurrency:1,fetchImpl:async()=>{
    await gate; return Response.json({model:'typesafe-ai/jev',answers:{c0:{type:'boolean',probability:1},c1:{type:'boolean',probability:0}}});
  }});
  const first=post(url); await new Promise(r=>setTimeout(r,30));
  assert.equal((await post(url)).status,409); assert.equal((await post(url,{...request(),session:'other'})).status,429); release(); assert.equal((await first).status,200);
  const limited=await server(t,{requestsPerSecond:1,now:()=>0}); assert.equal((await post(limited)).status,200); assert.equal((await post(limited)).status,429);
});
test('persistent bridge returns two correlated responses from one process',async t=>{
  const url=await server(t); const child=spawn(process.execPath,[fileURLToPath(new URL('../scripts/decision-bridge.mjs',import.meta.url)),url,'500'],{stdio:['pipe','pipe','pipe']});
  t.after(()=>child.kill()); let buffer=''; const waiting=[];
  child.stdout.on('data',chunk=>{buffer+=chunk; let i;while((i=buffer.indexOf('\n'))>=0){const line=buffer.slice(0,i);buffer=buffer.slice(i+1);waiting.shift()?.(JSON.parse(line));}});
  const ask=r=>new Promise(resolve=>{waiting.push(resolve);child.stdin.write(JSON.stringify(r)+'\n');});
  assert.equal((await ask(request())).request,'r'); assert.equal((await ask({...request(),request:'r2'})).request,'r2');
  child.kill(); await once(child,'exit');
});
test('provider failure becomes generic failure',async t=>{
  const url=await server(t,{mode:'live',apiKey:'private',fetchImpl:async()=>{throw new Error('private');}});
  const response=await post(url); assert.equal(response.status,502); assert.deepEqual(await response.json(),{error:'provider_unavailable'});
});

test('C++ pipe transport integrates with local proxy', {skip:!process.env.JEV_BRIDGE_HOST}, async t=>{
  const url=await server(t);
  const output=await new Promise((resolve,reject)=>execFile(process.env.JEV_BRIDGE_HOST,[url],{timeout:15000},(error,stdout,stderr)=>error?reject(new Error(stderr||error.message)):resolve(stdout)));
  assert.match(output,/C\+\+ bridge passed/);
});
