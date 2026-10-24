import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:intl/intl.dart' as intl;

import 'app_localizations_en.dart';

// ignore_for_file: type=lint

/// Callers can lookup localized strings with an instance of AppLocalizations
/// returned by `AppLocalizations.of(context)`.
///
/// Applications need to include `AppLocalizations.delegate()` in their app's
/// `localizationDelegates` list, and the locales they support in the app's
/// `supportedLocales` list. For example:
///
/// ```dart
/// import 'l10n/app_localizations.dart';
///
/// return MaterialApp(
///   localizationsDelegates: AppLocalizations.localizationsDelegates,
///   supportedLocales: AppLocalizations.supportedLocales,
///   home: MyApplicationHome(),
/// );
/// ```
///
/// ## Update pubspec.yaml
///
/// Please make sure to update your pubspec.yaml to include the following
/// packages:
///
/// ```yaml
/// dependencies:
///   # Internationalization support.
///   flutter_localizations:
///     sdk: flutter
///   intl: any # Use the pinned version from flutter_localizations
///
///   # Rest of dependencies
/// ```
///
/// ## iOS Applications
///
/// iOS applications define key application metadata, including supported
/// locales, in an Info.plist file that is built into the application bundle.
/// To configure the locales supported by your app, you’ll need to edit this
/// file.
///
/// First, open your project’s ios/Runner.xcworkspace Xcode workspace file.
/// Then, in the Project Navigator, open the Info.plist file under the Runner
/// project’s Runner folder.
///
/// Next, select the Information Property List item, select Add Item from the
/// Editor menu, then select Localizations from the pop-up menu.
///
/// Select and expand the newly-created Localizations item then, for each
/// locale your application supports, add a new item and select the locale
/// you wish to add from the pop-up menu in the Value field. This list should
/// be consistent with the languages listed in the AppLocalizations.supportedLocales
/// property.
abstract class AppLocalizations {
  AppLocalizations(String locale)
    : localeName = intl.Intl.canonicalizedLocale(locale.toString());

  final String localeName;

  static AppLocalizations of(BuildContext context) {
    return Localizations.of<AppLocalizations>(context, AppLocalizations)!;
  }

  static const LocalizationsDelegate<AppLocalizations> delegate =
      _AppLocalizationsDelegate();

  /// A list of this localizations delegate along with the default localizations
  /// delegates.
  ///
  /// Returns a list of localizations delegates containing this delegate along with
  /// GlobalMaterialLocalizations.delegate, GlobalCupertinoLocalizations.delegate,
  /// and GlobalWidgetsLocalizations.delegate.
  ///
  /// Additional delegates can be added by appending to this list in
  /// MaterialApp. This list does not have to be used at all if a custom list
  /// of delegates is preferred or required.
  static const List<LocalizationsDelegate<dynamic>> localizationsDelegates =
      <LocalizationsDelegate<dynamic>>[
        delegate,
        GlobalMaterialLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
      ];

  /// A list of this localizations delegate's supported locales.
  static const List<Locale> supportedLocales = <Locale>[Locale('en')];

  /// No description provided for @appTitle.
  ///
  /// In en, this message translates to:
  /// **'BeamMP Launcher'**
  String get appTitle;

  /// No description provided for @navHome.
  ///
  /// In en, this message translates to:
  /// **'Play'**
  String get navHome;

  /// No description provided for @navLogs.
  ///
  /// In en, this message translates to:
  /// **'Logs'**
  String get navLogs;

  /// No description provided for @navAccount.
  ///
  /// In en, this message translates to:
  /// **'Account'**
  String get navAccount;

  /// No description provided for @navSettings.
  ///
  /// In en, this message translates to:
  /// **'Settings'**
  String get navSettings;

  /// No description provided for @startupTitle.
  ///
  /// In en, this message translates to:
  /// **'Getting ready'**
  String get startupTitle;

  /// No description provided for @startupConnecting.
  ///
  /// In en, this message translates to:
  /// **'Starting the launcher engine…'**
  String get startupConnecting;

  /// No description provided for @startupUpdating.
  ///
  /// In en, this message translates to:
  /// **'Checking for launcher updates…'**
  String get startupUpdating;

  /// No description provided for @startupPreparing.
  ///
  /// In en, this message translates to:
  /// **'Preparing BeamNG.drive for multiplayer…'**
  String get startupPreparing;

  /// No description provided for @startupRetry.
  ///
  /// In en, this message translates to:
  /// **'Try again'**
  String get startupRetry;

  /// No description provided for @startupFailedTitle.
  ///
  /// In en, this message translates to:
  /// **'The launcher engine stopped'**
  String get startupFailedTitle;

  /// No description provided for @startupFailedNotFound.
  ///
  /// In en, this message translates to:
  /// **'The launcher engine could not be found. Reinstall BeamMP or set BEAMMP_ENGINE.'**
  String get startupFailedNotFound;

  /// No description provided for @startupFailedLost.
  ///
  /// In en, this message translates to:
  /// **'The launcher engine stopped unexpectedly.'**
  String get startupFailedLost;

  /// No description provided for @startupFailedIncompatible.
  ///
  /// In en, this message translates to:
  /// **'This launcher and its engine do not match. Update the launcher.'**
  String get startupFailedIncompatible;

  /// No description provided for @startupShowDetails.
  ///
  /// In en, this message translates to:
  /// **'Show details'**
  String get startupShowDetails;

  /// No description provided for @startupHideDetails.
  ///
  /// In en, this message translates to:
  /// **'Hide details'**
  String get startupHideDetails;

  /// No description provided for @startupCopyDetails.
  ///
  /// In en, this message translates to:
  /// **'Copy details'**
  String get startupCopyDetails;

  /// No description provided for @homeTitle.
  ///
  /// In en, this message translates to:
  /// **'Play'**
  String get homeTitle;

  /// No description provided for @gameBeamNG.
  ///
  /// In en, this message translates to:
  /// **'BeamNG.drive'**
  String get gameBeamNG;

  /// No description provided for @gameStateReady.
  ///
  /// In en, this message translates to:
  /// **'Ready to play'**
  String get gameStateReady;

  /// No description provided for @gameStateLaunching.
  ///
  /// In en, this message translates to:
  /// **'Starting…'**
  String get gameStateLaunching;

  /// No description provided for @gameStateRunning.
  ///
  /// In en, this message translates to:
  /// **'Running'**
  String get gameStateRunning;

  /// No description provided for @gameStatePreparing.
  ///
  /// In en, this message translates to:
  /// **'Preparing…'**
  String get gameStatePreparing;

  /// No description provided for @gameStateError.
  ///
  /// In en, this message translates to:
  /// **'Something went wrong'**
  String get gameStateError;

  /// No description provided for @gameStateNotFound.
  ///
  /// In en, this message translates to:
  /// **'Game not found'**
  String get gameStateNotFound;

  /// No description provided for @gameStateExited.
  ///
  /// In en, this message translates to:
  /// **'Closed'**
  String get gameStateExited;

  /// No description provided for @actionPlay.
  ///
  /// In en, this message translates to:
  /// **'Play'**
  String get actionPlay;

  /// No description provided for @actionStop.
  ///
  /// In en, this message translates to:
  /// **'Stop game'**
  String get actionStop;

  /// No description provided for @launchFailed.
  ///
  /// In en, this message translates to:
  /// **'Could not start the game: {reason}'**
  String launchFailed(String reason);

  /// No description provided for @launchNoAnswer.
  ///
  /// In en, this message translates to:
  /// **'The launcher engine did not answer. Try again.'**
  String get launchNoAnswer;

  /// No description provided for @launchArgumentsUnterminated.
  ///
  /// In en, this message translates to:
  /// **'The game arguments in Settings have an opening quote without a closing one.'**
  String get launchArgumentsUnterminated;

  /// No description provided for @launchArgumentsTooMany.
  ///
  /// In en, this message translates to:
  /// **'There are too many game arguments in Settings.'**
  String get launchArgumentsTooMany;

  /// No description provided for @launchArgumentsTooLong.
  ///
  /// In en, this message translates to:
  /// **'One of the game arguments in Settings is too long.'**
  String get launchArgumentsTooLong;

  /// No description provided for @logsTitle.
  ///
  /// In en, this message translates to:
  /// **'Logs'**
  String get logsTitle;

  /// No description provided for @logsLevelLabel.
  ///
  /// In en, this message translates to:
  /// **'Level: {level}'**
  String logsLevelLabel(String level);

  /// No description provided for @logsClear.
  ///
  /// In en, this message translates to:
  /// **'Clear'**
  String get logsClear;

  /// No description provided for @logsLevelDebug.
  ///
  /// In en, this message translates to:
  /// **'Debug'**
  String get logsLevelDebug;

  /// No description provided for @logsLevelInfo.
  ///
  /// In en, this message translates to:
  /// **'Info'**
  String get logsLevelInfo;

  /// No description provided for @logsLevelWarn.
  ///
  /// In en, this message translates to:
  /// **'Warnings'**
  String get logsLevelWarn;

  /// No description provided for @logsLevelError.
  ///
  /// In en, this message translates to:
  /// **'Errors'**
  String get logsLevelError;

  /// No description provided for @logsEmpty.
  ///
  /// In en, this message translates to:
  /// **'Nothing logged yet.'**
  String get logsEmpty;

  /// No description provided for @logsCopy.
  ///
  /// In en, this message translates to:
  /// **'Copy all'**
  String get logsCopy;

  /// No description provided for @logsCopied.
  ///
  /// In en, this message translates to:
  /// **'Copied to the clipboard'**
  String get logsCopied;

  /// No description provided for @logsFollow.
  ///
  /// In en, this message translates to:
  /// **'Follow'**
  String get logsFollow;

  /// No description provided for @logsCount.
  ///
  /// In en, this message translates to:
  /// **'{count} entries'**
  String logsCount(int count);

  /// No description provided for @accountTitle.
  ///
  /// In en, this message translates to:
  /// **'Account'**
  String get accountTitle;

  /// No description provided for @accountSignedOut.
  ///
  /// In en, this message translates to:
  /// **'Not signed in'**
  String get accountSignedOut;

  /// No description provided for @accountSignedOutHint.
  ///
  /// In en, this message translates to:
  /// **'Sign in to use your BeamMP account in the game. You sign in on the BeamMP website, never in the launcher.'**
  String get accountSignedOutHint;

  /// No description provided for @accountSignedInAs.
  ///
  /// In en, this message translates to:
  /// **'Signed in as {username}'**
  String accountSignedInAs(String username);

  /// No description provided for @accountRole.
  ///
  /// In en, this message translates to:
  /// **'Role: {role}'**
  String accountRole(String role);

  /// No description provided for @accountSignOut.
  ///
  /// In en, this message translates to:
  /// **'Sign out'**
  String get accountSignOut;

  /// No description provided for @settingsTitle.
  ///
  /// In en, this message translates to:
  /// **'Settings'**
  String get settingsTitle;

  /// No description provided for @settingsAppearance.
  ///
  /// In en, this message translates to:
  /// **'Appearance'**
  String get settingsAppearance;

  /// No description provided for @settingsTheme.
  ///
  /// In en, this message translates to:
  /// **'Theme'**
  String get settingsTheme;

  /// No description provided for @settingsThemeSystem.
  ///
  /// In en, this message translates to:
  /// **'Follow the system'**
  String get settingsThemeSystem;

  /// No description provided for @settingsThemeLight.
  ///
  /// In en, this message translates to:
  /// **'Light'**
  String get settingsThemeLight;

  /// No description provided for @settingsThemeDark.
  ///
  /// In en, this message translates to:
  /// **'Dark'**
  String get settingsThemeDark;

  /// No description provided for @settingsWindow.
  ///
  /// In en, this message translates to:
  /// **'Window'**
  String get settingsWindow;

  /// No description provided for @settingsTray.
  ///
  /// In en, this message translates to:
  /// **'Show an icon in the menu bar'**
  String get settingsTray;

  /// No description provided for @settingsTrayUnavailable.
  ///
  /// In en, this message translates to:
  /// **'Not available on this system'**
  String get settingsTrayUnavailable;

  /// No description provided for @settingsLaunchBehavior.
  ///
  /// In en, this message translates to:
  /// **'When the game starts'**
  String get settingsLaunchBehavior;

  /// No description provided for @settingsLaunchAuto.
  ///
  /// In en, this message translates to:
  /// **'Hide to the menu bar if it is on, otherwise stay open'**
  String get settingsLaunchAuto;

  /// No description provided for @settingsLaunchHide.
  ///
  /// In en, this message translates to:
  /// **'Hide to the menu bar'**
  String get settingsLaunchHide;

  /// No description provided for @settingsLaunchStay.
  ///
  /// In en, this message translates to:
  /// **'Stay open'**
  String get settingsLaunchStay;

  /// No description provided for @settingsLaunchMinimise.
  ///
  /// In en, this message translates to:
  /// **'Minimise'**
  String get settingsLaunchMinimise;

  /// No description provided for @settingsCloseBehavior.
  ///
  /// In en, this message translates to:
  /// **'When the window is closed'**
  String get settingsCloseBehavior;

  /// No description provided for @settingsCloseQuit.
  ///
  /// In en, this message translates to:
  /// **'Quit the launcher'**
  String get settingsCloseQuit;

  /// No description provided for @settingsCloseToTray.
  ///
  /// In en, this message translates to:
  /// **'Keep running in the menu bar'**
  String get settingsCloseToTray;

  /// No description provided for @settingsRestoreWindow.
  ///
  /// In en, this message translates to:
  /// **'Bring the window back when the game closes'**
  String get settingsRestoreWindow;

  /// No description provided for @settingsNotifications.
  ///
  /// In en, this message translates to:
  /// **'Notifications'**
  String get settingsNotifications;

  /// No description provided for @settingsNotificationsToggle.
  ///
  /// In en, this message translates to:
  /// **'Notify me when the game closes or the engine stops while the window is hidden'**
  String get settingsNotificationsToggle;

  /// No description provided for @settingsLaunchArguments.
  ///
  /// In en, this message translates to:
  /// **'Extra game arguments'**
  String get settingsLaunchArguments;

  /// No description provided for @settingsLaunchArgumentsHint.
  ///
  /// In en, this message translates to:
  /// **'-console -nosound'**
  String get settingsLaunchArgumentsHint;

  /// No description provided for @settingsLaunchArgumentsHelp.
  ///
  /// In en, this message translates to:
  /// **'Passed to the game every time it starts. Put quotes around values with spaces.'**
  String get settingsLaunchArgumentsHelp;

  /// No description provided for @settingsAdvanced.
  ///
  /// In en, this message translates to:
  /// **'Advanced'**
  String get settingsAdvanced;

  /// No description provided for @settingsShowLogFile.
  ///
  /// In en, this message translates to:
  /// **'Show the launcher log file'**
  String get settingsShowLogFile;

  /// No description provided for @settingsShowLogFileMissing.
  ///
  /// In en, this message translates to:
  /// **'The log file could not be found'**
  String get settingsShowLogFileMissing;

  /// No description provided for @settingsAbout.
  ///
  /// In en, this message translates to:
  /// **'About'**
  String get settingsAbout;

  /// No description provided for @aboutLauncher.
  ///
  /// In en, this message translates to:
  /// **'Launcher {version}'**
  String aboutLauncher(String version);

  /// No description provided for @aboutEngine.
  ///
  /// In en, this message translates to:
  /// **'Engine {version}'**
  String aboutEngine(String version);

  /// No description provided for @aboutProtocol.
  ///
  /// In en, this message translates to:
  /// **'Control protocol {version}'**
  String aboutProtocol(int version);

  /// No description provided for @aboutEngineUnavailable.
  ///
  /// In en, this message translates to:
  /// **'The engine is not running'**
  String get aboutEngineUnavailable;

  /// No description provided for @trayShow.
  ///
  /// In en, this message translates to:
  /// **'Show BeamMP Launcher'**
  String get trayShow;

  /// No description provided for @trayHide.
  ///
  /// In en, this message translates to:
  /// **'Hide BeamMP Launcher'**
  String get trayHide;

  /// No description provided for @trayPlay.
  ///
  /// In en, this message translates to:
  /// **'Play'**
  String get trayPlay;

  /// No description provided for @trayOpenLogs.
  ///
  /// In en, this message translates to:
  /// **'Open logs'**
  String get trayOpenLogs;

  /// No description provided for @trayQuit.
  ///
  /// In en, this message translates to:
  /// **'Quit'**
  String get trayQuit;

  /// No description provided for @trayStatus.
  ///
  /// In en, this message translates to:
  /// **'{game} - {state}'**
  String trayStatus(String game, String state);

  /// No description provided for @notifyEngineStoppedTitle.
  ///
  /// In en, this message translates to:
  /// **'The launcher engine stopped'**
  String get notifyEngineStoppedTitle;

  /// No description provided for @notifyGameClosedTitle.
  ///
  /// In en, this message translates to:
  /// **'BeamNG.drive closed'**
  String get notifyGameClosedTitle;

  /// No description provided for @quitTitle.
  ///
  /// In en, this message translates to:
  /// **'Quit the launcher?'**
  String get quitTitle;

  /// No description provided for @quitGameRunning.
  ///
  /// In en, this message translates to:
  /// **'BeamNG.drive is running. If you quit, multiplayer stops working until the launcher is started again.'**
  String get quitGameRunning;

  /// No description provided for @quitConfirm.
  ///
  /// In en, this message translates to:
  /// **'Quit'**
  String get quitConfirm;

  /// No description provided for @quitCancel.
  ///
  /// In en, this message translates to:
  /// **'Stay'**
  String get quitCancel;

  /// No description provided for @accountSignIn.
  ///
  /// In en, this message translates to:
  /// **'Sign in with BeamMP'**
  String get accountSignIn;

  /// No description provided for @accountWaiting.
  ///
  /// In en, this message translates to:
  /// **'Finish signing in in your browser'**
  String get accountWaiting;

  /// No description provided for @accountWaitingHint.
  ///
  /// In en, this message translates to:
  /// **'A BeamMP page opened in your browser. If it did not, open it yourself.'**
  String get accountWaitingHint;

  /// No description provided for @accountOpenAgain.
  ///
  /// In en, this message translates to:
  /// **'Open the page again'**
  String get accountOpenAgain;

  /// No description provided for @accountCopyLink.
  ///
  /// In en, this message translates to:
  /// **'Copy link'**
  String get accountCopyLink;

  /// No description provided for @accountLinkCopied.
  ///
  /// In en, this message translates to:
  /// **'Link copied'**
  String get accountLinkCopied;

  /// No description provided for @accountCancel.
  ///
  /// In en, this message translates to:
  /// **'Cancel'**
  String get accountCancel;

  /// No description provided for @accountExchanging.
  ///
  /// In en, this message translates to:
  /// **'Signing you in…'**
  String get accountExchanging;

  /// No description provided for @accountTryAgain.
  ///
  /// In en, this message translates to:
  /// **'Try again'**
  String get accountTryAgain;

  /// No description provided for @accountFailedCancelled.
  ///
  /// In en, this message translates to:
  /// **'The sign-in was cancelled.'**
  String get accountFailedCancelled;

  /// No description provided for @accountFailedTimeout.
  ///
  /// In en, this message translates to:
  /// **'The sign-in took too long.'**
  String get accountFailedTimeout;

  /// No description provided for @accountFailedDenied.
  ///
  /// In en, this message translates to:
  /// **'The sign-in was declined.'**
  String get accountFailedDenied;

  /// No description provided for @accountFailedNetwork.
  ///
  /// In en, this message translates to:
  /// **'BeamMP could not be reached. Check your connection.'**
  String get accountFailedNetwork;

  /// No description provided for @accountFailedServer.
  ///
  /// In en, this message translates to:
  /// **'BeamMP had a problem. Try again in a moment.'**
  String get accountFailedServer;

  /// No description provided for @accountFailedRejected.
  ///
  /// In en, this message translates to:
  /// **'The sign-in was refused.'**
  String get accountFailedRejected;

  /// No description provided for @accountFailedExpired.
  ///
  /// In en, this message translates to:
  /// **'Your saved sign-in has ended. Please sign in again.'**
  String get accountFailedExpired;

  /// No description provided for @accountFailedInternal.
  ///
  /// In en, this message translates to:
  /// **'Something went wrong while signing in.'**
  String get accountFailedInternal;

  /// No description provided for @gameStateUnavailable.
  ///
  /// In en, this message translates to:
  /// **'Coming soon'**
  String get gameStateUnavailable;

  /// No description provided for @gameStateBusy.
  ///
  /// In en, this message translates to:
  /// **'Another game is running'**
  String get gameStateBusy;

  /// No description provided for @sessionTitle.
  ///
  /// In en, this message translates to:
  /// **'Play together'**
  String get sessionTitle;

  /// No description provided for @sessionHost.
  ///
  /// In en, this message translates to:
  /// **'Host a room'**
  String get sessionHost;

  /// No description provided for @sessionJoin.
  ///
  /// In en, this message translates to:
  /// **'Join a room'**
  String get sessionJoin;

  /// No description provided for @sessionConnecting.
  ///
  /// In en, this message translates to:
  /// **'Connecting to the relay…'**
  String get sessionConnecting;

  /// No description provided for @sessionCancel.
  ///
  /// In en, this message translates to:
  /// **'Cancel'**
  String get sessionCancel;

  /// No description provided for @sessionRoomCode.
  ///
  /// In en, this message translates to:
  /// **'Room code'**
  String get sessionRoomCode;

  /// No description provided for @sessionCopyCode.
  ///
  /// In en, this message translates to:
  /// **'Copy the code'**
  String get sessionCopyCode;

  /// No description provided for @sessionCodeCopied.
  ///
  /// In en, this message translates to:
  /// **'Code copied'**
  String get sessionCodeCopied;

  /// No description provided for @sessionYouAreHost.
  ///
  /// In en, this message translates to:
  /// **'You are the host. Friends join with this code.'**
  String get sessionYouAreHost;

  /// No description provided for @sessionYouJoined.
  ///
  /// In en, this message translates to:
  /// **'You joined this room.'**
  String get sessionYouJoined;

  /// No description provided for @sessionPlayers.
  ///
  /// In en, this message translates to:
  /// **'Players: {count}'**
  String sessionPlayers(int count);

  /// No description provided for @sessionPlayerHost.
  ///
  /// In en, this message translates to:
  /// **'host'**
  String get sessionPlayerHost;

  /// No description provided for @sessionPlayerYou.
  ///
  /// In en, this message translates to:
  /// **'you'**
  String get sessionPlayerYou;

  /// No description provided for @sessionLeave.
  ///
  /// In en, this message translates to:
  /// **'Leave room'**
  String get sessionLeave;

  /// No description provided for @sessionHostTitle.
  ///
  /// In en, this message translates to:
  /// **'Host a room'**
  String get sessionHostTitle;

  /// No description provided for @sessionJoinTitle.
  ///
  /// In en, this message translates to:
  /// **'Join a room'**
  String get sessionJoinTitle;

  /// No description provided for @sessionMaxPlayers.
  ///
  /// In en, this message translates to:
  /// **'Players allowed'**
  String get sessionMaxPlayers;

  /// No description provided for @sessionMaxPlayersHelp.
  ///
  /// In en, this message translates to:
  /// **'2 to 32, yourself included'**
  String get sessionMaxPlayersHelp;

  /// No description provided for @sessionPassword.
  ///
  /// In en, this message translates to:
  /// **'Password (optional)'**
  String get sessionPassword;

  /// No description provided for @sessionCode.
  ///
  /// In en, this message translates to:
  /// **'Room code'**
  String get sessionCode;

  /// No description provided for @sessionCodeHelp.
  ///
  /// In en, this message translates to:
  /// **'Six letters and digits, from the host'**
  String get sessionCodeHelp;

  /// No description provided for @sessionCreate.
  ///
  /// In en, this message translates to:
  /// **'Create room'**
  String get sessionCreate;

  /// No description provided for @sessionJoinAction.
  ///
  /// In en, this message translates to:
  /// **'Join'**
  String get sessionJoinAction;

  /// No description provided for @sessionOtherGame.
  ///
  /// In en, this message translates to:
  /// **'You are in a room for {game} already.'**
  String sessionOtherGame(String game);

  /// No description provided for @sessionNoAnswer.
  ///
  /// In en, this message translates to:
  /// **'The launcher engine did not answer.'**
  String get sessionNoAnswer;

  /// No description provided for @gameSettingsTitle.
  ///
  /// In en, this message translates to:
  /// **'{game} settings'**
  String gameSettingsTitle(String game);

  /// No description provided for @gameSettingsTooltip.
  ///
  /// In en, this message translates to:
  /// **'Game settings'**
  String get gameSettingsTooltip;

  /// No description provided for @settingsGameFolder.
  ///
  /// In en, this message translates to:
  /// **'Game folder'**
  String get settingsGameFolder;

  /// No description provided for @settingsGameFolderHelp.
  ///
  /// In en, this message translates to:
  /// **'Leave empty to find the game automatically.'**
  String get settingsGameFolderHelp;

  /// No description provided for @settingsStartCommand.
  ///
  /// In en, this message translates to:
  /// **'Start command'**
  String get settingsStartCommand;

  /// No description provided for @settingsStartCommandHelp.
  ///
  /// In en, this message translates to:
  /// **'A program or script that starts the game instead of its own executable, for example one that runs it through Wine. It gets the game folder in an environment variable (BEAMNG_DIR, GEARBLOCKS_DIR) and the launch arguments.'**
  String get settingsStartCommandHelp;

  /// No description provided for @settingsUserFolder.
  ///
  /// In en, this message translates to:
  /// **'User folder'**
  String get settingsUserFolder;

  /// No description provided for @settingsUserFolderHelp.
  ///
  /// In en, this message translates to:
  /// **'Where the game keeps its user data (the folder that contains \"current\").'**
  String get settingsUserFolderHelp;

  /// No description provided for @settingsGameBusy.
  ///
  /// In en, this message translates to:
  /// **'A game is starting or running, so these settings are saved and apply the next time the launcher starts.'**
  String get settingsGameBusy;

  /// No description provided for @settingsGameNotApplied.
  ///
  /// In en, this message translates to:
  /// **'The launcher could not use these settings: {message}'**
  String settingsGameNotApplied(String message);

  /// No description provided for @installTitle.
  ///
  /// In en, this message translates to:
  /// **'Multiplayer support'**
  String get installTitle;

  /// No description provided for @installNeeds.
  ///
  /// In en, this message translates to:
  /// **'{game} needs a mod loader and the BeamMP plugin before you can play together. The launcher can set them up: it downloads the loader, the plugin and the mod from BeamMP\'s signed list (every file is checked before it is used) and puts them in the game\'s folder.'**
  String installNeeds(String game);

  /// No description provided for @installOutdated.
  ///
  /// In en, this message translates to:
  /// **'BeamMP\'s plugin or mod for {game} is not the current one. It is also brought up to date when the game starts.'**
  String installOutdated(String game);

  /// No description provided for @installUnsupportedBuild.
  ///
  /// In en, this message translates to:
  /// **'This build of the game ({build}) is not one the launcher\'s plugin was made for. Playing together may not work until the launcher is updated.'**
  String installUnsupportedBuild(String build);

  /// No description provided for @installGameMissing.
  ///
  /// In en, this message translates to:
  /// **'Set the game\'s folder first (the settings button).'**
  String get installGameMissing;

  /// No description provided for @installFilesMissing.
  ///
  /// In en, this message translates to:
  /// **'This launcher is not set up to get BeamMP\'s files for {game} (no list address or trusted key), so they cannot be installed.'**
  String installFilesMissing(String game);

  /// No description provided for @installAction.
  ///
  /// In en, this message translates to:
  /// **'Install'**
  String get installAction;

  /// No description provided for @installUpdateAction.
  ///
  /// In en, this message translates to:
  /// **'Update'**
  String get installUpdateAction;

  /// No description provided for @installRunning.
  ///
  /// In en, this message translates to:
  /// **'Installing…'**
  String get installRunning;

  /// No description provided for @installCancel.
  ///
  /// In en, this message translates to:
  /// **'Cancel'**
  String get installCancel;

  /// No description provided for @installCancelled.
  ///
  /// In en, this message translates to:
  /// **'Installing was cancelled.'**
  String get installCancelled;

  /// No description provided for @installFailedTitle.
  ///
  /// In en, this message translates to:
  /// **'Installing did not work.'**
  String get installFailedTitle;

  /// No description provided for @installErrorDownload.
  ///
  /// In en, this message translates to:
  /// **'The download failed. Check your connection and try again.'**
  String get installErrorDownload;

  /// No description provided for @installErrorChecksum.
  ///
  /// In en, this message translates to:
  /// **'The download is not the file BeamMP\'s list names, so it was not used.'**
  String get installErrorChecksum;

  /// No description provided for @installErrorConflict.
  ///
  /// In en, this message translates to:
  /// **'A winhttp.dll from another mod loader is in the game\'s folder. Remove it first, the launcher will not overwrite it.'**
  String get installErrorConflict;

  /// No description provided for @installErrorNotWritable.
  ///
  /// In en, this message translates to:
  /// **'The game\'s folder cannot be changed. Run the launcher with the rights to write there.'**
  String get installErrorNotWritable;

  /// No description provided for @installErrorUnsupportedBuild.
  ///
  /// In en, this message translates to:
  /// **'This build of the game is not one the launcher\'s plugin was made for.'**
  String get installErrorUnsupportedBuild;

  /// No description provided for @installErrorSignature.
  ///
  /// In en, this message translates to:
  /// **'BeamMP\'s list of files could not be verified (not signed by a trusted key, or older than one already seen), so nothing was installed.'**
  String get installErrorSignature;

  /// No description provided for @installErrorLauncherOld.
  ///
  /// In en, this message translates to:
  /// **'BeamMP\'s plugin for this build of the game needs a newer launcher. Update the launcher.'**
  String get installErrorLauncherOld;

  /// No description provided for @installErrorNotConfigured.
  ///
  /// In en, this message translates to:
  /// **'This launcher is not set up to get BeamMP\'s files for the game.'**
  String get installErrorNotConfigured;

  /// No description provided for @installErrorOther.
  ///
  /// In en, this message translates to:
  /// **'{message}'**
  String installErrorOther(String message);

  /// No description provided for @installBusy.
  ///
  /// In en, this message translates to:
  /// **'Wait until the game has stopped or the launcher is not busy, then try again.'**
  String get installBusy;

  /// No description provided for @installDismiss.
  ///
  /// In en, this message translates to:
  /// **'Dismiss'**
  String get installDismiss;

  /// No description provided for @installFirstStart.
  ///
  /// In en, this message translates to:
  /// **'The first start of the game takes a few minutes longer.'**
  String get installFirstStart;

  /// No description provided for @installInstalled.
  ///
  /// In en, this message translates to:
  /// **'Multiplayer support is installed.'**
  String get installInstalled;

  /// No description provided for @installInstalledVersion.
  ///
  /// In en, this message translates to:
  /// **'Multiplayer support is installed (version {version}).'**
  String installInstalledVersion(String version);

  /// No description provided for @installRemove.
  ///
  /// In en, this message translates to:
  /// **'Remove multiplayer support'**
  String get installRemove;

  /// No description provided for @installRemoveHelp.
  ///
  /// In en, this message translates to:
  /// **'Takes out the BeamMP plugin and mod. The mod loader stays.'**
  String get installRemoveHelp;

  /// No description provided for @installRemoving.
  ///
  /// In en, this message translates to:
  /// **'Removing…'**
  String get installRemoving;
}

class _AppLocalizationsDelegate
    extends LocalizationsDelegate<AppLocalizations> {
  const _AppLocalizationsDelegate();

  @override
  Future<AppLocalizations> load(Locale locale) {
    return SynchronousFuture<AppLocalizations>(lookupAppLocalizations(locale));
  }

  @override
  bool isSupported(Locale locale) =>
      <String>['en'].contains(locale.languageCode);

  @override
  bool shouldReload(_AppLocalizationsDelegate old) => false;
}

AppLocalizations lookupAppLocalizations(Locale locale) {
  // Lookup logic when only language code is specified.
  switch (locale.languageCode) {
    case 'en':
      return AppLocalizationsEn();
  }

  throw FlutterError(
    'AppLocalizations.delegate failed to load unsupported locale "$locale". This is likely '
    'an issue with the localizations generation tool. Please file an issue '
    'on GitHub with a reproducible sample app and the gen-l10n configuration '
    'that was used.',
  );
}
