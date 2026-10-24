import assert from 'node:assert/strict';
import { afterEach, test } from 'node:test';
import { DataKind, FromRelay, PROTOCOL_VERSION, ROOM_CODE_LENGTH, ToRelay, encodeFrame } from '../src/protocol.ts';
import { RelayServer } from '../src/server.ts';
import type { RelayOptions } from '../src/server.ts';
import { TestClient } from './client.ts';

const clients: TestClient[] = [];
const relays: RelayServer[] = [];

afterEach(async () => {
  for (const client of clients.splice(0)) {
    client.close();
  }
  for (const relay of relays.splice(0)) {
    await relay.close();
  }
});

async function start(options: RelayOptions = {}): Promise<{ relay: RelayServer; port: number }> {
  const relay = new RelayServer({ handshakeTimeoutMs: 400, ...options });
  relays.push(relay);
  return { relay, port: await relay.start() };
}

async function connect(port: number, name?: string): Promise<TestClient> {
  const client = await TestClient.connect(port);
  clients.push(client);
  if (name !== undefined) {
    await client.hello(name);
  }
  return client;
}

/** A host with a room, and `names` already joined (each one has consumed its own joined message). */
async function room(port: number, names: string[] = [], request: Record<string, unknown> = {}) {
  const host = await connect(port, 'Host');
  const code = await host.createRoom(request);
  const guests: TestClient[] = [];
  for (const name of names) {
    const guest = await connect(port, name);
    await guest.joinRoom(code);
    guests.push(guest);
  }
  // the host heard about everybody
  for (let i = 0; i < names.length; i++) {
    await host.expect(FromRelay.PeerJoined);
  }
  // and each guest about the ones that came after it
  for (let i = 0; i < guests.length; i++) {
    for (let later = i + 1; later < guests.length; later++) {
      await guests[i]!.expect(FromRelay.PeerJoined);
    }
  }
  return { host, guests, code };
}

test('hello is answered with the relay\'s version and limits', async () => {
  const { port } = await start();
  const client = await connect(port);
  const welcome = await client.hello('Ann');
  assert.equal(welcome.v, PROTOCOL_VERSION);
  assert.equal(welcome.server, 'beammp-relay');
  assert.ok((welcome.limits as { max_frame: number }).max_frame > 0);
});

test('anything but hello first ends the connection', async () => {
  const { port } = await start();
  const client = await connect(port);
  client.sendJson(ToRelay.CreateRoom, {});
  assert.equal(await client.error(), 'hello_first');
  assert.ok(await client.waitClosed());
});

test('a client that never says hello is dropped', async () => {
  const { port, relay } = await start({ handshakeTimeoutMs: 150 });
  const client = await connect(port);
  assert.equal(await client.error(), 'handshake_timeout');
  assert.ok(await client.waitClosed());
  assert.equal(relay.stats().connections, 0);
});

test('hello is refused for another version, another game and bad names', async () => {
  const { port } = await start();
  const cases: Array<[Record<string, unknown>, string]> = [
    [{ v: 2, name: 'Ann', game: 'gearblocks' }, 'unsupported_version'],
    [{ name: 'Ann', game: 'gearblocks' }, 'unsupported_version'],
    [{ v: 1, name: 'Ann', game: 'beamng' }, 'unknown_game'],
    [{ v: 1, name: 'Ann' }, 'unknown_game'],
    [{ v: 1, name: '', game: 'gearblocks' }, 'bad_name'],
    [{ v: 1, name: '   ', game: 'gearblocks' }, 'bad_name'],
    [{ v: 1, name: 'x'.repeat(33), game: 'gearblocks' }, 'bad_name'],
    [{ v: 1, name: 'bad\u0007name', game: 'gearblocks' }, 'bad_name'],
    [{ v: 1, name: 'line\nbreak', game: 'gearblocks' }, 'bad_name'],
    [{ v: 1, name: 5, game: 'gearblocks' }, 'bad_name'],
  ];
  for (const [hello, code] of cases) {
    const client = await connect(port);
    client.sendJson(ToRelay.Hello, hello);
    assert.equal(await client.error(), code, JSON.stringify(hello));
    assert.ok(await client.waitClosed());
  }
});

test('names are trimmed and may be long, unusual text', async () => {
  const { port } = await start();
  const { host, code } = await room(port);
  const guest = await connect(port);
  await guest.hello('  Zoë 🚗 Ünïcode  ');
  await guest.joinRoom(code);
  const joined = await host.json(FromRelay.PeerJoined);
  assert.equal(joined.name, 'Zoë 🚗 Ünïcode');
  const longest = await connect(port);
  await longest.hello('x'.repeat(32));
});

test('a malformed hello ends the connection', async () => {
  const { port } = await start();
  const client = await connect(port);
  client.send(encodeFrame(ToRelay.Hello, Buffer.from('not json')));
  assert.equal(await client.error(), 'bad_message');
  assert.ok(await client.waitClosed());
});

test('hello twice is an error the connection survives', async () => {
  const { port } = await start();
  const client = await connect(port, 'Ann');
  client.sendJson(ToRelay.Hello, { v: 1, name: 'Ann', game: 'gearblocks' });
  assert.equal(await client.error(), 'bad_message');
  assert.equal(await client.createRoom().then((code) => code.length), ROOM_CODE_LENGTH);
});

test('garbage on the wire ends the connection', async () => {
  const { port } = await start();
  const client = await connect(port, 'Ann');
  client.send(Buffer.from([0xff, 0xff, 0xff, 0xff, 1, 2, 3]));
  assert.equal(await client.error(), 'bad_frame');
  assert.ok(await client.waitClosed());
});

test('an unknown message type is an error the connection survives', async () => {
  const { port } = await start();
  const client = await connect(port, 'Ann');
  client.send(encodeFrame(0x55));
  assert.equal(await client.error(), 'bad_message');
  client.ping();
  await client.expect(FromRelay.Pong);
});

test('the authenticator decides who comes in and under which name', async () => {
  const seen: Array<{ token: string | undefined; name: string; ip: string }> = [];
  const { port } = await start({
    authenticate: async (hello) => {
      seen.push({ token: hello.token, name: hello.name, ip: hello.ip });
      return hello.token === 'good' ? { name: 'Verified Ann' } : 'bad token';
    },
  });
  const refused = await connect(port);
  refused.sendJson(ToRelay.Hello, { v: 1, name: 'Ann', game: 'gearblocks', token: 'nope' });
  assert.equal(await refused.error(), 'refused');
  assert.ok(await refused.waitClosed());

  const accepted = await connect(port);
  await accepted.hello('Ann', { token: 'good' });
  const code = await accepted.createRoom();
  const other = await connect(port);
  await other.hello('Bob', { token: 'good' });
  await other.joinRoom(code);
  // the name the authenticator returned is the one the others see, not the one the client sent
  assert.equal((await accepted.json(FromRelay.PeerJoined)).name, 'Verified Ann');
  assert.equal(seen[0]!.token, 'nope');
  assert.equal(seen[0]!.ip, '127.0.0.1');
});

test('an authenticator that fails refuses the connection', async () => {
  const { port } = await start({
    authenticate: async () => {
      throw new Error('backend down');
    },
  });
  const client = await connect(port);
  client.sendJson(ToRelay.Hello, { v: 1, name: 'Ann', game: 'gearblocks' });
  assert.equal(await client.error(), 'refused');
  assert.ok(await client.waitClosed());
});

test('a second hello while the first is being checked ends the connection', async () => {
  let release: () => void = () => {};
  const { port } = await start({
    authenticate: () => new Promise((resolve) => (release = () => resolve({ name: 'Ann' }))),
  });
  const client = await connect(port);
  client.sendJson(ToRelay.Hello, { v: 1, name: 'Ann', game: 'gearblocks' });
  client.sendJson(ToRelay.Hello, { v: 1, name: 'Ann', game: 'gearblocks' });
  assert.equal(await client.error(), 'bad_message');
  assert.ok(await client.waitClosed());
  release();
});

test('a room has a code, and its creator is the host, peer 1', async () => {
  const { port, relay } = await start();
  const host = await connect(port, 'Host');
  host.sendJson(ToRelay.CreateRoom, {});
  const created = await host.json(FromRelay.RoomCreated);
  assert.match(String(created.code), /^[A-HJKMNP-Z2-9]{6}$/);
  assert.equal(created.peer_id, 1);
  assert.equal(relay.stats().rooms, 1);
});

test('codes are random and different', async () => {
  const { port } = await start({ maxRoomsPerIp: 100, maxConnectionsPerIp: 100 });
  const codes = new Set<string>();
  for (let i = 0; i < 40; i++) {
    const host = await connect(port, `Host ${i}`);
    codes.add(await host.createRoom());
  }
  assert.equal(codes.size, 40);
});

test('joining a room tells the joiner who is there and the others who came', async () => {
  const { port } = await start();
  const host = await connect(port, 'Host');
  const code = await host.createRoom();

  const ann = await connect(port, 'Ann');
  const annJoined = await ann.joinRoom(code);
  assert.equal(annJoined.code, code);
  assert.equal(annJoined.peer_id, 2);
  assert.equal(annJoined.host_id, 1);
  assert.deepEqual(annJoined.peers, [{ id: 1, name: 'Host' }]);
  assert.deepEqual(await host.json(FromRelay.PeerJoined), { id: 2, name: 'Ann' });

  const bob = await connect(port, 'Bob');
  const bobJoined = await bob.joinRoom(code);
  assert.equal(bobJoined.peer_id, 3);
  assert.deepEqual(bobJoined.peers, [{ id: 1, name: 'Host' }, { id: 2, name: 'Ann' }]);
  assert.deepEqual(await host.json(FromRelay.PeerJoined), { id: 3, name: 'Bob' });
  assert.deepEqual(await ann.json(FromRelay.PeerJoined), { id: 3, name: 'Bob' });
});

test('a player that leaves is announced, and its id is not given again', async () => {
  const { port } = await start();
  const { host, guests, code } = await room(port, ['Ann', 'Bob']);
  guests[0]!.sendJson(ToRelay.LeaveRoom, {});
  assert.deepEqual(await host.json(FromRelay.PeerLeft), { id: 2 });
  assert.deepEqual(await guests[1]!.json(FromRelay.PeerLeft), { id: 2 });

  const carl = await connect(port, 'Carl');
  assert.equal((await carl.joinRoom(code)).peer_id, 4);
});

test('a player whose connection drops is announced too', async () => {
  const { port, relay } = await start();
  const { host, guests } = await room(port, ['Ann']);
  guests[0]!.close();
  assert.deepEqual(await host.json(FromRelay.PeerLeft), { id: 2 });
  assert.equal(relay.stats().rooms, 1);
});

test('when the host leaves the room is closed and everybody is back in the lobby', async () => {
  const { port, relay } = await start();
  const { host, guests, code } = await room(port, ['Ann', 'Bob']);
  host.sendJson(ToRelay.LeaveRoom, {});
  for (const guest of guests) {
    assert.deepEqual(await guest.json(FromRelay.RoomClosed), { reason: 'host_left' });
  }
  assert.equal(relay.stats().rooms, 0);

  // the connections live on: they can make a new room, and the old code is gone
  const newCode = await guests[0]!.createRoom();
  assert.notEqual(newCode, code);
  const late = await connect(port, 'Late');
  late.sendJson(ToRelay.JoinRoom, { code });
  assert.equal(await late.error(), 'room_not_found');
});

test('when the host\'s connection drops the room is closed', async () => {
  const { port, relay } = await start();
  const { host, guests } = await room(port, ['Ann']);
  host.close();
  assert.deepEqual(await guests[0]!.json(FromRelay.RoomClosed), { reason: 'host_gone' });
  assert.equal(relay.stats().rooms, 0);
});

test('a room can be joined by code, and only by a code that exists', async () => {
  const { port } = await start();
  await room(port);
  const guest = await connect(port, 'Ann');
  guest.sendJson(ToRelay.JoinRoom, { code: 'ZZZZZZ' });
  assert.equal(await guest.error(), 'room_not_found');
  for (const bad of ['', 'abc', 'ABCDEFG', 'ABC 12', 5, null]) {
    guest.sendJson(ToRelay.JoinRoom, { code: bad });
    assert.equal(await guest.error(), 'bad_code', String(bad));
  }
  guest.sendJson(ToRelay.JoinRoom, {});
  assert.equal(await guest.error(), 'bad_code');
});

test('a full room refuses more players, and has space again when one leaves', async () => {
  const { port } = await start();
  const { guests, code } = await room(port, ['Ann'], { max_players: 2 });
  const bob = await connect(port, 'Bob');
  bob.sendJson(ToRelay.JoinRoom, { code });
  assert.equal(await bob.error(), 'room_full');
  guests[0]!.sendJson(ToRelay.LeaveRoom, {});
  await new Promise((resolve) => setTimeout(resolve, 50));
  assert.equal((await bob.joinRoom(code)).peer_id, 3);
});

test('max_players is checked', async () => {
  const { port } = await start({ hardMaxPlayers: 10 });
  const host = await connect(port, 'Host');
  for (const bad of [0, 1, 11, 2.5, '4', -3, null]) {
    host.sendJson(ToRelay.CreateRoom, { max_players: bad });
    assert.equal(await host.error(), 'bad_message', String(bad));
  }
  assert.equal((await host.createRoom({ max_players: 10 })).length, ROOM_CODE_LENGTH);
});

test('a room with a password needs it', async () => {
  const { port } = await start();
  const { code } = await room(port, [], { password: 'swordfish' });
  const guest = await connect(port, 'Ann');
  for (const wrong of [undefined, '', 'swordfis', 'SWORDFISH', 'swordfish ']) {
    guest.sendJson(ToRelay.JoinRoom, wrong === undefined ? { code } : { code, password: wrong });
    assert.equal(await guest.error(), 'bad_password', String(wrong));
  }
  assert.equal((await guest.joinRoom(code, 'swordfish')).peer_id, 2);
});

test('a password is text of a sensible size, and empty means none', async () => {
  const { port } = await start();
  const host = await connect(port, 'Host');
  host.sendJson(ToRelay.CreateRoom, { password: 'x'.repeat(65) });
  assert.equal(await host.error(), 'bad_message');
  host.sendJson(ToRelay.CreateRoom, { password: 12 });
  assert.equal(await host.error(), 'bad_message');
  const code = await host.createRoom({ password: '' });
  const guest = await connect(port, 'Ann');
  await guest.joinRoom(code);
});

test('somebody who keeps guessing is cut off', async () => {
  const { port } = await start({ maxFailedJoins: 4 });
  await room(port);
  const guesser = await connect(port, 'Eve');
  for (let i = 0; i < 3; i++) {
    guesser.sendJson(ToRelay.JoinRoom, { code: 'ZZZZZ' + 'AB'[i % 2] });
    assert.equal(await guesser.error(), 'room_not_found');
  }
  guesser.sendJson(ToRelay.JoinRoom, { code: 'ZZZZZZ' });
  assert.equal(await guesser.error(), 'too_many_attempts');
  assert.ok(await guesser.waitClosed());
});

test('wrong passwords count as guesses too', async () => {
  const { port } = await start({ maxFailedJoins: 2 });
  const { code } = await room(port, [], { password: 'secret' });
  const guesser = await connect(port, 'Eve');
  guesser.sendJson(ToRelay.JoinRoom, { code, password: 'one' });
  assert.equal(await guesser.error(), 'bad_password');
  guesser.sendJson(ToRelay.JoinRoom, { code, password: 'two' });
  assert.equal(await guesser.error(), 'too_many_attempts');
  assert.ok(await guesser.waitClosed());
});

test('one connection is in one room at a time, and leaving needs a room', async () => {
  const { port } = await start();
  const { host, code } = await room(port);
  host.sendJson(ToRelay.CreateRoom, {});
  assert.equal(await host.error(), 'already_in_room');
  host.sendJson(ToRelay.JoinRoom, { code });
  assert.equal(await host.error(), 'already_in_room');
  const lobby = await connect(port, 'Ann');
  lobby.sendJson(ToRelay.LeaveRoom, {});
  assert.equal(await lobby.error(), 'not_in_room');
});

test('a game only finds rooms of its own game', async () => {
  const { port } = await start({ games: ['gearblocks', 'other'] });
  const { code } = await room(port);
  const other = await connect(port);
  await other.hello('Ann', { game: 'other' });
  other.sendJson(ToRelay.JoinRoom, { code });
  assert.equal(await other.error(), 'room_not_found');
});

test('data from a player goes to the host, with the sender', async () => {
  const { port } = await start();
  const { host, guests } = await room(port, ['Ann', 'Bob']);
  guests[1]!.sendData(DataKind.ToHost, 0, Buffer.from([1, 2, 3]));
  const received = await host.data();
  assert.deepEqual(received, { sender: 3, kind: DataKind.ToHost, data: Buffer.from([1, 2, 3]) });
  assert.ok(await guests[0]!.quiet());
});

test('data from the host to all goes to every player but the host', async () => {
  const { port } = await start();
  const { host, guests } = await room(port, ['Ann', 'Bob']);
  host.sendData(DataKind.ToAll, 0, Buffer.from('hello'));
  for (const guest of guests) {
    assert.deepEqual(await guest.data(), { sender: 1, kind: DataKind.ToAll, data: Buffer.from('hello') });
  }
  assert.ok(await host.quiet());
});

test('data from the host to one player goes to that player only', async () => {
  const { port } = await start();
  const { host, guests } = await room(port, ['Ann', 'Bob']);
  host.sendData(DataKind.ToPeer, 3, Buffer.from('for bob'));
  assert.deepEqual(await guests[1]!.data(), { sender: 1, kind: DataKind.ToPeer, data: Buffer.from('for bob') });
  assert.ok(await guests[0]!.quiet());
});

test('data to others reaches everybody else, the host included', async () => {
  const { port } = await start();
  const { host, guests } = await room(port, ['Ann', 'Bob']);
  guests[0]!.sendData(DataKind.ToOthers, 0, Buffer.from('state'));
  assert.deepEqual(await host.data(), { sender: 2, kind: DataKind.ToOthers, data: Buffer.from('state') });
  assert.deepEqual(await guests[1]!.data(), { sender: 2, kind: DataKind.ToOthers, data: Buffer.from('state') });
  assert.ok(await guests[0]!.quiet());

  host.sendData(DataKind.ToOthers, 0, Buffer.from('from host'));
  assert.equal((await guests[0]!.data()).sender, 1);
  assert.equal((await guests[1]!.data()).sender, 1);
});

test('only the host may send to everyone or to one player, and nobody sends to a missing player', async () => {
  const { port } = await start();
  const { host, guests } = await room(port, ['Ann']);
  guests[0]!.sendData(DataKind.ToAll, 0, Buffer.from('x'));
  assert.equal(await guests[0]!.error(), 'not_host');
  guests[0]!.sendData(DataKind.ToPeer, 1, Buffer.from('x'));
  assert.equal(await guests[0]!.error(), 'not_host');
  host.sendData(DataKind.ToPeer, 9, Buffer.from('x'));
  assert.equal(await host.error(), 'peer_not_found');
  host.sendData(DataKind.ToPeer, 1, Buffer.from('x')); // itself
  assert.equal(await host.error(), 'peer_not_found');
  host.sendData(DataKind.ToHost, 0, Buffer.from('x'));
  assert.equal(await host.error(), 'bad_target');
  host.sendData(9, 0, Buffer.from('x'));
  assert.equal(await host.error(), 'bad_message');
  assert.ok(await guests[0]!.quiet());
});

test('data needs a room and a header', async () => {
  const { port } = await start();
  const lobby = await connect(port, 'Ann');
  lobby.sendData(DataKind.ToOthers, 0, Buffer.from('x'));
  assert.equal(await lobby.error(), 'not_in_room');
  const { host } = await room(port);
  host.send(encodeFrame(ToRelay.Data, Buffer.from([0, 0])));
  assert.equal(await host.error(), 'bad_message');
});

test('a player cannot reach a room it is not in', async () => {
  const { port } = await start();
  await room(port, ['Ann']);
  const second = await room(port, ['Bob']);
  second.guests[0]!.sendData(DataKind.ToOthers, 0, Buffer.from('mine'));
  assert.equal((await second.host.data()).data.toString(), 'mine');
  assert.ok(await (await connect(port, 'Outsider')).quiet());
});

test('data arrives byte for byte, in order, empty payloads and big ones too', async () => {
  const { port } = await start();
  const { host, guests } = await room(port, ['Ann']);
  const big = Buffer.alloc(1024 * 1024);
  for (let i = 0; i < big.length; i++) {
    big[i] = (i * 31) & 0xff;
  }
  host.sendData(DataKind.ToAll, 0, Buffer.alloc(0));
  for (let i = 0; i < 200; i++) {
    host.sendData(DataKind.ToAll, 0, Buffer.from([i]));
  }
  host.sendData(DataKind.ToAll, 0, big);

  assert.equal((await guests[0]!.data()).data.length, 0);
  for (let i = 0; i < 200; i++) {
    assert.deepEqual((await guests[0]!.data()).data, Buffer.from([i]));
  }
  const received = await guests[0]!.data(5000);
  assert.ok(received.data.equals(big));
});

test('connections per address are limited', async () => {
  const { port, relay } = await start({ maxConnectionsPerIp: 2 });
  const a = await connect(port, 'A');
  await connect(port, 'B');
  const third = await TestClient.connect(port);
  clients.push(third);
  assert.ok(await third.waitClosed());
  assert.equal(relay.stats().connections, 2);
  a.close();
  await new Promise((resolve) => setTimeout(resolve, 100));
  await connect(port, 'C'); // room again
});

test('the number of connections is limited', async () => {
  const { port } = await start({ maxConnections: 2, maxConnectionsPerIp: 10 });
  await connect(port, 'A');
  await connect(port, 'B');
  const third = await TestClient.connect(port);
  clients.push(third);
  assert.ok(await third.waitClosed());
});

test('rooms are limited in number and per address', async () => {
  const { port } = await start({ maxRoomsPerIp: 2, maxConnectionsPerIp: 10 });
  const first = await connect(port, 'A');
  await first.createRoom();
  const second = await connect(port, 'B');
  await second.createRoom();
  const third = await connect(port, 'C');
  third.sendJson(ToRelay.CreateRoom, {});
  assert.equal(await third.error(), 'too_many_rooms');
  first.sendJson(ToRelay.LeaveRoom, {}); // closing a room frees the place
  await new Promise((resolve) => setTimeout(resolve, 50));
  assert.equal((await third.createRoom()).length, ROOM_CODE_LENGTH);

  const { port: port2 } = await start({ maxRooms: 1, maxConnectionsPerIp: 10 });
  const host = await connect(port2, 'A');
  await host.createRoom();
  const other = await connect(port2, 'B');
  other.sendJson(ToRelay.CreateRoom, {});
  assert.equal(await other.error(), 'too_many_rooms');
});

test('sending too much too fast ends the connection', async () => {
  const { port } = await start({ bytesPerSecond: 1000, burstBytes: 4000 });
  const { host } = await room(port);
  host.sendData(DataKind.ToOthers, 0, Buffer.alloc(3000));
  host.sendData(DataKind.ToOthers, 0, Buffer.alloc(3000));
  assert.equal(await host.error(), 'rate_limited');
  assert.ok(await host.waitClosed());
});

test('the allowance fills up again over time', async () => {
  const { port } = await start({ bytesPerSecond: 20_000, burstBytes: 4000 });
  const { host, guests } = await room(port, ['Ann']);
  for (let round = 0; round < 4; round++) {
    host.sendData(DataKind.ToAll, 0, Buffer.alloc(3000));
    assert.equal((await guests[0]!.data()).data.length, 3000);
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  assert.equal(host.closed, false);
});

test('a connection that says nothing for too long is dropped, one that pings is not', async () => {
  const { port } = await start({ idleTimeoutMs: 300, checkIntervalMs: 50 });
  const quiet = await connect(port, 'Quiet');
  const pinging = await connect(port, 'Pinging');
  for (let i = 0; i < 8; i++) {
    pinging.ping();
    await pinging.expect(FromRelay.Pong);
    await new Promise((resolve) => setTimeout(resolve, 60));
  }
  assert.ok(await quiet.waitClosed(1000));
  assert.equal(pinging.closed, false);
});

test('a player that goes quiet leaves its room', async () => {
  const { port } = await start({ idleTimeoutMs: 300, checkIntervalMs: 50 });
  const { host, guests } = await room(port, ['Ann']);
  const keepalive = setInterval(() => host.ping(), 80);
  try {
    let left: Record<string, unknown> | undefined;
    while (!left) {
      const frame = await host.next(2000);
      if (frame.type === FromRelay.PeerLeft) {
        left = JSON.parse(frame.payload.toString());
      } // the host's own pongs come in between
    }
    assert.deepEqual(left, { id: 2 });
    assert.ok(await guests[0]!.waitClosed(1000));
  } finally {
    clearInterval(keepalive);
  }
});

test('a player that cannot keep up is dropped and the others are not held back', async () => {
  // the host sends far more than the default allowance, that is not what is being tested here
  const { port, relay } = await start({ maxQueuedBytes: 256 * 1024, bytesPerSecond: 1e9, burstBytes: 1e9 });
  const { host, guests } = await room(port, ['Slow', 'Fast']);
  guests[0]!.socket.pause(); // reads nothing
  const chunk = Buffer.alloc(64 * 1024, 7);
  for (let i = 0; i < 600 && !host.closed; i++) {
    host.sendData(DataKind.ToAll, 0, chunk);
    if (i % 10 === 0) {
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
  }
  // the relay dropped the slow one; its own socket only notices once it reads again
  assert.equal(relay.stats().connections, 2);
  guests[0]!.socket.resume();
  assert.ok(await guests[0]!.waitClosed(3000));
  await new Promise((resolve) => setTimeout(resolve, 200));

  const types = (client: TestClient) => new Set(client.frames.map((frame) => frame.type));
  assert.ok(types(guests[1]!).has(FromRelay.Data), 'the fast player kept receiving');
  assert.ok(types(guests[1]!).has(FromRelay.PeerLeft), 'and heard about the slow one leaving');
  assert.ok(types(host).has(FromRelay.PeerLeft));
  assert.equal(guests[1]!.closed, false);
  assert.equal(host.closed, false);
});

test('stats count connections, rooms and traffic', async () => {
  const { port, relay } = await start();
  const { host } = await room(port, ['Ann']);
  host.sendData(DataKind.ToAll, 0, Buffer.alloc(100));
  await new Promise((resolve) => setTimeout(resolve, 100));
  const stats = relay.stats();
  assert.equal(stats.connections, 2);
  assert.equal(stats.rooms, 1);
  assert.ok(stats.bytesIn > 100);
  assert.ok(stats.bytesOut > 100);
});

test('closing the relay closes every connection', async () => {
  const { port, relay } = await start();
  const client = await connect(port, 'Ann');
  await relay.close();
  assert.ok(await client.waitClosed());
  assert.equal(relay.stats().connections, 0);
});
