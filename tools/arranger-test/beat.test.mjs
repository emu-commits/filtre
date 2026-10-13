// node tools/arranger-test/beat.test.mjs [file.f32 ...]
// Synthetic tracks with a known tempo and first downbeat; raw mono f32le
// 44.1 kHz files given on the command line are analysed and printed.
import { analyse } from '../../docs/arranger/beat.js';
import { readFileSync } from 'node:fs';

const SR = 44100;
let pass = 0, fail = 0;
const check = (ok, msg) => { if (ok) pass++; else { fail++; console.log('  FAIL ' + msg); } };

// four-on-the-floor with offbeat hats, a clap on 2 and 4, and a bass note
// that changes every bar: the bar line is where the harmony moves
function house(bpm, offset, bars, seedBass = [55, 55, 65.4, 49]) {
  const beat = 60 / bpm, n = Math.floor((offset + bars * 4 * beat + 1) * SR), x = new Float32Array(n);
  let seed = 1;
  const noise = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 2147483648 - 1; };
  for (let b = 0; b < bars * 4; b++) {
    const t0 = offset + b * beat, i0 = Math.round(t0 * SR);
    for (let i = 0; i < SR * 0.25 && i0 + i < n; i++) {                       // kick
      const t = i / SR; x[i0 + i] += 0.8 * Math.sin(2 * Math.PI * (50 * t + 40 * (1 - Math.exp(-t * 30)) / 30 * 3)) * Math.exp(-t * 14);
    }
    const h0 = Math.round((t0 + beat / 2) * SR);
    for (let i = 0; i < SR * 0.04 && h0 + i < n; i++) x[h0 + i] += 0.2 * noise() * Math.exp(-i / (SR * 0.01));   // hat
    if (b % 2 === 1) for (let i = 0; i < SR * 0.1 && i0 + i < n; i++) x[i0 + i] += 0.3 * noise() * Math.exp(-i / (SR * 0.03));   // clap
    const f = seedBass[Math.floor(b / 4) % seedBass.length] * 2;
    for (let i = 0; i < beat * SR && i0 + i < n; i++) {                       // bass + a chord tone
      const t = i / SR; x[i0 + i] += 0.25 * Math.sin(2 * Math.PI * f * t) * Math.exp(-t * 4) + 0.1 * Math.sin(2 * Math.PI * f * 3 * t) * Math.exp(-t * 3);
    }
  }
  return x;
}

for (const [bpm, off] of [[124, 0.0], [121.3, 0.731], [128, 2.4], [110.5, 0.25], [96, 1.1], [133.33, 0.05]]) {
  const r = analyse(house(bpm, off, 48), SR);
  const bar = 240 / r.bpm;
  let err = (r.downbeat - off) % bar; if (err > bar / 2) err -= bar; if (err < -bar / 2) err += bar;
  console.log(`bpm ${bpm} off ${off}  ->  ${r.bpm}  downbeat ${r.downbeat.toFixed(3)}  (bar phase error ${(err * 1000).toFixed(1)} ms, conf ${r.confidence.toFixed(2)}/${r.barConfidence.toFixed(2)})`);
  check(Math.abs(r.bpm - bpm) < 0.03, `tempo ${bpm}: got ${r.bpm}`);
  check(Math.abs(err) < 0.02, `bar line ${bpm}: off by ${(err * 1000).toFixed(1)} ms`);
  check(Math.abs(r.downbeat - off) < 0.02, `measure 1 at the first sound ${bpm}: ${r.downbeat.toFixed(3)} vs ${off}`);
}

for (const f of process.argv.slice(2)) {
  const b = readFileSync(f), x = new Float32Array(b.buffer, b.byteOffset, b.byteLength >> 2);
  const t = Date.now(), r = analyse(x, SR);
  console.log(`${f.split('/').pop()}: ${r.bpm} bpm, measure 1 at ${r.downbeat.toFixed(3)} s, first sound ${r.firstSound.toFixed(2)}, conf ${r.confidence.toFixed(2)}/${r.barConfidence.toFixed(2)}, ${(r.duration / 60).toFixed(1)} min in ${Date.now() - t} ms`);
}
console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
