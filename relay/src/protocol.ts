// The relay wire protocol, version 1.
//
// TCP (TLS when the relay is given a certificate), one frame after another:
//
//   u32 big endian length (type byte + payload)  |  u8 type  |  payload
//
// Control messages carry JSON, game traffic carries bytes the relay never looks into. The relay is for games
// that have a host (the game's own server role) and other players that talk to it: a player that creates a
// room is its host, the others join by the room's code.

export const PROTOCOL_VERSION = 1;

/** Largest frame (type byte + payload) in either direction. Constructions are sent whole, so this is generous. */
export const MAX_FRAME_BYTES = 4 * 1024 * 1024;

/** Client to relay. */
export const ToRelay = {
  /** JSON `{v, name, game, client?, token?}`. Must be the first frame. */
  Hello: 0x01,
  /** JSON `{max_players?, password?}`. The sender becomes the room's host. */
  CreateRoom: 0x02,
  /** JSON `{code, password?}`. */
  JoinRoom: 0x03,
  LeaveRoom: 0x04,
  /** `u8 kind | u16 target | bytes`, see `DataKind`. */
  Data: 0x10,
  Ping: 0x20,
} as const;

/** Relay to client. */
export const FromRelay = {
  /** JSON `{v, server, limits: {max_frame, max_players}}`. */
  Welcome: 0x81,
  /** JSON `{code, peer_id}`. */
  RoomCreated: 0x82,
  /** JSON `{code, peer_id, host_id, peers: [{id, name}]}`. */
  RoomJoined: 0x83,
  /** JSON `{id, name}`. */
  PeerJoined: 0x84,
  /** JSON `{id}`. */
  PeerLeft: 0x85,
  /** JSON `{reason}`: the room is gone (the host left), the connection stays. */
  RoomClosed: 0x86,
  /** `u16 sender | u8 kind | bytes`. */
  Data: 0x90,
  Pong: 0xa0,
  /** JSON `{code, message}`. Some errors close the connection. */
  Error: 0xe0,
} as const;

/** Who a data frame is for. */
export const DataKind = {
  /** Player to the host. */
  ToHost: 0,
  /** Host to every other player. */
  ToAll: 1,
  /** Host to one player (`target`). */
  ToPeer: 2,
  /** Anyone to everyone else, the host included (state that all players need). */
  ToOthers: 3,
} as const;

/** The host is always peer 1. Ids are never reused inside a room. */
export const HOST_PEER_ID = 1;

export class ProtocolError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}

export interface Frame {
  type: number;
  payload: Buffer;
}

export function encodeFrame(type: number, payload: Uint8Array = new Uint8Array(0)): Buffer {
  const length = 1 + payload.length;
  if (length > MAX_FRAME_BYTES) {
    throw new ProtocolError('frame_too_large', 'frame too large');
  }
  const frame = Buffer.allocUnsafe(4 + length);
  frame.writeUInt32BE(length, 0);
  frame[4] = type;
  frame.set(payload, 5);
  return frame;
}

export function encodeJson(type: number, value: unknown): Buffer {
  return encodeFrame(type, Buffer.from(JSON.stringify(value), 'utf8'));
}

/** Parses a JSON object payload; anything else is a protocol error. */
export function decodeJson(payload: Buffer): Record<string, unknown> {
  let value: unknown;
  try {
    value = JSON.parse(payload.toString('utf8'));
  } catch {
    throw new ProtocolError('bad_message', 'the payload is not JSON');
  }
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    throw new ProtocolError('bad_message', 'the payload is not a JSON object');
  }
  return value as Record<string, unknown>;
}

/** Cuts a byte stream into frames; throws ProtocolError for a length that cannot be right. */
export class FrameDecoder {
  #buffer: Buffer = Buffer.alloc(0);

  push(chunk: Buffer): Frame[] {
    this.#buffer = this.#buffer.length === 0 ? chunk : Buffer.concat([this.#buffer, chunk]);
    const frames: Frame[] = [];
    for (;;) {
      if (this.#buffer.length < 4) {
        break;
      }
      const length = this.#buffer.readUInt32BE(0);
      if (length < 1 || length > MAX_FRAME_BYTES) {
        throw new ProtocolError('bad_frame', 'invalid frame length');
      }
      if (this.#buffer.length < 4 + length) {
        break;
      }
      frames.push({ type: this.#buffer[4]!, payload: Buffer.from(this.#buffer.subarray(5, 4 + length)) });
      this.#buffer = this.#buffer.subarray(4 + length);
    }
    return frames;
  }

  /** Bytes held back waiting for the rest of a frame. */
  get pending(): number {
    return this.#buffer.length;
  }
}

/** `u8 kind | u16 target | bytes` (client to relay). */
export function encodeDataToRelay(kind: number, target: number, payload: Uint8Array): Buffer {
  const body = Buffer.allocUnsafe(3 + payload.length);
  body[0] = kind;
  body.writeUInt16BE(target, 1);
  body.set(payload, 3);
  return encodeFrame(ToRelay.Data, body);
}

export function decodeDataToRelay(payload: Buffer): { kind: number; target: number; data: Buffer } {
  if (payload.length < 3) {
    throw new ProtocolError('bad_message', 'a data frame needs a kind and a target');
  }
  return { kind: payload[0]!, target: payload.readUInt16BE(1), data: payload.subarray(3) };
}

/** `u16 sender | u8 kind | bytes` (relay to client). */
export function encodeDataFromRelay(sender: number, kind: number, payload: Uint8Array): Buffer {
  const body = Buffer.allocUnsafe(3 + payload.length);
  body.writeUInt16BE(sender, 0);
  body[2] = kind;
  body.set(payload, 3);
  return encodeFrame(FromRelay.Data, body);
}

export function decodeDataFromRelay(payload: Buffer): { sender: number; kind: number; data: Buffer } {
  if (payload.length < 3) {
    throw new ProtocolError('bad_message', 'a data frame needs a sender and a kind');
  }
  return { sender: payload.readUInt16BE(0), kind: payload[2]!, data: payload.subarray(3) };
}

/** Room codes avoid characters that look alike (no 0/O, 1/I/L). */
export const ROOM_CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
export const ROOM_CODE_LENGTH = 6;

export function isRoomCode(value: unknown): value is string {
  return (
    typeof value === 'string' &&
    value.length === ROOM_CODE_LENGTH &&
    [...value].every((character) => ROOM_CODE_ALPHABET.includes(character))
  );
}
