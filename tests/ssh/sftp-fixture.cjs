// Independent ssh2 server for the shared SFTP codec and native GUI tests.
const fs = require('node:fs');
const path = require('node:path');
const { utils: { sftp: { STATUS_CODE: S } } } = require('ssh2');
module.exports = function attachSftp(session, root, events = {}) {
  session.on('sftp', accept => {
    const stream = accept(); const handles = new Map(); let sequence = 0;
    events.connections = (events.connections || 0) + 1;
    const resolve = name => {
      const normalized = path.posix.resolve('/', name);
      const file = path.resolve(root, '.' + normalized);
      if (file !== root && !file.startsWith(root + path.sep)) throw Error('outside fixture');
      return file;
    };
    const attrs = stat => ({ size: stat.size, mode: stat.isDirectory() ? 0o40755 : 0o100644, uid: 1000, gid: 1000, atime: 1700000000, mtime: 1700000000 });
    const guard = handler => (id, ...args) => { try { handler(id, ...args); } catch(e) { stream.status(id, e.code === 'ENOENT' ? S.NO_SUCH_FILE : S.FAILURE, e.code || e.message); } };
    const handle = value => { const id = Buffer.alloc(4); id.writeUInt32BE(++sequence); handles.set(id.toString('hex'), value); return id; };
    const get = id => { const h=handles.get(id.toString('hex')); if(!h)throw Error('bad handle');return h; };
    stream.on('REALPATH',guard((id,name)=>{const real=path.posix.resolve('/',name);fs.statSync(resolve(real));stream.name(id,[{filename:real,longname:real,attrs:{}}]);}));
    stream.on('LSTAT',guard((id,name)=>stream.attrs(id,attrs(fs.lstatSync(resolve(name))))));
    stream.on('OPENDIR',guard((id,name)=>stream.handle(id,handle({dir:resolve(name),read:false}))));
    stream.on('READDIR',guard((id,h)=>{const item=get(h);if(item.read)return stream.status(id,S.EOF);item.read=true;stream.name(id,fs.readdirSync(item.dir).map(name=>({filename:name,longname:name,attrs:attrs(fs.statSync(path.join(item.dir,name)))})));}));
    stream.on('OPEN',guard((id,name,flags)=>{const file=resolve(name);const fd=fs.openSync(file,flags&2?'wx':'r');stream.handle(id,handle({fd,file}));}));
    stream.on('READ',guard((id,h,offset,length)=>{
      const item=get(h);const data=Buffer.alloc(Math.min(length,events.readLimit || 17003)); // Exercise legal short reads.
      const count=fs.readSync(item.fd,data,0,data.length,offset);
      const reply=()=>{if(stream.destroyed)return;if(!count)stream.status(id,S.EOF);else stream.data(id,data.subarray(0,count));};
      setTimeout(reply,events.readDelay ?? (item.file.endsWith('slow.bin')?35:(id%3)*2)); // Out-of-order replies.
    }));
    stream.on('WRITE',guard((id,h,offset,data)=>{fs.writeSync(get(h).fd,data,0,data.length,offset);setTimeout(()=>{if(!stream.destroyed)stream.status(id,S.OK);},(id%3)*2);}));
    stream.on('CLOSE',guard((id,h)=>{const item=get(h);if(item.fd!==undefined)fs.closeSync(item.fd);handles.delete(h.toString('hex'));stream.status(id,S.OK);}));
    stream.on('MKDIR',guard((id,name)=>{fs.mkdirSync(resolve(name));stream.status(id,S.OK);}));
    stream.on('REMOVE',guard((id,name)=>{fs.unlinkSync(resolve(name));stream.status(id,S.OK);}));
    stream.on('RMDIR',guard((id,name)=>{fs.rmdirSync(resolve(name));stream.status(id,S.OK);}));
    stream.on('RENAME',guard((id,from,to)=>{if(fs.existsSync(resolve(to)))return stream.status(id,S.FAILURE);fs.renameSync(resolve(from),resolve(to));stream.status(id,S.OK);}));
    stream.on('error',()=>{});
    stream.on('close',()=>{for(const item of handles.values())if(item.fd!==undefined)try{fs.closeSync(item.fd);}catch{}handles.clear();});
  });
};
