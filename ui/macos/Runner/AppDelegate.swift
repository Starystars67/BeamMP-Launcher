import Cocoa
import FlutterMacOS

@main
class AppDelegate: FlutterAppDelegate {
  // The window can be hidden to the menu bar while the app keeps running.
  override func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    return false
  }

  // Cmd+Q and the Dock menu only ask: Dart confirms (a game may be running) and then quits.
  override func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
    guard let shell = ShellChannel.shared, !shell.allowTerminate else {
      return .terminateNow
    }
    shell.notify("quitRequested")
    return .terminateCancel
  }

  // A click on the Dock icon brings a hidden window back.
  override func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
    if !flag {
      ShellChannel.shared?.showWindow(focus: true)
      ShellChannel.shared?.notify("restored")
    }
    return true
  }

  override func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
    return true
  }
}
