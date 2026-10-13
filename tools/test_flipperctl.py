"""Offline transport checks. Opens only fresh pseudo-terminals, never Flipper USB."""
from contextlib import contextmanager
import importlib.util
import os
from pathlib import Path
import pty
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('flipperctl', Path(__file__).with_name('flipperctl.py'))
ctl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ctl)


@contextmanager
def simulated_cli(silent_commands=(), responses=None, rpc=False,
                  frame_before_ack=False, empty_event=False, fragmented_frame=False):
    master, slave = pty.openpty()
    port = os.ttyname(slave)
    os.close(slave)
    commands = []
    stop = threading.Event()

    def serve():
        pending = bytearray()
        binary = False
        if rpc:
            from flipperzero_protobuf.flipperzero_protobuf_compiled import flipper_pb2
            from google.protobuf.internal.encoder import _VarintBytes

            def send_message(message):
                payload = message.SerializeToString()
                packet = _VarintBytes(len(payload)) + payload
                if fragmented_frame and len(packet) > 100:
                    os.write(master, packet[:12])
                    time.sleep(0.1)
                    os.write(master, packet[12:])
                else:
                    os.write(master, packet)

        while not stop.is_set():
            try:
                data = os.read(master, 4096)
            except OSError:
                stop.wait(0.005)
                continue
            pending.extend(data if binary else data.replace(b'\x03', b''))
            while pending:
                if binary:
                    size, shift, prefix = 0, 0, 0
                    for byte in pending:
                        prefix += 1
                        size |= (byte & 127) << shift
                        if byte < 128:
                            break
                        shift += 7
                    else:
                        break
                    if len(pending) < prefix + size:
                        break
                    message = flipper_pb2.Main()
                    message.ParseFromString(bytes(pending[prefix:prefix + size]))
                    del pending[:prefix + size]
                    answer = flipper_pb2.Main(command_id=message.command_id, command_status=0)
                    if message.HasField('gui_start_screen_stream_request'):
                        frame = flipper_pb2.Main(command_id=0, command_status=0)
                        frame.gui_screen_frame.data = b'\x00' * 1024
                        if frame_before_ack:
                            send_message(frame)
                            send_message(answer)
                            continue
                        send_message(answer)
                        if empty_event:
                            send_message(flipper_pb2.Main(command_id=0, command_status=0))
                        send_message(frame)
                    else:
                        send_message(answer)
                    if message.HasField('stop_session'):
                        binary = False
                    continue
                if b'\r' not in pending:
                    break
                command, _, pending = pending.partition(b'\r')
                text = command.decode()
                if text:
                    commands.append(text)
                if text in silent_commands:
                    continue
                if text == 'start_rpc_session' and rpc:
                    # A delayed old prompt must not enter the protobuf decoder.
                    os.write(master, ('\r\n>: ' + text + '\r\n').encode())
                    binary = True
                    continue
                body = 'loader input help' if text == 'help' else 'reply'
                body = (responses or {}).get(text, body)
                os.write(master, (text + '\r\n' + body + '\r\n>: ').encode())

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        yield port, commands
    finally:
        stop.set()
        os.close(master)
        thread.join(timeout=0.5)


class TransportTests(unittest.TestCase):
    def test_round_trip_and_complete_button_sequence(self):
        with simulated_cli() as (port, commands):
            with ctl.Cli(port, 1) as connection:
                self.assertIn('reply', connection.command('uptime'))
                connection.key('back', 'long')
            self.assertEqual(commands, ['help', 'uptime', 'input send back press',
                                        'input send back long', 'input send back release'])
            self.assertFalse(connection.stream.is_open)

    def test_greeting_is_not_a_ready_cli(self):
        with simulated_cli(silent_commands=['help']) as (port, commands):
            connection = ctl.Cli(port, 0.15)
            start = time.monotonic()
            with self.assertRaises(ctl.AccessError):
                with connection:
                    self.fail('Greeting must not authorize a command')
            self.assertLess(time.monotonic() - start, 1)
            self.assertFalse(connection.stream.is_open)
            self.assertEqual(commands, ['help'])

    def test_direct_call_uses_installed_isolated_runtime(self):
        with tempfile.TemporaryDirectory() as root:
            # An unrelated agent's PYTHONPATH must not shadow the installed
            # serial package before argparse or a device operation starts.
            (Path(root) / 'serial.py').write_text("raise RuntimeError('foreign serial module loaded')\n")
            env = os.environ.copy()
            env['PYTHONPATH'] = root
            env['FLIPPERCTL_HOME'] = str(Path(sys.prefix).parent)
            base_python = getattr(sys, '_base_executable', sys.executable)
            result = subprocess.run([base_python, str(Path(ctl.__file__)), '--help'],
                                    env=env, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('usage: flipperctl.py', result.stdout)

    def test_timeout_never_replays_a_mutation(self):
        with simulated_cli(silent_commands=['loader open /ext/test.fap']) as (port, commands):
            with ctl.Cli(port, 0.15) as connection:
                with self.assertRaises(ctl.AccessError):
                    connection.command('loader open /ext/test.fap')
            self.assertEqual(commands.count('loader open /ext/test.fap'), 1)

    def test_momentum_unknown_command_is_failure(self):
        with simulated_cli(responses={'missing': 'could not find command `missing`, try `help`'}) as (port, commands):
            with ctl.Cli(port, 1) as connection:
                with self.assertRaisesRegex(ctl.AccessError, 'could not find'):
                    connection.command('missing')

    def test_manual_app_exit_uses_full_input_before_upload(self):
        responses = {'loader close': 'Application "Room Sweep" has to be closed manually',
                     'loader info': 'No application is running'}
        with simulated_cli(responses=responses) as (port, commands):
            with ctl.Cli(port, 1) as connection:
                connection.close_app()
            self.assertEqual(commands, ['help', 'loader close', 'input send back press',
                                        'input send back long', 'input send back release', 'loader info'])

    def test_release_even_if_long_event_times_out(self):
        with simulated_cli(silent_commands=['input send back long']) as (port, commands):
            with ctl.Cli(port, 0.15) as connection:
                with self.assertRaises(ctl.AccessError):
                    connection.key('back', 'long')
            self.assertEqual(commands[-3:], ['input send back press', 'input send back long',
                                              'input send back release'])

    def test_two_agents_cannot_hold_same_lease(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {'FLIPPERCTL_LOCK_DIR': root}):
            with patch.object(ctl.list_ports, 'comports', return_value=[]):
                with ctl.device_lease('/tmp/offline-test-port'):
                    with self.assertRaisesRegex(ctl.AccessError, 'busy'):
                        with ctl.device_lease('/tmp/offline-test-port'):
                            self.fail('second lease acquired')
                with ctl.device_lease('/tmp/offline-test-port'):
                    pass

    def test_macos_aliases_share_a_lease(self):
        from types import SimpleNamespace
        record = SimpleNamespace(device='/dev/cu.usbmodemTEST', serial_number='offline-fixture',
                                 vid=0x0483, pid=0x5740)
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {'FLIPPERCTL_LOCK_DIR': root}):
            with patch.object(ctl.list_ports, 'comports', return_value=[record]):
                self.assertEqual(ctl.lock_path('/dev/tty.usbmodemTEST'),
                                 ctl.lock_path('/dev/cu.usbmodemTEST'))

    def test_external_serial_owner_is_detected(self):
        master, slave = pty.openpty()
        try:
            with self.assertRaisesRegex(ctl.AccessError, 'held by PID'):
                ctl.check_external_owner(os.ttyname(slave))
        finally:
            os.close(slave)
            os.close(master)

    def test_supervised_timeout_releases_lease(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {'FLIPPERCTL_LOCK_DIR': root}):
            with patch.object(ctl.list_ports, 'comports', return_value=[]):
                with ctl.device_lease('/tmp/offline-test-port') as lease:
                    start = time.monotonic()
                    with self.assertRaises(subprocess.TimeoutExpired):
                        ctl.run_child([sys.executable, '-c', 'import time; time.sleep(60)'],
                                      '/tmp/offline-test-port', lease, 0.15)
                    self.assertLess(time.monotonic() - start, 3)
                with ctl.device_lease('/tmp/offline-test-port'):
                    pass

    def test_timeout_stops_descendants_that_ignore_terminate(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {'FLIPPERCTL_LOCK_DIR': root}):
            pid_file = Path(root) / 'descendant.pid'
            descendant = '''
import os, signal, sys, time
from pathlib import Path
signal.signal(signal.SIGTERM, signal.SIG_IGN)
Path(sys.argv[1]).write_text(str(os.getpid()))
time.sleep(30)
'''
            launcher = '''
import os, subprocess, sys, time
subprocess.Popen([sys.executable, '-c', sys.argv[1], sys.argv[2]],
                 pass_fds=(int(os.environ['FLIPPERCTL_LEASE_FD']),))
time.sleep(30)
'''
            try:
                with patch.object(ctl.list_ports, 'comports', return_value=[]):
                    with ctl.device_lease('/tmp/offline-test-port') as lease:
                        with self.assertRaises(subprocess.TimeoutExpired):
                            ctl.run_child([sys.executable, '-c', launcher, descendant, str(pid_file)],
                                          '/tmp/offline-test-port', lease, 1)
                    self.assertTrue(pid_file.exists(), 'descendant must actually start')
                    # A surviving descendant inherits the lock FD and blocks
                    # the next agent even though its supervisor has exited.
                    end = time.monotonic() + 1
                    while True:
                        try:
                            with ctl.device_lease('/tmp/offline-test-port'):
                                break
                        except ctl.AccessError:
                            if time.monotonic() >= end:
                                raise
                            time.sleep(0.01)
            finally:
                if pid_file.exists():
                    try:
                        os.kill(int(pid_file.read_text()), signal.SIGKILL)
                    except ProcessLookupError:
                        pass

    def test_upload_hash_mismatch_is_failure(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {'FLIPPERCTL_LOCK_DIR': root}):
            scripts = Path(root) / 'sdk'
            (scripts / 'flipper').mkdir(parents=True)
            (scripts / 'flipper/__init__.py').touch()
            (scripts / 'flipper/storage.py').write_text('''
class FlipperStorage:
    def __init__(self, port): pass
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def send_file(self, local, remote): pass
    def size(self, path): return 3
    def hash_flipper(self, path): return 'wrong'
    def hash_local(self, path): return 'expected'
''')
            local = Path(root) / 'sample.txt'
            local.write_text('abc')
            with patch.dict(os.environ, {'UFBT_SCRIPT_DIR': str(scripts)}):
                with patch.object(ctl.list_ports, 'comports', return_value=[]):
                    with ctl.device_lease('/tmp/offline-test-port') as lease:
                        with self.assertRaisesRegex(ctl.AccessError, 'status 1'):
                            ctl.run_child([sys.executable, str(Path(ctl.__file__)), '_worker',
                                           'upload', str(local), '/ext/offline-sample.txt'],
                                          '/tmp/offline-test-port', lease, 3)

    def test_incomplete_frame_cannot_produce_a_screenshot(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'screen.png'
            with self.assertRaises(ctl.AccessError):
                ctl.write_frame(b'\x00' * 1000, path)
            self.assertFalse(path.exists())
            ctl.write_frame(b'\x00' * 1024, path)
            self.assertTrue(path.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'))

    def test_rpc_transition_discards_stale_text_before_binary_frame(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {'FLIPPERCTL_LOCK_DIR': root}):
            with simulated_cli(rpc=True) as (port, commands):
                with ctl.device_lease(port) as lease:
                    output = Path(root) / 'rpc-screen.png'
                    ctl.run_child([sys.executable, str(Path(ctl.__file__)), '_worker',
                                   'snapshot', str(output)], port, lease, 5)
                self.assertTrue(output.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'))
                self.assertEqual(commands, ['help', 'start_rpc_session'])

    def check_rpc_frame_delivery(self, **options):
        with tempfile.TemporaryDirectory() as root:
            with simulated_cli(rpc=True, **options) as (port, commands):
                with ctl.Cli(port, 1) as connection:
                    connection.stream.write(b'start_rpc_session\r')
                    connection.stream.read_until(b'start_rpc_session\r\n')
                    self.assertEqual(ctl.snapshot_rpc(connection.stream), b'\x00' * 1024)

    def test_rpc_frame_before_ack_is_preserved(self):
        self.check_rpc_frame_delivery(frame_before_ack=True)

    def test_rpc_empty_async_message_is_not_a_screen_frame(self):
        self.check_rpc_frame_delivery(empty_event=True)

    def test_rpc_fragmented_payload_waits_for_all_bytes(self):
        self.check_rpc_frame_delivery(fragmented_frame=True)


if __name__ == '__main__':
    unittest.main()
