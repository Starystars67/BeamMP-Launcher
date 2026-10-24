import 'package:beammp_launcher/core/platform/url_opener.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('https pages and the loopback address can be opened', () {
    expect(
      SystemUrlOpener.isOpenable(
        'https://portal.beammp.com/auth/external?client_id=x',
      ),
      isTrue,
    );
    expect(
      SystemUrlOpener.isOpenable('http://127.0.0.1:51234/callback'),
      isTrue,
    );
  });

  test('anything else is refused', () {
    for (final url in [
      '',
      'not a url',
      'http://portal.beammp.com/',
      'http://localhost:1234/',
      'http://127.0.0.1.evil.example/',
      'file:///etc/passwd',
      'ftp://example.com/',
      'javascript:alert(1)',
      'https://user:pass@example.com/',
      'https:///nohost',
      '-a Calculator',
    ]) {
      expect(SystemUrlOpener.isOpenable(url), isFalse, reason: url);
    }
  });

  test('open refuses a bad address without starting anything', () async {
    expect(await const SystemUrlOpener().open('file:///etc/passwd'), isFalse);
  });
}
