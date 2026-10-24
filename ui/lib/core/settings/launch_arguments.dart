/// Why a launch arguments text cannot be used.
enum ArgumentsProblem { unterminatedQuote, tooMany, tooLong }

/// Protocol limits (see CONTROL_PROTOCOL.md).
const int maxLaunchArguments = 64;
const int maxLaunchArgumentLength = 4096;

const Set<String> _escapable = {'"', "'", ' '};

typedef ParsedArguments = ({List<String> args, ArgumentsProblem? problem});

/// Splits a command-line style text into arguments.
///
/// Whitespace separates arguments; single or double quotes group words
/// (`-userpath "C:\My Games"`). A backslash only escapes a quote or a space
/// (outside single quotes) and is otherwise kept, so Windows paths work
/// as typed. Nothing is expanded, nothing is run.
ParsedArguments parseLaunchArguments(String text) {
  final args = <String>[];
  final current = StringBuffer();
  var started = false;
  String? quote;

  void finish() {
    if (started) {
      args.add(current.toString());
      current.clear();
      started = false;
    }
  }

  final runes = text.runes.map(String.fromCharCode).toList();
  for (var i = 0; i < runes.length; i++) {
    final char = runes[i];
    if (quote == "'") {
      if (char == "'") {
        quote = null;
      } else {
        current.write(char);
      }
    } else if (char == r'\' &&
        i + 1 < runes.length &&
        _escapable.contains(runes[i + 1])) {
      current.write(runes[++i]);
      started = true;
    } else if (quote == '"') {
      if (char == '"') {
        quote = null;
      } else {
        current.write(char);
      }
    } else if (char == '"' || char == "'") {
      quote = char;
      started = true;
    } else if (char.trim().isEmpty) {
      finish();
    } else {
      current.write(char);
      started = true;
    }
  }
  if (quote != null) {
    return (args: const [], problem: ArgumentsProblem.unterminatedQuote);
  }
  finish();
  if (args.length > maxLaunchArguments) {
    return (args: const [], problem: ArgumentsProblem.tooMany);
  }
  if (args.any((arg) => arg.length > maxLaunchArgumentLength)) {
    return (args: const [], problem: ArgumentsProblem.tooLong);
  }
  return (args: args, problem: null);
}
