import fs from 'node:fs';
import { RelayServer } from './server.ts';

// node src/main.ts [--host 0.0.0.0] [--port 7777] [--tls-key key.pem --tls-cert cert.pem] [--games gearblocks,other]
const args = process.argv.slice(2);
function option(name: string): string | undefined {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : undefined;
}

const keyPath = option('--tls-key');
const certPath = option('--tls-cert');
if ((keyPath === undefined) !== (certPath === undefined)) {
  console.error('--tls-key and --tls-cert go together');
  process.exit(2);
}

const relay = new RelayServer({
  host: option('--host') ?? process.env.RELAY_HOST ?? '127.0.0.1',
  port: Number(option('--port') ?? process.env.RELAY_PORT ?? 7777),
  games: (option('--games') ?? process.env.RELAY_GAMES ?? 'gearblocks').split(','),
  tls: keyPath && certPath ? { key: fs.readFileSync(keyPath), cert: fs.readFileSync(certPath) } : undefined,
  log: (event, fields) => console.log(JSON.stringify({ t: new Date().toISOString(), event, ...fields })),
});

const port = await relay.start();
console.log(JSON.stringify({ t: new Date().toISOString(), event: 'listening', port, tls: keyPath !== undefined }));

for (const signal of ['SIGINT', 'SIGTERM'] as const) {
  process.on(signal, () => {
    void relay.close().then(() => process.exit(0));
  });
}
