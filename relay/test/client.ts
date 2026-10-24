import net from 'node:net';
import { FrameDecoder, FromRelay, PROTOCOL_VERSION, ToRelay, decodeDataFromRelay, decodeJson, encodeDataToRelay, encodeFrame, encodeJson } from '../src/protocol.ts';
import type { Frame } from '../src/protocol.ts';

/** A client of the relay for tests: real socket, the real codec. */
export class TestClient {
  readonly socket: net.Socket;
  readonly decoder = new FrameDecoder();
  readonly frames: Frame[] = [];
  closed = false;
  #waiting: Array<() => void> = [];

  private constructor(socket: net.Socket) {
    this.socket = socket;
    socket.on('data', (chunk) => {
      try {
        this.frames.push(...this.decoder.push(chunk));
      } catch {
        socket.destroy();
      }
      this.#wake();
    });
    socket.on('close', () => {
      this.closed = true;
      this.#wake();
    });
    socket.on('error', () => {});
  }

  static async connect(port: number, host = '127.0.0.1'): Promise<TestClient> {
    const socket = net.connect(port, host);
    await new Promise<void>((resolve, reject) => {
      socket.once('connect', resolve);
      socket.once('error', reject);
    });
    return new TestClient(socket);
  }

  #wake(): void {
    const waiting = this.#waiting;
    this.#waiting = [];
    for (const wake of waiting) {
      wake();
    }
  }

  send(frame: Buffer): void {
    this.socket.write(frame);
  }

  sendJson(type: number, value: unknown): void {
    this.send(encodeJson(type, value));
  }

  /** hello and wait for the welcome. */
  async hello(name = 'Ann', extra: Record<string, unknown> = {}): Promise<Record<string, unknown>> {
    this.sendJson(ToRelay.Hello, { v: PROTOCOL_VERSION, name, game: 'gearblocks', client: 'tests', ...extra });
    return this.json(FromRelay.Welcome);
  }

  async next(timeoutMs = 1500): Promise<Frame> {
    const deadline = Date.now() + timeoutMs;
    while (this.frames.length === 0) {
      if (this.closed) {
        throw new Error('the connection closed while waiting for a frame');
      }
      const left = deadline - Date.now();
      if (left <= 0) {
        throw new Error('timed out waiting for a frame');
      }
      await new Promise<void>((resolve) => {
        this.#waiting.push(resolve);
        setTimeout(resolve, left).unref();
      });
    }
    return this.frames.shift()!;
  }

  /** The next frame, which has to be `type`. */
  async expect(type: number, timeoutMs?: number): Promise<Frame> {
    const frame = await this.next(timeoutMs);
    if (frame.type !== type) {
      throw new Error(`expected frame 0x${type.toString(16)}, got 0x${frame.type.toString(16)}: ${frame.payload.toString('utf8').slice(0, 100)}`);
    }
    return frame;
  }

  async json(type: number, timeoutMs?: number): Promise<Record<string, unknown>> {
    return decodeJson((await this.expect(type, timeoutMs)).payload);
  }

  async error(timeoutMs?: number): Promise<string> {
    return String((await this.json(FromRelay.Error, timeoutMs)).code);
  }

  async data(timeoutMs?: number): Promise<{ sender: number; kind: number; data: Buffer }> {
    return decodeDataFromRelay((await this.expect(FromRelay.Data, timeoutMs)).payload);
  }

  sendData(kind: number, target: number, payload: Uint8Array): void {
    this.send(encodeDataToRelay(kind, target, payload));
  }

  async createRoom(request: Record<string, unknown> = {}): Promise<string> {
    this.sendJson(ToRelay.CreateRoom, request);
    return String((await this.json(FromRelay.RoomCreated)).code);
  }

  async joinRoom(code: string, password?: string): Promise<Record<string, unknown>> {
    this.sendJson(ToRelay.JoinRoom, password === undefined ? { code } : { code, password });
    return this.json(FromRelay.RoomJoined);
  }

  ping(): void {
    this.send(encodeFrame(ToRelay.Ping));
  }

  /** True when nothing arrives in `ms`. */
  async quiet(ms = 150): Promise<boolean> {
    await new Promise((resolve) => setTimeout(resolve, ms));
    return this.frames.length === 0;
  }

  /** Waits until the relay has closed the connection. */
  async waitClosed(timeoutMs = 2500): Promise<boolean> {
    const deadline = Date.now() + timeoutMs;
    while (!this.closed && Date.now() < deadline) {
      await new Promise<void>((resolve) => {
        this.#waiting.push(resolve);
        setTimeout(resolve, 50).unref();
      });
    }
    return this.closed;
  }

  close(): void {
    this.socket.destroy();
  }
}
