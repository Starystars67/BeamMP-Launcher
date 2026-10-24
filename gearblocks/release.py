#!/usr/bin/env python3
"""Publishing BeamMP's files for GearBlocks: the plugin, the script mod, and the signed manifest the launcher follows.

  gearblocks/release.py keygen <private key file> <trusted keys file>
      Makes an Ed25519 key. The private key (PEM) is what signs releases: keep it off shared machines and out of git.
      The trusted keys file gets the public half as one `ed25519:<base64>` line; that is the file players' launchers are
      given with --gearblocks-trust (several lines are fine, they allow a key to be replaced without breaking anyone).

  gearblocks/release.py publish <output folder> --base-url <https://...> --key <private key file> [--game <folder>]
                                [--build <game build>]... [--sequence <n>] [--plugin <built dll>]
      Builds the plugin (dotnet; --game is a game folder with BepInEx, for its references; --plugin uses a dll that is
      already built), zips the script mod, and writes into the output folder:
          plugin-<version>.dll, mod-<version>.zip, manifest.json, manifest.json.sig
      With --base-url bundle: the files are named for the launcher's bundle (a `gearblocks` folder next to the engine, with
      the output folder's files and a trust.txt beside them) instead of for a server.
      Upload the folder's files to <base-url>; the manifest's own address is <base-url>manifest.json and that is what the
      launcher gets with --gearblocks-manifest. Releases already in the folder's manifest stay (the launcher picks the highest
      one for the player's game build), and the sequence number goes up by one, which is what keeps an old signed manifest
      from being served again. Everything that is listed has its SHA-256 in the manifest, and the manifest is what is signed.

  gearblocks/release.py verify <manifest.json> <trusted keys file>
      Checks the signature (manifest.json.sig next to it) the way the launcher does.

Needs python3 with `cryptography` (pip install cryptography); `publish` without --plugin needs the .NET SDK.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
BRIDGE_PROTOCOL = 1  # GearBlocksBridgeProtocol in include/Games/GearBlocksManifest.h


def load_crypto():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ed25519
    except ImportError:
        sys.exit("This needs the python package `cryptography` (pip install cryptography)")
    return serialization, ed25519


def public_line(public_key, serialization):
    raw = public_key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    return "ed25519:" + base64.b64encode(raw).decode()


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def keygen(args):
    serialization, ed25519 = load_crypto()
    for path in (args.private, args.trusted):
        if os.path.exists(path):
            sys.exit(f"{path} exists already, not overwriting it")
    key = ed25519.Ed25519PrivateKey.generate()
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    fd = os.open(args.private, os.O_WRONLY | os.O_CREAT | os.O_EXCL, stat.S_IRUSR | stat.S_IWUSR)
    with os.fdopen(fd, "wb") as f:
        f.write(pem)
    with open(args.trusted, "w") as f:
        f.write("# BeamMP GearBlocks release keys\n" + public_line(key.public_key(), serialization) + "\n")
    print(f"Private key: {args.private} (keep it secret)\nTrusted keys for the launcher: {args.trusted}")


def plugin_version():
    text = open(os.path.join(HERE, "plugin", "Plugin.cs")).read()
    found = re.search(r'BepInPlugin\("[^"]*", "[^"]*", "([^"]*)"\)', text)
    if not found:
        sys.exit("The plugin's version was not found in plugin/Plugin.cs")
    return found.group(1)


def build_plugin(game):
    env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1")
    env["PATH"] = os.path.expanduser("~/.dotnet") + os.pathsep + env["PATH"]
    subprocess.run(["dotnet", "build", "-c", "Release", f"-p:GameDir={game}"], cwd=os.path.join(HERE, "plugin"), env=env, check=True,
                   stdout=subprocess.DEVNULL)
    return os.path.join(HERE, "plugin", "bin", "Release", "net6.0", "BeamMP.GearBlocks.dll")


def zip_mod(destination):
    source = os.path.join(HERE, "mod", "BeamMP")
    if not os.path.isfile(os.path.join(source, "main.lua")):
        sys.exit(f"{source}/main.lua is missing")
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for folder, _, files in sorted(os.walk(source)):
            for name in sorted(files):
                path = os.path.join(folder, name)
                info = zipfile.ZipInfo(os.path.relpath(path, source).replace(os.sep, "/"), (2020, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, open(path, "rb").read())


def publish(args):
    serialization, ed25519 = load_crypto()
    # "bundle:" is the address form for files that ship next to the launcher (see --bundle in the help)
    base = args.base_url if args.base_url.endswith(("/", ":")) else args.base_url + "/"
    key = serialization.load_pem_private_key(open(args.key, "rb").read(), password=None)
    if not isinstance(key, ed25519.Ed25519PrivateKey):
        sys.exit("The key is not an Ed25519 key")
    os.makedirs(args.out, exist_ok=True)
    version = plugin_version()
    builds = args.build or [os.environ.get("GEARBLOCKS_BUILD", "0.8.9740")]

    plugin = args.plugin or build_plugin(args.game)
    plugin_file = os.path.join(args.out, f"plugin-{version}.dll")
    mod_file = os.path.join(args.out, f"mod-{version}.zip")
    with open(plugin, "rb") as src, open(plugin_file, "wb") as dst:
        dst.write(src.read())
    zip_mod(mod_file)

    manifest_path = os.path.join(args.out, "manifest.json")
    releases, sequence = [], 0
    if os.path.exists(manifest_path):
        previous = json.load(open(manifest_path))
        releases = [r for r in previous.get("releases", []) if r["version"] != version]
        sequence = previous.get("sequence", 0)
    if args.sequence is not None:
        if args.sequence <= sequence:
            sys.exit(f"The sequence has to be higher than the last one ({sequence}): an older number would be refused by launchers")
        sequence = args.sequence
    else:
        sequence += 1
    releases.append({
        "version": version,
        "bridge_protocol": BRIDGE_PROTOCOL,
        "game_builds": builds,
        "plugin": {"url": base + os.path.basename(plugin_file), "sha256": sha256(plugin_file)},
        "mod": {"url": base + os.path.basename(mod_file), "sha256": sha256(mod_file)},
    })
    manifest = {
        "schema": 1,
        "game": "gearblocks",
        "sequence": sequence,
        "loader": json.load(open(os.path.join(HERE, "loader.json"))),
        "releases": releases,
    }
    text = json.dumps(manifest, indent=2) + "\n"
    with open(manifest_path, "w") as f:
        f.write(text)
    with open(manifest_path + ".sig", "w") as f:
        f.write(base64.b64encode(key.sign(text.encode())).decode() + "\n")
    print(f"Published {version} for GearBlocks {', '.join(builds)} (sequence {sequence}) in {args.out}\n"
          f"Manifest address: {base}manifest.json   Key: {public_line(key.public_key(), serialization)}")


def verify(args):
    serialization, ed25519 = load_crypto()
    from cryptography.exceptions import InvalidSignature
    text = open(args.manifest, "rb").read()
    signatures = [base64.b64decode(line) for line in open(args.manifest + ".sig").read().splitlines() if line.strip() and not line.startswith("#")]
    keys = []
    for line in open(args.trusted):
        line = line.strip()
        if line and not line.startswith("#"):
            keys.append(ed25519.Ed25519PublicKey.from_public_bytes(base64.b64decode(line.removeprefix("ed25519:"))))
    for signature in signatures:
        for key in keys:
            try:
                key.verify(signature, text)
                print("Signature is valid")
                return
            except InvalidSignature:
                pass
    sys.exit("Signature is NOT valid")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    k = commands.add_parser("keygen")
    k.add_argument("private")
    k.add_argument("trusted")
    k.set_defaults(run=keygen)
    p = commands.add_parser("publish")
    p.add_argument("out")
    p.add_argument("--base-url", required=True)
    p.add_argument("--key", required=True)
    p.add_argument("--game", default=os.path.expanduser("~/Wine/gearblocks-copy"))
    p.add_argument("--build", action="append")
    p.add_argument("--sequence", type=int)
    p.add_argument("--plugin")
    p.set_defaults(run=publish)
    v = commands.add_parser("verify")
    v.add_argument("manifest")
    v.add_argument("trusted")
    v.set_defaults(run=verify)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
