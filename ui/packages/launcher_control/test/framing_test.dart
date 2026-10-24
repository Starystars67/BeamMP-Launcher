import 'dart:async';
import 'dart:convert';

import 'package:launcher_control/launcher_control.dart';
import 'package:test/test.dart';

Stream<List<int>> chunks(List<List<int>> parts) => Stream.fromIterable(parts);

Future<List<String>> collect(Stream<List<int>> bytes, {int? maxLength}) =>
    (maxLength == null
            ? frameLines(bytes)
            : frameLines(bytes, maxLength: maxLength))
        .toList();

void main() {
  test('splits on newlines and drops the terminator', () async {
    expect(await collect(chunks([utf8.encode('one\ntwo\nthree\n')])),
        ['one', 'two', 'three']);
  });

  test('reassembles lines split across chunks', () async {
    expect(
      await collect(chunks(
          [utf8.encode('hel'), utf8.encode('lo\nwor'), utf8.encode('ld\n')])),
      ['hello', 'world'],
    );
  });

  test('handles a chunk boundary inside a multi-byte character', () async {
    final bytes = utf8.encode('héllo €\n');
    final split = bytes.indexOf(0xC3) + 1;
    expect(
      await collect(chunks([bytes.sublist(0, split), bytes.sublist(split)])),
      ['héllo €'],
    );
  });

  test('one byte at a time works', () async {
    final bytes = utf8.encode('a\nbc\n');
    expect(
        await collect(chunks([
          for (final b in bytes) [b]
        ])),
        ['a', 'bc']);
  });

  test('keeps empty lines and a carriage return', () async {
    expect(await collect(chunks([utf8.encode('\n\r\nx\n')])), ['', '\r', 'x']);
  });

  test('drops a trailing line without a terminator', () async {
    expect(await collect(chunks([utf8.encode('done\npartial')])), ['done']);
  });

  test('an empty stream gives no lines', () async {
    expect(await collect(chunks([])), isEmpty);
  });

  test('a line over the limit fails the stream', () async {
    final stream =
        frameLines(chunks([utf8.encode('${'x' * 20}\n')]), maxLength: 10);
    await expectLater(stream.toList(), throwsA(isA<FramingException>()));
  });

  test('a long line is caught before its terminator arrives', () async {
    final controller = StreamController<List<int>>();
    final result = frameLines(controller.stream, maxLength: 10).toList();
    controller.add(utf8.encode('x' * 11));
    await expectLater(result, throwsA(isA<FramingException>()));
    await controller.close();
  });

  test('a line exactly at the limit is fine', () async {
    expect(await collect(chunks([utf8.encode('${'x' * 10}\n')]), maxLength: 10),
        ['x' * 10]);
  });

  test('lines before the offending one are still delivered', () async {
    final seen = <String>[];
    final stream =
        frameLines(chunks([utf8.encode('ok\n${'x' * 20}\n')]), maxLength: 10);
    await expectLater(
        stream.forEach(seen.add), throwsA(isA<FramingException>()));
    expect(seen, ['ok']);
  });

  test('invalid UTF-8 fails the stream', () async {
    await expectLater(
      collect(chunks([
        [0xFF, 0xFE, 0x0A],
      ])),
      throwsA(isA<FramingException>()),
    );
  });
}
