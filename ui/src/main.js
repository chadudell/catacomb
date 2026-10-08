import { BitDisplay } from './bits.js';
import { formatParam } from './format.js';
import { createHost } from './host.js';
import { Knob, setPanelScale } from './knob.js';
import { PatchBay } from './patchbay.js';

const PANEL_W = 1800;
const PANEL_H = 720;

const SCALES = [
  'Unquantized', 'Chromatic', 'Major', 'Pentatonic', 'Melodic Minor', 'Harmonic Minor',
  'Diminished 6th', 'Whole Tone', 'Hirajoshi', '7 Sus 4', 'Major 7th', 'Major 13th',
  'Minor 7th', 'Minor 11th', 'Hang Drum', 'Quads (Minor 3rds)',
];

const panel = document.getElementById('panel');
const readout = document.getElementById('readout');
const status = document.getElementById('status');

// ---- Fit the fixed-size panel to the window ------------------------------------------------
function fit() {
  const s = Math.min(window.innerWidth / PANEL_W, window.innerHeight / PANEL_H);
  panel.style.transform = `scale(${s})`;
  setPanelScale(s);
}
window.addEventListener('resize', fit);
fit();

const host = createHost();
const knobs = new Map(); // param id → Knob
const controls = new Map(); // param id → {set(value)} for switches, menus, checkboxes
let meta = {};

// ---- Module grid ----------------------------------------------------------------------------
// Everything in a module is placed from data-x / data-y (module coordinates): a knob so
// its centre lands on the point (same-size knobs on a row line then share a label
// baseline), anything else centred on it. Links run from data-x1 to data-x2 at data-y.
function layoutModules() {
  for (const el of document.querySelectorAll('.mod > [data-x]')) {
    let cx = el.offsetWidth / 2;
    let cy = el.offsetHeight / 2;
    const knob = el.classList.contains('ctl') && el.querySelector('.knob');
    if (knob) {
      cx = knob.offsetLeft + knob.offsetWidth / 2;
      cy = knob.offsetTop + knob.offsetHeight / 2;
    }
    el.style.left = `${Number(el.dataset.x) - cx}px`;
    el.style.top = `${Number(el.dataset.y) - cy}px`;
  }
  for (const el of document.querySelectorAll('.mod > .link')) {
    el.style.left = `${el.dataset.x1}px`;
    el.style.top = `${el.dataset.y}px`;
    el.style.width = `${el.dataset.x2 - el.dataset.x1}px`;
  }
}
layoutModules();
document.fonts?.ready.then(layoutModules);

// ---- Value readout ------------------------------------------------------------------------
function showReadout(knob, id, on) {
  if (!on) {
    readout.hidden = true;
    return;
  }
  const r = knob.el.getBoundingClientRect();
  const p = panel.getBoundingClientRect();
  const s = p.width / PANEL_W;
  readout.style.left = `${(r.left + r.width / 2 - p.left) / s}px`;
  readout.style.top = `${(r.top - p.top) / s - 4}px`;
  readout.textContent = formatParam(id, knob.value, meta[id]);
  readout.hidden = false;
}

// ---- Build controls from the markup ----------------------------------------------------------
function buildKnobs() {
  for (const ctl of document.querySelectorAll('.ctl[data-param]')) {
    const id = ctl.dataset.param;
    const m = meta[id];
    if (!m) continue;
    ctl.replaceChildren();
    const label = document.createElement('div');
    label.className = 'label';
    label.textContent = ctl.dataset.label || m.name;
    ctl.appendChild(label);
    ctl.title = m.name;
    const knob = new Knob(ctl, {
      size: ctl.dataset.size,
      min: m.min,
      max: m.max,
      def: m.def,
      value: m.def,
      onChange: (v) => host.post({ type: 'param', id, value: v }),
      onGesture: (begin) => host.post({ type: 'gesture', id, begin }),
      onHover: (k, on) => showReadout(k, id, on),
    });
    if (ctl.dataset.lo || ctl.dataset.hi) {
      const legend = document.createElement('div');
      legend.className = 'legend';
      legend.innerHTML = `<span>${ctl.dataset.lo || ''}</span><span>${ctl.dataset.hi || ''}</span>`;
      ctl.appendChild(legend);
    }
    knobs.set(id, knob);
  }
}

function setParam(id, value) {
  host.post({ type: 'gesture', id, begin: true });
  host.post({ type: 'param', id, value });
  host.post({ type: 'gesture', id, begin: false });
}

function buildOrder() {
  const box = document.querySelector('.order');
  const names = meta.order?.choices || ['Parallel', 'VCW > VCF', 'VCF > VCW'];
  const buttons = names.map((name, i) => {
    const b = document.createElement('button');
    b.textContent = name.replace('>', '→');
    b.addEventListener('click', () => setParam('order', i));
    box.appendChild(b);
    return b;
  });
  controls.set('order', { set: (v) => buttons.forEach((b, i) => b.classList.toggle('on', i === Math.round(v))) });
}

function buildMenus() {
  for (const sel of document.querySelectorAll('select[data-param]')) {
    const id = sel.dataset.param;
    (meta[id]?.choices || []).forEach((name, i) => sel.add(new Option(name, i)));
    sel.addEventListener('change', () => setParam(id, Number(sel.value)));
    controls.set(id, { set: (v) => (sel.value = String(Math.round(v))) });
  }
  for (const box of document.querySelectorAll('input[type="checkbox"][data-param]')) {
    const id = box.dataset.param;
    box.addEventListener('change', () => setParam(id, box.checked ? 1 : 0));
    controls.set(id, { set: (v) => (box.checked = v > 0.5) });
  }

  const scale = document.getElementById('scale');
  SCALES.forEach((name, i) => scale.add(new Option(`${i + 1}. ${name}`, i)));
  scale.addEventListener('change', () => host.post({ type: 'setQuantMode', value: Number(scale.value) }));

  document.getElementById('reroll').addEventListener('click', () => host.post({ type: 'reroll' }));

  const preset = document.getElementById('preset');
  preset.addEventListener('change', () => host.post({ type: 'loadPreset', index: Number(preset.value) }));
}

// Momentary buttons: a press and a release, so held combos work like the hardware.
function buildButtons() {
  for (const b of document.querySelectorAll('[data-button]')) {
    const button = b.dataset.button;
    let down = false;
    b.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      b.setPointerCapture(e.pointerId);
      down = true;
      b.classList.add('held');
      host.post({ type: 'press', button });
    });
    const up = () => {
      if (!down) return;
      down = false;
      b.classList.remove('held');
      host.post({ type: 'release', button });
    };
    b.addEventListener('pointerup', up);
    b.addEventListener('pointercancel', up);
  }
}

function applyParams(values) {
  for (const [id, v] of Object.entries(values)) {
    knobs.get(id)?.set(v);
    controls.get(id)?.set(v);
  }
}

// ---- Sequencer view -----------------------------------------------------------------------
const bits = new BitDisplay(document.getElementById('leds1'), document.getElementById('leds2'), (cell) =>
  host.post({ type: 'toggleCell', cell }),
);
const runLamp = document.getElementById('runLamp');
const chainLamp = document.getElementById('chainLamp');
const scaleMenu = document.getElementById('scale');
let version = '';

function applySeq(v) {
  bits.update(v);
  runLamp.classList.toggle('on', v.running);
  chainLamp.classList.toggle('on', v.chained);
  if (document.activeElement !== scaleMenu) scaleMenu.value = String(v.quantMode);
  const audio = v.audio === undefined ? '' : v.audio ? `audio ${(v.sampleRate / 1000).toFixed(1)} kHz` : 'no audio yet';
  const level = v.peakDb === undefined ? '' : v.peakDb <= -100 ? 'silent' : `out ${v.peakDb} dB`;
  status.textContent = [
    `v${version}`, audio, `${v.bpm.toFixed(1)} BPM`, v.hostPlaying ? 'Logic playing' : 'Logic stopped',
    v.notes !== undefined ? `MIDI ${v.notes}` : '', level,
  ].filter(Boolean).join(' · ');
}

// ---- Patch bay ------------------------------------------------------------------------------
const tip = document.getElementById('tip');
function showTip(t) {
  if (!t) {
    tip.hidden = true;
    return;
  }
  const r = t.el.getBoundingClientRect();
  const p = panel.getBoundingClientRect();
  const s = p.width / PANEL_W;
  tip.style.left = `${(r.left - p.left) / s - 10}px`;
  tip.style.top = `${(r.top + r.height / 2 - p.top) / s}px`;
  tip.querySelector('b').textContent = t.title;
  tip.querySelector('span').textContent = t.body;
  tip.hidden = false;
}
const bay = new PatchBay(document.getElementById('bay'), panel, PANEL_W, (cables) => host.post({ type: 'patch', cables }), showTip);
document.getElementById('clearCables').addEventListener('click', () => bay.clear());

// ---- Messages ------------------------------------------------------------------------------
let built = false;
host.onMessage((msg) => {
  switch (msg.type) {
    case 'init':
      version = msg.version;
      meta = msg.meta;
      if (!built) {
        buildKnobs();
        buildOrder();
        buildMenus();
        buildButtons();
        built = true;
        layoutModules();
      }
      applyParams(msg.params);
      bay.setText(msg.cables);
      {
        const preset = document.getElementById('preset');
        preset.replaceChildren(...(msg.presets || []).map((name, i) => new Option(name, i)));
        preset.value = String(msg.preset ?? 0);
      }
      break;
    case 'patch':
      bay.setText(msg.cables);
      break;
    case 'params':
      applyParams(msg.values);
      break;
    case 'seq':
      applySeq(msg);
      break;
    case 'panel':
      bits.event(msg.event, msg.value);
      break;
  }
});
host.post({ type: 'ready' });
