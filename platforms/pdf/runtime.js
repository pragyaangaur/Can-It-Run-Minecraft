// Runs inside the PDF. The engine above this line is rdgeneric compiled to
// wasm and then turned into plain JavaScript by wasm2js, because PDFium's
// JavaScript has no WebAssembly. `rd` holds its exports.
//
// Each screen row is a text field. Pixels become one of six character
// groups that are the same width in the sans-serif font Chrome uses for
// fields, the trick DoomPDF found.

var W = __W__, H = __H__;
var SHADES = ["_", "::", "?", "//", "b", "#"]; // light to dark
var NAMES = ["Air", "Stone", "Grass", "Dirt", "Cobble", "Planks", "Log", "Leaves"];
var hold = {};
var oneshot = { dig: 0, place: 0, reset: 0, select: 0 };
var prevRows = [];
var last = Date.now(), acc = 0, frames = 0, fpsT = last, fps = 0;

rd.init((Date.now() % 1000000) | 0, 1, 28);

function field(name) { return globalThis.getField(name); }

// Typed keys give no release event, so each one counts as held for a moment.
function press(k) {
  var t = Date.now();
  hold[k] = t + ((hold[k] || 0) > t ? 140 : 450);
}

function key_pressed(s) {
  if (!s) return;
  for (var i = 0; i < s.length; i++) {
    var c = s.charAt(i).toLowerCase();
    if ("wasdijkl ".indexOf(c) >= 0) press(c);
    else if (c === "f") oneshot.dig = 1;
    else if (c === "e") oneshot.place = 1;
    else if (c === "r") oneshot.reset = 1;
    else if (c >= "1" && c <= "7") oneshot.select = +c;
    // "!" starts the scripted demo and "." steps it by two ticks, so a
    // recorder can capture frames that match the other platforms.
    else if (c === "!") { demo = true; rd.demo_start(); draw(); }
    else if (c === "." && demo) { rd.demo_step(2); draw(); }
  }
}

function btn_down(k) {
  if (k === "f") oneshot.dig = 1;
  else if (k === "e") oneshot.place = 1;
  else if (k === "n") {
    var s = rd.selected() + 1;
    oneshot.select = s > 7 ? 1 : s;
  } else hold[k] = 1e15;
}
function btn_up(k) { if (hold[k] > 1e14) hold[k] = 0; }

function held(k) { return (hold[k] || 0) > Date.now() ? 1 : 0; }

var demo = false;

function draw() {
  var ptr = rd.render(W, H);
  var px = new Uint8Array(rd.memory.buffer, ptr, W * H * 4);
  for (var y = 0; y < H; y++) {
    var row = [];
    for (var x = 0; x < W; x++) {
      var i = (y * W + x) * 4;
      var l = (px[i] * 3 + px[i + 1] * 6 + px[i + 2]) / 10;
      row.push(l > 205 ? SHADES[0] : l > 165 ? SHADES[1] : l > 125 ? SHADES[2] :
               l > 90 ? SHADES[3] : l > 55 ? SHADES[4] : SHADES[5]);
    }
    var s = row.join("");
    if (s !== prevRows[y]) { field("row_" + y).value = s; prevRows[y] = s; }
  }
}

function frame() {
  if (demo) return;
  try {
    var now = Date.now();
    acc += Math.min(250, now - last);
    last = now;
    rd.set_input(held("w") - held("s"), held("d") - held("a"), held(" "));
    rd.action(oneshot.dig, oneshot.place, oneshot.select, oneshot.reset);
    oneshot = { dig: 0, place: 0, reset: 0, select: 0 };
    while (acc >= 1000 / 60) {
      rd.turn((held("l") - held("j")) * 0.035, (held("i") - held("k")) * 0.03);
      rd.tick();
      acc -= 1000 / 60;
    }

    draw();
    frames++;
    if (now - fpsT > 1000) {
      fps = Math.round(frames * 1000 / (now - fpsT));
      frames = 0;
      fpsT = now;
      field("status").value = "block " + NAMES[rd.selected()] + ", " + fps + " fps";
    }
  } catch (e) {
    field("status").value = "error: " + e;
  }
}

function reset_input_box() { field("keys").value = "Click here, then type to play"; }
app.setInterval("reset_input_box()", 1500);
app.setInterval("frame()", 0);
