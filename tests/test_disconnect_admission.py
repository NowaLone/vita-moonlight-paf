"""Interpret actual API admission/cleanup source with mocked mutex and worker calls.

The C++ scoped request is normalized to its real C helper, with scope release simulated.
This does not compile C++, execute pthreads or perform network/Vita I/O.
"""
import re
import unittest

from pycparser import c_ast, c_parser
from test_adapter_lifecycle import Returned, execute
from test_xml_source import ROOT, strip_directives


ENTRIES = ('connect_host', 'prepare_pairing', 'pair_current_host',
           'get_applications', 'start_application', 'stop_application')


class DisconnectAdmission(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'src/moonlight/api.cpp').read_text()
        types = (ROOT / 'include/moonlight/types.h').read_text()
        enums = '\n'.join(re.search(
            r'typedef enum ' + name + r'\s*{.*?}\s*' + name + ';', types, re.S).group()
            for name in ('MoonlightConnectionState', 'MoonlightEventType'))
        names = ('disconnect_pending', 'set_disconnect_pending', 'try_begin_host_request',
                 'disconnect_cleanup_worker', 'begin_disconnect_cleanup')
        names += tuple('moonlight_api_' + entry for entry in ENTRIES + ('disconnect_host',))
        names += ('moonlight_api_shutdown',)
        functions = []
        for name in names:
            match = re.search(r'^(?:static )?(?:bool|void \*|void|int)\s*' + name + r'\(', source, re.M)
            start = match.start()
            end = source.index('\n}', start) + 2
            body = source[start:end]
            body = re.sub(r'HostControlRequest request\((true|false)\);',
                          r'int request = try_begin_host_request(\1);', body)
            functions.append(body.replace('request.Acquired()', 'request'))
        preamble = ('typedef int bool, pthread_t, uint32_t, MoonlightCancellation;'
                    'typedef unsigned int size_t;'
                    'typedef struct { int id; char *internal; } MoonlightHost;'
                    'typedef int MoonlightApplication, MoonlightSettings, MoonlightGameStreamResponse;'
                    'typedef int LegacyGameStreamPairingError;')
        cls.unit = c_parser.CParser().parse(strip_directives(preamble + enums + '\n'.join(functions)))
        cls.functions = {item.decl.name: item.body for item in cls.unit.ext
                         if isinstance(item, c_ast.FuncDef)}

    def setUp(self):
        self.values = dict(false=0, true=1, NULL=0, __ATOMIC_ACQUIRE=2, __ATOMIC_RELEASE=3,
                           locked=False, s_disconnect_pending=False, s_disconnect_worker_running=False,
                           s_has_current_host=True, s_game_stream_initialized=True,
                           s_stream_active=False, s_stream_pending=False, s_stream_application_id=42,
                           s_server=0, s_current_host={'id': 7, 'internal': 0})
        for declaration in self.unit.ext:
            if isinstance(declaration, c_ast.Typedef) and isinstance(declaration.type.type, c_ast.Enum):
                value = -1
                for member in declaration.type.type.values.enumerators:
                    value = int(member.value.value) if member.value else value + 1
                    self.values[member.name] = value
        self.values['s_connection_state'] = self.values['MOONLIGHT_CONNECTION_PAIRED']
        self.calls = []

        def try_lock(pointer):
            self.assertEqual(pointer, 's_host_control_mutex')
            if self.values['locked']:
                return 16
            self.values['locked'] = True
            return 0

        def lock(pointer):
            self.assertFalse(self.values['locked'])
            return try_lock(pointer)

        def unlock(pointer):
            self.assertEqual(pointer, 's_host_control_mutex')
            self.assertTrue(self.values['locked'])
            self.values['locked'] = False

        self.values.update(pthread_mutex_trylock=try_lock, pthread_mutex_lock=lock,
                           pthread_mutex_unlock=unlock,
                           __atomic_load_n=lambda pointer, order: self.values[pointer],
                           __atomic_store_n=lambda pointer, value, order: self.values.__setitem__(pointer, value),
                           moonlight_stream_is_running=lambda: 0,
                           moonlight_stream_stop=lambda: 0,
                           invalidate_applications=lambda: self.calls.append('invalidate'),
                           pthread_create=lambda *args: self.calls.append('create') or 0,
                           pthread_detach=lambda *args: self.calls.append('detach'),
                           legacy_gamestream_shutdown=lambda *args: self.calls.append('shutdown'),
                           sceKernelDelayThread=lambda *args: self.calls.append('wait'))
        for name in self.functions:
            self.values[name] = lambda *args, name=name: self.invoke(name, *args)

    def invoke(self, name, *args):
        if name == 'try_begin_host_request':
            self.values['for_disconnect'] = args[0]
        elif name == 'set_disconnect_pending':
            self.values['pending'] = args[0]
        try:
            execute(self.functions[name], self.values)
        except Returned as result:
            return result.value

    def api(self, entry):
        # Model the scoped C++ guard's release on every return, not a real destructor.
        outer_request = self.values.pop('request', None)
        try:
            return self.invoke('moonlight_api_' + entry)
        finally:
            if self.values.pop('request', False):
                self.values['pthread_mutex_unlock']('s_host_control_mutex')
            if outer_request is not None:
                self.values['request'] = outer_request

    def test_gate_rejects_contention_or_pending_cleanup(self):
        for locked in (False, True):
            for pending in (False, True):
                for disconnect in (False, True):
                    with self.subTest(locked=locked, pending=pending, disconnect=disconnect):
                        self.values.update(locked=locked, s_disconnect_pending=pending)
                        accepted = self.invoke('try_begin_host_request', disconnect)
                        self.assertEqual(bool(accepted), not locked and (disconnect or not pending))
                        self.assertEqual(self.values['locked'], locked or bool(accepted))

    def test_every_host_entry_refuses_before_touching_resources(self):
        for locked, pending in ((True, False), (False, True), (True, True)):
            for entry in ENTRIES:
                with self.subTest(entry=entry, locked=locked, pending=pending):
                    self.values.update(locked=locked, s_disconnect_pending=pending)
                    # Backend/resource stubs are absent: proceeding past the gate fails.
                    self.assertEqual(self.api(entry), -1)
                    self.assertEqual(self.values['locked'], locked)
                    self.assertEqual(self.calls, [])

    def test_disconnect_waits_for_owner_and_publishes_closed_after_cleanup(self):
        self.values['s_stream_pending'] = True
        self.values['moonlight_stream_stop'] = lambda: self.calls.append('stop') or 0
        paired = self.values['s_connection_state']
        self.assertEqual(self.api('disconnect_host'), 0)
        self.assertTrue(self.values['s_disconnect_pending'])
        self.assertTrue(self.values['s_disconnect_worker_running'])
        self.assertEqual(self.values['s_connection_state'], paired)
        self.assertTrue(self.values['s_has_current_host'])
        self.assertEqual(self.api('disconnect_host'), 0)
        self.assertEqual(self.calls.count('create'), 1)
        for entry in ENTRIES:
            self.assertEqual(self.api(entry), -1)

        running = iter((1, 0))
        self.values['moonlight_stream_is_running'] = lambda: next(running)

        def emit(event, *args):
            self.assertEqual(event, self.values['MOONLIGHT_EVENT_CONNECTION_CLOSED'])
            self.assertEqual(self.values['s_connection_state'],
                             self.values['MOONLIGHT_CONNECTION_DISCONNECTED'])
            self.assertFalse(self.values['s_has_current_host'])
            self.assertFalse(self.values['s_game_stream_initialized'])
            self.assertLess(self.calls.index('stop'), self.calls.index('wait'))
            self.assertLess(self.calls.index('wait'), self.calls.index('shutdown'))
            self.assertFalse(self.invoke('try_begin_host_request', False))
            # A duplicate remains a successful no-op even while the worker holds the gate.
            self.assertEqual(self.api('disconnect_host'), 0)
            self.calls.append('closed')

        self.values['emit'] = emit
        self.invoke('disconnect_cleanup_worker')
        self.assertEqual(self.calls.count('closed'), 1)
        self.assertFalse(self.values['s_disconnect_pending'])
        self.assertFalse(self.values['s_disconnect_worker_running'])
        self.assertFalse(self.values['locked'])
        self.assertTrue(self.invoke('try_begin_host_request', False))

    def test_worker_creation_failure_reopens_admission_without_closed(self):
        def fail_create(*args):
            self.assertFalse(self.values['s_disconnect_pending'])
            # Another disconnect must not report acceptance of this failed creation.
            self.assertEqual(self.api('disconnect_host'), -1)
            return 12

        self.values['pthread_create'] = fail_create
        paired = self.values['s_connection_state']
        self.assertEqual(self.api('disconnect_host'), 12)
        self.assertFalse(self.values['s_disconnect_pending'])
        self.assertFalse(self.values['s_disconnect_worker_running'])
        self.assertTrue(self.values['s_has_current_host'])
        self.assertEqual(self.values['s_connection_state'], paired)
        self.assertTrue(self.invoke('try_begin_host_request', False))

    def test_stop_failure_reopens_admission_without_creating_worker(self):
        self.values['s_stream_active'] = True
        self.values['moonlight_stream_stop'] = lambda: -7
        self.assertEqual(self.api('disconnect_host'), -7)
        self.assertFalse(self.values['s_disconnect_pending'])
        self.assertFalse(self.values['s_disconnect_worker_running'])
        self.assertNotIn('create', self.calls)
        self.assertTrue(self.invoke('try_begin_host_request', False))

    def test_busy_request_is_not_accepted_and_idle_disconnect_is_noop(self):
        self.values['locked'] = True
        self.assertEqual(self.api('disconnect_host'), -1)
        self.assertFalse(self.values['s_disconnect_pending'])
        self.values.update(locked=False, s_has_current_host=False,
                           s_connection_state=self.values['MOONLIGHT_CONNECTION_DISCONNECTED'])
        self.assertEqual(self.api('disconnect_host'), 0)
        self.assertEqual(self.calls, [])

    def test_shutdown_yields_gate_to_queued_cleanup_worker(self):
        self.assertEqual(self.api('disconnect_host'), 0)
        self.assertTrue(self.values['s_disconnect_worker_running'])
        self.values['emit'] = lambda *args: self.calls.append('closed')
        self.values['legacy_host_discovery_shutdown'] = lambda: self.calls.append('discovery shutdown')
        self.values['memset'] = lambda *args: None

        def delay(*args):
            # Shutdown must unlock before giving the queued worker a chance to run.
            self.assertFalse(self.values['locked'])
            self.invoke('disconnect_cleanup_worker')

        self.values['sceKernelDelayThread'] = delay
        self.invoke('moonlight_api_shutdown')
        self.assertFalse(self.values['locked'])
        self.assertFalse(self.values['s_disconnect_worker_running'])
        self.assertEqual(self.calls.count('shutdown'), 1)
        self.assertLess(self.calls.index('closed'), self.calls.index('discovery shutdown'))


if __name__ == '__main__':
    unittest.main()
