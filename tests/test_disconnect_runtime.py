"""Shared source interpreter and checks for the opt-in idle runtime scenario."""
import re
import json
import unittest

from pycparser import c_ast, c_parser
from test_adapter_lifecycle import Returned, execute
from test_xml_source import ROOT, strip_directives


class ScenarioHarness:
    source_name = 'disconnect_smoke.cpp'
    extra_functions = ()
    log_prefix = '[DisconnectPendingTest]'
    checkpoint_description = 'after launch response before RTSP'

    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'tests/runtime' / cls.source_name).read_text()
        source = source.replace('DISCONNECT_TEST_LOG_PREFIX ', json.dumps(cls.log_prefix) + ' ')
        source = source.replace('DISCONNECT_CHECKPOINT_DESCRIPTION ', json.dumps(cls.checkpoint_description) + ' ')
        types = (ROOT / 'include/moonlight/types.h').read_text()
        enums = re.search(r'enum TestPhase\s*{.*?};', source, re.S).group()
        enums += 'typedef enum TestPhase TestPhase;'
        enums += '\n'.join(re.search(
            r'typedef enum ' + name + r'\s*{.*?}\s*' + name + ';', types, re.S).group()
            for name in ('MoonlightConnectionState', 'MoonlightEventType'))
        functions = []
        names = ('set_phase', 'fail', 'disconnect_test_capture_host', 'disconnect_test_poll',
                 'disconnect_test_event', 'disconnect_test_shutdown') + cls.extra_functions
        for name in names:
            start = re.search(r'^(?:static |extern "C" )?void ' + name + r'\(', source, re.M).start()
            body = source[start:source.index('\n}', start) + 2]
            body = body.replace('extern "C" ', '')
            body = body.replace('MoonlightApp &app', 'int app').replace(
                'const MoonlightHost &host', 'const MoonlightHost host').replace(
                'const MoonlightEvent &event', 'const MoonlightEvent *event').replace('event.', 'event->')
            for original, stub in (
                    ('app.Connection().IsBusy', 'connection_busy'),
                    ('app.Connection().State', 'connection_state'),
                    ('app.Connection().Disconnect', 'disconnect_request'),
                    ('app.Connection().Connect', 'connect_request'),
                    ('app.Connection().Start', 'start_request'),
                    ('app.Applications().Refresh', 'refresh_request')):
                body = body.replace(original, stub)
            functions.append(body)
        preamble = ('typedef unsigned long long uint64_t;'
                    'typedef struct { char *internal; unsigned short port; } MoonlightHost;'
                    'typedef struct { int type; int result; int application_id; char *address; } MoonlightEvent;')
        cls.unit = c_parser.CParser().parse(strip_directives(preamble + enums + '\n'.join(functions)))
        cls.functions = {item.decl.name: item.body for item in cls.unit.ext
                         if isinstance(item, c_ast.FuncDef)}
        cls.timeout = int(re.search(r'kStepTimeoutUs\s*=\s*(\d+)', source).group(1))

    def setUp(self):
        self.values = dict(false=0, true=1, app=0, s_have_host=0, now=0, busy=0,
                           kStepTimeoutUs=self.timeout, s_step_started_us=0)
        for declaration in self.unit.ext:
            enum = declaration.type.type if isinstance(declaration, c_ast.Typedef) else (
                declaration.type if isinstance(declaration, c_ast.Decl) else None)
            if isinstance(enum, c_ast.Enum) and enum.values:
                value = -1
                for member in enum.values.enumerators:
                    value = int(member.value.value) if member.value else value + 1
                    self.values[member.name] = value
        self.values.update(s_phase=self.values['AwaitApplications'],
                           state=self.values['MOONLIGHT_CONNECTION_PAIRED'],
                           host={'internal': 'test-host', 'port': 47989})
        self.calls, self.logs = [], []
        self.results = dict(disconnect=0, connect=0, refresh=0)
        self.values.update(sceKernelGetProcessTimeWide=lambda: self.values['now'],
                           connection_busy=lambda: self.values['busy'],
                           connection_state=lambda: self.values['state'],
                           strcmp=lambda left, right: 0 if left == right else 1,
                           vita_debug_log=lambda fmt, *args: self.logs.append(fmt % args))
        for name in ('disconnect', 'connect', 'refresh'):
            self.values[name + '_request'] = lambda *args, name=name: (
                self.calls.append(name) or self.results[name])
        self.values['set_phase'] = lambda phase: self.run_body('set_phase', phase=phase)
        self.values['fail'] = lambda reason, result: self.run_body('fail', reason=reason, result=result)

    def run_body(self, name, **parameters):
        self.values.update(parameters)
        try:
            execute(self.functions[name], self.values)
        except Returned:
            pass

    def event(self, kind, result=0, address='test-host'):
        self.run_body('disconnect_test_event', event=dict(
            type=self.values['MOONLIGHT_EVENT_' + kind], result=result, address=address))

    def arm(self):
        self.run_body('disconnect_test_capture_host')
        self.event('APPLICATIONS_READY', 4)


class DisconnectRuntime(ScenarioHarness, unittest.TestCase):
    def test_single_idle_closed_reconnect_refresh_pass(self):
        self.arm()
        self.assertEqual(self.calls, [])
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        # A changed state alone must not replace the CLOSED event.
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.event('CONNECTION_CLOSED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect', 'connect'])
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_PAIRED']
        self.event('CONNECTION_READY')
        self.run_body('disconnect_test_poll')
        self.event('APPLICATIONS_READY', 4)
        self.assertEqual(self.calls, ['disconnect', 'connect', 'refresh'])
        self.assertTrue(self.logs[-1].startswith('[DisconnectTest] PASS'))
        self.run_body('disconnect_test_poll')
        self.event('APPLICATIONS_READY', 4)
        self.assertEqual(len(self.calls), 3)

    def test_busy_defers_and_timeout_stops_without_more_requests(self):
        self.arm()
        self.values['busy'] = 1
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.values['now'] = self.timeout + 1
        self.run_body('disconnect_test_poll')
        self.assertIn('FAIL', self.logs[-1])
        self.values['busy'] = 0
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])

    def test_rejected_disconnect_never_reconnects(self):
        self.arm()
        self.results['disconnect'] = -1
        self.run_body('disconnect_test_poll')
        self.event('CONNECTION_CLOSED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.assertIn('FAIL', self.logs[-1])

    def test_mismatched_closed_host_aborts(self):
        self.arm()
        self.run_body('disconnect_test_poll')
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('CONNECTION_CLOSED', address='another-host')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.assertIn('FAIL', self.logs[-1])

    def test_user_connect_during_test_aborts(self):
        self.arm()
        self.run_body('disconnect_test_capture_host')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.assertIn('FAIL', self.logs[-1])


if __name__ == '__main__':
    unittest.main()
