const { Server,utils } = require('ssh2');
const crypto=require('node:crypto'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const attach=require('./sftp-fixture.cjs');
const root=path.resolve(__dirname,'../..'),artifact=path.join(root,'build','sftp-test-'+Date.now());
const local=path.join(artifact,'local'),remote=path.join(artifact,'remote');
fs.mkdirSync(local,{recursive:true});fs.mkdirSync(remote);
const payload=crypto.randomBytes(1200003);fs.writeFileSync(path.join(local,'payload.bin'),payload);fs.writeFileSync(path.join(remote,'slow.bin'),payload);fs.writeFileSync(path.join(remote,'안녕하세요.txt'),'SFTP fixture');
const key=crypto.generateKeyPairSync('rsa',{modulusLength:2048}).privateKey.export({type:'pkcs1',format:'pem'});
const pin='SHA256:'+crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/,'');
const clients=new Set();const server=new Server({hostKeys:[key]},client=>{
  clients.add(client);client.on('close',()=>clients.delete(client));client.on('error',()=>{});
  client.on('authentication',ctx=>ctx.username==='none'&&ctx.method==='none'?ctx.accept():ctx.reject());
  client.on('ready',()=>client.on('session',accept=>attach(accept(),remote)));
});
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const test=spawn(path.join(root,'build/native/sftp-tests.exe'),[],{windowsHide:true,env:{...process.env,WOOK_DATA_DIR:path.join(artifact,'data'),WOOK_SFTP_ENGINE:path.join(root,'build/native/wShell.exe'),WOOK_TEST_PORT:String(server.address().port),WOOK_TEST_FINGERPRINT:pin,WOOK_SFTP_LOCAL:local}});
  test.stdout.pipe(process.stdout);test.stderr.pipe(process.stderr);
  const timer=setTimeout(()=>{test.kill();for(const c of clients)c.destroy();},60000);
  const code=await new Promise(resolve=>test.on('close',resolve));clearTimeout(timer);assert.equal(code,0);
  assert.deepEqual(fs.readFileSync(path.join(local,'한글-🙂.bin')),payload,'download and cancellation preserve exact bytes');
  assert.deepEqual(fs.readdirSync(remote).filter(n=>!n.startsWith('.wshell-')).sort(),['slow.bin','안녕하세요.txt'].sort());
  console.log('PASS: independent SFTP server + SHA-256 byte equality ('+artifact+')');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>{for(const c of clients)c.destroy();server.close();});
