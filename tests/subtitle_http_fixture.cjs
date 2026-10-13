const fs=require('fs');const path=require('path');const http=require('http');
const root=process.argv[2];
require('../plugin-service').start();
const server=http.createServer((req,res)=>{
  const name=path.basename(new URL(req.url,'http://localhost').pathname);
  const file=path.join(root,name);if(!fs.existsSync(file)){res.writeHead(404).end();return;}
  const size=fs.statSync(file).size;let start=0,end=size-1;
  const range=/^bytes=(\d+)-(\d*)$/.exec(req.headers.range||'');
  if(range){start=Number(range[1]);end=range[2]?Math.min(end,Number(range[2])):end;}
  if(start>=size){res.writeHead(416,{'Content-Range':`bytes */${size}`}).end();return;}
  const headers={'Accept-Ranges':'bytes','Content-Length':end-start+1};
  if(range)headers['Content-Range']=`bytes ${start}-${end}/${size}`;
  res.writeHead(range?206:200,headers);
  if(req.method==='HEAD')res.end();else fs.createReadStream(file,{start,end}).pipe(res);
});
server.listen(28444,'127.0.0.1',()=>console.log('fixture ready'));
