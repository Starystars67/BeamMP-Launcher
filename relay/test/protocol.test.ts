import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  DataKind,
  FrameDecoder,
  FromRelay,
  MAX_FRAME_BYTES,
  ProtocolError,
  ROOM_CODE_ALPHABET,
  ToRelay,
  decodeDataFromRelay,
  decodeDataToRelay,
  decodeJson,
  encodeDataFromRelay,
  encodeDataToRelay,
  encodeFrame,
  encodeJson,
  isRoomCode,
} from '../src/protocol.ts';

test('a frame is its length, its type and its payload', () => {
  const frame = encodeFrame(0x42, Buffer.from('abc'));
  assert.deepEqual([...frame], [0, 0, 0, 4, 0x42, 0x61, 0x62, 0x63]);
  assert.deepEqual([...encodeFrame(0x20)], [0, 0, 0, 1, 0x20]);
});

test('frames come out whole whatever way the bytes arrive', () => {
  const a = encodeFrame(1, Buffer.from('one'));
  const b = encodeFrame(2, Buffer.from('two'));
  const c = encodeFrame(3);
  const stream = Buffer.concat([a, b, c]);

  const whole = new FrameDecoder().push(stream);
  assert.deepEqual(whole.map((f) => [f.type, f.payload.toString()]), [[1, 'one'], [2, 'two'], [3, '']]);

  const byByte = new FrameDecoder();
  const seen: Array<[number, string]> = [];
  for (const byte of stream) {
    for (const frame of byByte.push(Buffer.from([byte]))) {
      seen.push([frame.type, frame.payload.toString()]);
    }
  }
  assert.deepEqual(seen, [[1, 'one'], [2, 'two'], [3, '']]);
  assert.equal(byByte.pending, 0);
});

test('a half frame is held back until the rest comes', () => {
  const decoder = new FrameDecoder();
  const frame = encodeFrame(7, Buffer.from('payload'));
  assert.deepEqual(decoder.push(frame.subarray(0, 6)), []);
  assert.equal(decoder.pending, 6);
  const rest = decoder.push(frame.subarray(6));
  assert.equal(rest.length, 1);
  assert.equal(rest[0]!.payload.toString(), 'payload');
});

test('the decoder gives back copies, not views of its buffer', () => {
  const decoder = new FrameDecoder();
  const [first] = decoder.push(Buffer.concat([encodeFrame(1, Buffer.from('keep')), encodeFrame(2, Buffer.from('next'))]));
  decoder.push(encodeFrame(3, Buffer.alloc(1000, 0x78)));
  assert.equal(first!.payload.toString(), 'keep');
});

test('lengths that cannot be right are refused', () => {
  const zero = Buffer.from([0, 0, 0, 0]);
  assert.throws(() => new FrameDecoder().push(zero), (e: unknown) => e instanceof ProtocolError && e.code === 'bad_frame');
  const huge = Buffer.alloc(4);
  huge.writeUInt32BE(MAX_FRAME_BYTES + 1);
  assert.throws(() => new FrameDecoder().push(huge), (e: unknown) => e instanceof ProtocolError && e.code === 'bad_frame');
  const largest = Buffer.alloc(4);
  largest.writeUInt32BE(MAX_FRAME_BYTES);
  assert.doesNotThrow(() => new FrameDecoder().push(largest)); // waits for the payload
});

test('a frame over the limit cannot be built', () => {
  assert.throws(() => encodeFrame(1, Buffer.alloc(MAX_FRAME_BYTES)), (e: unknown) => e instanceof ProtocolError && e.code === 'frame_too_large');
  assert.doesNotThrow(() => encodeFrame(1, Buffer.alloc(MAX_FRAME_BYTES - 1)));
});

test('JSON payloads are objects, nothing else', () => {
  assert.deepEqual(decodeJson(Buffer.from('{"a":1}')), { a: 1 });
  for (const bad of ['[1]', '"text"', '5', 'null', '{oops', '']) {
    assert.throws(() => decodeJson(Buffer.from(bad)), (e: unknown) => e instanceof ProtocolError && e.code === 'bad_message', bad);
  }
  const frame = encodeJson(FromRelay.Welcome, { v: 1 });
  assert.equal(frame[4], FromRelay.Welcome);
});

test('data frames carry kind and target going in, sender and kind coming out', () => {
  const going = encodeDataToRelay(DataKind.ToPeer, 0x0102, Buffer.from([9, 8, 7]));
  const parsed = new FrameDecoder().push(going)[0]!;
  assert.equal(parsed.type, ToRelay.Data);
  assert.deepEqual(decodeDataToRelay(parsed.payload), { kind: DataKind.ToPeer, target: 0x0102, data: Buffer.from([9, 8, 7]) });

  const coming = new FrameDecoder().push(encodeDataFromRelay(0x0304, DataKind.ToAll, Buffer.from([1])))[0]!;
  assert.equal(coming.type, FromRelay.Data);
  assert.deepEqual(decodeDataFromRelay(coming.payload), { sender: 0x0304, kind: DataKind.ToAll, data: Buffer.from([1]) });
});

test('a data frame without its header is refused', () => {
  assert.throws(() => decodeDataToRelay(Buffer.from([0, 0])), (e: unknown) => e instanceof ProtocolError);
  assert.throws(() => decodeDataFromRelay(Buffer.from([0, 0])), (e: unknown) => e instanceof ProtocolError);
  assert.deepEqual(decodeDataToRelay(Buffer.from([0, 0, 0])).data, Buffer.alloc(0)); // an empty body is fine
});

test('room codes are six characters from an alphabet without look-alikes', () => {
  assert.ok(isRoomCode('ABC234'));
  for (const bad of ['', 'ABC23', 'ABC2345', 'abc234', 'ABC 34', 'ABCO34', 'ABCI34', 'ABC134', 'ABC0L4', 5, null, undefined]) {
    assert.equal(isRoomCode(bad), false, String(bad));
  }
  for (const look of '01OIL') {
    assert.equal(ROOM_CODE_ALPHABET.includes(look), false);
  }
});
