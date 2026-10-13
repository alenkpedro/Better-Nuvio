// webOS 3/4 use Node 0.12; webOS 5/6 use Node 8. These API polyfills are
// bundled at build time, so the installed service has no npm dependencies.
require("core-js/stable");
var url = require("url");
if (!url.URL) url.URL = global.URL;
if (!Buffer.from) Buffer.from = function (value, encoding) { return new Buffer(value, encoding); };
if (!Buffer.alloc) Buffer.alloc = function (size) { var buffer = new Buffer(size); buffer.fill(0); return buffer; };
module.exports = require("../../plugin-service/src/index.js");
if (process.env.NUVIO_PLUGIN_TEST === "1" || process.env.NUVIO_PLUGIN_STANDALONE === "1") module.exports.start();
