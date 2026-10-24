import 'package:path/path.dart' as p;

import '../settings/launch_arguments.dart';

/// A command that starts the engine.
final class EngineCommand {
  const EngineCommand({
    required this.executable,
    this.engineArguments = const [],
  });

  final String executable;

  /// Extra engine flags, appended after the control flags.
  final List<String> engineArguments;
}

const String engineExecutableName = 'BeamMP-Launcher-Console';

/// Where the engine is expected, in order of preference.
///
/// `BEAMMP_ENGINE` (and optionally `BEAMMP_ENGINE_ARGS`) override everything
/// for development; otherwise the engine sits next to the UI (Windows, Linux)
/// or in the app bundle's Helpers folder (macOS).
List<String> engineCandidates({
  required Map<String, String> environment,
  required String uiExecutable,
  required String platform,
}) {
  final override = environment['BEAMMP_ENGINE'];
  if (override != null && override.isNotEmpty) {
    return [override];
  }
  final directory = p.dirname(uiExecutable);
  return switch (platform) {
    'macos' => [
      p.normalize(p.join(directory, '..', 'Helpers', engineExecutableName)),
    ],
    'windows' => [p.join(directory, '$engineExecutableName.exe')],
    _ => [p.join(directory, engineExecutableName)],
  };
}

/// Finds the engine or reports what was searched.
({EngineCommand? command, List<String> searched}) locateEngine({
  required Map<String, String> environment,
  required String uiExecutable,
  required String platform,
  required bool Function(String path) exists,
}) {
  final candidates = engineCandidates(
    environment: environment,
    uiExecutable: uiExecutable,
    platform: platform,
  );
  for (final candidate in candidates) {
    if (exists(candidate)) {
      // quotes keep a path with spaces together
      final args = parseLaunchArguments(
        environment['BEAMMP_ENGINE_ARGS'] ?? '',
      ).args;
      return (
        command: EngineCommand(executable: candidate, engineArguments: args),
        searched: candidates,
      );
    }
  }
  return (command: null, searched: candidates);
}
