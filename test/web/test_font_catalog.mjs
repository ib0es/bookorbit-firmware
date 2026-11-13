import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { build } from '../../web-flasher/node_modules/esbuild/lib/main.js';
const bundle = async path => {
  const result = await build({ entryPoints: [new URL(`../../web-flasher/src/services/${path}.ts`,import.meta.url).pathname], bundle:true, platform:'node',format:'esm',write:false,define:{'import.meta.env.BASE_URL':'"./"'} });
  return import('data:text/javascript;base64,'+Buffer.from(result.outputFiles[0].text).toString('base64'));
};
const { readFontCatalog, installCatalogFont, removeCatalogFont } = await bundle('fontCatalog');
const { buildFontPartitionImage, fontCatalogUsage, buildFontDirectory, parseFontDirectory, planFontAddition } = await bundle('fontPartition');
const partition={name:'spiffs',type:1,subtype:130,offset:0x10000,size:65536};
function fixture(){
  const memory=new Uint8Array(partition.size).fill(255),packets=[];
  let writes=0,failCommit=false;
  const loader={
    detectFlashSize:async()=> '1MB',
    checkCommand:async(_name,op,data)=>{
      if(op!==0xd2)return 0;
      const v=new DataView(data.buffer,data.byteOffset,data.byteLength),at=v.getUint32(0,true)-partition.offset,n=v.getUint32(4,true),chunk=v.getUint32(8,true);
      const bytes=memory.slice(at,at+n);
      for(let i=0;i<n;i+=chunk)packets.push(bytes.slice(i,i+chunk));
      packets.push(new Uint8Array(createHash('md5').update(bytes).digest()));return 0;
    },
    transport:{read:async()=>{assert.ok(packets.length);return packets.shift();},write:async()=>{}},
    flashMd5sum:async(address,size)=>createHash('md5').update(memory.subarray(address-partition.offset,address-partition.offset+size)).digest('hex'),
    writeFlash:async({fileArray})=>{
      for(const f of fileArray){
        writes++;const at=f.address-partition.offset,end=Math.ceil((at+f.data.length)/4096)*4096;
        assert.ok(at>=0&&end<=memory.length);memory.fill(255,at,end);
        if(failCommit&&at>=memory.length-8192){memory.set(f.data.subarray(0,8),at);throw Error('Simulated power loss');}
        memory.set(f.data,at);
      }
    },
  };
  return {memory,loader,writes:()=>writes,failCommit:()=>{failCommit=true;}};
}
const image=(name,byte,size=1000)=>buildFontPartitionImage(name,[new Uint8Array(size).fill(byte)]);

test('USB installation adds families, removes only the requested family and reuses space',async()=>{
  const f=fixture();
  await installCatalogFont(f.loader,partition,image('Alpha',11));
  await installCatalogFont(f.loader,partition,image('Beta',22));
  let state=await readFontCatalog(f.loader,partition);
  assert.deepEqual(state.families.map(f=>f.name),['Alpha','Beta']);
  const beta=state.families[1];const saved=f.memory.slice(beta.offset,beta.offset+beta.span);
  const before=f.writes();assert.equal(await installCatalogFont(f.loader,partition,image('Beta',22)),false);assert.equal(f.writes(),before);
  await removeCatalogFont(f.loader,partition,'Alpha');
  await installCatalogFont(f.loader,partition,image('Gamma',33));
  state=await readFontCatalog(f.loader,partition);
  assert.deepEqual(state.families.map(f=>f.name),['Beta','Gamma']);
  assert.deepEqual(f.memory.slice(beta.offset,beta.offset+beta.span),saved);
  assert.equal(state.families[1].offset,0);
  assert.equal(fontCatalogUsage(state).usedBytes,16384);
});

test('USB adopts legacy data without rewriting it and survives interrupted catalog commit',async()=>{
  const f=fixture(),legacy=image('Legacy',77);f.memory.set(legacy);
  await installCatalogFont(f.loader,partition,image('Alpha',11));
  assert.deepEqual(f.memory.slice(0,legacy.length),legacy);
  f.failCommit();
  await assert.rejects(installCatalogFont(f.loader,partition,image('Beta',22)),/power loss/);
  const state=await readFontCatalog(f.loader,partition);
  assert.deepEqual(state.families.map(f=>f.name),['Legacy','Alpha']);
});

test('full partition refuses writes before erasing any data',async()=>{
  const f=fixture();await installCatalogFont(f.loader,partition,image('Alpha',11));
  const before=f.memory.slice(),writes=f.writes();
  await assert.rejects(installCatalogFont(f.loader,partition,image('Huge',44,55*1024)),/space/);
  assert.equal(f.writes(),writes);assert.deepEqual(f.memory,before);
});

test('catalog rejects overlapping records and corrupt checksums',()=>{
  assert.throws(()=>buildFontDirectory({generation:1,records:[{offset:0,span:8192},{offset:4096,span:8192}]},57344));
  const data=buildFontDirectory({generation:1,records:[]},57344);data[8]^=1;assert.equal(parseFontDirectory(data,57344),null);
});

test('interrupted removal leaves every previously committed family readable',async()=>{
  const f=fixture();
  await installCatalogFont(f.loader,partition,image('Alpha',11));
  await installCatalogFont(f.loader,partition,image('Beta',22));
  f.failCommit();
  await assert.rejects(removeCatalogFont(f.loader,partition,'Beta'),/power loss/);
  const state=await readFontCatalog(f.loader,partition);
  assert.deepEqual(state.families.map(f=>f.name),['Alpha','Beta']);
});

test('catalog entry limit is checked even when byte capacity remains',()=>{
  const records=Array.from({length:32},(_,i)=>({offset:i*8192,span:8192}));
  const families=records.map((record,i)=>({...record,name:`Family${i}`,bytes:100}));
  assert.throws(()=>planFontAddition({generation:1,slot:0,records,families,dataLimit:512*1024},image('Extra',44)),/32 families/);
});

test('built-in Noto Serif cannot be installed or removed through the USB service',async()=>{
  const f=fixture();
  await assert.rejects(installCatalogFont(f.loader,partition,image('NotoSerif',99)),/built-in/);
  await assert.rejects(removeCatalogFont(f.loader,partition,'Noto Serif'),/built-in/);
  assert.equal(f.writes(),0);
});
