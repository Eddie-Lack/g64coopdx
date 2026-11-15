// Runs a browser port of Glover (not public) in node and dumps game frames for the native renderer's test (gfxtest.c).
// It starts a level the way the browser port's screenshot tool does and steps the same frames with the same keys. A dump and
// a browser screenshot of the same frame show the same picture.
//
// Usage: node dump.mjs ROM LEVEL OUTDIR [--frames N ...] [--keys CODE:FROM:TO ...] [--wasm FILE]
//   --frames: the game frames after which to dump (default 30 90)
//   --keys: hold a key (a KeyboardEvent code, as in the page) from one game frame to another, e.g. ArrowUp:40:80
//   --wasm: the browser port's glover.wasm (default: $GLOVEWORLD/build/port/web/glover.wasm, GLOVEWORLD defaults to
//           ../gloveworld next to this repository)
//
// For each frame it writes OUTDIR/L<level>-<frame>.mem (the whole WebAssembly memory) and
// OUTDIR/L<level>-<frame>.txt: the two frame buffer addresses, then one line per task: address, size in bytes and
// 1 for a Sprite2D task. The dumps hold game data: keep them out of the repository.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const KEYS = {
  ArrowUp: "up", ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right",
  KeyW: "up", KeyS: "down", KeyA: "left", KeyD: "right",
  Space: 0x8000, KeyX: 0x4000, KeyZ: 0x2000, ShiftLeft: 0x2000, Enter: 0x1000,
  KeyQ: 0x0020, KeyE: 0x0010, KeyI: 0x0008, KeyK: 0x0004, KeyJ: 0x0002, KeyL: 0x0001,
};

const args = process.argv.slice(2);
const positional = [];
let frames = [30, 90];
const keys = [];
const here = path.dirname(fileURLToPath(import.meta.url));
const gloveworld = process.env.GLOVEWORLD || path.join(here, "../../../gloveworld");
let wasmPath = path.join(gloveworld, "build/port/web/glover.wasm");
for (let i = 0; i < args.length; i++) {
  if (args[i] === "--frames") {
    frames = [];
    while (i + 1 < args.length && !args[i + 1].startsWith("--")) frames.push(Number(args[++i]));
  } else if (args[i] === "--keys") {
    while (i + 1 < args.length && !args[i + 1].startsWith("--")) {
      const [code, from, to] = args[++i].split(":");
      keys.push({ code, from: Number(from), to: Number(to) });
    }
  } else if (args[i] === "--wasm") {
    wasmPath = args[++i];
  } else {
    positional.push(args[i]);
  }
}
const [romPath, levelArg, outDir] = positional;
if (!romPath || levelArg === undefined || !outDir) {
  console.error("usage: node dump.mjs ROM LEVEL OUTDIR [--frames N ...] [--keys CODE:FROM:TO ...] [--wasm FILE]");
  process.exit(2);
}
fs.mkdirSync(outDir, { recursive: true });

let memory;
const text = (p, n) => Buffer.from(memory.buffer, p, n).toString("latin1");
const t0 = performance.now();
const env = {
  log: (p, n) => console.log("[game] " + text(p, n).replace(/\n+$/, "")),
  abort: (p, n) => { throw new Error("abort: " + text(p, n)); },
  time_us: () => (performance.now() - t0) * 1000,
  sin: Math.sin, cos: Math.cos, tan: Math.tan, atan2: Math.atan2,
  sound: () => 0,
};
const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), { env });
const ex = instance.exports;
memory = ex.memory;
const rom = fs.readFileSync(romPath);
const need = ex.port_memory_needed(rom.length);
if (need > memory.buffer.byteLength) memory.grow(Math.ceil((need - memory.buffer.byteLength) / 65536));
new Uint8Array(memory.buffer, ex.port_rom_buffer(rom.length), rom.length).set(rom);
const level = Number(levelArg);
console.log(`level ${level}: mode ${ex.port_start(level)}`);

// The keys held at a frame, as the page's input() turns them into buttons and a stick position.
function input(held) {
  let buttons = 0, x = 0, y = 0;
  for (const code of held) {
    const k = KEYS[code];
    if (k === "up") y += 80;
    else if (k === "down") y -= 80;
    else if (k === "left") x -= 80;
    else if (k === "right") x += 80;
    else if (k !== undefined) buttons |= k;
  }
  return [buttons, Math.max(-80, Math.min(80, x)), Math.max(-80, Math.min(80, y))];
}

const held = new Set();
const last = Math.max(...frames);
for (let frame = 0; frame < last;) {
  for (const k of keys) {
    if (frame === k.from) held.add(k.code);
    if (frame === k.to) held.delete(k.code);
  }
  // As the page does: retrace until the game builds a frame (at most 11 retraces).
  let drawn = false;
  for (let n = 0; n <= 10 && !drawn; n++) {
    ex.port_set_input(...input(held));
    drawn = ex.port_retrace() !== 0;
  }
  frame++;
  if (!frames.includes(frame)) continue;
  const n = ex.port_frame_task_count();
  const t = new Uint32Array(memory.buffer, ex.port_frame_tasks(), n * 3);
  const lines = [`${ex.port_framebuffer(0)} ${ex.port_framebuffer(1)}`];
  for (let i = 0; i < n; i++) lines.push(`${t[i * 3]} ${t[i * 3 + 1]} ${t[i * 3 + 2] ? 1 : 0}`);
  const base = path.join(outDir, `L${level}-${frame}`);
  fs.writeFileSync(base + ".mem", new Uint8Array(memory.buffer));
  fs.writeFileSync(base + ".txt", lines.join("\n") + "\n");
  console.log(`frame ${frame}: ${n} tasks, mode ${ex.port_game_mode()} -> ${base}.mem`);
}
