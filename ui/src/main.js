import { createHost } from './host.js';

const PANEL_W = 1600;
const PANEL_H = 640;

const SCALES = [
  'Unquantized', 'Chromatic', 'Major', 'Pentatonic', 'Melodic Minor', 'Harmonic Minor',
  'Diminished 6th', 'Whole Tone', 'Hirajoshi', '7 Sus 4', 'Major 7th', 'Major 13th',
  'Minor 7th', 'Minor 11th', 'Hang Drum', 'Quads',
];

const panel = document.getElementById('panel');
const status = document.getElementById('status');

// Scale the fixed-size panel to the window.
function fit() {
  const s = Math.min(window.innerWidth / PANEL_W, window.innerHeight / PANEL_H);
  panel.style.transform = `scale(${s})`;
}
window.addEventListener('resize', fit);
fit();

function buildLeds(id) {
  const row = document.getElementById(id);
  return Array.from({ length: 8 }, () => {
    const led = document.createElement('div');
    led.className = 'led';
    row.appendChild(led);
    return led;
  });
}
const leds = [...buildLeds('seq1'), ...buildLeds('seq2')];

const host = createHost();
let version = '';

// Buttons send a press and a release, so held combos work like the hardware.
for (const el of document.querySelectorAll('button[data-button]')) {
  const button = el.dataset.button;
  el.addEventListener('pointerdown', () => host.post({ type: 'press', button }));
  for (const ev of ['pointerup', 'pointerleave'])
    el.addEventListener(ev, (e) => {
      if (e.type === 'pointerleave' && !(e.buttons & 1)) return;
      host.post({ type: 'release', button });
    });
}
document.querySelector('[data-action="reroll"]').addEventListener('click', () => host.post({ type: 'reroll' }));

host.onMessage((msg) => {
  if (msg.type === 'init') version = msg.version;
  if (msg.type !== 'seq') return;
  msg.bits.forEach((on, i) => {
    const seq = i < 8 ? 0 : 1;
    const inLoop = i % 8 < msg.length[seq];
    leds[i].classList.toggle('on', !!on);
    leds[i].classList.toggle('play', msg.play.includes(i));
    leds[i].classList.toggle('out', !inLoop);
  });
  status.textContent =
    `v${version} · ${host.kind} · ${msg.running ? 'running' : 'stopped'} · ` +
    `${SCALES[msg.quantMode]}${msg.chained ? ' · chained' : ''} · ${msg.bpm.toFixed(1)} BPM` +
    (msg.hostPlaying ? ' · host playing' : '');
});
host.post({ type: 'ready' });
