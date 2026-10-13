"use strict";
const fs = require("fs");
const path = require("path");
const esbuild = require("./service-build/node_modules/esbuild");
const babel = require("./service-build/node_modules/@babel/core");
const preset = require("./service-build/node_modules/@babel/preset-env");
const root = path.resolve(__dirname,"..");
const output = process.argv[2] || path.join(root,"plugin-service","runtime","service.cjs");
(async function () {
  const bundle = await esbuild.build({entryPoints:[path.join(__dirname,"service-build","entry.cjs")],
    bundle:true,write:false,platform:"node",format:"cjs",target:"node8",external:["webos-service"],
    logLevel:"warning"});
  const compiled = babel.transformSync(bundle.outputFiles[0].text,{babelrc:false,configFile:false,
    presets:[[preset,{targets:{node:"0.12"},modules:false,bugfixes:true}]],
    comments:true,compact:false});
  fs.mkdirSync(path.dirname(output),{recursive:true});
  fs.writeFileSync(output,compiled.code+"\n");
  fs.copyFileSync(path.join(__dirname,"service-build","node_modules","core-js","LICENSE"),
    path.join(path.dirname(output),"core-js.LICENSE"));
  fs.copyFileSync(path.join(__dirname,"service-build","node_modules","@babel","core","LICENSE"),
    path.join(path.dirname(output),"babel.LICENSE"));
  console.log("webOS service (Node 0.12+): "+output);
})().catch(function (error) {console.error(error);process.exit(1);});
