"""Interpret actual discovery service/API/native lifecycle with serial kernel mocks.

No PAF jobs, Vita threads, socket I/O, compilation or real synchronization is executed.
"""
import re
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives


def function(source, name):
    start = re.search(r'^(?:static )?(?:int|void) ' + name + r'\(', source, re.M).start()
    return source[start:source.index('\n}', start) + 2]


class Scope:
    def __init__(self, shared, parameters):
        self.shared, self.local = shared, parameters

    def __getitem__(self, key):
        return self.local[key] if key in self.local else self.shared[key]

    def __setitem__(self, key, value):
        (self.shared if key.startswith('s_') else self.local)[key] = value

    def __contains__(self, key):
        return key in self.local or key in self.shared


class DiscoveryLifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.service = (ROOT / 'src/services/host_discovery_service.cpp').read_text()
        native = (ROOT / 'src/backend/legacy/legacy_host_discovery.c').read_text()
        api = (ROOT / 'src/moonlight/api.cpp').read_text()
        bodies = []
        for name in ('Start', 'Stop'):
            body = function(cls.service, 'HostDiscoveryService::' + name)
            body = body.replace('HostDiscoveryService::', 'service_')
            body = body.replace('m_backend.SearchHosts()', 'moonlight_api_search_hosts()')
            body = body.replace('m_backend.StopHostSearch()', 'moonlight_api_stop_host_search()')
            bodies.append(body)
        for name in ('clear_hosts', 'emit_event', 'discovery_thread',
                     'legacy_host_discovery_start', 'legacy_host_discovery_stop',
                     'legacy_host_discovery_is_running', 'legacy_host_discovery_shutdown'):
            bodies.append(function(native, name))
        for name in ('on_discovery_event', 'moonlight_api_search_hosts', 'moonlight_api_stop_host_search'):
            bodies.append(function(api, name))
        search = (ROOT / 'src/pages/page_search.cpp').read_text()
        body = search[search.index('    int result = MoonlightApp::Instance()->Discovery().Start();'):]
        body = body[:body.index('\n}')]
        body = body.replace('MoonlightApp::Instance()->Discovery().Start()', 'service_Start()')
        body = body.replace('MoonlightApp::Instance()->Connection().ShowError(', 'show_error(')
        bodies.append('void search_open(void) {\n' + body + '\n}')
        source = '\n'.join(bodies)
        source = re.sub(r'//[^\n]*', '', strip_directives(source))
        preamble = '''typedef int SceSize, SceUInt, LegacyHostDiscoveryEventType;
        typedef void (*LegacyHostDiscoveryCallback)(int, void *, void *);
        typedef struct {int id; char *internal;} MoonlightHost;'''
        unit = c_parser.CParser().parse(preamble + source)
        cls.functions = {item.decl.name: item for item in unit.ext if isinstance(item, c_ast.FuncDef)}

    def setUp(self):
        self.events, self.errors, self.calls = [], [], []
        self.dead = False
        self.create_result, self.start_result = 10, 0
        self.interrupt_on_poll = False
        self.wait_failures = 0
        self.original_expression, self.original_execute = runtime.expression, runtime.execute
        self.values = dict(NULL=0, DISCOVERY_IDLE=0, DISCOVERY_RUNNING=1, DISCOVERY_STOP_REQUESTED=2,
                           LEGACY_HOST_DISCOVERY_FINISHED=1, LEGACY_HOST_DISCOVERY_FOUND=0,
                           MOONLIGHT_EVENT_HOST_SCAN_STARTED=10, MOONLIGHT_EVENT_HOST_SCAN_FINISHED=11,
                           MOONLIGHT_EVENT_HOSTS_CHANGED=12, MOONLIGHT_EVENT_HOST_SCAN_FAILED=13,
                           LEGACY_HOST_DISCOVERY_DURATION_TICKS=100,
                           LEGACY_HOST_DISCOVERY_TICK_USEC=100000,
                           s_status=0, s_thread=-1, s_suppress_events=0, s_host_count=0,
                           s_hosts=[], s_userdata=0, on_mdns_host=99,
                           memset=lambda *args: None, sceKernelCreateThread=self.create,
                           sceKernelStartThread=self.start, sceKernelWaitThreadEnd=self.wait,
                           sceKernelDeleteThread=self.delete, sceKernelDelayThread=lambda *args: None,
                           udp_sniffer_vita_init=lambda: self.calls.append('network_init'),
                           udp_sniffer_vita_deinit=lambda: self.calls.append('network_deinit'),
                           udp_sniffer_vita_set_callback=lambda *args: None,
                           udp_sniffer_vita_poll=self.poll,
                           emit=lambda *args: self.events.append(args),
                           show_error=lambda *args: self.errors.append(args))
        for name in self.functions:
            self.values[name] = lambda *args, name=name: self.invoke(name, *args)
        self.values['s_callback'] = lambda kind, host, userdata: self.invoke(
            'on_discovery_event', kind, host, userdata)

    def invoke(self, name, *args):
        declaration = self.functions[name].decl.type.args
        parameters = declaration.params if declaration else []
        values = Scope(self.values, dict(zip((arg.name for arg in parameters), args)))
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            try:
                self.execute(self.functions[name].body, values)
            except runtime.Returned as result:
                return result.value

    def expression(self, node, values):
        if isinstance(node, c_ast.Constant) and node.type == 'int':
            return int(node.value, 0)
        if isinstance(node, c_ast.UnaryOp) and node.op == '++':
            value = self.expression(node.expr, values) + 1
            values[node.expr.name] = value
            return value
        return self.original_expression(node, values)

    def execute(self, node, values):
        if isinstance(node, (c_ast.UnaryOp, c_ast.Cast)):
            self.expression(node, values)
        elif isinstance(node, (c_ast.While, c_ast.DoWhile)):
            iterations = 0
            first = isinstance(node, c_ast.DoWhile)
            while first or self.expression(node.cond, values):
                first = False
                iterations += 1
                self.assertLess(iterations, 1000)
                self.execute(node.stmt, values)
        else:
            self.original_execute(node, values)

    def create(self, *args):
        self.calls.append('create')
        self.dead = False
        return self.create_result

    def start(self, *args):
        self.calls.append('start')
        return self.start_result

    def poll(self):
        self.calls.append('poll')
        if self.interrupt_on_poll:
            self.values['s_status'] = 2

    def finish_scan(self):
        self.invoke('discovery_thread', 0, 0)
        self.dead = True

    def wait(self, *args):
        self.calls.append('wait')
        if self.wait_failures:
            self.wait_failures -= 1
            return -5
        if not self.dead:
            self.finish_scan()
        return 0

    def delete(self, *args):
        self.calls.append('delete')
        # Failed StartThread has no running worker; otherwise join must precede deletion.
        self.assertTrue(self.dead or self.start_result < 0)
        return 0

    def test_start_only_schedules_native_scan_and_is_idempotent_while_running(self):
        self.assertEqual(self.invoke('service_Start'), 0)
        self.assertEqual(self.calls, ['create', 'start'])
        self.assertEqual(self.values['s_status'], 1)
        self.assertEqual(self.invoke('service_Start'), 0)
        self.assertEqual(self.calls, ['create', 'start'])
        self.assertEqual([event[0] for event in self.events], [10])

    def test_natural_completion_allows_restart_and_reaps_old_worker(self):
        self.invoke('service_Start')
        self.finish_scan()
        self.assertEqual(self.values['s_status'], 0)
        self.assertEqual([event[0] for event in self.events], [10, 11])
        self.calls.clear()
        self.create_result = 20
        self.assertEqual(self.invoke('service_Start'), 0)
        self.assertEqual(self.calls, ['wait', 'delete', 'create', 'start'])
        self.assertEqual(self.values['s_thread'], 20)

    def test_finished_notification_before_thread_return_is_joined_before_restart(self):
        self.invoke('service_Start')
        self.values['s_status'] = 0  # worker published FINISHED, not yet returned
        self.dead = False
        self.calls.clear()
        self.assertEqual(self.invoke('service_Start'), 0)
        self.assertLess(self.calls.index('wait'), self.calls.index('delete'))
        self.assertLess(self.calls.index('delete'), self.calls.index('create'))

    def test_create_error_reaches_search_once_and_retry_is_not_stuck(self):
        self.create_result = -123
        self.invoke('search_open')
        self.assertEqual(self.errors, [('Unable to start PC search.', -123)])
        self.assertEqual(self.values['s_status'], 0)
        self.assertEqual(self.values['s_thread'], -1)
        self.assertNotIn(13, [event[0] for event in self.events])  # no delayed second error
        self.create_result = 10
        self.assertEqual(self.invoke('service_Start'), 0)
        self.assertEqual(self.values['s_status'], 1)

    def test_start_thread_error_is_exact_and_failed_uid_is_deleted(self):
        self.start_result = -234
        self.invoke('search_open')
        self.assertEqual(self.errors, [('Unable to start PC search.', -234)])
        self.assertEqual(self.calls, ['create', 'start', 'delete'])
        self.assertEqual((self.values['s_status'], self.values['s_thread']), (0, -1))
        self.start_result = 0
        self.assertEqual(self.invoke('service_Start'), 0)

    def test_stop_reaps_even_completed_or_not_yet_scheduled_native_worker(self):
        for natural in (False, True):
            self.setUp()
            self.invoke('service_Start')
            if natural:
                self.finish_scan()
            self.calls.clear()
            self.assertEqual(self.invoke('service_Stop'), 0)
            self.assertEqual((self.values['s_status'], self.values['s_thread']), (0, -1))
            self.assertEqual(self.calls[0], 'wait')
            self.assertEqual(self.calls[-1], 'delete')
            before = list(self.calls)
            self.invoke('service_Stop')
            self.assertEqual(self.calls, before)
            self.assertEqual([event[0] for event in self.events], [10, 11] if natural else [10])

    def test_cancelled_scan_cleans_network_without_finished_event(self):
        self.invoke('service_Start')
        self.interrupt_on_poll = True
        self.finish_scan()
        self.assertEqual(self.values['s_status'], 0)
        self.assertEqual(self.calls.count('poll'), 1)
        self.assertEqual(self.calls[-1], 'network_deinit')
        self.assertEqual([event[0] for event in self.events], [10])

    def test_stop_waits_through_transient_timeout_and_shutdown_clears_callback(self):
        self.invoke('service_Start')
        self.wait_failures = 1
        self.invoke('legacy_host_discovery_shutdown')
        self.assertEqual(self.calls.count('wait'), 2)
        self.assertEqual(self.values['s_callback'], 0)
        self.assertEqual(self.values['s_thread'], -1)

    def test_service_has_no_queued_start_or_shadow_running_state(self):
        header = (ROOT / 'include/services/host_discovery_service.h').read_text()
        self.assertNotIn('m_running', header + self.service)
        self.assertNotIn('DiscoveryJob', self.service)
        self.assertNotIn('Enqueue', self.service)
        self.assertNotIn('paf::job', self.service)


if __name__ == '__main__':
    unittest.main()
