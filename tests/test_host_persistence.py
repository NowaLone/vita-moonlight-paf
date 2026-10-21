"""Interpret actual persistence functions with injected stdio/store failures.

No compilation, real file I/O, Vita storage, threading or durability validation.
"""
import re
import unittest

from pycparser import c_ast, c_parser
from test_adapter_lifecycle import Returned, execute
from test_xml_source import ROOT, strip_directives


def function(source, name):
    start = re.search(r'^(?:static )?(?:int|void) ' + name + r'\(', source, re.M).start()
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

    def store(self, name, *, existing, save_result=0, directory=True):
        stored = record() if existing else None
        calls = []
        values = dict(s_initialized=1, NULL=0, true=1, false=0,
                      address=CString('192.0.2.1'), name=CString('PC'), port=47989,
                      host=record(), host_id=CString('uuid'))

        def memset(pointer, *args):
            values[pointer] = record()

        def copy_string(destination, size, source):
            destination.text = str(source)

        def append(pointer):
            calls.append('append')
            return values[pointer]

        values.update(find_device_by_address=lambda *args: stored,
                      find_device=lambda *args: stored, find_stored_host=lambda *args: stored,
                      strcmp=lambda left, right: 0 if str(left) == str(right) else 1,
                      copy_string=copy_string, memset=memset, append_device=append,
                      ensure_device_directory=lambda *args: directory,
                      save_device_info=lambda *args: calls.append('save') or save_result,
                      vita_debug_log=lambda *args: calls.append('log'))
        return run(self.stores[name], values), calls

    def test_store_preserves_save_result_for_new_and_existing_records(self):
        for name in self.stores:
            for existing in (False, True):
                for status in (0, -1, -9):
                    with self.subTest(name=name, existing=existing, status=status):
                        result, calls = self.store(name, existing=existing, save_result=status)
                        self.assertEqual(result, status)
                        self.assertEqual(calls.count('save'), 1)
                        if name == 'set_host_id' and status:
                            self.assertNotIn('log', calls)  # no UUID-saved success diagnostic

    def test_directory_failure_does_not_attempt_save(self):
        for name in self.stores:
            for existing in (False, True):
                result, calls = self.store(name, existing=existing, directory=False)
                self.assertEqual(result, -1)
                self.assertNotIn('save', calls)

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


if __name__ == '__main__':
    unittest.main()
