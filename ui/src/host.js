// The bridge to the plugin. Inside the AU, JUCE's WebView exposes
// window.__JUCE__.backend; in a plain browser (UI development) there is no backend
// and the page runs on demo data.
//
//   UI → plugin: 'ready', 'press' / 'release' {button}, 'setQuantMode' {value}, 'reroll'
//   plugin → UI: 'init' {version}, and 'batch' {msgs} of:
//                'seq' {bits[16], volts[16], play[2], write[2], length[2], loopLength[2],
//                       quantMode, chained, running, hasBuffer, hostPlaying, bpm}
//                'panel' {event, value}   (LED feedback: showLength, bufferSaved, …)

const EVENT = 'cat';

export function createHost() {
  const backend = window.__JUCE__?.backend;
  return backend ? pluginHost(backend) : browserHost();
}

function dispatch(listeners, msg) {
  const msgs = msg.type === 'batch' ? msg.msgs : [msg];
  for (const m of msgs) for (const fn of listeners) fn(m);
}

function pluginHost(backend) {
  const listeners = new Set();
  backend.addEventListener(EVENT, (msg) => dispatch(listeners, msg));
  return {
    kind: 'plugin',
    post: (msg) => backend.emitEvent(EVENT, msg),
    onMessage: (fn) => listeners.add(fn),
  };
}

// A stand-in sequencer so the page can be worked on in a browser.
function browserHost() {
  const listeners = new Set();
  const bits = Array.from({ length: 16 }, () => Math.random() < 0.5);
  let step = 0;
  let running = false;
  const send = () =>
    dispatch(listeners, {
      type: 'seq', bits, volts: bits.map(() => 0), play: [step % 8, 8 + (step % 8)], write: [step % 8, 8 + (step % 8)],
      length: [8, 8], loopLength: [8, 8], quantMode: 2, chained: false, running, hasBuffer: false,
      hostPlaying: false, bpm: 120,
    });
  setInterval(() => {
    if (running) step++;
    send();
  }, 250);
  return {
    kind: 'browser',
    post(msg) {
      if (msg.type === 'ready') setTimeout(() => dispatch(listeners, { type: 'init', version: 'browser' }));
      if (msg.type === 'press' && msg.button === 'RunStop') running = !running;
      if (msg.type === 'press' && msg.button === 'Reset') step = 0;
    },
    onMessage: (fn) => listeners.add(fn),
  };
}
