// Runs the script embedded in build/rdgeneric.pdf under Node, with small
// stand-ins for PDFium's getField and app objects. Fails if the script
// throws or draws an empty screen. Usage: node pdf_script.js file.pdf
const fs = require("fs"), vm = require("vm");
const pdf = fs.readFileSync(process.argv[2], "latin1");
let script = "";
for (const m of pdf.matchAll(/stream\n([\s\S]*?)\nendstream/g)) if (m[1].length > script.length) script = m[1];
const fields = {}, intervals = [], alerts = [];
const ctx = { app: { setInterval: e => intervals.push(e), alert: m => alerts.push(m) }, Date, Math };
ctx.globalThis = ctx;
ctx.getField = n => (fields[n] ||= { value: "" });
vm.createContext(ctx);
vm.runInContext(script, ctx);
const t0 = Date.now();
for (let i = 0; i < 10; i++) vm.runInContext("frame()", ctx);
const ms = (Date.now() - t0) / 10;
const rows = Object.keys(fields).filter(k => k.startsWith("row_"));
const distinct = new Set(rows.map(k => fields[k].value)).size;
if (alerts.length) { console.error("script alerted:", alerts[0]); process.exit(1); }
if (!intervals.includes("frame()")) { console.error("no frame timer"); process.exit(1); }
if (rows.length < 50 || distinct < 10) { console.error("screen looks empty"); process.exit(1); }
console.log(`pdf script: ${rows.length} rows, ${distinct} distinct, ${ms.toFixed(0)} ms per frame in node`);
