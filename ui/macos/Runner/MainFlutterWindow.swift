import Cocoa
import FlutterMacOS
import UserNotifications

/// Window and menu bar icon controls for the Dart side (lib/core/window/native_shell.dart).
/// Closing the window or quitting is only reported: Dart decides what happens.
final class ShellChannel: NSObject, NSWindowDelegate, UNUserNotificationCenterDelegate {
  static var shared: ShellChannel?

  /// Set once Dart asked to quit, so the termination that follows is let through.
  var allowTerminate = false

  private let channel: FlutterMethodChannel
  private weak var window: NSWindow?
  private var statusItem: NSStatusItem?

  init(window: NSWindow, messenger: FlutterBinaryMessenger) {
    channel = FlutterMethodChannel(name: "com.beammp.launcher/shell", binaryMessenger: messenger)
    self.window = window
    super.init()
    window.delegate = self
    UNUserNotificationCenter.current().delegate = self
    channel.setMethodCallHandler { [weak self] call, result in
      self?.handle(call, result: result)
    }
  }

  func notify(_ method: String, arguments: Any? = nil) {
    channel.invokeMethod(method, arguments: arguments)
  }

  func showWindow(focus: Bool) {
    guard let window = window else { return }
    NSApp.setActivationPolicy(.regular)
    if window.isMiniaturized {
      window.deminiaturize(nil)
    }
    if focus {
      window.makeKeyAndOrderFront(nil)
      NSApp.activate(ignoringOtherApps: true)
    } else {
      window.orderFront(nil)
    }
  }

  private func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    let arguments = call.arguments as? [String: Any]
    switch call.method {
    case "show":
      showWindow(focus: arguments?["focus"] as? Bool ?? true)
      result(nil)
    case "hide":
      if statusItem == nil {
        // Nothing to bring the window back from: minimise instead of losing it.
        window?.miniaturize(nil)
      } else {
        window?.orderOut(nil)
        NSApp.setActivationPolicy(.accessory)
      }
      result(nil)
    case "minimise":
      window?.miniaturize(nil)
      result(nil)
    case "quit":
      allowTerminate = true
      NSApp.terminate(nil)
      result(nil)
    case "notify":
      showNotification(
        title: arguments?["title"] as? String ?? "",
        body: arguments?["body"] as? String ?? "")
      result(nil)
    case "tray.probe":
      result(true)
    case "tray.setVisible":
      setTrayVisible(arguments?["visible"] as? Bool ?? false)
      result(nil)
    case "tray.setMenu":
      setTrayMenu(arguments?["items"] as? [[String: Any]] ?? [])
      result(nil)
    default:
      result(FlutterMethodNotImplemented)
    }
  }

  /// Asks for permission the first time; a refusal simply means no notification.
  private func showNotification(title: String, body: String) {
    let center = UNUserNotificationCenter.current()
    center.requestAuthorization(options: [.alert, .sound]) { granted, _ in
      guard granted else { return }
      let content = UNMutableNotificationContent()
      content.title = title
      content.body = body
      center.add(UNNotificationRequest(identifier: UUID().uuidString, content: content, trigger: nil))
    }
  }

  // Clicking a notification brings the window back.
  func userNotificationCenter(
    _ center: UNUserNotificationCenter, didReceive response: UNNotificationResponse,
    withCompletionHandler completionHandler: @escaping () -> Void
  ) {
    DispatchQueue.main.async {
      self.showWindow(focus: true)
      self.notify("restored")
    }
    completionHandler()
  }

  private func setTrayVisible(_ visible: Bool) {
    if visible, statusItem == nil {
      let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
      let image = NSImage(systemSymbolName: "gamecontroller.fill", accessibilityDescription: "BeamMP Launcher")
      image?.isTemplate = true
      item.button?.image = image
      statusItem = item
    } else if !visible, let item = statusItem {
      NSStatusBar.system.removeStatusItem(item)
      statusItem = nil
    }
  }

  private func setTrayMenu(_ items: [[String: Any]]) {
    guard let statusItem = statusItem else { return }
    let menu = NSMenu()
    menu.autoenablesItems = false
    for entry in items {
      if entry["separator"] as? Bool == true {
        menu.addItem(.separator())
        continue
      }
      let item = NSMenuItem(
        title: entry["label"] as? String ?? "",
        action: #selector(trayItemChosen(_:)),
        keyEquivalent: "")
      item.target = self
      item.isEnabled = entry["enabled"] as? Bool ?? true
      item.representedObject = entry["id"] as? String
      menu.addItem(item)
    }
    statusItem.menu = menu
  }

  @objc private func trayItemChosen(_ sender: NSMenuItem) {
    if let id = sender.representedObject as? String {
      notify("trayAction", arguments: id)
    }
  }

  func windowShouldClose(_ sender: NSWindow) -> Bool {
    notify("closeRequested")
    return false
  }

  func windowDidDeminiaturize(_ notification: Notification) {
    notify("restored")
  }
}

class MainFlutterWindow: NSWindow {
  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    self.contentViewController = flutterViewController
    self.title = "BeamMP Launcher"
    self.minSize = NSSize(width: 760, height: 520)
    self.setContentSize(NSSize(width: 980, height: 660))
    self.center()

    RegisterGeneratedPlugins(registry: flutterViewController)
    ShellChannel.shared = ShellChannel(
      window: self, messenger: flutterViewController.engine.binaryMessenger)

    super.awakeFromNib()
  }
}
