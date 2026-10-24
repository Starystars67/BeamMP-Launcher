import 'package:beammp_launcher/core/settings/launch_arguments.dart';
import 'package:flutter_test/flutter_test.dart';

List<String> args(String text) {
  final parsed = parseLaunchArguments(text);
  expect(parsed.problem, isNull, reason: text);
  return parsed.args;
}

void main() {
  test('empty and blank text have no arguments', () {
    expect(args(''), isEmpty);
    expect(args('   \t  '), isEmpty);
  });

  test('whitespace separates arguments', () {
    expect(args('-console -nosound'), ['-console', '-nosound']);
    expect(args('  a   b\tc  '), ['a', 'b', 'c']);
  });

  test('double and single quotes group words', () {
    expect(args('-userpath "C:\\My Games"'), ['-userpath', 'C:\\My Games']);
    expect(args("--name 'two words'"), ['--name', 'two words']);
    expect(args('a"b c"d'), ['ab cd']);
  });

  test('Windows paths keep their backslashes', () {
    expect(args(r'-userpath C:\Users\me\BeamNG'), [
      '-userpath',
      r'C:\Users\me\BeamNG',
    ]);
    expect(args(r'"\\server\share"'), [r'\\server\share']);
  });

  test('a backslash escapes a quote or a space', () {
    expect(args(r'a\ b'), ['a b']);
    expect(args(r'"say \"hi\""'), ['say "hi"']);
    expect(args(r"'keep\this'"), [r'keep\this']);
  });

  test('empty quotes are an empty argument', () {
    expect(args('a "" b'), ['a', '', 'b']);
  });

  test('an unterminated quote is reported', () {
    expect(
      parseLaunchArguments('a "b').problem,
      ArgumentsProblem.unterminatedQuote,
    );
    expect(
      parseLaunchArguments("a 'b").problem,
      ArgumentsProblem.unterminatedQuote,
    );
    expect(parseLaunchArguments('a "b').args, isEmpty);
  });

  test('a trailing backslash is kept literally', () {
    expect(args(r'path\'), [r'path\']);
  });

  test('non-ASCII text survives', () {
    expect(args('-name "Zoë 🚗"'), ['-name', 'Zoë 🚗']);
  });

  test('nothing is expanded or executed', () {
    expect(args(r'$HOME `whoami` ~ *'), [r'$HOME', '`whoami`', '~', '*']);
  });

  test('the protocol limits are enforced', () {
    expect(
      parseLaunchArguments(
        List.filled(maxLaunchArguments, 'a').join(' '),
      ).problem,
      isNull,
    );
    expect(
      parseLaunchArguments(
        List.filled(maxLaunchArguments + 1, 'a').join(' '),
      ).problem,
      ArgumentsProblem.tooMany,
    );
    expect(parseLaunchArguments('a' * maxLaunchArgumentLength).problem, isNull);
    expect(
      parseLaunchArguments('a' * (maxLaunchArgumentLength + 1)).problem,
      ArgumentsProblem.tooLong,
    );
  });
}
