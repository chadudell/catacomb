// The bridge to the plugin. Inside the AU, JUCE's WebView exposes
// window.__JUCE__.backend; in a plain browser (UI development) there is no backend
// and the page runs on demo data.
//
//   UI → plugin: 'ready'
//   plugin → UI: 'init' {version, bits[16], quantMode}

const EVENT = 'cat';

export function createHost() {
  const backend = window.__JUCE__?.backend;
  return backend ? pluginHost(backend) : browserHost();
}

function pluginHost(backend) {
  const listeners = new Set();
  backend.addEventListener(EVENT, (msg) => {
    for (const fn of listeners) fn(msg);
  });
  return {
    kind: 'plugin',
    post: (msg) => backend.emitEvent(EVENT, msg),
    onMessage: (fn) => listeners.add(fn),
  };
}

function browserHost() {
  const listeners = new Set();
  return {
    kind: 'browser',
    post(msg) {
      if (msg.type !== 'ready') return;
      const bits = Array.from({ length: 16 }, () => Math.random() < 0.5);
      setTimeout(() => {
        for (const fn of listeners) fn({ type: 'init', version: 'browser', bits, quantMode: 2 });
      });
    },
    onMessage: (fn) => listeners.add(fn),
  };
}
