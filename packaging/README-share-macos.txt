BeamMP Launcher (test build) for macOS, Apple Silicon only
===========================================================

1. Move BeamMP-Launcher.app to Applications (or anywhere you like).
2. This build is not signed by an Apple developer, so macOS will refuse to open it the first time.
   Either right-click the app > Open > Open, or run this once in Terminal:

       xattr -dr com.apple.quarantine /Applications/BeamMP-Launcher.app

3. Start it. BeamNG.drive and GearBlocks both need their Windows builds run through Wine on a Mac; see
   README-macOS.md in the BeamMP-Launcher repository for the setup. The launcher does not install Wine.

GearBlocks multiplayer: open the GearBlocks card in the launcher and press Install. The BeamMP plugin and
script mod come with this build (inside the app). BepInEx, the mod loader they need, is downloaded by the launcher
from builds.bepinex.dev when you press Install (about 30 MB, checked against a checksum before it is used), and
the first start of the game afterwards takes a few minutes longer.

This is an early test build; expect rough edges.
