import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { requestHttp, colorSchema, type State } from '../shared/protocol/src/index.js';
import { parsePreview } from '../shared/protocol/src/preview.js';
const address = process.env.DEVICE_ADDRESS;
if (!address) throw new Error('Set DEVICE_ADDRESS for this opt-in hardware test');
let id = 320000;
const initial: State = process.env.RESTORE_STATE_FILE ? JSON.parse(await readFile(process.env.RESTORE_STATE_FILE, 'utf8')).state : (await requestHttp(address,{id:id++,operation:0})).state!;
let sourceToRestore = '';
if (initial.mode === 2 && !initial.activeScriptId) {
  if (!process.env.RESTORE_ANIMATION_SOURCE) throw new Error('Provide RESTORE_ANIMATION_SOURCE before replacing a RAM animation');
  sourceToRestore = await readFile(process.env.RESTORE_ANIMATION_SOURCE,'utf8');
}
const before = (await requestHttp(address,{id:id++,operation:5})).scripts.map(item=>item.id).sort();
const source = '-- @preview #Ff0000,00ff00,0000ff\n-- @parameter Speed,Number,50,0,100\nfunction loop() luxio_set_pixel_color(0,1,2,3) end';
const preview = parsePreview(source);
let savedId = '';
try {
  let reply = await requestHttp(address,{id:id++,operation:31,script:{id:'',name:'Preview regression',source,preview:[]}});
  assert.deepEqual(reply.state!.animationPreview,preview);
  assert.equal(reply.state!.activeScriptId,'');
  const saved = (await requestHttp(address,{id:id++,operation:7,script:{id:'',name:'Preview regression',source,preview:[]}})).script!;
  savedId = saved.id; assert.deepEqual(saved.preview,preview);
  const listed = (await requestHttp(address,{id:id++,operation:5})).scripts.find(item=>item.id===savedId)!;
  assert.equal(listed.source,''); assert.deepEqual(listed.preview,preview);
  assert.deepEqual((await requestHttp(address,{id:id++,operation:6,scriptId:savedId})).script!.preview,preview);
  await assert.rejects(requestHttp(address,{id:id++,operation:8,script:{...saved,parameters:[],source:'-- @preview invalid\nfunction loop() end'}}),/Preview|preview/);
  assert.deepEqual((await requestHttp(address,{id:id++,operation:6,scriptId:savedId})).script!.preview,preview);
  reply = await requestHttp(address,{id:id++,operation:10,scriptId:savedId});
  assert.deepEqual(reply.state!.animationPreview,preview);
  await requestHttp(address,{id:id++,operation:9,scriptId:savedId}); savedId='';
  assert.deepEqual((await requestHttp(address,{id:id++,operation:0})).state!.animationPreview,[]);
  reply = await requestHttp(address,{id:id++,operation:31,script:{id:'',name:'No preview',source:'function loop() end'}});
  assert.deepEqual(reply.state!.animationPreview,[]);
  reply = await requestHttp(address,{id:id++,operation:17,gradient:{id:'',name:'',colors:[colorSchema.parse({red:255}),colorSchema.parse({blue:255})]}});
  assert.deepEqual(reply.state!.animationPreview,[]);
  console.log('PASS: RAM/saved preview, metadata listing, readback, invalid-update atomicity and clearing');
} finally {
  if(savedId) await requestHttp(address,{id:id++,operation:9,scriptId:savedId});
  await requestHttp(address,{id:id++,operation:1,color:initial.color});
  await requestHttp(address,{id:id++,operation:2,brightness:initial.brightness});
  if(initial.mode===2) {
    if(initial.activeScriptId) await requestHttp(address,{id:id++,operation:10,scriptId:initial.activeScriptId});
    else await requestHttp(address,{id:id++,operation:31,script:{id:'',name:initial.scriptName!,source:sourceToRestore,parameters:initial.parameters}});
    if(initial.activeScriptId && initial.parameters?.length) await requestHttp(address,{id:id++,operation:32,scriptId:initial.activeScriptId,parameters:initial.parameters});
  } else if(initial.mode===1) await requestHttp(address,{id:id++,operation:initial.activeGradientId?23:17,gradientId:initial.activeGradientId,gradient:initial.gradient});
  else if(initial.activeColorId) await requestHttp(address,{id:id++,operation:30,colorId:initial.activeColorId});
  await requestHttp(address,{id:id++,operation:15,on:initial.on});
  const restored=(await requestHttp(address,{id:id++,operation:0})).state!;
  assert.equal(restored.mode,initial.mode);assert.equal(restored.brightness,initial.brightness);assert.equal(restored.on,initial.on);
  assert.deepEqual((await requestHttp(address,{id:id++,operation:5})).scripts.map(item=>item.id).sort(),before);
  console.log('PASS: original output restored; saved library unchanged');
}
