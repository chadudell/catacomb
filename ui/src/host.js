// The bridge to the plugin. Inside the AU, JUCE's WebView exposes
// window.__JUCE__.backend; in a plain browser (UI development) there is no backend
// and a small stand-in plays the part (demo.js).
//
//   UI → plugin: 'ready'; 'param' {id, value}; 'gesture' {id, begin};
//                'press' / 'release' {button}; 'toggleCell' {cell};
//                'setQuantMode' {value}; 'reroll'
//   plugin → UI: 'init' {version, meta: {id: {name, min, max, def, choices?}}, params: {id: value}},
//                then 'batch' {msgs} of:
//                'params' {values: {id: value}}      (automation, project loads)
//                'seq' {bits[16], volts[16], play[2], write[2], length[2], loopLength[2],
//                       quantMode, chained, running, hasBuffer, hostPlaying, bpm}
//                'panel' {event, value}              (LED feedback: showLength, bufferSaved, …)

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

function browserHost() {
  const listeners = new Set();
  let demo = null;
  const emit = (msg) => dispatch(listeners, msg);
  return {
    kind: 'browser',
    post(msg) {
      if (!demo) {
        import('./demo.js').then(({ Demo }) => {
          demo = new Demo(emit);
          demo.handle(msg);
        });
        return;
      }
      demo.handle(msg);
    },
    onMessage: (fn) => listeners.add(fn),
  };
}
