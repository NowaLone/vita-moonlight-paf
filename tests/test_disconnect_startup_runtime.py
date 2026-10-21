"""Interpret startup-test source with mocked atomics/services; no real common-c or PAF."""
import re
import unittest

from pycparser import c_ast, c_parser
from test_adapter_lifecycle import Returned, execute
from test_disconnect_runtime import ScenarioHarness
from test_xml_source import ROOT, strip_directives


class StartupDisconnectRuntime(ScenarioHarness, unittest.TestCase):
    source_name = 'disconnect_startup.cpp'
    extra_functions = ('release_checkpoint', 'disconnect_test_capture_start',
                       'disconnect_test_startup_checkpoint')

    def setUp(self):
        super().setUp()
        self.values.update(s_saw_started=0, s_saw_stopped=0, s_application_id=-1,
                           s_checkpoint_used=0, s_checkpoint_release=0, s_checkpoint_status=0,
                           STAGE_RTSP_HANDSHAKE=4, __ATOMIC_RELEASE=1, __ATOMIC_ACQUIRE=2,
                           __ATOMIC_ACQ_REL=3)
        self.values['__atomic_load_n'] = lambda key, order: self.values[key]
        self.values['__atomic_store_n'] = lambda key, value, order: self.values.update({key: value})

        def exchange(key, value, order):
            previous = self.values[key]
            self.values[key] = value
            return previous

        self.values['__atomic_exchange_n'] = exchange
        self.values['release_checkpoint'] = lambda: self.run_body('release_checkpoint')
        self.delays, self.starts, self.targets = [], [], []
        self.values['sceKernelDelayThread'] = lambda us: self.delays.append(us)
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

    def request_disconnect(self):
        self.start_accepted()
        self.values['s_checkpoint_status'] = 1
        self.run_body('disconnect_test_poll')

    def closed(self):
        # The adapter can already be disconnected while PAF drains STARTED/STOPPED/CLOSED.
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('STREAM_STARTED')
        self.event('STREAM_STOPPED')
        self.event('CONNECTION_CLOSED')

    def recover(self):
        self.closed()
        self.values['busy'] = 0
        self.run_body('disconnect_test_poll')
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_PAIRED']
        self.event('CONNECTION_READY')
        self.run_body('disconnect_test_poll')
        self.event('APPLICATIONS_READY', 4)
        self.run_body('disconnect_test_poll')

    def test_stage_filter_and_one_shot_worker_gate_request_disconnect_before_release(self):
        self.start_accepted()
        for stage in (1, 2, 3, 5, 11):
            self.run_body('disconnect_test_startup_checkpoint', stage=stage)
        self.assertEqual(self.values['s_checkpoint_used'], 0)
        self.assertEqual(self.delays, [])
        seen = []

        def delay(us):
            seen.append(self.values['s_checkpoint_status'])
            self.assertEqual(self.values['s_checkpoint_release'], 0)
            self.run_body('disconnect_test_poll')
            self.assertEqual(self.calls, ['disconnect'])

        self.values['sceKernelDelayThread'] = delay
        self.run_body('disconnect_test_startup_checkpoint', stage=4)
        self.assertEqual(seen, [1])
        self.assertEqual(self.values['s_checkpoint_status'], 2)
        self.assertEqual(self.values['s_checkpoint_release'], 1)
        self.run_body('disconnect_test_startup_checkpoint', stage=4)
        self.assertEqual(seen, [1])

    def test_user_selection_wait_and_no_disconnect_until_checkpoint(self):
        self.arm()
        self.values['now'] = self.timeout * 2
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.values['busy'] = 1
        self.run_body('disconnect_test_capture_start', application_id=42, result=0)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.values['s_checkpoint_status'] = 1
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.assertNotIn('FAIL', self.logs[-1])

    def test_late_started_owner_stopped_closed_then_paired_recovery_and_actual_pass(self):
        self.request_disconnect()
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('STREAM_STARTED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.assertIn('late STREAM_STARTED', self.logs[-1])
        self.event('STREAM_STOPPED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.event('CONNECTION_CLOSED')
        self.run_body('disconnect_test_poll')  # Foreground busy still defers reconnect.
        self.assertEqual(self.calls, ['disconnect'])
        self.values['busy'] = 0
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
        self.assertIn('PASS startup owner STOPPED', self.logs[-1])
        self.assertIn('late_start=1', self.logs[-1])
        self.values['now'] += self.timeout * 2
        self.event('STREAM_STOPPED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(len(self.calls), 4)
        self.assertIn('PASS', self.logs[-1])

    def test_closed_before_owner_stopped_or_stop_before_late_start_cannot_pass(self):
        for sequence in (('CONNECTION_CLOSED',), ('STREAM_STOPPED',),
                         ('STREAM_STARTED', 'CONNECTION_CLOSED'),
                         ('STREAM_STARTED', 'STREAM_STARTED'),
                         ('STREAM_STARTED', 'STREAM_STOPPED', 'STREAM_STOPPED')):
            with self.subTest(sequence=sequence):
                self.setUp()
                self.request_disconnect()
                self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
                for event in sequence:
                    self.event(event)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.calls, ['disconnect'])

    def test_wrong_start_stop_or_closed_target_result_rejects_recovery(self):
        for event, kwargs in (
                ('STREAM_STARTED', dict(app_id=43)), ('STREAM_STARTED', dict(address='other')),
                ('STREAM_STARTED', dict(result=-1)), ('STREAM_STOPPED', dict(app_id=43)),
                ('STREAM_STOPPED', dict(result=-1)), ('CONNECTION_CLOSED', dict(address='other')),
                ('CONNECTION_CLOSED', dict(result=-1))):
            with self.subTest(event=event, kwargs=kwargs):
                self.setUp()
                self.request_disconnect()
                self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
                if event != 'STREAM_STARTED':
                    self.event('STREAM_STARTED')
                if event == 'CONNECTION_CLOSED':
                    self.event('STREAM_STOPPED')
                self.event(event, **kwargs)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.calls, ['disconnect'])

    def test_real_setup_failures_are_preserved_not_normalized_to_cancel_or_pass(self):
        for result in (-1, -7, -10, 123):
            with self.subTest(result=result):
                self.setUp()
                self.request_disconnect()
                self.event('STREAM_FAILED', result)
                self.event('CONNECTION_CLOSED')
                self.run_body('disconnect_test_poll')
                self.assertIn(f'result={result}', self.logs[-1])
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.calls, ['disconnect'])

    def test_checkpoint_worker_deadline_and_main_failures_release_barrier(self):
        self.start_accepted()
        self.values['sceKernelDelayThread'] = lambda us: self.values.update(
            now=self.values['now'] + self.timeout + 1)
        self.run_body('disconnect_test_startup_checkpoint', stage=4)
        self.assertEqual(self.values['s_checkpoint_status'], -1)
        self.run_body('disconnect_test_poll')
        self.assertIn('FAIL', self.logs[-1])
        self.assertEqual(self.values['s_checkpoint_release'], 1)
        for failure in ('reject', 'not_busy', 'wrong_state', 'main_timeout', 'shutdown'):
            with self.subTest(failure=failure):
                self.setUp()
                self.start_accepted()
                self.values['s_checkpoint_status'] = 1
                if failure == 'reject':
                    self.results['disconnect'] = -1
                elif failure == 'not_busy':
                    self.values['busy'] = 0
                elif failure == 'wrong_state':
                    self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
                elif failure == 'main_timeout':
                    self.values['now'] += self.timeout + 1
                else:
                    self.run_body('disconnect_test_shutdown')
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.values['s_checkpoint_release'], 1)
                self.run_body('disconnect_test_startup_checkpoint', stage=4)
                self.assertEqual(self.delays, [])

    def test_early_start_other_connect_and_rejected_start_never_arm_recovery(self):
        for action in ('early_start', 'other_connect', 'reject_start'):
            with self.subTest(action=action):
                self.setUp()
                self.arm()
                if action == 'early_start':
                    self.event('STREAM_STARTED')
                elif action == 'other_connect':
                    self.run_body('disconnect_test_capture_host')
                else:
                    self.run_body('disconnect_test_capture_start', application_id=42, result=-1)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.calls, [])

    def test_recovery_requires_original_host_trust_app_and_success(self):
        for failure in ('trust', 'host', 'restart_reject', 'wrong_restart_app', 'wrong_restart_state', 'restart_failure'):
            with self.subTest(failure=failure):
                self.setUp()
                self.request_disconnect()
                if failure in ('trust', 'host'):
                    self.closed()
                    self.values['busy'] = 0
                    self.run_body('disconnect_test_poll')
                    self.values['state'] = self.values['MOONLIGHT_CONNECTION_READY']
                    self.event('CONNECTION_READY', address='other' if failure == 'host' else 'test-host')
                else:
                    self.results['start'] = -1 if failure == 'restart_reject' else 0
                    self.recover()
                    if failure == 'wrong_restart_app':
                        self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
                        self.event('STREAM_STARTED', app_id=43)
                    elif failure == 'wrong_restart_state':
                        self.event('STREAM_STARTED')
                    elif failure == 'restart_failure':
                        self.event('STREAM_FAILED', -1)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertFalse(any('PASS' in line for line in self.logs))

    def test_actual_stage_callback_is_test_guarded_and_does_not_interrupt_common_c(self):
        source = (ROOT / 'src/moonlight/stream_session.cpp').read_text()
        callback = source.split('static void on_stage_start(int stage)', 1)[1].split('static void on_stage_complete', 1)[0]
        self.assertIn('#ifdef MOONLIGHT_STARTUP_DISCONNECT_TEST\n    disconnect_test_startup_checkpoint(stage);\n#endif', callback)
        unit = c_parser.CParser().parse('void on_stage_start(int stage)' + strip_directives(callback))
        called = []
        values = dict(stage=4, LiGetStageName=lambda stage: 'RTSP handshake',
                      disconnect_test_startup_checkpoint=lambda stage: called.append(stage))
        execute(unit.ext[0].body, values)
        self.assertEqual(called, [4])
        runtime = (ROOT / 'tests/runtime' / self.source_name).read_text()
        self.assertNotIn('LiInterruptConnection(', runtime + source)
        self.assertNotIn('LiStopConnection(', runtime)
        common = (ROOT / 'third_party/moonlight-common-c/src/Connection.c').read_text()
        audio_init = common.index('ListenerCallbacks.stageComplete(STAGE_AUDIO_STREAM_INIT);')
        hook = common.index('ListenerCallbacks.stageStarting(STAGE_RTSP_HANDSHAKE);')
        handshake = common.index('err = performRtspHandshake(serverInfo);')
        self.assertLess(audio_init, hook)
        self.assertLess(hook, handshake)
        self.assertIn('callbacks->stageStarting = on_stage_start;', source)
        self.assertNotIn('disconnect_test_pending_checkpoint(application_id);', callback)

    def test_five_mode_exclusion_and_default_reset_cover_cached_options(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        options = re.findall(r'option\((MOONLIGHT_\w*DISCONNECT_TEST) "[^"]*" OFF\)', cmake)
        self.assertEqual(len(options), 5)
        condition = cmake.split('if((MOONLIGHT_STARTUP_DISCONNECT_TEST AND', 1)[1].split('\n  message(FATAL_ERROR', 1)[0]
        condition = '(MOONLIGHT_STARTUP_DISCONNECT_TEST AND' + condition.rstrip()[:-1]
        for bits in range(32):
            values = {name: bool(bits & (1 << index)) for index, name in enumerate(options)}
            expr = condition.replace('AND', 'and').replace('OR', 'or').replace('\n', ' ')
            self.assertEqual(eval(expr, {'__builtins__': {}}, values), bits.bit_count() > 1)
        script = (ROOT / 'build.sh').read_text().split('local -a profile_args=', 1)[1].split('"${CMAKE_ARGS[@]}"', 1)[0]
        for option in options:
            self.assertIn('-D' + option + '=OFF', script)
        branch = cmake.split('if(MOONLIGHT_STARTUP_DISCONNECT_TEST)', 1)[1].split('elseif(', 1)[0]
        self.assertIn('tests/runtime/disconnect_startup.cpp', branch)
        self.assertIn('MOONLIGHT_STARTUP_DISCONNECT_TEST=1', branch)
        self.assertNotIn('MOONLIGHT_PENDING_DISCONNECT_TEST=1', branch)
        self.assertNotIn('disconnect_pending.cpp', branch)


if __name__ == '__main__':
    unittest.main()
