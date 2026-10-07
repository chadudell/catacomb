// The patch bay: 32 jacks and the cables between them.
//
//   drag from a jack to another       patch a cable (output ↔ input, either direction)
//   drag from an output again         another cable from it (a multiple)
//   drag from a patched input         pull its newest cable out: drop it elsewhere, or anywhere to remove it
//   double-click a jack               remove every cable at it
//
// An input with no cable runs on its normal (the tooltip says what that is). An input
// with several cables sums them. onChange(text) gets the cables as "out>in,…".

const OUT = 'out';
const IN = 'in';

// One row per function. [id, label, kind, what it does]
const ROWS = [
  [['vco1voct', 'VCO 1V/OCT', IN, '1 V/octave, summed with VCO FREQUENCY'],
   ['mvco1voct', 'M VCO 1V/OCT', IN, '1 V/octave, summed with MOD VCO FREQUENCY'],
   ['mvcoSync', 'M VCO SYNC', IN, 'Restarts the MOD VCO on a rising edge'],
   ['mvco', 'M VCO', OUT, 'The MOD VCO triangle, ±5 V']],
  [['umix1', 'U MIX 1', IN, 'Utility mixer channel 1, set by U MIX 1 LVL. Normal: RING MOD'],
   ['umix2', 'U MIX 2', IN, 'Utility mixer channel 2, at unity'],
   ['noise', 'NOISE', OUT, 'The noise generator, after NOISE TONE'],
   ['mixer', 'MIXER', OUT, 'The mixer, before the wavefolder and filter']],
  [['vcwIn', 'VCW IN', IN, 'Wavefolder audio in. Normal: MIXER (or the VCF, with ORDER at VCF → VCW)'],
   ['fold', 'FOLD', IN, 'Fold CV, scaled by EG1/CV AMT. Normal: EG1'],
   ['vcfIn', 'VCF IN', IN, 'Filter audio in. Normal: MIXER (or the VCW, with ORDER at VCW → VCF)'],
   ['cutoff', 'CUTOFF', IN, 'Cutoff CV, 1 V/oct at full EG1/CV AMT. Normal: EG1']],
  [['vcwVcaCv', 'VCW VCA', IN, 'Wavefolder path VCA, 0–8 V. Normal: EG2'],
   ['vcfVcaCv', 'VCF VCA', IN, 'Filter path VCA, 0–8 V. Normal: EG2'],
   ['blend', 'BLEND', IN, 'BLEND CV, ±5 V, summed with the knob'],
   ['vca', 'VCA', OUT, 'The final output, after VOLUME']],
  [['trigger', 'TRIGGER', IN, 'Fires EG1 and EG2 (just EG1 if EG2 TRIG is patched). Normal: the TRIGGER button and EG TRIG MIX'],
   ['eg2Trig', 'EG2 TRIG', IN, 'Fires EG2. Normal: the TRIGGER input'],
   ['eg1', 'EG1', OUT, 'Envelope 1, 0–8 V'],
   ['eg2', 'EG2', OUT, 'Envelope 2, 0–8 V']],
  [['clock1', 'CLOCK 1', IN, 'Clocks SEQ1. Normal: the internal clock'],
   ['bitFlip1', 'BIT FLIP 1', IN, 'High on a step: flips SEQ1’s bit at the write head'],
   ['seq1Cv', 'SEQ1 CV', OUT, 'SEQ1’s voltage, after CV RANGE and the quantizer'],
   ['seq1Trig', 'SEQ1 TRIG', OUT, 'A trigger on each SEQ1 step that’s on']],
  [['clock2', 'CLOCK 2', IN, 'Clocks SEQ2. Normal: CLOCK 1'],
   ['bitFlip2', 'BIT FLIP 2', IN, 'High on a step: flips SEQ2’s bit at the write head'],
   ['seq2Cv', 'SEQ2 CV', OUT, 'SEQ2’s voltage, after CV RANGE and the quantizer'],
   ['seq2Trig', 'SEQ2 TRIG', OUT, 'A trigger on each SEQ2 step that’s on']],
  [['reset', 'RESET', IN, 'Sends both play heads to bit 1 (optionally recalls the BUFFER)'],
   ['umix12', 'U MIX 1+2', OUT, 'The utility mixer’s output'],
   ['clock', 'CLOCK', OUT, 'The clock driving the sequencers'],
   ['sidechain', 'SIDECHAIN', OUT, 'Audio from Logic’s side chain (plugin header → Side Chain), 0 dBFS = ±10 V']],
];

const COLORS = ['#e8604f', '#5fb8e8', '#f2c14e', '#4fc9a4', '#c792ea', '#ff9f5a', '#d9d1bd', '#7bd88f'];
const SVGNS = 'http://www.w3.org/2000/svg';

export class PatchBay {
  constructor(root, panel, panelWidth, onChange, onTip) {
    this.root = root;
    this.panel = panel;
    this.panelWidth = panelWidth;
    this.onChange = onChange;
    this.onTip = onTip;
    this.cables = []; // [{out, in}]
    this.jacks = new Map(); // id → {el, kind, label, info}
    this.drag = null;

    const grid = document.createElement('div');
    grid.className = 'jacks';
    for (const row of ROWS)
      for (const [id, label, kind, info] of row) {
        const cell = document.createElement('div');
        cell.className = `jack-cell ${kind}`;
        const jack = document.createElement('div');
        jack.className = 'jack';
        jack.dataset.jack = id;
        const name = document.createElement('div');
        name.className = 'jack-label';
        name.textContent = label;
        cell.append(jack, name);
        grid.appendChild(cell);
        this.jacks.set(id, { el: jack, kind, label, info });
        this.bindJack(id, jack);
      }
    root.appendChild(grid);

    this.svg = document.createElementNS(SVGNS, 'svg');
    this.svg.classList.add('cables');
    root.appendChild(this.svg);
    window.addEventListener('pointermove', (e) => this.move(e));
    window.addEventListener('pointerup', (e) => this.drop(e));
  }

  // ---- Cables as text ------------------------------------------------------------------
  setText(text) {
    this.cables = (text || '')
      .split(',')
      .map((s) => s.split('>'))
      .filter(([o, i]) => this.jacks.get(o)?.kind === OUT && this.jacks.get(i)?.kind === IN)
      .map(([out, inp]) => ({ out, in: inp }));
    this.draw();
  }
  text() {
    return this.cables.map((c) => `${c.out}>${c.in}`).join(',');
  }
  changed() {
    this.draw();
    this.onChange(this.text());
  }
  clear() {
    this.cables = [];
    this.changed();
  }

  // ---- Interaction ----------------------------------------------------------------------
  bindJack(id, el) {
    el.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      const jack = this.jacks.get(id);
      let from = id;
      let color = COLORS[this.cables.length % COLORS.length];
      if (jack.kind === IN) {
        // Pull the newest cable out of this input and carry it by its free end.
        for (let i = this.cables.length - 1; i >= 0; i--)
          if (this.cables[i].in === id) {
            from = this.cables[i].out;
            color = this.colorOf(i);
            this.cables.splice(i, 1);
            this.changed();
            break;
          }
      }
      this.drag = { from, color, x: 0, y: 0 };
      this.move(e);
    });
    el.addEventListener('dblclick', () => {
      const before = this.cables.length;
      this.cables = this.cables.filter((c) => c.out !== id && c.in !== id);
      if (this.cables.length !== before) this.changed();
    });
    el.addEventListener('pointerenter', () => this.tip(id, el));
    el.addEventListener('pointerleave', () => this.onTip(null));
  }

  tip(id, el) {
    const j = this.jacks.get(id);
    const n = this.cables.filter((c) => c.out === id || c.in === id).length;
    const state = j.kind === IN ? (n ? `${n} cable${n > 1 ? 's' : ''}` : 'on its normal') : n ? `→ ${n}` : '';
    this.onTip({ el, title: `${j.label} · ${j.kind === IN ? 'input' : 'output'}${state ? ` · ${state}` : ''}`, body: j.info });
  }

  // Pointer position in the bay's own (unscaled) coordinates.
  local(e) {
    const r = this.root.getBoundingClientRect();
    const s = this.panel.getBoundingClientRect().width / this.panelWidth;
    return { x: (e.clientX - r.left) / s, y: (e.clientY - r.top) / s };
  }

  move(e) {
    if (!this.drag) return;
    const p = this.local(e);
    this.drag.x = p.x;
    this.drag.y = p.y;
    this.draw();
  }

  drop(e) {
    if (!this.drag) return;
    const { from } = this.drag;
    this.drag = null;
    const target = document.elementFromPoint(e.clientX, e.clientY)?.closest?.('.jack')?.dataset.jack;
    if (target && target !== from) {
      const a = this.jacks.get(from).kind;
      const b = this.jacks.get(target).kind;
      if (a !== b) {
        const out = a === OUT ? from : target;
        const inp = a === OUT ? target : from;
        if (!this.cables.some((c) => c.out === out && c.in === inp)) this.cables.push({ out, in: inp });
      }
    }
    this.changed();
  }

  // ---- Drawing ----------------------------------------------------------------------------
  colorOf(i) {
    return COLORS[i % COLORS.length];
  }

  centre(id) {
    const el = this.jacks.get(id).el;
    let x = el.offsetWidth / 2;
    let y = el.offsetHeight / 2;
    for (let n = el; n && n !== this.root; n = n.offsetParent) {
      x += n.offsetLeft;
      y += n.offsetTop;
    }
    return { x, y };
  }

  path(a, b) {
    const dist = Math.hypot(b.x - a.x, b.y - a.y);
    const sag = 18 + dist * 0.35;
    return `M ${a.x} ${a.y} C ${a.x} ${a.y + sag}, ${b.x} ${b.y + sag}, ${b.x} ${b.y}`;
  }

  cable(a, b, color, live) {
    const g = document.createElementNS(SVGNS, 'g');
    g.classList.add('cable');
    if (live) g.classList.add('live');
    const d = this.path(a, b);
    for (const [cls, stroke] of [['shadow', 'rgba(0,0,0,0.55)'], ['body', color], ['shine', 'rgba(255,255,255,0.18)']]) {
      const p = document.createElementNS(SVGNS, 'path');
      p.setAttribute('d', d);
      p.setAttribute('class', cls);
      p.setAttribute('stroke', stroke);
      g.appendChild(p);
    }
    for (const end of [a, b]) {
      const plug = document.createElementNS(SVGNS, 'circle');
      plug.setAttribute('cx', end.x);
      plug.setAttribute('cy', end.y);
      plug.setAttribute('r', 7);
      plug.setAttribute('fill', color);
      plug.setAttribute('class', 'plug');
      g.appendChild(plug);
    }
    this.svg.appendChild(g);
  }

  draw() {
    this.svg.replaceChildren();
    const patchedIn = new Set(this.cables.map((c) => c.in));
    for (const [id, j] of this.jacks) j.el.classList.toggle('patched', patchedIn.has(id) || this.cables.some((c) => c.out === id));
    this.cables.forEach((c, i) => this.cable(this.centre(c.out), this.centre(c.in), this.colorOf(i), false));
    if (this.drag) this.cable(this.centre(this.drag.from), { x: this.drag.x, y: this.drag.y }, this.drag.color, true);
  }
}
