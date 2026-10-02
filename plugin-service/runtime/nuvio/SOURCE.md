# Nuvio official embedded subtitle service

Source from NuvioMedia/NuvioTVSmart, commit
48a94b347837965e2e052f82707820c0dd7c8028,
services/webos/src/bitmapSubtitles.js. Distributed under GPL-3.0.

Local compatibility change: requestRange uses url.parse(url) and
http(s).request(options, callback), supported by webOS Node 0.12/8. The
extraction algorithm remains the upstream implementation.

Used directly for getEmbeddedTextSubtitleWindow. It replaces Better Nuvio's
mkvass extractor, prefetch scheduler and sidecar caches.
