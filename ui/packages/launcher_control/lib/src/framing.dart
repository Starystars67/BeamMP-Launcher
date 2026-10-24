import 'dart:convert';

import 'exceptions.dart';

/// Default upper bound for one line, in characters. The engine limits itself to 1 MiB.
const int maxLineLength = 1024 * 1024;

/// Splits a byte stream into UTF-8 lines on `\n`.
///
/// Fails the stream with a [FramingException] when a line grows past
/// [maxLength] or the bytes are not valid UTF-8. A trailing partial line at
/// the end of the stream is dropped: the protocol terminates every message.
Stream<String> frameLines(
  Stream<List<int>> bytes, {
  int maxLength = maxLineLength,
}) async* {
  final pending = StringBuffer();
  var pendingLength = 0;
  try {
    await for (final chunk in utf8.decoder.bind(bytes)) {
      var start = 0;
      while (true) {
        final end = chunk.indexOf('\n', start);
        if (end < 0) {
          pending.write(chunk.substring(start));
          pendingLength += chunk.length - start;
          break;
        }
        pending.write(chunk.substring(start, end));
        pendingLength += end - start;
        if (pendingLength > maxLength) {
          throw const FramingException('Line too long');
        }
        yield pending.toString();
        pending.clear();
        pendingLength = 0;
        start = end + 1;
      }
      if (pendingLength > maxLength) {
        throw const FramingException('Line too long');
      }
    }
  } on FormatException {
    throw const FramingException('Invalid UTF-8');
  }
}
