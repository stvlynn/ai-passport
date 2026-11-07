#!/usr/bin/env python3
"""Install the user-owned Node connector as a macOS LaunchAgent, without sudo."""
import argparse
import os
import pathlib
import plistlib
import shutil
import subprocess
import tempfile
import time


def private_write(destination, content):
    descriptor, temporary = tempfile.mkstemp(dir=destination.parent, prefix='.install-')
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=pathlib.Path, required=True, help='Existing private JSON config; never pass secrets as command arguments')
    parser.add_argument('--node', default=shutil.which('node'), help='Absolute Node 22+ executable')
    parser.add_argument('--no-load', action='store_true', help='Write installation without starting the LaunchAgent')
    args = parser.parse_args()
    if not args.node or not pathlib.Path(args.node).is_absolute():
        parser.error('Node needs an absolute executable path')
    config = args.config.expanduser().resolve()
    if config.stat().st_mode & 0o077:
        parser.error('Config permissions must be 0600')
    # Validate configuration before replacing any installed files. No secrets are printed.
    source = pathlib.Path(__file__).resolve().parent
    check = 'import {validateConfig} from "./src/server.js"; import {readFileSync} from "node:fs"; try {validateConfig(JSON.parse(readFileSync(process.argv[1],"utf8")))} catch {process.stderr.write("Invalid connector configuration\\n"); process.exit(1)}'
    subprocess.run([args.node, '--input-type=module', '-e', check, str(config)], cwd=source, check=True)
    destination = pathlib.Path.home() / 'Library' / 'Application Support' / 'AI Passport Connector'
    destination.mkdir(parents=True, exist_ok=True, mode=0o700)
    destination.chmod(0o700)
    runtime = destination / 'connector'
    runtime.mkdir(exist_ok=True, mode=0o700)
    shutil.copytree(source / 'src', runtime / 'src', dirs_exist_ok=True)
    shutil.copy2(source / 'package.json', runtime / 'package.json')
    catalog = destination / 'docs'
    catalog.mkdir(exist_ok=True, mode=0o700)
    shutil.copy2(source.parent / 'docs' / 'lynn-tools.json', catalog / 'lynn-tools.json')
    asr = destination / 'asr'
    asr.mkdir(exist_ok=True, mode=0o700)
    for name in ['transcribe.py', 'download_model.py', 'requirements.lock', 'model-sha256.txt']:
        shutil.copy2(source / 'asr' / name, asr / name)
    private_write(destination / 'config.json', config.read_bytes())
    logs = destination / 'logs'
    logs.mkdir(exist_ok=True, mode=0o700)
    for name in ['stdout.log', 'stderr.log']:
        path = logs / name
        path.touch(mode=0o600, exist_ok=True)
        path.chmod(0o600)
    agents = pathlib.Path.home() / 'Library' / 'LaunchAgents'
    agents.mkdir(parents=True, exist_ok=True)
    label = 'com.aipassport.lynn-connector'
    plist = agents / f'{label}.plist'
    specification = {
        'Label': label,
        'ProgramArguments': [str(pathlib.Path(args.node).resolve()), str(runtime / 'src' / 'server.js'), str(destination / 'config.json')],
        'WorkingDirectory': str(destination),
        'RunAtLoad': True,
        'KeepAlive': True,
        'ThrottleInterval': 10,
        'ProcessType': 'Background',
        'Umask': 0o077,
        'StandardOutPath': str(logs / 'stdout.log'),
        'StandardErrorPath': str(logs / 'stderr.log'),
    }
    private_write(plist, plistlib.dumps(specification))
    if not args.no_load:
        domain = f'gui/{os.getuid()}'
        subprocess.run(['launchctl', 'bootout', f'{domain}/{label}'], capture_output=True, check=False)
        # launchd removal can finish asynchronously after bootout returns.
        for attempt in range(10):
            loaded = subprocess.run(['launchctl', 'bootstrap', domain, str(plist)], capture_output=True)
            if loaded.returncode == 0:
                break
            time.sleep(0.2)
        else:
            raise RuntimeError('LaunchAgent bootstrap failed; inspect launchctl status before retrying')
    print(f'Installed connector: {destination}')
    print(f'LaunchAgent: {plist}')


if __name__ == '__main__':
    main()
