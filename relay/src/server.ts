import crypto from 'node:crypto';
import net from 'node:net';
import tls from 'node:tls';
import {
  DataKind,
  FrameDecoder,
  FromRelay,
  HOST_PEER_ID,
  MAX_FRAME_BYTES,
  PROTOCOL_VERSION,
  ProtocolError,
  ROOM_CODE_ALPHABET,
  ROOM_CODE_LENGTH,
  ToRelay,
  decodeDataToRelay,
  decodeJson,
  encodeDataFromRelay,
  encodeFrame,
  encodeJson,
  isRoomCode,
} from './protocol.ts';
import type { Frame } from './protocol.ts';

export interface HelloInfo {
  name: string;
  game: string;
  client: string;
  token: string | undefined;
  ip: string;
}

/** Decides whether a connection may come in and under which name. Returning a string refuses it with that reason. */
export type Authenticator = (hello: HelloInfo) => Promise<{ name: string } | string>;

export interface RelayOptions {
  host?: string;
  port?: number;
  /** PEM key and certificate: the relay then speaks TLS. */
  tls?: { key: string | Buffer; cert: string | Buffer };
  /** The games that may use this relay. */
  games?: string[];
  authenticate?: Authenticator;
  maxConnections?: number;
  maxConnectionsPerIp?: number;
  maxRooms?: number;
  maxRoomsPerIp?: number;
  defaultMaxPlayers?: number;
  hardMaxPlayers?: number;
  handshakeTimeoutMs?: number;
  /** A connection that sent nothing for this long is dropped (clients ping more often than this). */
  idleTimeoutMs?: number;
  checkIntervalMs?: number;
  /** Sustained and burst limit of what one connection may send. */
  bytesPerSecond?: number;
  burstBytes?: number;
  /** A peer that cannot take what is queued for it is dropped. */
  maxQueuedBytes?: number;
  /** Wrong codes or passwords before a connection is dropped. */
  maxFailedJoins?: number;
  log?: (event: string, fields: Record<string, unknown>) => void;
}

type Resolved = Required<Omit<RelayOptions, 'tls' | 'authenticate' | 'log'>> &
  Pick<RelayOptions, 'tls' | 'authenticate' | 'log'>;

const DEFAULTS = {
  host: '127.0.0.1',
  port: 0,
  games: ['gearblocks'],
  maxConnections: 2000,
  maxConnectionsPerIp: 20,
  maxRooms: 1000,
  maxRoomsPerIp: 5,
  defaultMaxPlayers: 8,
  hardMaxPlayers: 32,
  handshakeTimeoutMs: 10_000,
  idleTimeoutMs: 45_000,
  checkIntervalMs: 5_000,
  bytesPerSecond: 2 * 1024 * 1024,
  burstBytes: 8 * 1024 * 1024,
  maxQueuedBytes: 8 * 1024 * 1024,
  maxFailedJoins: 10,
};

const MAX_NAME_LENGTH = 32;
const MAX_PASSWORD_LENGTH = 64;
const MAX_FIELD_LENGTH = 64;

interface Room {
  code: string;
  host: Connection;
  password: string | undefined;
  maxPlayers: number;
  members: Map<number, Connection>;
  nextPeerId: number;
  ip: string;
}

class Connection {
  readonly id: number;
  readonly socket: net.Socket;
  readonly ip: string;
  readonly decoder = new FrameDecoder();
  ready = false;
  helloPending = false;
  closed = false;
  name = '';
  game = '';
  room: Room | undefined;
  peerId = 0;
  lastSeen: number;
  tokens: number;
  tokensAt: number;
  failedJoins = 0;
  handshakeTimer: NodeJS.Timeout | undefined;

  constructor(id: number, socket: net.Socket, ip: string, now: number, burst: number) {
    this.id = id;
    this.socket = socket;
    this.ip = ip;
    this.lastSeen = now;
    this.tokens = burst;
    this.tokensAt = now;
  }
}

export interface RelayStats {
  connections: number;
  rooms: number;
  bytesIn: number;
  bytesOut: number;
}

export class RelayServer {
  readonly options: Resolved;
  #server: net.Server | undefined;
  #connections = new Set<Connection>();
  #perIp = new Map<string, number>();
  #roomsPerIp = new Map<string, number>();
  #rooms = new Map<string, Room>();
  #nextConnectionId = 1;
  #timer: NodeJS.Timeout | undefined;
  #bytesIn = 0;
  #bytesOut = 0;

  constructor(options: RelayOptions = {}) {
    this.options = { ...DEFAULTS, ...options } as Resolved;
  }

  /** Starts listening and returns the port. */
  async start(): Promise<number> {
    const onConnection = (socket: net.Socket) => this.#accept(socket);
    const server = this.options.tls
      ? tls.createServer({ key: this.options.tls.key, cert: this.options.tls.cert }, onConnection)
      : net.createServer(onConnection);
    this.#server = server;
    await new Promise<void>((resolve, reject) => {
      server.once('error', reject);
      server.listen(this.options.port, this.options.host, () => {
        server.off('error', reject);
        resolve();
      });
    });
    this.#timer = setInterval(() => this.#check(), this.options.checkIntervalMs);
    this.#timer.unref();
    return (server.address() as net.AddressInfo).port;
  }

  async close(): Promise<void> {
    if (this.#timer) {
      clearInterval(this.#timer);
    }
    for (const connection of [...this.#connections]) {
      this.#drop(connection);
    }
    const server = this.#server;
    if (server) {
      await new Promise<void>((resolve) => server.close(() => resolve()));
    }
  }

  stats(): RelayStats {
    return { connections: this.#connections.size, rooms: this.#rooms.size, bytesIn: this.#bytesIn, bytesOut: this.#bytesOut };
  }

  #log(event: string, fields: Record<string, unknown> = {}): void {
    this.options.log?.(event, fields);
  }

  #accept(socket: net.Socket): void {
    const ip = socket.remoteAddress ?? 'unknown';
    const perIp = this.#perIp.get(ip) ?? 0;
    if (this.#connections.size >= this.options.maxConnections || perIp >= this.options.maxConnectionsPerIp) {
      this.#log('refused', { ip, reason: 'connection limit' });
      socket.destroy();
      return;
    }
    this.#perIp.set(ip, perIp + 1);
    socket.setNoDelay(true);
    const connection = new Connection(this.#nextConnectionId++, socket, ip, Date.now(), this.options.burstBytes);
    this.#connections.add(connection);
    connection.handshakeTimer = setTimeout(() => {
      if (!connection.ready) {
        this.#fail(connection, 'handshake_timeout', 'no hello in time');
      }
    }, this.options.handshakeTimeoutMs);
    connection.handshakeTimer.unref();

    socket.on('data', (chunk) => this.#onData(connection, chunk));
    socket.on('error', () => this.#drop(connection));
    socket.on('close', () => this.#drop(connection));
  }

  #onData(connection: Connection, chunk: Buffer): void {
    if (connection.closed) {
      return;
    }
    const now = Date.now();
    connection.lastSeen = now;
    this.#bytesIn += chunk.length;
    // token bucket on what the connection sends
    connection.tokens = Math.min(
      this.options.burstBytes,
      connection.tokens + ((now - connection.tokensAt) / 1000) * this.options.bytesPerSecond,
    );
    connection.tokensAt = now;
    connection.tokens -= chunk.length;
    if (connection.tokens < 0) {
      this.#fail(connection, 'rate_limited', 'sending too much');
      return;
    }
    let frames: Frame[];
    try {
      frames = connection.decoder.push(chunk);
    } catch (error) {
      this.#fail(connection, 'bad_frame', (error as Error).message);
      return;
    }
    for (const frame of frames) {
      if (connection.closed) {
        return;
      }
      try {
        this.#handle(connection, frame);
      } catch (error) {
        if (error instanceof ProtocolError) {
          this.#error(connection, error.code, error.message);
        } else {
          this.#log('internal_error', { message: String(error) });
          this.#fail(connection, 'internal_error', 'internal error');
        }
      }
    }
  }

  #handle(connection: Connection, frame: Frame): void {
    if (!connection.ready) {
      if (frame.type !== ToRelay.Hello) {
        this.#fail(connection, 'hello_first', 'the first message must be hello');
        return;
      }
      this.#hello(connection, frame);
      return;
    }
    switch (frame.type) {
      case ToRelay.Hello:
        throw new ProtocolError('bad_message', 'already said hello');
      case ToRelay.CreateRoom:
        this.#createRoom(connection, decodeJson(frame.payload));
        break;
      case ToRelay.JoinRoom:
        this.#joinRoom(connection, decodeJson(frame.payload));
        break;
      case ToRelay.LeaveRoom:
        this.#leaveRoom(connection, 'left');
        break;
      case ToRelay.Data:
        this.#data(connection, frame.payload);
        break;
      case ToRelay.Ping:
        this.#send(connection, encodeFrame(FromRelay.Pong));
        break;
      default:
        throw new ProtocolError('bad_message', 'unknown message type');
    }
  }

  #hello(connection: Connection, frame: Frame): void {
    if (connection.helloPending) {
      this.#fail(connection, 'bad_message', 'hello while one is being checked');
      return;
    }
    let hello: Record<string, unknown>;
    try {
      hello = decodeJson(frame.payload);
    } catch (error) {
      this.#fail(connection, 'bad_message', (error as Error).message);
      return;
    }
    if (hello.v !== PROTOCOL_VERSION) {
      this.#fail(connection, 'unsupported_version', `this relay speaks version ${PROTOCOL_VERSION}`);
      return;
    }
    const name = validName(hello.name);
    if (name === undefined) {
      this.#fail(connection, 'bad_name', 'the name must be 1 to 32 characters without control characters');
      return;
    }
    const game = typeof hello.game === 'string' ? hello.game : '';
    if (!this.options.games.includes(game)) {
      this.#fail(connection, 'unknown_game', 'this relay does not serve that game');
      return;
    }
    const info: HelloInfo = {
      name,
      game,
      client: typeof hello.client === 'string' ? hello.client.slice(0, MAX_FIELD_LENGTH) : '',
      token: typeof hello.token === 'string' ? hello.token : undefined,
      ip: connection.ip,
    };
    const authenticate = this.options.authenticate;
    if (!authenticate) {
      this.#welcome(connection, info);
      return;
    }
    connection.helloPending = true;
    authenticate(info).then(
      (result) => {
        connection.helloPending = false;
        if (connection.closed) {
          return;
        }
        if (typeof result === 'string') {
          this.#fail(connection, 'refused', result);
        } else {
          this.#welcome(connection, { ...info, name: result.name });
        }
      },
      () => {
        connection.helloPending = false;
        this.#fail(connection, 'refused', 'could not check the sign-in');
      },
    );
  }

  #welcome(connection: Connection, info: HelloInfo): void {
    connection.ready = true;
    connection.name = info.name;
    connection.game = info.game;
    clearTimeout(connection.handshakeTimer);
    this.#send(
      connection,
      encodeJson(FromRelay.Welcome, {
        v: PROTOCOL_VERSION,
        server: 'beammp-relay',
        limits: { max_frame: MAX_FRAME_BYTES, max_players: this.options.hardMaxPlayers },
      }),
    );
    this.#log('hello', { connection: connection.id, ip: connection.ip, game: info.game });
  }

  #createRoom(connection: Connection, request: Record<string, unknown>): void {
    if (connection.room) {
      throw new ProtocolError('already_in_room', 'leave the room first');
    }
    if (this.#rooms.size >= this.options.maxRooms || (this.#roomsPerIp.get(connection.ip) ?? 0) >= this.options.maxRoomsPerIp) {
      throw new ProtocolError('too_many_rooms', 'no more rooms can be created right now');
    }
    let maxPlayers = this.options.defaultMaxPlayers;
    if (request.max_players !== undefined) {
      if (!Number.isInteger(request.max_players) || (request.max_players as number) < 2 || (request.max_players as number) > this.options.hardMaxPlayers) {
        throw new ProtocolError('bad_message', `max_players must be 2 to ${this.options.hardMaxPlayers}`);
      }
      maxPlayers = request.max_players as number;
    }
    let password: string | undefined;
    if (request.password !== undefined && request.password !== '') {
      if (typeof request.password !== 'string' || request.password.length > MAX_PASSWORD_LENGTH) {
        throw new ProtocolError('bad_message', 'the password must be text of at most 64 characters');
      }
      password = request.password;
    }
    const code = this.#newCode();
    const room: Room = { code, host: connection, password, maxPlayers, members: new Map(), nextPeerId: HOST_PEER_ID + 1, ip: connection.ip };
    room.members.set(HOST_PEER_ID, connection);
    connection.room = room;
    connection.peerId = HOST_PEER_ID;
    this.#rooms.set(code, room);
    this.#roomsPerIp.set(connection.ip, (this.#roomsPerIp.get(connection.ip) ?? 0) + 1);
    this.#send(connection, encodeJson(FromRelay.RoomCreated, { code, peer_id: HOST_PEER_ID }));
    this.#log('room_created', { code, connection: connection.id });
  }

  #newCode(): string {
    for (;;) {
      let code = '';
      for (let i = 0; i < ROOM_CODE_LENGTH; i++) {
        code += ROOM_CODE_ALPHABET[crypto.randomInt(ROOM_CODE_ALPHABET.length)];
      }
      if (!this.#rooms.has(code)) {
        return code;
      }
    }
  }

  #joinRoom(connection: Connection, request: Record<string, unknown>): void {
    if (connection.room) {
      throw new ProtocolError('already_in_room', 'leave the room first');
    }
    if (!isRoomCode(request.code)) {
      throw new ProtocolError('bad_code', 'a room code is 6 letters and digits');
    }
    const room = this.#rooms.get(request.code);
    const given = typeof request.password === 'string' ? request.password : '';
    // the same answer for "no such room" and "wrong password" would hide whether a code exists, but a code is
    // random and short lived, so the useful protection is to cut off whoever keeps guessing
    if (!room || room.host.game !== connection.game) {
      this.#failedJoin(connection);
      throw new ProtocolError('room_not_found', 'there is no such room');
    }
    if (room.password !== undefined && !sameSecret(room.password, given)) {
      this.#failedJoin(connection);
      throw new ProtocolError('bad_password', 'wrong password');
    }
    if (room.members.size >= room.maxPlayers) {
      throw new ProtocolError('room_full', 'the room is full');
    }
    const peerId = room.nextPeerId++;
    const others = [...room.members.entries()].map(([id, member]) => ({ id, name: member.name }));
    room.members.set(peerId, connection);
    connection.room = room;
    connection.peerId = peerId;
    this.#send(connection, encodeJson(FromRelay.RoomJoined, { code: room.code, peer_id: peerId, host_id: HOST_PEER_ID, peers: others }));
    const announcement = encodeJson(FromRelay.PeerJoined, { id: peerId, name: connection.name });
    for (const [id, member] of room.members) {
      if (id !== peerId) {
        this.#send(member, announcement);
      }
    }
    this.#log('room_joined', { code: room.code, connection: connection.id, peer: peerId });
  }

  #failedJoin(connection: Connection): void {
    connection.failedJoins++;
    if (connection.failedJoins >= this.options.maxFailedJoins) {
      this.#fail(connection, 'too_many_attempts', 'too many wrong room codes or passwords');
    }
  }

  #leaveRoom(connection: Connection, reason: string): void {
    const room = connection.room;
    if (!room) {
      throw new ProtocolError('not_in_room', 'not in a room');
    }
    this.#removeFromRoom(connection, reason);
  }

  #removeFromRoom(connection: Connection, reason: string): void {
    const room = connection.room;
    if (!room) {
      return;
    }
    connection.room = undefined;
    const peerId = connection.peerId;
    connection.peerId = 0;
    room.members.delete(peerId);
    if (room.host === connection) {
      // no host, no room: the others go back to the lobby
      this.#rooms.delete(room.code);
      const rooms = (this.#roomsPerIp.get(room.ip) ?? 1) - 1;
      if (rooms <= 0) {
        this.#roomsPerIp.delete(room.ip);
      } else {
        this.#roomsPerIp.set(room.ip, rooms);
      }
      const closed = encodeJson(FromRelay.RoomClosed, { reason: reason === 'left' ? 'host_left' : 'host_gone' });
      for (const member of room.members.values()) {
        member.room = undefined;
        member.peerId = 0;
        this.#send(member, closed);
      }
      room.members.clear();
      this.#log('room_closed', { code: room.code, reason });
      return;
    }
    const left = encodeJson(FromRelay.PeerLeft, { id: peerId });
    for (const member of room.members.values()) {
      this.#send(member, left);
    }
  }

  #data(connection: Connection, payload: Buffer): void {
    const room = connection.room;
    if (!room) {
      throw new ProtocolError('not_in_room', 'join a room first');
    }
    const { kind, target, data } = decodeDataToRelay(payload);
    const frame = encodeDataFromRelay(connection.peerId, kind, data);
    const isHost = room.host === connection;
    switch (kind) {
      case DataKind.ToHost:
        if (isHost) {
          throw new ProtocolError('bad_target', 'the host is the host');
        }
        this.#send(room.host, frame);
        break;
      case DataKind.ToAll:
        if (!isHost) {
          throw new ProtocolError('not_host', 'only the host can send to everyone');
        }
        for (const member of [...room.members.values()]) {
          if (member !== connection) {
            this.#send(member, frame);
          }
        }
        break;
      case DataKind.ToPeer: {
        if (!isHost) {
          throw new ProtocolError('not_host', 'only the host can send to one player');
        }
        const member = room.members.get(target);
        if (!member || member === connection) {
          throw new ProtocolError('peer_not_found', 'there is no such player in the room');
        }
        this.#send(member, frame);
        break;
      }
      case DataKind.ToOthers:
        for (const member of [...room.members.values()]) {
          if (member !== connection) {
            this.#send(member, frame);
          }
        }
        break;
      default:
        throw new ProtocolError('bad_message', 'unknown kind of data');
    }
  }

  #send(connection: Connection, frame: Buffer): void {
    if (connection.closed) {
      return;
    }
    if (connection.socket.writableLength + frame.length > this.options.maxQueuedBytes) {
      // a peer that cannot keep up must not make the relay hold everything for it
      this.#log('slow_consumer', { connection: connection.id });
      this.#drop(connection);
      return;
    }
    this.#bytesOut += frame.length;
    connection.socket.write(frame);
  }

  /** An error the connection survives. */
  #error(connection: Connection, code: string, message: string): void {
    this.#send(connection, encodeJson(FromRelay.Error, { code, message }));
  }

  /** An error that ends the connection. */
  #fail(connection: Connection, code: string, message: string): void {
    if (connection.closed) {
      return;
    }
    this.#log('failed', { connection: connection.id, code });
    connection.socket.write(encodeJson(FromRelay.Error, { code, message }));
    this.#drop(connection, true);
  }

  #drop(connection: Connection, graceful = false): void {
    if (connection.closed) {
      return;
    }
    connection.closed = true;
    clearTimeout(connection.handshakeTimer);
    this.#removeFromRoom(connection, 'gone');
    this.#connections.delete(connection);
    const count = (this.#perIp.get(connection.ip) ?? 1) - 1;
    if (count <= 0) {
      this.#perIp.delete(connection.ip);
    } else {
      this.#perIp.set(connection.ip, count);
    }
    if (graceful) {
      connection.socket.end();
      setTimeout(() => connection.socket.destroy(), 1000).unref();
    } else {
      connection.socket.destroy();
    }
  }

  #check(): void {
    const now = Date.now();
    for (const connection of [...this.#connections]) {
      if (now - connection.lastSeen > this.options.idleTimeoutMs) {
        this.#log('idle', { connection: connection.id });
        this.#drop(connection);
      }
    }
  }
}

function validName(value: unknown): string | undefined {
  if (typeof value !== 'string') {
    return undefined;
  }
  const name = value.trim();
  if (name.length < 1 || name.length > MAX_NAME_LENGTH || /[\p{Cc}\p{Zl}\p{Zp}]/u.test(name)) {
    return undefined;
  }
  return name;
}

function sameSecret(expected: string, given: string): boolean {
  const a = crypto.createHash('sha256').update(expected).digest();
  const b = crypto.createHash('sha256').update(given).digest();
  return crypto.timingSafeEqual(a, b);
}
