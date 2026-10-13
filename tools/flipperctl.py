#!/usr/bin/env python3
"""Bounded, exclusive Flipper USB operations for macOS/Linux coding agents."""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
import zlib

# Direct `python3 tools/flipperctl.py` calls must use the installed environment
# too: the current SDK uses Python 3.10 union annotations in its storage API.
if __name__ == '__main__':
    installed_venv = Path(os.environ.get('FLIPPERCTL_HOME', Path.home() / '.local/share/flipperctl')) / 'venv'
    installed_python = installed_venv / 'bin/python'
    if installed_python.is_file() and Path(sys.prefix).resolve() != installed_venv.resolve():
        os.execv(str(installed_python), [str(installed_python), '-I',
                                       str(Path(__file__).resolve()), *sys.argv[1:]])
    if sys.version_info < (3, 10):
        raise SystemExit('flipperctl: Python 3.10+ is required by the SDK; run sh tools/install_flipperctl.sh')

import serial
from serial.tools import list_ports


class AccessError(Exception):
    pass


def select_port(requested):
    ports = list(list_ports.comports())
    if requested:
        return requested
    matches = [p.device for p in ports if (p.vid, p.pid) == (0x0483, 0x5740)]
    if len(matches) != 1:
        raise AccessError('Select one Flipper with --port or FLIPPER_PORT; found %d' % len(matches))
    return matches[0]


def lock_path(port):
    # macOS cu/tty nodes address the same device. Re-enumeration keeps its name.
    identity = os.path.realpath(port).replace('/dev/tty.', '/dev/cu.')
    for p in list_ports.comports():
        if os.path.realpath(p.device).replace('/dev/tty.', '/dev/cu.') == identity and p.serial_number:
            identity = '%s:%s:%s' % (p.vid, p.pid, p.serial_number)
            break
    root = Path(os.environ.get('FLIPPERCTL_LOCK_DIR', Path.home() / '.cache/flipperctl'))
    root.mkdir(parents=True, exist_ok=True)
    return root / (hashlib.sha256(identity.encode()).hexdigest()[:24] + '.lock')


@contextmanager
def device_lease(port):
    with lock_path(port).open('a+') as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            handle.seek(0)
            raise AccessError('Flipper is busy: ' + handle.read().strip())
        handle.seek(0)
        handle.truncate()
        json.dump({'pid': os.getpid(), 'pane': os.environ.get('HERDR_PANE_ID'), 'port': port}, handle)
        handle.flush()
        # Leave the inode in place: unlinking a lock permits two separate owners.
        yield handle


def check_external_owner(port):
    executable = shutil.which('lsof')
    if not executable:
        raise AccessError('Install lsof so external serial owners can be checked before opening USB')
    aliases = [port]
    if port.startswith('/dev/cu.'):
        aliases.append(port.replace('/dev/cu.', '/dev/tty.', 1))
    elif port.startswith('/dev/tty.'):
        aliases.append(port.replace('/dev/tty.', '/dev/cu.', 1))
    result = subprocess.run([executable, '-t', '--', *aliases], capture_output=True,
                            text=True, timeout=5)
    if result.stdout.strip():
        raise AccessError('Serial port already held by PID(s): ' + result.stdout.strip().replace('\n', ', '))
    if result.returncode not in (0, 1):
        raise AccessError('Cannot inspect serial ownership: ' + result.stderr.strip())


class Cli:
    def __init__(self, port, timeout):
        self.port = port
        self.timeout = timeout
        self.stream = None

    def __enter__(self):
        self.stream = serial.Serial(self.port, 230400, timeout=0.05,
                                    write_timeout=1, exclusive=True)
        try:
            # End a stale text monitor, then get a prompt. No reboot or app exit.
            self.stream.write(b'\x03\r')
            self.read_prompt()
            self.stream.reset_input_buffer()
            help_text = self.command('help')
            if not re.search(r'\bloader\b', help_text) or not re.search(r'\binput\b', help_text):
                raise AccessError('No Flipper CLI command response; greeting alone is insufficient')
            return self
        except BaseException:
            self.stream.close()
            raise

    def __exit__(self, *unused):
        self.stream.close()

    def read_prompt(self, command=None):
        end = time.monotonic() + self.timeout
        data = bytearray()
        while time.monotonic() < end:
            data.extend(self.stream.read(self.stream.in_waiting or 1))
            if data.endswith(b'>: ') and (command is None or (command.encode() + b'\r\n') in data):
                return data.decode(errors='replace')
        raise AccessError('CLI timeout (%d bytes received). Command was not replayed. '
                          'Check USB ownership/bridge mode; a silent device may require '
                          'physical LEFT + BACK for 5 seconds.' % len(data))

    def command(self, text):
        if not text or any(c in text for c in '\r\n\x00'):
            raise AccessError('Supply a single CLI command without line breaks')
        self.stream.write(text.encode() + b'\r')
        reply = self.read_prompt(text)
        if any(marker in reply.lower() for marker in
               ('cannot be run', 'command not found', 'could not find command',
                'failed to load external command')):
            raise AccessError(reply.strip())
        return reply

    def key(self, key, kind):
        replies = []
        try:
            for event in ('press', kind):
                replies.append(self.command('input send %s %s' % (key, event)))
        finally:
            # Also release if the semantic event timed out after PRESS was accepted.
            replies.append(self.command('input send %s release' % key))
        return ''.join(replies)

    def close_app(self):
        reply = self.command('loader close')
        if 'has to be closed manually' in reply:
            self.key('back', 'long')
        end = time.monotonic() + self.timeout
        while time.monotonic() < end:
            if 'No application is running' in self.command('loader info'):
                return reply
            time.sleep(0.05)
        raise AccessError('App did not exit; upload has not started')


def run_child(command, port, lease, timeout):
    env = os.environ.copy()
    env['FLIPPER_PORT'] = port
    env['FLIPPERCTL_LEASE_FD'] = str(lease.fileno())
    process = subprocess.Popen(command, env=env, start_new_session=True,
                               pass_fds=(lease.fileno(),))
    try:
        code = process.wait(timeout=timeout)
        if code:
            raise AccessError('USB helper exited with status %d; operation was not retried' % code)
    except BaseException:
        # Stop the whole helper tree, including ufbt/runfap descendants, before
        # releasing the parent's lease. No unrelated process is stopped.
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass
        # The supervisor may exit before a descendant that ignores TERM.
        # That descendant can still hold the inherited USB lease FD.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        raise


def write_frame(data, path):
    if len(data) != 1024:
        raise AccessError('Incomplete screen frame: %d bytes' % len(data))
    rows = b''.join(b'\x00' + bytes(0 if data[x + (y // 8) * 128] & (1 << (y % 8))
                                   else 255 for x in range(128)) for y in range(64))
    def chunk(kind, content):
        body = kind + content
        return struct.pack('>I', len(content)) + body + struct.pack('>I', zlib.crc32(body))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', 128, 64, 8, 0, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    Path(path).write_bytes(png)


def read_rpc_message(stream, deadline):
    from flipperzero_protobuf.flipperzero_protobuf_compiled import flipper_pb2

    def exact(size):
        data = bytearray()
        while len(data) < size:
            if time.monotonic() >= deadline:
                raise AccessError('RPC response timed out before a complete message arrived')
            data.extend(stream.read(size - len(data)))
        return bytes(data)

    size = 0
    for shift in range(0, 35, 7):
        byte = exact(1)[0]
        if shift == 28 and byte > 15:
            raise AccessError('Invalid RPC message length')
        size |= (byte & 127) << shift
        if not byte & 128:
            message = flipper_pb2.Main()
            message.ParseFromString(exact(size))
            return message
    raise AccessError('Invalid RPC message length')


def snapshot_rpc(stream):
    from flipperzero_protobuf import FlipperProto
    from flipperzero_protobuf.flipperzero_protobuf_compiled import flipper_pb2, gui_pb2
    rpc = FlipperProto(serial_port=stream, in_session=True)
    rpc._rpc_send(gui_pb2.StartScreenStreamRequest(), 'gui_start_screen_stream_request')
    start_id = rpc._command_id
    acknowledged = False
    frame = None
    deadline = time.monotonic() + 5
    # Async frames can arrive before the command ACK. The package's generic
    # ID-filtered reader discards them, or accepts a non-frame ID=0 message.
    while not acknowledged or frame is None:
        message = read_rpc_message(stream, deadline)
        if message.command_id == start_id:
            if message.command_status:
                raise AccessError('RPC screen stream refused: %d' % message.command_status)
            acknowledged = True
        if message.HasField('gui_screen_frame') and len(message.gui_screen_frame.data) == 1024:
            frame = message.gui_screen_frame.data
    for request, field in [(gui_pb2.StopScreenStreamRequest(), 'gui_stop_screen_stream_request'),
                           (flipper_pb2.StopSession(), 'stop_session')]:
        rpc._rpc_send(request, field)
        deadline = time.monotonic() + 5
        while True:
            message = read_rpc_message(stream, deadline)
            if message.command_id == rpc._command_id:
                if message.command_status:
                    raise AccessError('RPC cleanup refused: %d' % message.command_status)
                break
    return frame


def sdk_worker(action, args):
    # Only the supervising process may spawn a worker with its live lock FD.
    fd = int(os.environ['FLIPPERCTL_LEASE_FD'])
    os.fstat(fd)
    port = os.environ['FLIPPER_PORT']
    if action == 'snapshot':
        with Cli(port, 5) as connection:
            stream = connection.stream
            stream.write(b'start_rpc_session\r')
            stream.timeout = 5
            echo = stream.read_until(b'start_rpc_session\r\n')
            stream.timeout = 0.05
            if not echo.endswith(b'start_rpc_session\r\n'):
                raise AccessError('RPC transition did not receive the complete command echo')
            write_frame(snapshot_rpc(stream), args[0])
        print('Screen frame saved:', args[0], flush=True)
        return
    scripts = Path(os.environ.get('UFBT_SCRIPT_DIR', Path.home() / '.ufbt/current/scripts'))
    sys.path.insert(0, str(scripts))
    from flipper.storage import FlipperStorage
    with FlipperStorage(port) as storage:
        if action == 'upload':
            local, remote = args
            storage.send_file(local, remote)
            size = storage.size(remote)
            remote_md5 = storage.hash_flipper(remote).strip().lower()
            local_md5 = storage.hash_local(local)
            if size != Path(local).stat().st_size or remote_md5 != local_md5:
                raise AccessError('Upload verification failed: remote size/MD5 does not match local file')
            print('VERIFIED upload: %d bytes, MD5 %s' % (size, local_md5), flush=True)
        elif action == 'download':
            remote, local = args
            storage.receive_file(remote, local)
            if storage.hash_flipper(remote).strip().lower() != storage.hash_local(local):
                raise AccessError('Download MD5 verification failed')
            print('VERIFIED download:', local, flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default=os.environ.get('FLIPPER_PORT'))
    parser.add_argument('--timeout', type=float, default=5,
                        help='seconds per CLI exchange or supervised operation')
    commands = parser.add_subparsers(dest='action', required=True)
    commands.add_parser('ports', help='enumerate host ports without opening USB')
    cli = commands.add_parser('cli', help='execute a single firmware CLI command')
    cli.add_argument('command')
    key = commands.add_parser('key', help='send PRESS, SHORT/LONG, RELEASE in one session')
    key.add_argument('key', choices=['up', 'down', 'left', 'right', 'ok', 'back'])
    key.add_argument('--long', action='store_true')
    snap = commands.add_parser('snapshot', help='save the real 128x64 RPC screen frame as PNG')
    snap.add_argument('output')
    upload = commands.add_parser('upload', help='close the app, upload a file, verify size and MD5')
    upload.add_argument('local')
    upload.add_argument('remote')
    download = commands.add_parser('download', help='download a file and verify MD5')
    download.add_argument('remote')
    download.add_argument('local')
    run = commands.add_parser('run', help='hold device ownership around any external USB tool')
    run.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    if args.action == 'ports':
        for p in list_ports.comports():
            print(p.device, p.description, p.hwid)
        return 0
    if args.action == 'run':
        if args.command and args.command[0] == '--':
            args.command = args.command[1:]
        if not args.command:
            parser.error('run needs a command after --')
    if args.action == 'upload' and not Path(args.local).is_file():
        parser.error('local upload file does not exist')
    if args.action in ('upload', 'download'):
        if not args.remote.startswith('/') or any(c in args.remote for c in '\r\n\x00"'):
            parser.error('remote path must be absolute and contain no quotes or line breaks')
    port = select_port(args.port)
    with device_lease(port) as lease:
        check_external_owner(port)
        if args.action == 'run':
            run_child(args.command, port, lease, args.timeout)
            return 0
        with Cli(port, args.timeout) as connection:
            if args.action == 'cli':
                print(connection.command(args.command), end='')
                return 0
            if args.action == 'key':
                print(connection.key(args.key, 'long' if args.long else 'short'), end='')
                return 0
            if args.action == 'upload':
                print(connection.close_app(), end='')
        params = {'upload': [getattr(args, 'local', ''), getattr(args, 'remote', '')],
                  'download': [getattr(args, 'remote', ''), getattr(args, 'local', '')],
                  'snapshot': [getattr(args, 'output', '')]}[args.action]
        run_child([sys.executable, str(Path(__file__).resolve()), '_worker', args.action, *params],
                  port, lease, args.timeout)
    return 0


if __name__ == '__main__':
    try:
        if len(sys.argv) > 1 and sys.argv[1] == '_worker':
            sdk_worker(sys.argv[2], sys.argv[3:])
        else:
            sys.exit(main())
    except (AccessError, serial.SerialException, OSError, subprocess.TimeoutExpired) as error:
        print('flipperctl:', error, file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print('flipperctl: cancelled; serial ownership released', file=sys.stderr)
        sys.exit(130)
