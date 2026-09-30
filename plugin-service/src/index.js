"use strict";

// The native SDL app cannot host a Web Worker. Run the same bounded QuickJS
// worker used by the webOS app in this local JS service. Plugin code never
// runs in Node's own context: only the QuickJS guest evaluates it.
const fs = require("fs");
const http = require("http");
const path = require("path");
const vm = require("vm");
const ROOT = path.join(__dirname, "..");
const PORT = Number(process.env.NUVIO_PLUGIN_PORT) || 2732;
const FETCH_PORT = Number(process.env.NUVIO_PLUGIN_FETCH_PORT) || 2733;
const MAX_BODY = 1024 * 1024;
// A fonte nativa aguarda no maximo 70 s. Um provedor travado nao pode manter
// os addons normais escondidos atras de uma busca de plugins de dois minutos.
const FETCH_TIMEOUT = 15000;
const serviceId = "com.betternuvio.app.plugin";
const fetchServer = require("../runtime/plugin-http.cjs").createPluginHttpServer({port: FETCH_PORT});
const embeddedSubtitles = require("../runtime/nuvio/embedded-subtitles.cjs");
let service;
try { service = new (require("webos-service"))(serviceId); }
catch (error) {
  console.warn("[plugin] webOS service module unavailable",String(error.message || error));
  service = {register() {}};
}

global.self = global;
// Emscripten's Node path reads its embedded WASM through fs. The browser
// bundle resolves require dynamically when evaluated by vm.runInThisContext.
global.require = require;
global.importScripts = function () {
  if (!global.QJS) vm.runInThisContext(fs.readFileSync(path.join(ROOT,"runtime/quickjs-emscripten.global.js"),"utf8"));
};
vm.runInThisContext(fs.readFileSync(path.join(ROOT,"runtime/plugin-worker.js"),"utf8"));
const workerReceive = global.onmessage;
let active = null;
const queue = [];
const documentCache = new Map();

function internalFetch(payload) {
  return new Promise((resolve, reject) => {
    const request = http.request({host:"127.0.0.1",port:FETCH_PORT,path:"/fetch",method:"POST",
      headers:{"Content-Type":"application/json"}}, response => {
      const chunks = [];
      response.on("data", chunk => chunks.push(chunk));
      response.on("end", () => {
        try { resolve(JSON.parse(Buffer.concat(chunks).toString("utf8"))); }
        catch (e) { reject(e); }
      });
    });
    request.on("error", reject);
    request.end(JSON.stringify(payload));
  });
}

global.postMessage = function (message) {
  if (!active) return;
  if (message.type === "fetch") {
    internalFetch({...message.payload, requestId:message.requestId,
      timeoutMs:FETCH_TIMEOUT, maxBodyBytes:MAX_BODY,
      maxResponseBytes:MAX_BODY, androidResponseContract:true})
      .then(payload => workerReceive({data:{type:"fetchResult",requestId:message.requestId,payload}}))
      .catch(error => workerReceive({data:{type:"fetchResult",requestId:message.requestId,error:String(error.message || error)}}));
  } else if (message.type === "result" || message.type === "error") {
    const current = active;
    active = null;
    clearTimeout(current.timer);
    if (message.type === "error") current.reject(new Error(message.error));
    else current.resolve(message.results || []);
    setImmediate(runNext);
  }
};

function runNext() {
  if (active || !queue.length) return;
  active = queue.shift();
  active.timer = setTimeout(() => {
    // QuickJS interrupt handles synchronous loops. The timeout protects the
    // host when a provider awaits network forever; late results are ignored.
    // A browser Worker can be terminated on timeout. This service cannot
    // safely reset the worker's closure in the same process, so restart it.
    process.exit(1);
  }, FETCH_TIMEOUT + 1000);
  workerReceive({data:{type:"execute", executionId:String(Date.now()),
    code:active.code, filename:active.filename, scraperId:active.scraperId,
    args:active.args, settings:{}, tmdbApiKey:active.tmdbKey,
    timeoutMs:FETCH_TIMEOUT,
    deadline:Date.now()+FETCH_TIMEOUT,
    quota:{memoryLimitBytes:32*1024*1024,maxCodeBytes:MAX_BODY,
      maxResultsPerScraper:25,maxFetchBytes:MAX_BODY}}});
}

function execute(code, filename, scraperId, args, tmdbKey="") {
  return new Promise((resolve,reject) => { queue.push({code,filename,scraperId,args,tmdbKey,resolve,reject}); runNext(); });
}

async function fetchText(url, maxBytes=MAX_BODY) {
  const cached = documentCache.get(url);
  if (cached && cached.until > Date.now()) return cached.body;
  const response = await internalFetch({url,method:"GET",headers:{Accept:"application/json, text/javascript, */*"},
    timeoutMs:15000,maxBodyBytes:maxBytes,maxResponseBytes:maxBytes,
    requestId:"manifest-"+Date.now()+"-"+Math.random().toString(36).slice(2)});
  if (!response.ok || typeof response.body !== "string") throw new Error("HTTP "+response.status);
  if (documentCache.size > 96) documentCache.clear();
  documentCache.set(url,{body:response.body,until:Date.now()+5*60*1000});
  return response.body;
}

function playerHeaders(input) {
  const out = {};
  if (!input || typeof input !== "object" || Array.isArray(input)) return out;
  for (const [name,value] of Object.entries(input)) {
    if (!/^(referer|referrer|user-agent|cookie|cookies)$/i.test(name) ||
        typeof value !== "string" || /[\r\n]/.test(value)) continue;
    out[name] = value.slice(0,319);
  }
  return out;
}

async function streams(request, loadText=fetchText) {
  const deadline = Date.now()+60000;
  const repositories = Array.isArray(request.repositories) ? request.repositories.slice(0,32) : [];
  const args = {tmdbId:String(request.tmdbId || ""),mediaType:request.mediaType === "tv" ? "tv":"movie",
    season:Number(request.season || 0) || null,episode:Number(request.episode || 0) || null};
  if (!args.tmdbId && /^tt\d+(?::|$)/.test(request.imdb || "") && request.tmdbKey) {
    try {
      const imdb = String(request.imdb).split(":",1)[0];
      const url = "https://api.themoviedb.org/3/find/"+encodeURIComponent(imdb)+
        "?api_key="+encodeURIComponent(request.tmdbKey)+"&external_source=imdb_id";
      const found = JSON.parse(await loadText(url,256*1024));
      const matches = args.mediaType === "tv" ? found.tv_results : found.movie_results;
      const match = Array.isArray(matches) ? matches[0] : null;
      if (match && match.id) args.tmdbId=String(match.id);
    } catch (error) {console.warn("[plugin] TMDB id lookup failed",String(error.message || error));}
  }
  // A maioria dos titulos vindos de historico ou busca tem apenas IMDb. O
  // Cinemeta publica moviedb_id sem depender de chave pessoal do TMDB.
  if (!args.tmdbId && /^tt\d+(?::|$)/.test(request.imdb || "")) {
    try {
      const imdb = String(request.imdb).split(":",1)[0];
      const meta = JSON.parse(await loadText("https://v3-cinemeta.strem.io/meta/"+
        (args.mediaType === "tv" ? "series" : "movie")+"/"+encodeURIComponent(imdb)+".json",256*1024));
      if (meta.meta && Number(meta.meta.moviedb_id) > 0)
        args.tmdbId = String(meta.meta.moviedb_id);
    } catch (error) {console.warn("[plugin] Cinemeta id lookup failed",String(error.message || error));}
  }
  if (!args.tmdbId) return {streams:[]};
  const output = [];
  for (const repository of repositories) {
    if (Date.now() >= deadline) break;
    if (repository.enabled === false || !/^https:\/\//i.test(repository.url || "") ||
        repository.repo_type !== "NUVIO_JS") continue;
    try {
      const manifestUrl = /manifest\.json(?:[?#]|$)/.test(repository.url) ? repository.url :
        new URL("manifest.json",repository.url.endsWith("/") ? repository.url : repository.url+"/").href;
      const manifest = JSON.parse(await loadText(manifestUrl,256*1024));
      const scrapers = Array.isArray(manifest.scrapers) ? manifest.scrapers : [];
      for (const scraper of scrapers.slice(0,32)) {
        if (Date.now() >= deadline) break;
        if (!scraper || scraper.enabled === false || !(scraper.codeUrl || scraper.filename)) continue;
        if (Array.isArray(scraper.supportedTypes)) {
          const supported = args.mediaType === "tv" ? ["tv","series","anime"] : [args.mediaType];
          if (!scraper.supportedTypes.some(type => supported.includes(type))) continue;
        }
        const codeUrl = new URL(scraper.codeUrl || scraper.filename,manifestUrl).href;
        if (!/^https:\/\//i.test(codeUrl)) continue;
        try {
          const results = await execute(await loadText(codeUrl),scraper.filename || codeUrl,
            scraper.id || scraper.name || "plugin",args,String(request.tmdbKey || ""));
          for (const result of results) {
            const url = typeof result.url === "string" ? result.url : result.url && result.url.url;
            if (typeof url !== "string" || !/^https?:\/\//i.test(url)) continue;
            const name = String(result.name || result.title || scraper.name || "Plugin");
            const quality = result.quality ? " - "+String(result.quality) : "";
            output.push({name:name+quality,title:String(result.title || name),url,
              description:String(result.description || ""),
              behaviorHints:{proxyHeaders:{request:playerHeaders(result.headers)}},
              provider:String(request.groupByRepository
                ? ((repository.name && repository.name !== "Repositório local"
                    ? repository.name : manifest.name) || repository.name || "Plugin")
                : (scraper.name || repository.name || manifest.name || "Plugin"))});
            if (output.length >= 100) return {streams:output};
          }
        } catch (error) { console.warn("[plugin] provider",scraper.id,String(error.stack || error)); }
      }
    } catch (error) { console.warn("[plugin] repository",String(error.message || error)); }
  }
  return {streams:output};
}

function readJson(req) {
  return new Promise((resolve,reject) => {
    const parts=[]; let size=0;
    req.on("data",chunk => { size+=chunk.length; if(size>MAX_BODY) req.destroy(); else parts.push(chunk); });
    req.on("end",()=>{ try {resolve(JSON.parse(Buffer.concat(parts).toString("utf8")));}catch(e){reject(e);} });
    req.on("error",reject);
  });
}

const server = http.createServer(async (req,res) => {
  if (req.socket.remoteAddress !== "127.0.0.1" && req.socket.remoteAddress !== "::1") {res.writeHead(403).end();return;}
  if (req.method === "GET" && req.url === "/health") {
    res.writeHead(200,{"Content-Type":"text/plain"}).end("ok"); return;
  }
  if (req.method === "POST" && req.url === "/clear") {
    embeddedSubtitles.clearBitmapSubtitleCaches();
    documentCache.clear(); res.writeHead(200).end("ok"); return;
  }
  if (req.method === "POST" && req.url === "/subtitles/window") {
    try {
      const result = await embeddedSubtitles.getEmbeddedTextSubtitleWindow(await readJson(req));
      res.writeHead(200,{"Content-Type":"application/json"}).end(JSON.stringify(result));
    } catch (error) {
      res.writeHead(502,{"Content-Type":"application/json"}).end(JSON.stringify({
        errorCode: String(error.code || "SUBTITLE_WINDOW_FAILED"),
        errorText: String(error.message || error)
      }));
    }
    return;
  }
  if (req.method !== "POST" || req.url !== "/streams") {res.writeHead(404).end();return;}
  try {
    const result = await streams(await readJson(req));
    res.writeHead(200,{"Content-Type":"application/json"}).end(JSON.stringify(result));
  } catch(error) {res.writeHead(500).end(JSON.stringify({error:String(error.message || error)}));}
});

function start() {
  if (!fetchServer.listening) fetchServer.listen(FETCH_PORT,"127.0.0.1");
  if (!server.listening) server.listen(PORT,"127.0.0.1");
}
service.register("ping", message => {
  try { start(); message.respond({returnValue:true,port:PORT}); }
  catch(error) {message.respond({returnValue:false,errorText:String(error.message || error)});}
});
if (require.main === module &&
    (process.env.NUVIO_PLUGIN_TEST === "1" || process.env.NUVIO_PLUGIN_STANDALONE === "1")) start();
module.exports = {start,streams,execute};
