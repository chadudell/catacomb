// The 2 × 8 bit display. Normally: red = bit on, a green ring = play head, a blinking
// dashed ring = write head (when it's offset from the play head), dim = outside the
// sequence's length. The panel's feedback temporarily takes it over, as on the
// hardware (manual p. 37):
//   LENGTH        green LEDs show that sequencer's length
//   scale change  all green, red at the scale's number (1–16)
//   BUFFER save / clear   all green, flashing three times
//   BUFFER recall a single green flash

const OVERLAY_MS = { showLength: 1200, showQuantMode: 1500, bufferSaved: 900, cleared: 900, bufferRecalled: 250 };

export class BitDisplay {
  constructor(row1, row2, onToggle) {
    this.leds = [];
    for (const [row, offset] of [[row1, 0], [row2, 8]]) {
      for (let i = 0; i < 8; i++) {
        const led = document.createElement('div');
        led.className = 'led';
        const cell = offset + i;
        led.addEventListener('click', () => onToggle(cell));
        row.appendChild(led);
        this.leds.push(led);
      }
    }
    this.view = null;
    this.overlay = null; // {kind, value, until}
    const tick = () => {
      this.render();
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }

  update(view) {
    this.view = view;
  }

  event(kind, value) {
    if (!(kind in OVERLAY_MS)) return;
    this.overlay = { kind, value, start: performance.now(), until: performance.now() + OVERLAY_MS[kind] };
  }

  render() {
    const v = this.view;
    if (!v) return;
    const now = performance.now();
    const o = this.overlay && now < this.overlay.until ? this.overlay : null;

    for (let i = 0; i < 16; i++) {
      const led = this.leds[i];
      const seq = i < 8 ? 0 : 1;
      const bit = i % 8;
      let color = v.bits[i] ? 'red' : '';
      let play = v.play.includes(i);
      let write = v.write.includes(i) && !v.play.includes(i) && v.running;
      let out = bit >= v.length[seq];

      if (o) {
        play = write = out = false;
        if (o.kind === 'showLength') {
          color = seq === o.value ? (bit < v.length[seq] ? 'green' : '') : v.bits[i] ? 'red' : '';
          out = seq !== o.value;
        } else if (o.kind === 'showQuantMode') {
          color = i === v.quantMode ? 'red' : 'green';
        } else {
          // Flashes: three on/off cycles (one for a recall).
          const cycles = o.kind === 'bufferRecalled' ? 1 : 3;
          const phase = ((now - o.start) / (OVERLAY_MS[o.kind] / cycles)) % 1;
          color = phase < 0.5 ? 'green' : '';
        }
      }

      setClass(led, 'red', color === 'red');
      setClass(led, 'green', color === 'green');
      setClass(led, 'play', play);
      setClass(led, 'write', write);
      setClass(led, 'out', out);
    }
  }
}

function setClass(el, name, on) {
  if (el.classList.contains(name) !== on) el.classList.toggle(name, on);
}
