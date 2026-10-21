"""Interpret pending-test main flow/checkpoint with mocked atomics, time and services."""
import re
import unittest

from pycparser import c_ast, c_parser
from test_adapter_lifecycle import Returned, execute
from test_disconnect_runtime import ScenarioHarness
from test_xml_source import ROOT, strip_directives


class PendingDisconnectRuntime(ScenarioHarness, unittest.TestCase):
    source_name = 'disconnect_pending.cpp'
    extra_functions = ('release_checkpoint', 'disconnect_test_capture_start',
                       'disconnect_test_pending_checkpoint')

    def setUp(self):
        super().setUp()
        self.values.update(s_saw_cancelled=0, s_application_id=-1,
                           s_checkpoint_used=0, s_checkpoint_release=0,
                           s_checkpoint_status=0, s_checkpoint_app=-1,
                           __ATOMIC_RELEASE=1, __ATOMIC_ACQUIRE=2,
                           __ATOMIC_RELAXED=3, __ATOMIC_ACQ_REL=4)
        self.values['__atomic_load_n'] = lambda key, order: self.values[key]
        self.values['__atomic_store_n'] = lambda key, value, order: self.values.update({key: value})

        def exchange(key, value, order):
            previous = self.values[key]
            self.values[key] = value
            return previous

        self.values['__atomic_exchange_n'] = exchange
        self.values['release_checkpoint'] = lambda: self.run_body('release_checkpoint')
        self.delay_calls = []
        self.values['sceKernelDelayThread'] = lambda us: self.delay_calls.append(us)
        self.starts, self.targets = [], []
        self.results['start'] = 0
        self.values['start_request'] = lambda app_id: (
            self.starts.append(app_id) or self.calls.append('start') or self.results['start'])
        self.values['connect_request'] = lambda host: (
            self.targets.append(dict(host)) or self.calls.append('connect') or self.results['connect'])

    def event(self, kind, result=0, address='test-host', app_id=42):
        self.run_body('disconnect_test_event', event=dict(
            type=self.values['MOONLIGHT_EVENT_' + kind], result=result,
            address=address, application_id=app_id))

    def start_accepted(self):
        self.arm()
        self.values['busy'] = 1
        self.run_body('disconnect_test_capture_start', application_id=42, result=0)

    def checkpoint_reached(self):
        self.start_accepted()
        self.values.update(s_checkpoint_status=1, s_checkpoint_app=42)
        self.run_body('disconnect_test_poll')

    def cancelled_closed(self):
        # As with active stop, the backend may already be disconnected when PAF drains failure.
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('STREAM_FAILED', -7)
        self.event('CONNECTION_CLOSED')

    def recover(self):
        self.cancelled_closed()
        self.values['busy'] = 0  # Normal native error was dismissed by the user.
        self.run_body('disconnect_test_poll')
        self.run_body('disconnect_test_poll')
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_PAIRED']
        self.event('CONNECTION_READY')
        self.run_body('disconnect_test_poll')
        self.event('APPLICATIONS_READY', 4)
        self.run_body('disconnect_test_poll')

    def test_waits_for_actual_checkpoint_then_disconnects_while_launch_busy(self):
        self.arm()
        self.values['now'] = self.timeout * 2
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.run_body('disconnect_test_capture_start', application_id=42, result=0)
        self.values['busy'] = 1
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.values.update(s_checkpoint_status=1, s_checkpoint_app=42)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.assertEqual(self.values['s_checkpoint_release'], 1)
        self.assertFalse(any('PASS' in line for line in self.logs))

    def test_worker_checkpoint_waits_for_main_request_and_only_holds_once(self):
        self.start_accepted()
        seen = []

        def delay(us):
            seen.append(self.values['s_checkpoint_status'])
            self.assertEqual(self.values['s_checkpoint_app'], 42)
            self.run_body('disconnect_test_poll')

        self.values['sceKernelDelayThread'] = delay
        self.run_body('disconnect_test_pending_checkpoint', application_id=42)
        self.assertEqual(seen, [1])
        self.assertEqual(self.calls, ['disconnect'])
        self.assertEqual(self.values['s_checkpoint_status'], 2)
        self.run_body('disconnect_test_pending_checkpoint', application_id=42)
        self.assertEqual(seen, [1])

    def test_checkpoint_timeouts_release_worker_and_prevent_pass(self):
        self.start_accepted()
        self.values['sceKernelDelayThread'] = lambda us: self.values.update(
            now=self.values['now'] + self.timeout + 1)
        self.run_body('disconnect_test_pending_checkpoint', application_id=42)
        self.assertEqual(self.values['s_checkpoint_status'], -1)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.assertIn('FAIL', self.logs[-1])
        self.assertEqual(self.values['s_checkpoint_release'], 1)

    def test_failure_then_closed_ack_and_same_target_restart_event_required(self):
        self.checkpoint_reached()
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('STREAM_FAILED', -7)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.event('CONNECTION_CLOSED')
        self.values['now'] += self.timeout * 2
        self.run_body('disconnect_test_poll')  # Error/OK is user-paced, not a backend timeout.
        self.assertEqual(self.calls, ['disconnect'])
        self.assertNotIn('FAIL', self.logs[-1])
        self.values['busy'] = 0
        self.run_body('disconnect_test_poll')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.targets, [{'internal': 'test-host', 'port': 47989}])
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_PAIRED']
        self.event('CONNECTION_READY')
        self.run_body('disconnect_test_poll')
        self.event('APPLICATIONS_READY', 4)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect', 'connect', 'refresh', 'start'])
        self.assertEqual(self.starts, [42])
        self.assertFalse(any('PASS' in line for line in self.logs))
        self.run_body('disconnect_test_capture_start', application_id=42, result=0)
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
        self.event('STREAM_STARTED')
        self.assertIn('PASS pending', self.logs[-1])
        self.run_body('disconnect_test_poll')
        self.event('STREAM_STOPPED')
        self.assertEqual(len(self.calls), 4)
        self.assertIn('PASS pending', self.logs[-1])

    def test_closed_without_cancel_event_never_reconnects(self):
        self.checkpoint_reached()
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('CONNECTION_CLOSED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.assertIn('FAIL', self.logs[-1])

    def test_early_start_or_wrong_cancel_result_app_host_never_passes(self):
        for kind, kwargs in (
                ('STREAM_STARTED', {}),
                ('STREAM_STOPPED', {}),
                ('STREAM_FAILED', dict(result=-8)),
                ('STREAM_FAILED', dict(result=-7, app_id=43)),
                ('STREAM_FAILED', dict(result=-7, address='other-host'))):
            with self.subTest(kind=kind, kwargs=kwargs):
                self.setUp()
                self.checkpoint_reached()
                self.event(kind, **kwargs)
                self.run_body('disconnect_test_poll')
                self.assertEqual(self.calls, ['disconnect'])
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.values['s_checkpoint_release'], 1)

    def test_checkpoint_mismatch_request_rejection_and_main_timeout_release_barrier(self):
        for failure in ('wrong_app', 'not_pending', 'reject', 'timeout'):
            with self.subTest(failure=failure):
                self.setUp()
                self.start_accepted()
                self.values.update(s_checkpoint_status=1, s_checkpoint_app=42)
                if failure == 'wrong_app':
                    self.values['s_checkpoint_app'] = 43
                elif failure == 'not_pending':
                    self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
                elif failure == 'reject':
                    self.results['disconnect'] = -1
                else:
                    self.values['now'] += self.timeout + 1
                self.run_body('disconnect_test_poll')
                self.assertEqual(self.calls, ['disconnect'] if failure == 'reject' else [])
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.values['s_checkpoint_release'], 1)

    def test_start_errors_user_connect_and_shutdown_release_checkpoint(self):
        for action in ('initial_error', 'other_connect', 'shutdown'):
            with self.subTest(action=action):
                self.setUp()
                self.arm()
                if action == 'initial_error':
                    self.run_body('disconnect_test_capture_start', application_id=42, result=-1)
                elif action == 'other_connect':
                    self.run_body('disconnect_test_capture_host')
                else:
                    self.run_body('disconnect_test_shutdown')
                self.run_body('disconnect_test_poll')
                self.assertEqual(self.calls, [])
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.values['s_checkpoint_release'], 1)
                self.run_body('disconnect_test_pending_checkpoint', application_id=42)
                self.assertEqual(self.delay_calls, [])

    def test_recovery_wrong_trust_host_restart_id_or_failure_cannot_pass(self):
        for failure in ('trust', 'host', 'restart_reject', 'restart_app', 'restart_failed'):
            with self.subTest(failure=failure):
                self.setUp()
                self.checkpoint_reached()
                if failure in ('trust', 'host'):
                    self.cancelled_closed()
                    self.values['busy'] = 0
                    self.run_body('disconnect_test_poll')
                    self.run_body('disconnect_test_poll')
                    self.values['state'] = self.values['MOONLIGHT_CONNECTION_READY']
                    self.event('CONNECTION_READY', address='other-host' if failure == 'host' else 'test-host')
                else:
                    self.results['start'] = -1 if failure == 'restart_reject' else 0
                    self.recover()
                    if failure == 'restart_app':
                        self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
                        self.event('STREAM_STARTED', app_id=43)
                    elif failure == 'restart_failed':
                        self.event('STREAM_FAILED', -7)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertFalse(any('PASS' in line for line in self.logs))

    def test_real_stream_cancel_check_still_runs_after_successful_response_checkpoint(self):
        source = (ROOT / 'src/moonlight/stream_session.cpp').read_text()
        start = source.index('static int run_stream_session(')
        hook = source.index('disconnect_test_pending_checkpoint(application_id);', start)
        check = source.index('    if (result != 0) return result;', start)
        end = source.index('    LiInitializeStreamConfiguration', check)
        self.assertLess(check, hook)
        block = strip_directives(source[check:end])
        unit = c_parser.CParser().parse('int check_stage(int result) {' + block + 'return 0;}')
        body = next(item.body for item in unit.ext if isinstance(item, c_ast.FuncDef))
        for result, cancel, expected, calls in ((0, 1, -7, [42]), (0, 0, 0, [42]), (-8, 1, -8, [])):
            with self.subTest(result=result, cancel=cancel):
                checkpoint = []
                values = dict(result=result, application_id=42,
                              disconnect_test_pending_checkpoint=lambda app_id: checkpoint.append(app_id),
                              stream_cancel_requested=lambda: cancel)
                with self.assertRaises(Returned) as returned:
                    execute(body, values)
                self.assertEqual(returned.exception.value, expected)
                self.assertEqual(checkpoint, calls)


if __name__ == '__main__':
    unittest.main()
