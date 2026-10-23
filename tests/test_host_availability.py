"""Interpret the actual TCP probe/cache predicates with mocked Vita calls.

No compiler, sockets, DNS, threads, PAF execution or hardware validation.
"""
import copy
import itertools
import re
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives
from test_host_persistence import CString


class Jump(Exception):
    pass


class HostAvailability(unittest.TestCase):
    def probe(self, **options):
        source = (ROOT / 'src/backend/legacy/legacy_host_probe.c').read_text()
        types = '''typedef unsigned long long uint64_t;
        typedef int MoonlightCancellation, SceNetSockaddr;
        typedef struct { int sin_len, sin_family, sin_port, sin_addr; } SceNetSockaddrIn;
        typedef struct { int events; } SceNetEpollEvent;
        typedef struct { char internal[256]; int port; } MoonlightHost;'''
        tree = c_parser.CParser().parse(types + strip_directives(source))
        body = tree.ext[-1].body
        values = dict(NULL=0, host=dict(internal=CString('pc.example'), port=47989),
                      cancel=1, cancelled=False, errno_value=36, now=0)
        values.update({key: val for key, val in zip(
            ('SCE_NET_AF_INET', 'SCE_NET_SOCK_STREAM', 'SCE_NET_SOL_SOCKET',
             'SCE_NET_SO_NBIO', 'SCE_NET_SO_ERROR', 'SCE_NET_EINPROGRESS',
             'SCE_NET_EPOLL_CTL_ADD', 'SCE_NET_EPOLLOUT', 'SCE_NET_EPOLLERR', 'SCE_NET_EPOLLHUP'),
            (2, 1, 65535, 1, 2, 36, 1, 2, 8, 16))})
        calls = []
        original_expression, original_execute = runtime.expression, runtime.execute

        def expression(node, scope):
            if isinstance(node, c_ast.UnaryOp):
                if node.op == '&' and isinstance(node.expr, c_ast.StructRef):
                    return (expression(node.expr.name, scope), node.expr.field.name)
                if node.op == '*':
                    return expression(node.expr, scope)
            if isinstance(node, c_ast.BinaryOp) and node.op in ('&', '|'):
                a, b = expression(node.left, scope), expression(node.right, scope)
                return a & b if node.op == '&' else a | b
            return original_expression(node, scope)

        def execute(node, scope):
            if isinstance(node, c_ast.Goto):
                self.assertEqual(node.name, 'done')
                raise Jump()
            if isinstance(node, c_ast.Label):
                execute(node.stmt, scope)
            elif isinstance(node, c_ast.While):
                iterations = 0
                while expression(node.cond, scope):
                    iterations += 1
                    self.assertLess(iterations, 200)
                    try:
                        execute(node.stmt, scope)
                    except runtime.BreakSwitch:
                        break
            else:
                original_execute(node, scope)

        def stage(name, default):
            def call(*args):
                calls.append(name)
                return options.get(name, default)
            return call

        def memset(target, *args):
            values[target] = {}

        def resolve(*args):
            calls.append('dns')
            self.assertEqual(args[3:], (1, 1, 0))
            if options.get('cancel_dns'): values['cancelled'] = True
            return options.get('dns', 0)

        def ready(eid, event, count, timeout):
            calls.append('poll')
            self.assertEqual(timeout, 0)
            values[event]['events'] = options.get('events', 2)
            if options.get('cancel_poll'): values['cancelled'] = True
            return options.get('poll', 1)

        def sock_error(sock, level, option, output, size):
            calls.append('so_error')
            values[output] = options.get('socket_error', 0)
            return options.get('getsockopt', 0)

        def clock():
            values['now'] += 100000
            return values['now']

        values.update(
            moonlight_is_cancelled=lambda token: values['cancelled'],
            sceNetHtons=lambda port: port,
            sceNetInetPton=stage('numeric', 1), memset=memset,
            sceNetResolverCreate=stage('resolver', 4), sceNetResolverStartNtoa=resolve,
            sceNetResolverDestroy=stage('destroy_resolver', 0),
            sceNetSocket=stage('socket', 5), sceNetSetsockopt=stage('nonblocking', 0),
            sceNetConnect=stage('connect', -1), sceNetErrnoLoc=lambda: options.get('errno', 36),
            sceNetEpollCreate=stage('epoll', 6), sceNetEpollControl=stage('register', 0),
            sceNetEpollWait=ready, sceNetGetsockopt=sock_error,
            sceNetEpollDestroy=stage('destroy_epoll', 0), sceNetSocketClose=stage('close', 0),
            sceKernelGetProcessTimeWide=clock, sceKernelDelayThread=stage('delay', 0))
        values['cancelled'] = options.get('cancelled', False)
        if options.get('empty'): values['host']['internal'] = CString()
        with patch.object(runtime, 'expression', expression), patch.object(runtime, 'execute', execute):
            try:
                try:
                    execute(body, values)
                except Jump:
                    statements = body.block_items
                    index = next(i for i, n in enumerate(statements) if isinstance(n, c_ast.Label))
                    for statement in statements[index:]: execute(statement, values)
            except runtime.Returned as result:
                return result.value, calls
        self.fail('Probe did not return')

    def test_only_successful_connect_or_writable_zero_error_is_positive(self):
        self.assertEqual(self.probe(connect=0)[0], 1)
        for events, error, api_result in itertools.product((2, 8, 16, 10, 18), (0, 61), (0, -1)):
            with self.subTest(events=events, error=error, api_result=api_result):
                result, calls = self.probe(events=events, socket_error=error, getsockopt=api_result)
                expected = (-1 if api_result < 0 else int(error == 0)) if events == 2 else 0
                self.assertEqual(result, expected)
                self.assertEqual(calls[-2:], ['destroy_epoll', 'close'])
                self.assertEqual(calls.count('close'), 1)

    def test_timeout_and_cancel_never_publish_green_and_close_resources(self):
        for options, expected in ((dict(poll=0), 0), (dict(cancel_poll=True), -2),
                                  (dict(cancelled=True), -2), (dict(empty=True), -1)):
            with self.subTest(options=options):
                result, calls = self.probe(**options)
                self.assertEqual(result, expected)
                self.assertEqual(calls.count('close'), int('socket' in calls))
                self.assertEqual(calls.count('destroy_epoll'), int('epoll' in calls))

    def test_local_failures_and_refusal_close_only_acquired_handles(self):
        for stage in ('socket', 'nonblocking', 'epoll', 'register', 'poll'):
            with self.subTest(stage=stage):
                result, calls = self.probe(**{stage: -1})
                self.assertEqual(result, -1)
                self.assertEqual(calls.count('close'), int(stage != 'socket'))
                self.assertEqual(calls.count('destroy_epoll'), int(stage in ('register', 'poll')))
        result, calls = self.probe(errno=61)
        self.assertEqual(result, 0)
        self.assertNotIn('epoll', calls)
        self.assertEqual(calls[-1], 'close')

    def test_hostname_resolution_failure_and_cancel_destroy_resolver(self):
        for options, expected in ((dict(), 1), (dict(dns=-1), 0),
                                  (dict(cancel_dns=True), -2), (dict(resolver=-1), -1)):
            with self.subTest(options=options):
                result, calls = self.probe(numeric=0, **options)
                self.assertEqual(result, expected)
                self.assertEqual(calls.count('destroy_resolver'), int('resolver' not in options))
                if expected <= 0: self.assertNotIn('socket', calls)

    def test_failed_submission_preserves_barrier_and_connect_waits_in_worker(self):
        service = (ROOT / 'src/services/host_service.cpp').read_text()
        branch = re.search(r'    if \(m_queue->Enqueue.*?\n    }\n}', service, re.S).group()[:-2]
        branch = branch.replace('m_queue->Enqueue', 'enqueue')
        branch = branch.replace('AvailabilityJob *job = static_cast<AvailabilityJob *>(m_job.get())',
                                'int job = m_job')
        branch = branch.replace('job->cancel', 'job_cancel').replace('job->done', 'job_done')
        body = c_parser.CParser().parse('void submit(void) {' + branch + '}').ext[0].body
        for failure in (-1, 0):
            calls = []
            values = dict(m_job=2, m_barrier=1, __ATOMIC_RELEASE=0,
                          enqueue=lambda item: failure,
                          moonlight_cancel=lambda token: calls.append('cancel'),
                          __atomic_store_n=lambda *args: calls.append('done'))
            runtime.execute(body, values)
            self.assertEqual(values['m_barrier'], 1 if failure < 0 else 2)
            self.assertEqual(calls, ['cancel', 'done'] if failure < 0 else [])
        connection = (ROOT / 'src/services/connection_service.cpp').read_text()
        worker = connection.split('virtual void Run()', 1)[1].split('virtual void Finish()', 1)[0]
        self.assertLess(worker.index('WaitForAvailability'), worker.index('m_backend.ConnectHost'))
        begin = connection.split('int ConnectionService::BeginOperation(', 1)[1].split('\n}', 1)[0]
        self.assertIn('m_hosts.CancelAvailability()', begin)
        self.assertNotIn('WaitForAvailability', begin)
        app = (ROOT / 'src/app/moonlight_app.cpp').read_text().split('void MoonlightApp::Shutdown()', 1)[1]
        self.assertLess(app.index('m_hosts.Shutdown()'), app.index('m_backend.Shutdown()'))
        self.assertLess(app.index('m_backend.Shutdown()'), app.index('ShutdownNetwork()'))

    def test_freshness_and_identity_use_actual_predicates(self):
        source = (ROOT / 'src/services/host_service.cpp').read_text()
        predicate = re.search(r'    bool Available\(.*?\n    }', source, re.S).group()
        predicate = predicate.replace('const Entry &entry', 'const Entry *entry').replace(') const', ')')
        refresh = re.search(r'    bool RefreshDue\(.*?\n    }', source, re.S).group().replace(') const', ')')
        identity = re.search(r'static bool same_probe_host\(.*?\n}', source, re.S).group()
        identity = re.sub(r'const MoonlightHost &(\w+)', r'const MoonlightHost *\1', identity)
        types = '''typedef int bool; typedef struct { char host_id[33], internal[256]; int port; } MoonlightHost;
        typedef unsigned long long uint64_t;
        typedef struct { int result; uint64_t checked_at, previous_checked_at; bool previous_available; } Entry;'''
        unit = c_parser.CParser().parse(types + strip_directives(identity + predicate + refresh))
        bodies = {n.decl.name: n.body for n in unit.ext if isinstance(n, c_ast.FuncDef)}
        original = runtime.expression
        def expression(node, scope):
            if isinstance(node, c_ast.UnaryOp) and node.op == '&':
                return original(node.expr, scope)
            return original(node, scope)
        values = dict(false=0, cancel=1, __ATOMIC_ACQUIRE=0,
                      __atomic_load_n=lambda value, order: value,
                      strcmp=lambda a,b: 0 if a==b else 1)
        def run(name, **fields):
            scope = dict(values, **fields)
            try: runtime.execute(bodies[name], scope)
            except runtime.Returned as result: return bool(result.value)
        with patch.object(runtime, 'expression', expression):
            for result, age, cancelled in itertools.product((0, 1, 2, -1, -2), (0, 29999999, 30000000), (0,1)):
                actual = run('Available', entry=dict(result=result, checked_at=100, previous_available=0),
                             moonlight_is_cancelled=lambda token: cancelled,
                             sceKernelGetProcessTimeWide=lambda: 100+age)
                self.assertEqual(actual, result == 1 and age < 30000000 and not cancelled)
            # A pending refresh keeps the previous positive only until its original expiry.
            # Negative/local/cancel results must override that fallback immediately.
            for result, age, cancelled in itertools.product((0, 1, 2, -1, -2), (15000000, 29999999, 30000000), (0, 1)):
                observation = dict(result=result, previous_available=1, previous_checked_at=100)
                # The worker's timestamp is not safe to read while its result is pending.
                if result != 0: observation['checked_at'] = 100
                actual = run('Available', entry=observation,
                             moonlight_is_cancelled=lambda token: cancelled,
                             sceKernelGetProcessTimeWide=lambda: 100+age)
                self.assertEqual(actual, result in (0, 1) and age < 30000000 and not cancelled)
            for done, age in itertools.product((0, 1), (0, 14999999, 15000000, 30000000)):
                self.assertEqual(run('RefreshDue', done=done, started_at=100,
                                     sceKernelGetProcessTimeWide=lambda: 100+age),
                                 bool(done and age >= 15000000))
            a = dict(host_id='uuid', internal='192.0.2.1', port=47989)
            self.assertTrue(run('same_probe_host', a=a, b=copy.deepcopy(a)))
            for field in a:
                b = dict(a); b[field] = 47990 if field=='port' else 'replacement'
                self.assertFalse(run('same_probe_host', a=a, b=b))


if __name__ == '__main__':
    unittest.main()
