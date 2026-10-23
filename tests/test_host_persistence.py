"""Interpret actual persistence functions with injected stdio/store failures.

No compilation, real file I/O, Vita storage, threading or durability validation.
"""
import re
import unittest
from itertools import product
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
import test_event_queue as event_runtime
from test_adapter_lifecycle import Returned, execute
from test_xml_source import ROOT, strip_directives


def function(source, name):
    start = re.search(r'^(?:static )?(?:int |void |device_info_t \*)' + name + r'\(', source, re.M).start()
    return source[start:source.index('\n}', start) + 2]


def parse_body(text):
    preamble = 'typedef int bool, uint16_t, FILE; typedef unsigned int size_t;'
    preamble += '''typedef struct {
        char name[256], host_id[33], storage_name[256];
        int port, paired; char internal[256], external[256], mac[18];
        int prefer_external;
    } device_info_t, MoonlightHost;'''
    unit = c_parser.CParser().parse(re.sub(r'//[^\n]*', '', strip_directives(preamble + text)))
    return next(item.body for item in unit.ext if isinstance(item, c_ast.FuncDef))


def run(body, values):
    try:
        execute(body, values)
    except Returned as result:
        return result.value


class CString:
    """Mutable char-array mock; [0] has C's zero/nonzero meaning."""
    def __init__(self, text=''):
        self.text = text

    def __getitem__(self, index):
        return ord(self.text[index]) if index < len(self.text) else 0

    def __str__(self):
        return self.text


def record():
    return dict(name=CString('PC'), host_id=CString('uuid'), storage_name=CString('PC'),
                port=47989, paired=1, internal=CString('192.0.2.1'), external=CString(),
                mac=CString('00:11:22:33:44:55'), prefer_external=0)


class HostPersistence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        device = (ROOT / 'src/backend/legacy/device.c').read_text()
        writer = function(device, 'save_device_info')
        # Expand only the three one-line fprintf macros using their actual definitions.
        for name, parameters, expansion in re.findall(
                r'^#define (write_\w+)\(([^)]*)\) (.*)$', device, re.M):
            def expand(match, parameters=parameters, expansion=expansion):
                result = expansion
                for formal, actual in zip(parameters.split(','), match.group(1).split(',')):
                    result = re.sub(r'\b' + formal.strip() + r'\b', actual.strip(), result)
                return result.rstrip(';') + ';'
            writer = re.sub(name + r'\(([^;\n]*)\);', expand, writer)
        # Path formatting is mocked, not pointer arithmetic or fixed-buffer layout.
        writer = writer.replace('char path[512] = {0};', 'char *path = "PC/device.ini";')
        cls.writer = parse_body(writer)
        store = (ROOT / 'src/backend/legacy_device_store.cpp').read_text()
        cls.stores = {name: parse_body(function(store, 'legacy_device_store_' + name))
                      for name in ('add_host', 'mark_paired', 'set_host_id')}
        cls.helpers = {}
        for name, parameters in (
                ('update_host_metadata', ('stored', 'host')),
                ('append_host', ('host', 'host_id', 'paired')),
                ('save_host', ('stored',))):
            if re.search(r'\b' + name + r'\(', store):
                cls.helpers[name] = (parse_body(function(store, name)), parameters)
        api = (ROOT / 'src/moonlight/api.cpp').read_text()
        pair = function(api, 'moonlight_api_pair_current_host')
        success = pair[pair.index('    update_host_from_server();'):]
        cls.paired = parse_body('int paired_success(void) {\n' + success)

    def write(self, *, open_result=1, write_failure=None, close_result=0, info=True):
        calls, lines, logs = [], [], []
        error = False

        def fprintf(fd, fmt, key, value):
            nonlocal error
            calls.append('write')
            if len(lines) == write_failure:
                error = True
            lines.append(fmt % (key, str(value) if isinstance(value, CString) else value))
            return -1 if error else len(lines[-1])

        values = dict(info=record() if info else None, errno=28,
                      device_file_path=lambda *args: None,
                      fopen=lambda *args: calls.append('open') or open_result,
                      fprintf=fprintf, ferror=lambda fd: int(error),
                      fclose=lambda fd: calls.append('close') or close_result,
                      vita_debug_log=lambda fmt, *args: logs.append(fmt % args))
        return run(self.writer, values), calls, ''.join(lines), logs

    def test_success_preserves_ini_fields_and_closes_once(self):
        result, calls, text, logs = self.write()
        self.assertEqual(result, 0)
        self.assertEqual(calls, ['open'] + ['write'] * 8 + ['close'])
        self.assertEqual(text, 'name = PC\npaired = true\nhost_id = uuid\n'
                         'internal = 192.0.2.1\nexternal = \nmac = 00:11:22:33:44:55\n'
                         'port = 47989\nprefer_external = false\n')
        self.assertIn('save_device_info: file closed\n', logs)

    def test_open_failure_does_not_write_or_close(self):
        result, calls, text, logs = self.write(open_result=0)
        self.assertEqual((result, calls, text), (-1, ['open'], ''))
        self.assertTrue(any('cannot open' in line and 'errno=28' in line for line in logs))

    def test_each_failed_write_is_reported_and_file_still_closed(self):
        for position in range(8):
            with self.subTest(position=position):
                result, calls, _, logs = self.write(write_failure=position)
                self.assertEqual(result, -1)
                self.assertEqual(calls.count('close'), 1)
                self.assertTrue(any('write failed' in line for line in logs))
                self.assertNotIn('save_device_info: file closed\n', logs)

    def test_buffered_failure_on_close_is_not_success(self):
        for write_failure in (None, 0, 7):
            result, calls, _, logs = self.write(write_failure=write_failure, close_result=-1)
            self.assertEqual(result, -1)
            self.assertEqual(calls.count('close'), 1)
            self.assertTrue(any('close/flush failed' in line for line in logs))
            self.assertNotIn('save_device_info: file closed\n', logs)

    def test_null_record_is_rejected_without_io(self):
        result, calls, _, _ = self.write(info=False)
        self.assertEqual((result, calls), (-1, []))

    def store(self, name, *, existing, save_result=0, directory=True,
              host=None, stored=None, address='192.0.2.1', display_name='PC', port=47989,
              host_id='uuid', append_failure=False, initialized=1):
        stored = (stored if stored is not None else record()) if existing else None
        calls = []
        directories = []
        values = dict(s_initialized=initialized, NULL=0, true=1, false=0,
                      address=CString(address) if address is not None else None,
                      name=CString(display_name) if display_name is not None else None, port=port,
                      host=host if host is not None else record(),
                      host_id=CString(host_id) if host_id is not None else None)

        def copy_string(destination, size, source):
            destination.text = str(source)

        values.update(find_device_by_address=lambda *args: stored,
                      find_device=lambda *args: stored, find_stored_host=lambda *args: stored,
                      strcmp=lambda left, right: 0 if str(left) == str(right) else 1,
                      copy_string=copy_string,
                      ensure_device_directory=lambda path: directories.append(str(path)) or directory,
                      save_device_info=lambda *args: calls.append('save') or save_result,
                      vita_debug_log=lambda *args: calls.append('log'))

        def invoke(body, arguments):
            local = dict(values, **arguments)

            def memset(pointer, *args):
                local[pointer] = {key: CString() if isinstance(value, CString) else 0
                                  for key, value in record().items()}

            def resolve(pointer):
                return local[pointer] if isinstance(pointer, str) else pointer

            def append(pointer):
                calls.append('append')
                return None if append_failure else resolve(pointer)

            local.update(memset=memset, append_device=append)
            for helper, (helper_body, parameters) in self.helpers.items():
                local[helper] = lambda *args, body=helper_body, params=parameters: invoke(
                    body, dict(zip(params, map(resolve, args))))[0]
            return run(body, local), local

        result, local = invoke(self.stores[name], {})
        snapshot = {key: str(value) if isinstance(value, CString) else value
                    for key, value in (local.get('stored') or {}).items()}
        return result, calls, snapshot, directories

    def test_store_preserves_save_result_for_new_and_existing_records(self):
        for name in self.stores:
            for existing in (False, True):
                for status in (0, -1, -9):
                    with self.subTest(name=name, existing=existing, status=status):
                        result, calls, _, _ = self.store(name, existing=existing, save_result=status)
                        self.assertEqual(result, status)
                        self.assertEqual(calls.count('save'), 1)
                        if name == 'set_host_id' and status:
                            self.assertNotIn('log', calls)  # no UUID-saved success diagnostic

    def test_directory_failure_does_not_attempt_save(self):
        for name in self.stores:
            for existing in (False, True):
                result, calls, _, _ = self.store(name, existing=existing, directory=False)
                self.assertEqual(result, -1)
                self.assertNotIn('save', calls)

    def test_metadata_creation_and_update_rules(self):
        for name in self.stores:
            for existing in (False, True):
                for populated in (False, True):
                    for port in (0, 48000):
                        with self.subTest(name=name, existing=existing, populated=populated, port=port):
                            host = record()
                            for field in ('name', 'external', 'mac', 'host_id'):
                                host[field] = CString('new-' + field if populated else '')
                            host['internal'] = CString('192.0.2.2')
                            host['port'] = port
                            host['prefer_external'] = 1
                            old = record()
                            old['port'] = 47990
                            old['paired'] = 0
                            old['external'] = CString('old-external')
                            expected = {key: str(value) if isinstance(value, CString) else value
                                        for key, value in old.items()} if existing else {
                                            key: '' if isinstance(value, CString) else 0
                                            for key, value in record().items()}
                            if name == 'add_host':
                                expected['name'] = str(host['name']) or (
                                    'PC' if existing else '192.0.2.2')
                                expected['port'] = port or 47989
                            else:
                                for field in ('name', 'internal', 'external', 'mac'):
                                    if str(host[field]):
                                        expected[field] = str(host[field])
                                expected['port'] = port or (47990 if existing else 47989)
                                if not existing:
                                    expected['name'] = str(host['name']) or '192.0.2.2'
                                    expected['prefer_external'] = 1
                                if name == 'mark_paired':
                                    expected['paired'] = expected['prefer_external'] = 1
                                    if not existing:
                                        expected['host_id'] = str(host['host_id'])
                                else:
                                    expected['host_id'] = 'server-uuid'
                            expected['internal'] = '192.0.2.2'
                            result, calls, saved, directories = self.store(
                                name, existing=existing, host=host, stored=old,
                                address='192.0.2.2', display_name=str(host['name']), port=port,
                                host_id='server-uuid')
                            self.assertEqual(result, 0)
                            self.assertEqual(calls.count('append'), int(not existing))
                            self.assertEqual(calls.count('save'), 1)
                            self.assertEqual(saved, expected)
                            self.assertEqual(directories, ['PC' if existing else saved['name']])

    def test_pairing_only_fills_a_missing_uuid_and_preserves_storage_name(self):
        for uuid in ('', 'old-uuid'):
            old = record()
            old['host_id'] = CString(uuid)
            old['storage_name'] = CString('original-directory')
            result, _, saved, directories = self.store('mark_paired', existing=True, stored=old)
            self.assertEqual(result, 0)
            self.assertEqual(saved['host_id'], uuid or 'uuid')
            self.assertEqual(directories, ['original-directory'])
        old = record()
        old['storage_name'] = CString()
        old['paired'] = old['prefer_external'] = 1
        host = record()
        host['internal'] = CString()
        result, _, saved, directories = self.store('set_host_id', existing=True, stored=old, host=host)
        self.assertEqual(result, 0)
        self.assertEqual((saved['internal'], saved['paired'], saved['prefer_external']),
                         ('192.0.2.1', 1, 1))
        self.assertEqual(directories, ['PC'])

    def test_append_failure_and_invalid_input_do_not_write(self):
        for name in self.stores:
            result, calls, _, directories = self.store(name, existing=False, append_failure=True)
            self.assertEqual((result, calls, directories), (-1, ['append'], []))
            for arguments in ({'initialized': 0}, {
                    'add_host': {'address': ''},
                    'mark_paired': {'host': dict(record(), internal=CString())},
                    'set_host_id': {'host_id': ''}}[name]):
                result, calls, _, directories = self.store(name, existing=True, **arguments)
                self.assertEqual((result, calls, directories), (-1, [], []))

    def test_existing_device_append_preserves_metadata_and_fill_only_identity(self):
        source = (ROOT / 'src/backend/legacy/device.c').read_text()
        start = source.index('device_info_t* append_device(')
        prefix = source[start:source.index('  // FIXME: need mutex', start)]
        append = parse_body(prefix + '  return NULL;\n}')
        merge = parse_body(function(source, 'merge_device_info'))
        harness = event_runtime.EventQueue()
        harness.base_expression, harness.base_execute = runtime.expression, runtime.execute

        def snapshot(info):
            return {key: value.text() if isinstance(value, event_runtime.Buffer) else value
                    for key, value in info.items()}

        def invoke(body, **arguments):
            values = dict(NULL=0, true=1, false=0, **arguments)
            values.update(strncpy=event_runtime.EventQueue.strncpy,
                          strcmp=lambda left, right: (left.text() > right.text()) -
                          (left.text() < right.text()),
                          find_device_by_identity=lambda info: stored,
                          vita_debug_log=lambda *args: None,
                          merge_device_info=lambda destination, source: invoke(
                              merge, destination=destination, source=source))
            with patch.object(runtime, 'expression', harness.expression), \
                    patch.object(runtime, 'execute', harness.execute):
                return run(body, values)

        for metadata, old_paired, paired, port, prefer, identity in product(
                ('empty', 'same', 'new'), (0, 1), (0, 1), (0, 48000), (0, 1), ('', 'old')):
            with self.subTest(metadata=metadata, paired=paired, port=port,
                              old_paired=old_paired, prefer=prefer, identity=identity):
                stored, incoming = record(), record()
                stored.update(paired=old_paired, host_id=CString(identity), storage_name=CString(identity))
                incoming.update(paired=paired, port=port, prefer_external=prefer)
                for field in ('name', 'internal', 'external', 'mac'):
                    incoming[field] = CString('' if metadata == 'empty' else (
                        str(stored[field]) if metadata == 'same' else 'new-' + field))
                expected = {key: str(value) if isinstance(value, CString) else value
                            for key, value in stored.items()}
                for field, value in incoming.items():
                    if isinstance(value, CString):
                        incoming[field] = event_runtime.Buffer(str(value), 18 if field == 'mac' else (
                            33 if field == 'host_id' else 256))
                        if value.text and (field not in ('host_id', 'storage_name') or not expected[field]):
                            expected[field] = value.text
                expected.update(paired=old_paired or paired, port=port or 47989, prefer_external=prefer)
                for field, value in stored.items():
                    if isinstance(value, CString):
                        stored[field] = event_runtime.Buffer(str(value), 18 if field == 'mac' else (
                            33 if field == 'host_id' else 256))
                self.assertIs(invoke(append, info=incoming), stored)
                self.assertEqual(snapshot(stored), expected)

    def test_metadata_failure_does_not_convert_proven_pairing_into_failure(self):
        for status in (0, -1):
            events, logs = [], []
            values = dict(s_current_host=dict(id=1, internal='192.0.2.1'),
                          MOONLIGHT_CONNECTION_PAIRED=2, MOONLIGHT_PAIRING_SUCCESS=0,
                          MOONLIGHT_EVENT_PAIRING_FINISHED=11,
                          update_host_from_server=lambda: None,
                          legacy_device_store_mark_paired=lambda host: status,
                          emit=lambda *args: events.append(args),
                          vita_debug_log=lambda *args: logs.append(args))
            self.assertEqual(run(self.paired, values), 0)
            self.assertEqual(values['s_connection_state'], 2)
            self.assertEqual(events, [(11, 0, 1, -1, '192.0.2.1')])
            self.assertEqual(len(logs), int(status != 0))

    def test_new_device_terminates_full_length_strings_in_uninitialized_storage(self):
        source = (ROOT / 'src/backend/legacy/device.c').read_text()
        start = source.index('  device_info_t *p = &known_devices.devices[known_devices.count];')
        tail = source[start:source.index('\n}', start) + 2]
        tail = tail.replace('known_devices.count++;', 'known_devices.count = known_devices.count + 1;')
        append = parse_body('device_info_t *append_new(device_info_t *info) {\n' + tail)
        harness = event_runtime.EventQueue()
        harness.base_expression, harness.base_execute = runtime.expression, runtime.execute
        for length in (0, 1, 254, 255):
            with self.subTest(length=length):
                incoming, destination = record(), record()
                for field in ('name', 'internal', 'external'):
                    incoming[field] = CString('x' * length)
                for field, value in incoming.items():
                    if isinstance(value, CString):
                        size = 18 if field == 'mac' else (33 if field == 'host_id' else 256)
                        incoming[field] = event_runtime.Buffer(str(value), size)
                        destination[field] = event_runtime.Buffer('!' * size, size)
                devices = dict(devices=[destination], count=0)
                values = dict(info=incoming, known_devices=devices,
                              strncpy=event_runtime.EventQueue.strncpy,
                              ensure_host_identity=lambda info: None,
                              vita_debug_log=lambda *args: None)
                with patch.object(runtime, 'expression', harness.expression), \
                        patch.object(runtime, 'execute', harness.execute):
                    self.assertIs(run(append, values), destination)
                self.assertEqual(devices['count'], 1)
                for field, value in incoming.items():
                    with self.subTest(field=field):
                        actual = destination[field]
                        self.assertEqual(actual.text() if isinstance(actual, event_runtime.Buffer) else actual,
                                         value.text() if isinstance(value, event_runtime.Buffer) else value)


if __name__ == '__main__':
    unittest.main()
