// Renders a page in WebKit (what Logic's plugin window uses) and saves a PNG.
//   swift snap.swift <url> <out.png> <width> <height> [js to run first]
import AppKit
import WebKit

let args = CommandLine.arguments
let url = URL(string: args[1])!
let out = args[2]
let w = Double(args[3])!, h = Double(args[4])!
let js = args.count > 5 ? args[5] : ""

final class Delegate: NSObject, WKNavigationDelegate {
  func webView(_ wv: WKWebView, didFinish _: WKNavigation!) {
    DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
      wv.evaluateJavaScript(js.isEmpty ? "0" : js) { result, error in
        if let result { print("js:", result) }
        if let error { print("js error:", error) }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
          wv.takeSnapshot(with: WKSnapshotConfiguration()) { img, err in
            guard let img, let tiff = img.tiffRepresentation, let rep = NSBitmapImageRep(data: tiff) else {
              print("snapshot failed", err as Any); exit(1)
            }
            try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: out))
            exit(0)
          }
        }
      }
    }
  }
}

let app = NSApplication.shared
let config = WKWebViewConfiguration()
config.websiteDataStore = .nonPersistent() // never a cached stylesheet
let wv = WKWebView(frame: NSRect(x: 0, y: 0, width: w, height: h), configuration: config)
let win = NSWindow(contentRect: wv.frame, styleMask: [.borderless], backing: .buffered, defer: false)
win.contentView = wv
let delegate = Delegate()
wv.navigationDelegate = delegate
wv.load(URLRequest(url: url))
DispatchQueue.main.asyncAfter(deadline: .now() + 15) { print("timeout"); exit(2) }
app.run()
