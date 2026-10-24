/// Client for the BeamMP launcher engine control protocol (version 1).
///
/// Specification: `docs/CONTROL_PROTOCOL.md` in the launcher repository.
library;

export 'src/client.dart';
export 'src/engine_process.dart';
export 'src/exceptions.dart';
export 'src/framing.dart' show frameLines, maxLineLength;
export 'src/models.dart';
export 'src/transport.dart';
