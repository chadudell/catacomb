// A stand-in for the plugin, so the panel can be worked on in a plain browser. It's a
// rough sketch of the sequencer (no combos, no sound); the real one is engine/.

const P = (name, min, max, def, choices) => ({ name, min, max, def, ...(choices && { choices }) });
const DIVS = ['1/64', '1/32T', '1/32', '1/16T', '1/16', '1/8T', '1/16.', '1/8', '1/4T', '1/8.', '1/4', '1/2T', '1/4.', '1/2', '1/1', '2/1'];

const META = {
  vcoFreq: P('VCO Frequency', 0, 1, 0.5), vcoEg1Amt: P('VCO EG1 Amount', -1, 1, 0),
  vcoSeq1Amt: P('VCO SEQ1 Amount', 0, 1, 0), fmAmt: P('MOD VCO → VCO FM', 0, 1, 0),
  mvcoFreq: P('MOD VCO Frequency', 0, 1, 0.5), mvcoEg1Amt: P('MOD VCO EG1 Amount', -1, 1, 0),
  mvcoSeq2Amt: P('MOD VCO SEQ2 Amount', 0, 1, 0), vcoLvl: P('VCO Level', 0, 1, 0.5),
  ringLvl: P('Ring Mod Level', 0, 1, 0), mvcoLvl: P('MOD VCO Level', 0, 1, 0),
  noiseLvl: P('Noise Level', 0, 1, 0), noiseTone: P('Noise Tone', 0, 1, 0.5),
  fold: P('VCW Fold', 0, 1, 0), foldEg1Amt: P('VCW EG1/CV Amount', -1, 1, 0),
  foldSeq1Amt: P('VCW SEQ1 Amount', 0, 1, 0), bias: P('VCW Bias', -1, 1, 0),
  cutoff: P('VCF Cutoff', 0, 1, 1), cutoffEg1Amt: P('VCF EG1/CV Amount', -1, 1, 0),
  cutoffSeq2Amt: P('VCF SEQ2 Amount', 0, 1, 0), resonance: P('Resonance', 0, 1, 0),
  filterMode: P('Filter Mode', 0, 1, 0), order: P('Order', 0, 2, 0, ['Parallel', 'VCW > VCF', 'VCF > VCW']),
  blend: P('Blend', 0, 1, 0), volume: P('Volume', 0, 1, 0.7), umix1Lvl: P('U Mix 1 Level', 0, 1, 0),
  eg1Decay: P('EG1 Decay', 0, 1, 0.35), eg2Decay: P('EG2 (VCA) Decay', 0, 1, 0.35),
  egTrigMix: P('EG Trig Mix', 0, 1, 0), tempo: P('Tempo', 0, 1, 0.5),
  corrupt1: P('SEQ1 Corrupt', 0, 1, 0), corrupt2: P('SEQ2 Corrupt', 0, 1, 0),
  cvRange1: P('SEQ1 CV Range', 0, 1, 0), cvRange2: P('SEQ2 CV Range', 0, 1, 0),
  clockSource: P('Clock Source', 0, 1, 0, ['Host Tempo', 'Tempo Knob']),
  clockDiv: P('Clock Division', 0, 15, 4, DIVS),
  clock2Div: P('SEQ2 Clock Division', 0, 16, 0, ['Same as Clock 1', ...DIVS]),
  followTransport: P('Follow Transport', 0, 1, 1),
  resetRecallsBuffer: P('RESET Jack Recalls Buffer', 0, 1, 0),
  cvOutUnipolar: P('Unipolar CV Outs', 0, 1, 0),
};

export class Demo {
  constructor(emit) {
    this.emit = emit;
    this.params = Object.fromEntries(Object.entries(META).map(([id, m]) => [id, m.def]));
    this.bits = Array.from({ length: 16 }, () => Math.random() < 0.5);
    this.length = [8, 8];
    this.pos = [0, 0];
    this.offset = [0, 0];
    this.quantMode = 2;
    this.running = false;
    this.cables = 'eg2>bitFlip1,clock>vco1voct,mvco>blend';
    this.chained = false;
    setInterval(() => this.tick(), 125);
  }

  handle(msg) {
    switch (msg.type) {
      case 'ready':
        this.emit({ type: 'init', version: 'browser demo', meta: META, params: this.params, cables: this.cables });
        this.send();
        break;
      case 'patch':
        this.cables = msg.cables;
        break;
      case 'param':
        this.params[msg.id] = msg.value;
        break;
      case 'toggleCell':
        this.bits[msg.cell] = !this.bits[msg.cell];
        this.send();
        break;
      case 'setQuantMode':
        this.quantMode = msg.value;
        this.emit({ type: 'panel', event: 'showQuantMode', value: 0 });
        this.send();
        break;
      case 'press':
        this.press(msg.button);
        break;
    }
  }

  press(b) {
    if (b === 'RunStop') this.running = !this.running;
    if (b === 'Reset') this.pos = [0, 0];
    if (b === 'Chain') this.chained = !this.chained;
    if (b === 'Length1' || b === 'Length2') {
      const s = b === 'Length1' ? 0 : 1;
      this.length[s] = this.length[s] === 1 ? 8 : this.length[s] - 1;
      this.emit({ type: 'panel', event: 'showLength', value: s });
    }
    if (b === 'Flip1' || b === 'Flip2') {
      const s = b === 'Flip1' ? 0 : 1;
      const cell = s * 8 + ((this.pos[s] + this.offset[s]) % this.length[s]);
      this.bits[cell] = !this.bits[cell];
    }
    if (b === 'Buffer') this.emit({ type: 'panel', event: 'bufferSaved', value: 0 });
    this.send();
  }

  tick() {
    if (!this.running) return;
    for (const s of [0, 1]) this.pos[s] = (this.pos[s] + 1) % this.length[s];
    this.send();
  }

  send() {
    this.emit({
      type: 'seq',
      bits: this.bits,
      volts: this.bits.map(() => 0),
      play: [this.pos[0], 8 + this.pos[1]],
      write: [(this.pos[0] + this.offset[0]) % this.length[0], 8 + ((this.pos[1] + this.offset[1]) % this.length[1])],
      length: this.length,
      loopLength: this.length,
      quantMode: this.quantMode,
      chained: this.chained,
      running: this.running,
      hasBuffer: false,
      hostPlaying: false,
      bpm: 120,
    });
  }
}
