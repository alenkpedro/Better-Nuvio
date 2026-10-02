// Run this against the packaged service with the actual old webOS Node.
// This file deliberately uses ES5 and callback HTTP APIs supported by 0.12.
var assert = require("assert");
var fs = require("fs");
var path = require("path");
var http = require("http");
var root = process.argv[2];
process.env.NUVIO_PLUGIN_PORT="28532";
process.env.NUVIO_PLUGIN_FETCH_PORT="28533";
var service = require(path.resolve(process.argv[3]));
var requests = 0;
service.start();
http.createServer(function (req,res) {
  if (req.url === "/redirect.mkv") {res.writeHead(302,{Location:"/srt.mkv"});res.end();return;}
  var file = path.join(root,path.basename(req.url));
  if (!fs.existsSync(file)) {res.writeHead(404);res.end();return;}
  var match = /^bytes=(\d+)-(\d+)$/.exec(req.headers.range || "");
  assert(match,"the extractor must request a bounded byte range");
  requests++;
  var size = fs.statSync(file).size;
  var start = Number(match[1]),end = Math.min(size-1,Number(match[2]));
  if (start >= size) {res.writeHead(416);res.end();return;}
  res.writeHead(206,{"Content-Length":end-start+1,"Content-Range":"bytes "+start+"-"+end+"/"+size});
  fs.createReadStream(file,{start:start,end:end}).pipe(res);
}).listen(28534,"127.0.0.1",function () {
  function call(route,body) {
    return new Promise(function (resolve,reject) {
      var req = http.request({host:"127.0.0.1",port:28532,path:route,
        method:body ? "POST":"GET",headers:{"Content-Type":"application/json"}},function (res) {
        var parts=[];res.on("data",function (part) {parts.push(part);});
        res.on("end",function () {resolve({status:res.statusCode,body:Buffer.concat(parts).toString("utf8")});});
      });req.on("error",reject);req.end(body ? JSON.stringify(body):undefined);
    });
  }
  function extract(file,ordinal,ass) {
    return call("/subtitles/window",{url:"http://127.0.0.1:28534/"+file,
      trackOrdinal:ordinal,startSeconds:0,endSeconds:120,includeAssBody:ass}).then(function (res) {
      assert.strictEqual(res.status,200,res.body);
      var result=JSON.parse(res.body);
      assert(result.body.indexOf("00:00:02.100") >= 0,result.body);
      assert(result.body.indexOf("FIRST") >= 0,result.body);
      assert.strictEqual(result.codecId,ass ? "S_TEXT/ASS":"S_TEXT/UTF8");
      if (ass) assert(result.assBody.indexOf("Dialogue:") >= 0);
      else assert(!result.assBody,"SubRip must stay in the external subtitle text renderer");
    });
  }
  call("/health").then(function (res) {
    assert.strictEqual(res.status,200);assert.strictEqual(res.body,"ok");
    return extract("redirect.mkv",0,false);
  }).then(function () {return extract("ass.mkv",0,true);
  }).then(function () {return extract("many.mkv",42,false);
  }).then(function () {
    return call("/subtitles/window",{url:"invalid",trackOrdinal:0});
  }).then(function (res) {
    assert.strictEqual(res.status,502);assert.strictEqual(JSON.parse(res.body).errorCode,"INVALID_URL");
    if (typeof WebAssembly === "object") return;
    return service.execute("module.exports={}","test.js","test",{}).then(function () {
      throw new Error("unsupported optional plugin engine must reject");
    },function (error) {assert(/WebAssembly/.test(error.message));});
  }).then(function () {return extract("srt.mkv",0,false);
  }).then(function () {return call("/health");
  }).then(function (res) {
    assert.strictEqual(res.status,200);assert(requests>0);
    console.log("PASS packaged service on Node "+process.version+": health, SubRip/ASS, 43 tracks, redirect, HTTP Range, error recovery");
    process.exit(0);
  }).catch(function (error) {console.error(error.stack || error);process.exit(1);});
});
