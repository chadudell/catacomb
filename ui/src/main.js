import { createHost } from './host.js';

const PANEL_W = 1600;
const PANEL_H = 640;

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
host.onMessage((msg) => {
  if (msg.type !== 'init') return;
  msg.bits.forEach((on, i) => leds[i].classList.toggle('on', !!on));
  status.textContent = `v${msg.version} · ${host.kind} · engine connected`;
});
host.post({ type: 'ready' });
