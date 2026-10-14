// Real local shell + isolated SFTP server. OLE data exchange is exercised through
// a deterministic receiver; physical Explorer pointer routing remains a manual check.
const {Server,utils}=require('ssh2');
const crypto=require('node:crypto'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const attach=require('./sftp-fixture.cjs');
const root=path.resolve(__dirname,'../..'),artifact=path.join(root,'build','drag-test-'+Date.now());
const local=path.join(artifact,'local'),remote=path.join(artifact,'remote'),download=path.join(artifact,'download');
for(const dir of [local,remote,download])fs.mkdirSync(dir,{recursive:true});
fs.mkdirSync(path.join(remote,'projects'));
for(const dir of [remote,download])fs.writeFileSync(path.join(dir,'delete-me.txt'),'delete shortcut fixture');
const payload=crypto.randomBytes(1200003);fs.writeFileSync(path.join(local,'upload-한글.bin'),payload);
const key=crypto.generateKeyPairSync('rsa',{modulusLength:2048}).privateKey.export({type:'pkcs1',format:'pem'});
const pin='SHA256:'+crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/,'');
const clients=new Set();const server=new Server({hostKeys:[key]},client=>{
  clients.add(client);client.on('close',()=>clients.delete(client));client.on('error',()=>{});
  client.on('authentication',ctx=>ctx.username==='none'&&ctx.method==='none'?ctx.accept():ctx.reject());
  client.on('ready',()=>client.on('session',accept=>attach(accept(),remote)));
});
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const executable=path.join(artifact,'wShell.exe');fs.copyFileSync(path.join(root,'build/native/ui-smoke-tests.exe'),executable);
  const test=spawn(executable,[],{cwd:artifact,windowsHide:false,env:{...process.env,WOOK_TEST_DRAG_ONLY:'1',WOOK_TEST_PORT:String(server.address().port),WOOK_TEST_FINGERPRINT:pin}});
  const timer=setTimeout(()=>test.kill(),110000);await new Promise(resolve=>test.on('close',resolve));clearTimeout(timer);
  const result=JSON.parse(fs.readFileSync(path.join(artifact,'drag-result.json'),'utf8'));
  console.log(JSON.stringify({artifact,...result},null,2));assert.equal(result.passed,true);
  for(const file of [path.join(remote,'upload-한글.bin'),path.join(remote,'export.bin'),path.join(download,'export.bin'),path.join(download,'upload-한글.bin')])
    assert.deepEqual(fs.readFileSync(file),payload,'upload and both download paths preserve exact bytes');
  assert.equal(fs.existsSync(path.join(download,'projects')),false);
  for(const dir of [remote,download])assert.equal(fs.existsSync(path.join(dir,'delete-me.txt')),false);
  console.log('PASS: live session detach/reattach, SFTP drops, virtual-file stream, Unicode, folder alert and byte equality');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>{for(const c of clients)c.end();server.close();});
