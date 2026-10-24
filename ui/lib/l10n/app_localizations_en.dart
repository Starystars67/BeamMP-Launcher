// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for English (`en`).
class AppLocalizationsEn extends AppLocalizations {
  AppLocalizationsEn([String locale = 'en']) : super(locale);

  @override
  String get appTitle => 'BeamMP Launcher';

  @override
  String get navHome => 'Play';

  @override
  String get navLogs => 'Logs';

  @override
  String get navAccount => 'Account';

  @override
  String get navSettings => 'Settings';

  @override
  String get startupTitle => 'Getting ready';

  @override
  String get startupConnecting => 'Starting the launcher engine…';

  @override
  String get startupUpdating => 'Checking for launcher updates…';

  @override
  String get startupPreparing => 'Preparing BeamNG.drive for multiplayer…';

  @override
  String get startupRetry => 'Try again';

  @override
  String get startupFailedTitle => 'The launcher engine stopped';

  @override
  String get startupFailedNotFound =>
      'The launcher engine could not be found. Reinstall BeamMP or set BEAMMP_ENGINE.';

  @override
  String get startupFailedLost => 'The launcher engine stopped unexpectedly.';

  @override
  String get startupFailedIncompatible =>
      'This launcher and its engine do not match. Update the launcher.';

  @override
  String get startupShowDetails => 'Show details';

  @override
  String get startupHideDetails => 'Hide details';

  @override
  String get startupCopyDetails => 'Copy details';

  @override
  String get homeTitle => 'Play';

  @override
  String get gameBeamNG => 'BeamNG.drive';

  @override
  String get gameStateReady => 'Ready to play';

  @override
  String get gameStateLaunching => 'Starting…';

  @override
  String get gameStateRunning => 'Running';

  @override
  String get gameStatePreparing => 'Preparing…';

  @override
  String get gameStateError => 'Something went wrong';

  @override
  String get gameStateNotFound => 'Game not found';

  @override
  String get gameStateExited => 'Closed';

  @override
  String get actionPlay => 'Play';

  @override
  String get actionStop => 'Stop game';

  @override
  String launchFailed(String reason) {
    return 'Could not start the game: $reason';
  }

  @override
  String get launchNoAnswer => 'The launcher engine did not answer. Try again.';

  @override
  String get launchArgumentsUnterminated =>
      'The game arguments in Settings have an opening quote without a closing one.';

  @override
  String get launchArgumentsTooMany =>
      'There are too many game arguments in Settings.';

  @override
  String get launchArgumentsTooLong =>
      'One of the game arguments in Settings is too long.';

  @override
  String get logsTitle => 'Logs';

  @override
  String logsLevelLabel(String level) {
    return 'Level: $level';
  }

  @override
  String get logsClear => 'Clear';

  @override
  String get logsLevelDebug => 'Debug';

  @override
  String get logsLevelInfo => 'Info';

  @override
  String get logsLevelWarn => 'Warnings';

  @override
  String get logsLevelError => 'Errors';

  @override
  String get logsEmpty => 'Nothing logged yet.';

  @override
  String get logsCopy => 'Copy all';

  @override
  String get logsCopied => 'Copied to the clipboard';

  @override
  String get logsFollow => 'Follow';

  @override
  String logsCount(int count) {
    return '$count entries';
  }

  @override
  String get accountTitle => 'Account';

  @override
  String get accountSignedOut => 'Not signed in';

  @override
  String get accountSignedOutHint =>
      'Sign in to use your BeamMP account in the game. You sign in on the BeamMP website, never in the launcher.';

  @override
  String accountSignedInAs(String username) {
    return 'Signed in as $username';
  }

  @override
  String accountRole(String role) {
    return 'Role: $role';
  }

  @override
  String get accountSignOut => 'Sign out';

  @override
  String get settingsTitle => 'Settings';

  @override
  String get settingsAppearance => 'Appearance';

  @override
  String get settingsTheme => 'Theme';

  @override
  String get settingsThemeSystem => 'Follow the system';

  @override
  String get settingsThemeLight => 'Light';

  @override
  String get settingsThemeDark => 'Dark';

  @override
  String get settingsWindow => 'Window';

  @override
  String get settingsTray => 'Show an icon in the menu bar';

  @override
  String get settingsTrayUnavailable => 'Not available on this system';

  @override
  String get settingsLaunchBehavior => 'When the game starts';

  @override
  String get settingsLaunchAuto =>
      'Hide to the menu bar if it is on, otherwise stay open';

  @override
  String get settingsLaunchHide => 'Hide to the menu bar';

  @override
  String get settingsLaunchStay => 'Stay open';

  @override
  String get settingsLaunchMinimise => 'Minimise';

  @override
  String get settingsCloseBehavior => 'When the window is closed';

  @override
  String get settingsCloseQuit => 'Quit the launcher';

  @override
  String get settingsCloseToTray => 'Keep running in the menu bar';

  @override
  String get settingsRestoreWindow =>
      'Bring the window back when the game closes';

  @override
  String get settingsNotifications => 'Notifications';

  @override
  String get settingsNotificationsToggle =>
      'Notify me when the game closes or the engine stops while the window is hidden';

  @override
  String get settingsLaunchArguments => 'Extra game arguments';

  @override
  String get settingsLaunchArgumentsHint => '-console -nosound';

  @override
  String get settingsLaunchArgumentsHelp =>
      'Passed to the game every time it starts. Put quotes around values with spaces.';

  @override
  String get settingsAdvanced => 'Advanced';

  @override
  String get settingsShowLogFile => 'Show the launcher log file';

  @override
  String get settingsShowLogFileMissing => 'The log file could not be found';

  @override
  String get settingsAbout => 'About';

  @override
  String aboutLauncher(String version) {
    return 'Launcher $version';
  }

  @override
  String aboutEngine(String version) {
    return 'Engine $version';
  }

  @override
  String aboutProtocol(int version) {
    return 'Control protocol $version';
  }

  @override
  String get aboutEngineUnavailable => 'The engine is not running';

  @override
  String get trayShow => 'Show BeamMP Launcher';

  @override
  String get trayHide => 'Hide BeamMP Launcher';

  @override
  String get trayPlay => 'Play';

  @override
  String get trayOpenLogs => 'Open logs';

  @override
  String get trayQuit => 'Quit';

  @override
  String trayStatus(String game, String state) {
    return '$game - $state';
  }

  @override
  String get notifyEngineStoppedTitle => 'The launcher engine stopped';

  @override
  String get notifyGameClosedTitle => 'BeamNG.drive closed';

  @override
  String get quitTitle => 'Quit the launcher?';

  @override
  String get quitGameRunning =>
      'BeamNG.drive is running. If you quit, multiplayer stops working until the launcher is started again.';

  @override
  String get quitConfirm => 'Quit';

  @override
  String get quitCancel => 'Stay';

  @override
  String get accountSignIn => 'Sign in with BeamMP';

  @override
  String get accountWaiting => 'Finish signing in in your browser';

  @override
  String get accountWaitingHint =>
      'A BeamMP page opened in your browser. If it did not, open it yourself.';

  @override
  String get accountOpenAgain => 'Open the page again';

  @override
  String get accountCopyLink => 'Copy link';

  @override
  String get accountLinkCopied => 'Link copied';

  @override
  String get accountCancel => 'Cancel';

  @override
  String get accountExchanging => 'Signing you in…';

  @override
  String get accountTryAgain => 'Try again';

  @override
  String get accountFailedCancelled => 'The sign-in was cancelled.';

  @override
  String get accountFailedTimeout => 'The sign-in took too long.';

  @override
  String get accountFailedDenied => 'The sign-in was declined.';

  @override
  String get accountFailedNetwork =>
      'BeamMP could not be reached. Check your connection.';

  @override
  String get accountFailedServer =>
      'BeamMP had a problem. Try again in a moment.';

  @override
  String get accountFailedRejected => 'The sign-in was refused.';

  @override
  String get accountFailedExpired =>
      'Your saved sign-in has ended. Please sign in again.';

  @override
  String get accountFailedInternal => 'Something went wrong while signing in.';

  @override
  String get gameStateUnavailable => 'Coming soon';

  @override
  String get gameStateBusy => 'Another game is running';

  @override
  String get sessionTitle => 'Play together';

  @override
  String get sessionHost => 'Host a room';

  @override
  String get sessionJoin => 'Join a room';

  @override
  String get sessionConnecting => 'Connecting to the relay…';

  @override
  String get sessionCancel => 'Cancel';

  @override
  String get sessionRoomCode => 'Room code';

  @override
  String get sessionCopyCode => 'Copy the code';

  @override
  String get sessionCodeCopied => 'Code copied';

  @override
  String get sessionYouAreHost =>
      'You are the host. Friends join with this code.';

  @override
  String get sessionYouJoined => 'You joined this room.';

  @override
  String sessionPlayers(int count) {
    return 'Players: $count';
  }

  @override
  String get sessionPlayerHost => 'host';

  @override
  String get sessionPlayerYou => 'you';

  @override
  String get sessionLeave => 'Leave room';

  @override
  String get sessionHostTitle => 'Host a room';

  @override
  String get sessionJoinTitle => 'Join a room';

  @override
  String get sessionMaxPlayers => 'Players allowed';

  @override
  String get sessionMaxPlayersHelp => '2 to 32, yourself included';

  @override
  String get sessionPassword => 'Password (optional)';

  @override
  String get sessionCode => 'Room code';

  @override
  String get sessionCodeHelp => 'Six letters and digits, from the host';

  @override
  String get sessionCreate => 'Create room';

  @override
  String get sessionJoinAction => 'Join';

  @override
  String sessionOtherGame(String game) {
    return 'You are in a room for $game already.';
  }

  @override
  String get sessionNoAnswer => 'The launcher engine did not answer.';

  @override
  String gameSettingsTitle(String game) {
    return '$game settings';
  }

  @override
  String get gameSettingsTooltip => 'Game settings';

  @override
  String get settingsGameFolder => 'Game folder';

  @override
  String get settingsGameFolderHelp =>
      'Leave empty to find the game automatically.';

  @override
  String get settingsStartCommand => 'Start command';

  @override
  String get settingsStartCommandHelp =>
      'A program or script that starts the game instead of its own executable, for example one that runs it through Wine. It gets the game folder in an environment variable (BEAMNG_DIR, GEARBLOCKS_DIR) and the launch arguments.';

  @override
  String get settingsUserFolder => 'User folder';

  @override
  String get settingsUserFolderHelp =>
      'Where the game keeps its user data (the folder that contains \"current\").';

  @override
  String get settingsGameBusy =>
      'A game is starting or running, so these settings are saved and apply the next time the launcher starts.';

  @override
  String settingsGameNotApplied(String message) {
    return 'The launcher could not use these settings: $message';
  }

  @override
  String get installTitle => 'Multiplayer support';

  @override
  String installNeeds(String game) {
    return '$game needs a mod loader and the BeamMP plugin before you can play together. The launcher can set them up: it downloads the loader, the plugin and the mod from BeamMP\'s signed list (every file is checked before it is used) and puts them in the game\'s folder.';
  }

  @override
  String installOutdated(String game) {
    return 'BeamMP\'s plugin or mod for $game is not the current one. It is also brought up to date when the game starts.';
  }

  @override
  String installUnsupportedBuild(String build) {
    return 'This build of the game ($build) is not one the launcher\'s plugin was made for. Playing together may not work until the launcher is updated.';
  }

  @override
  String get installGameMissing =>
      'Set the game\'s folder first (the settings button).';

  @override
  String installFilesMissing(String game) {
    return 'This launcher is not set up to get BeamMP\'s files for $game (no list address or trusted key), so they cannot be installed.';
  }

  @override
  String get installAction => 'Install';

  @override
  String get installUpdateAction => 'Update';

  @override
  String get installRunning => 'Installing…';

  @override
  String get installCancel => 'Cancel';

  @override
  String get installCancelled => 'Installing was cancelled.';

  @override
  String get installFailedTitle => 'Installing did not work.';

  @override
  String get installErrorDownload =>
      'The download failed. Check your connection and try again.';

  @override
  String get installErrorChecksum =>
      'The download is not the file BeamMP\'s list names, so it was not used.';

  @override
  String get installErrorConflict =>
      'A winhttp.dll from another mod loader is in the game\'s folder. Remove it first, the launcher will not overwrite it.';

  @override
  String get installErrorNotWritable =>
      'The game\'s folder cannot be changed. Run the launcher with the rights to write there.';

  @override
  String get installErrorUnsupportedBuild =>
      'This build of the game is not one the launcher\'s plugin was made for.';

  @override
  String get installErrorSignature =>
      'BeamMP\'s list of files could not be verified (not signed by a trusted key, or older than one already seen), so nothing was installed.';

  @override
  String get installErrorLauncherOld =>
      'BeamMP\'s plugin for this build of the game needs a newer launcher. Update the launcher.';

  @override
  String get installErrorNotConfigured =>
      'This launcher is not set up to get BeamMP\'s files for the game.';

  @override
  String installErrorOther(String message) {
    return '$message';
  }

  @override
  String get installBusy =>
      'Wait until the game has stopped or the launcher is not busy, then try again.';

  @override
  String get installDismiss => 'Dismiss';

  @override
  String get installFirstStart =>
      'The first start of the game takes a few minutes longer.';

  @override
  String get installInstalled => 'Multiplayer support is installed.';

  @override
  String installInstalledVersion(String version) {
    return 'Multiplayer support is installed (version $version).';
  }

  @override
  String get installRemove => 'Remove multiplayer support';

  @override
  String get installRemoveHelp =>
      'Takes out the BeamMP plugin and mod. The mod loader stays.';

  @override
  String get installRemoving => 'Removing…';
}
