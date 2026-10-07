// How each parameter reads out while you turn it. The curves mirror the engine
// (engine/Engine.cpp); this is display only.

const exp = (k, lo, hi) => lo * Math.pow(hi / lo, k);

function hz(f) {
  if (f >= 1000) return `${(f / 1000).toFixed(f >= 10000 ? 1 : 2)} kHz`;
  if (f >= 100) return `${f.toFixed(0)} Hz`;
  if (f >= 10) return `${f.toFixed(1)} Hz`;
  return `${f.toFixed(2)} Hz`;
}

function seconds(t) {
  return t < 1 ? `${Math.round(t * 1000)} ms` : `${t.toFixed(2)} s`;
}

const pct = (v) => `${Math.round(v * 100)}%`;
const signed = (v) => `${v > 0 ? '+' : v < 0 ? '−' : ''}${Math.round(Math.abs(v) * 100)}%`;

// Mixer levels: unity at noon.
const level = (v) => (v <= 0.001 ? 'off' : `${(20 * Math.log10(2 * v)).toFixed(1)} dB${v > 0.5 ? ' · drive' : ''}`);

const FORMATS = {
  vcoFreq: (v) => hz(exp(v, 20, 5000)),
  mvcoFreq: (v) => hz(exp(v, 0.05, 1300)),
  cutoff: (v) => hz(exp(v, 20, 20480)),
  eg1Decay: (v) => seconds(exp(v, 0.01, 6)),
  eg2Decay: (v) => seconds(exp(v, 0.01, 6)),
  tempo: (v) => `${exp(v, 0.5, 30).toFixed(2)} steps/s`,
  vcoLvl: level,
  mvcoLvl: level,
  noiseLvl: level,
  ringLvl: (v) => (v <= 0.001 ? 'off' : `${(20 * Math.log10(2 * v)).toFixed(1)} dB`),
  vcoSeq1Amt: (v) => (v >= 0.999 ? 'QTZ (1 V/oct)' : pct(v)),
  mvcoSeq2Amt: (v) => (v >= 0.999 ? 'QTZ (1 V/oct)' : pct(v)),
  blend: (v) => (v < 0.005 ? 'VCW' : v > 0.995 ? 'VCF' : `VCW ${pct(1 - v)} · VCF ${pct(v)}`),
  egTrigMix: (v) => `SEQ1 ${pct(Math.min(1, 2 * (1 - v)))} · SEQ2 ${pct(Math.min(1, 2 * v))}`,
  filterMode: (v) => (v < 0.005 ? 'Lowpass' : v > 0.995 ? 'Bandpass' : `LP ${pct(1 - v)} · BP ${pct(v)}`),
  corrupt1: corrupt,
  corrupt2: corrupt,
};

function corrupt(v) {
  if (v <= 0.001) return 'locked';
  const volts = v <= 0.5 ? 0.5 * v : 0.25 + 0.5 * (v - 0.5);
  const flips = v <= 0.5 ? 0 : v - 0.5;
  return `notes ${pct(volts)}${flips > 0 ? ` · bits ${pct(flips)}` : ''}`;
}

export function formatParam(id, v, meta) {
  if (FORMATS[id]) return FORMATS[id](v);
  if (meta && meta.min < 0) return signed(v);
  return pct(v);
}
