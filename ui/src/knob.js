// A rotary knob drawn in SVG. Drag up/down (Shift = fine), scroll, double-click for
// the default. Calls onChange(value) while moving and onGesture(begin) around each
// gesture, so the host records automation cleanly.

const SIZES = { L: 92, M: 64, S: 46 };
const SWEEP = 270; // degrees, from 7:30 to 4:30
const DRAG_PIXELS = 220; // full range

const NS = 'http://www.w3.org/2000/svg';

function el(name, attrs) {
  const e = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  return e;
}

function polar(cx, cy, r, deg) {
  const a = ((deg - 90) * Math.PI) / 180;
  return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
}

function arc(cx, cy, r, from, to) {
  if (Math.abs(to - from) < 0.01) return '';
  const [x1, y1] = polar(cx, cy, r, Math.min(from, to));
  const [x2, y2] = polar(cx, cy, r, Math.max(from, to));
  const large = Math.abs(to - from) > 180 ? 1 : 0;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`;
}

export class Knob {
  constructor(container, { size = 'M', min = 0, max = 1, def = 0, value = def, format, onChange, onGesture, onHover }) {
    this.min = min;
    this.max = max;
    this.def = def;
    this.value = value;
    this.format = format || ((v) => v.toFixed(2));
    this.onChange = onChange || (() => {});
    this.onGesture = onGesture || (() => {});
    this.onHover = onHover || (() => {});
    this.bipolar = min < 0 && max > 0;
    this.dragging = false;

    const d = SIZES[size] || SIZES.M;
    const pad = 9;
    const box = d + pad * 2;
    const c = box / 2;
    const rTrack = d / 2 + 4;

    this.el = document.createElement('div');
    this.el.className = 'knob';
    const svg = el('svg', { width: box, height: box, viewBox: `0 0 ${box} ${box}` });

    // Scale ticks
    const ticks = size === 'S' ? 5 : 11;
    for (let i = 0; i < ticks; i++) {
      const deg = -SWEEP / 2 + (SWEEP * i) / (ticks - 1);
      const [x1, y1] = polar(c, c, rTrack + 3, deg);
      const [x2, y2] = polar(c, c, rTrack + (i % 5 === 0 || size === 'S' ? 8 : 6), deg);
      svg.appendChild(el('line', { x1, y1, x2, y2, class: 'tick' }));
    }

    const stroke = size === 'L' ? 3 : 2.5;
    svg.appendChild(el('path', { d: arc(c, c, rTrack, -SWEEP / 2, SWEEP / 2), class: 'track', 'stroke-width': stroke }));
    this.valueArc = el('path', { class: 'value', 'stroke-width': stroke });
    svg.appendChild(this.valueArc);

    // The knob body: a dark machined cap.
    const gradId = `kg${Math.random().toString(36).slice(2)}`;
    const defs = el('defs', {});
    const grad = el('radialGradient', { id: gradId, cx: '40%', cy: '32%', r: '75%' });
    grad.appendChild(el('stop', { offset: '0%', 'stop-color': '#4a453e' }));
    grad.appendChild(el('stop', { offset: '55%', 'stop-color': '#24211d' }));
    grad.appendChild(el('stop', { offset: '100%', 'stop-color': '#121110' }));
    defs.appendChild(grad);
    svg.appendChild(defs);
    svg.appendChild(el('circle', { cx: c, cy: c + 2, r: d / 2 - 1, fill: 'rgba(0,0,0,0.55)' }));
    svg.appendChild(el('circle', { cx: c, cy: c, r: d / 2 - 2, fill: `url(#${gradId})`, stroke: '#4d473f', 'stroke-width': 1 }));
    if (size !== 'S')
      svg.appendChild(el('circle', { cx: c, cy: c, r: d / 2 - 9, fill: 'none', stroke: 'rgba(255,255,255,0.05)', 'stroke-width': 1 }));

    this.pointer = el('line', { class: 'pointer', 'stroke-width': size === 'L' ? 3.5 : 3 });
    svg.appendChild(this.pointer);
    this.el.appendChild(svg);
    container.appendChild(this.el);

    this.geom = { c, d, rTrack };
    this.render();
    this.bind();
  }

  norm(v = this.value) {
    return (v - this.min) / (this.max - this.min);
  }

  render() {
    const { c, d, rTrack } = this.geom;
    const deg = -SWEEP / 2 + SWEEP * this.norm();
    const from = this.bipolar ? 0 : -SWEEP / 2;
    this.valueArc.setAttribute('d', arc(c, c, rTrack, from, deg));
    const [x1, y1] = polar(c, c, d * 0.12, deg);
    const [x2, y2] = polar(c, c, d / 2 - 6, deg);
    this.pointer.setAttribute('x1', x1);
    this.pointer.setAttribute('y1', y1);
    this.pointer.setAttribute('x2', x2);
    this.pointer.setAttribute('y2', y2);
  }

  // From the host (automation): ignored while the user holds the knob.
  set(v) {
    if (this.dragging) return;
    this.value = Math.min(this.max, Math.max(this.min, v));
    this.render();
  }

  change(v) {
    const clamped = Math.min(this.max, Math.max(this.min, v));
    if (clamped === this.value) return;
    this.value = clamped;
    this.render();
    this.onChange(clamped);
    this.onHover(this, true);
  }

  bind() {
    const k = this.el;
    let startY = 0;
    let startValue = 0;

    k.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      k.setPointerCapture(e.pointerId);
      this.dragging = true;
      startY = e.clientY;
      startValue = this.value;
      this.onGesture(true);
      this.onHover(this, true);
      e.preventDefault();
    });
    k.addEventListener('pointermove', (e) => {
      if (!this.dragging) return;
      const scale = (this.max - this.min) / DRAG_PIXELS / panelScale();
      const fine = e.shiftKey ? 0.15 : 1;
      // Re-anchor on each move so switching Shift mid-drag doesn't jump.
      this.change(startValue + (startY - e.clientY) * scale * fine);
      startY = e.clientY;
      startValue = this.value;
    });
    const end = (e) => {
      if (!this.dragging) return;
      this.dragging = false;
      k.releasePointerCapture?.(e.pointerId);
      this.onGesture(false);
    };
    k.addEventListener('pointerup', end);
    k.addEventListener('pointercancel', end);

    k.addEventListener('dblclick', () => {
      this.onGesture(true);
      this.change(this.def);
      this.onGesture(false);
    });

    let wheelTimer = 0;
    k.addEventListener(
      'wheel',
      (e) => {
        e.preventDefault();
        if (!wheelTimer) this.onGesture(true);
        clearTimeout(wheelTimer);
        wheelTimer = setTimeout(() => {
          wheelTimer = 0;
          this.onGesture(false);
        }, 300);
        const step = (this.max - this.min) * (e.shiftKey ? 0.002 : 0.01);
        this.change(this.value - Math.sign(e.deltaY) * step);
      },
      { passive: false },
    );

    k.addEventListener('pointerenter', () => this.onHover(this, true));
    k.addEventListener('pointerleave', () => {
      if (!this.dragging) this.onHover(this, false);
    });
  }
}

// The panel is CSS-scaled; drag distances are measured in panel pixels.
let currentScale = 1;
export function setPanelScale(s) {
  currentScale = s;
}
function panelScale() {
  return currentScale;
}
