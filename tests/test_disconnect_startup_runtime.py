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
        self.values.update(s_saw_cancelled=0, s_application_id=-1,
                           s_checkpoint_used=0, s_checkpoint_release=0, s_checkpoint_status=0,
                           STAGE_RTSP_HANDSHAKE=4, STAGE_AUDIO_STREAM_START=10, kCheckpointStage=4,
                           s_checkpoint_generation=0, __ATOMIC_RELAXED=4, __ATOMIC_RELEASE=1, __ATOMIC_ACQUIRE=2,
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

    def enable_late_checkpoint(self):
        self.values.update(kCheckpointStage=10, owned=3, pool_generation=17)
        self.values['moonlight_video_test_owned_frame_buffers'] = lambda: self.values['owned']
        self.values['moonlight_video_get_frame_pool_generation'] = lambda: self.values['pool_generation']

    def reach_late_checkpoint(self):
        self.enable_late_checkpoint()
        self.start_accepted()
        self.values['sceKernelDelayThread'] = lambda us: self.run_body('disconnect_test_poll')
        self.run_body('disconnect_test_startup_checkpoint', stage=10)

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
        # The adapter can already be disconnected while PAF drains failure/CLOSED.
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('STREAM_FAILED', -7)
        self.event('CONNECTION_CLOSED')

    def recover(self):
        self.closed()
        self.values['busy'] = 0
        self.run_body('disconnect_test_poll')
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

    def test_late_checkpoint_publishes_allocated_generation_before_disconnect(self):
        self.enable_late_checkpoint()
        self.start_accepted()
        self.run_body('disconnect_test_startup_checkpoint', stage=4)
        self.assertEqual(self.values['s_checkpoint_used'], 0)
        seen = []

        def delay(us):
            seen.append((self.values['s_checkpoint_status'], self.values['s_checkpoint_generation']))
            self.assertEqual(self.values['s_checkpoint_release'], 0)
            self.run_body('disconnect_test_poll')
            self.assertEqual(self.calls, ['disconnect'])

        self.values['sceKernelDelayThread'] = delay
        self.run_body('disconnect_test_startup_checkpoint', stage=10)
        self.assertEqual(seen, [(1, 17)])
        self.assertEqual(self.values['s_checkpoint_status'], 2)
        self.assertIn('stage=10 buffers=3 generation=17', self.logs[-2])
        self.run_body('disconnect_test_startup_checkpoint', stage=10)
        self.assertEqual(seen, [(1, 17)])

    def test_late_checkpoint_rejects_absent_or_partial_pool_without_disconnect(self):
        for owned, generation in ((0, 17), (1, 17), (2, 17), (3, 0)):
            with self.subTest(owned=owned, generation=generation):
                self.setUp()
                self.enable_late_checkpoint()
                self.start_accepted()
                self.values.update(owned=owned, pool_generation=generation)
                self.run_body('disconnect_test_startup_checkpoint', stage=10)
                self.assertEqual(self.values['s_checkpoint_status'], -2)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.calls, [])
                self.assertEqual(self.values['s_checkpoint_release'], 1)

    def test_late_recovery_waits_for_old_pool_after_closed_and_error_ok(self):
        self.reach_late_checkpoint()
        self.closed()
        self.values['now'] += self.timeout * 2
        self.run_body('disconnect_test_poll')  # User-paced OK, even with allocated buffers.
        self.assertEqual(self.calls, ['disconnect'])
        self.values['busy'] = 0
        self.run_body('disconnect_test_poll')
        for owned in (3, 2, 1):
            self.values['owned'] = owned
            self.run_body('disconnect_test_poll')
            self.assertEqual(self.calls, ['disconnect'])
        self.values['owned'] = 0
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect', 'connect'])
        self.assertIn('cancelled video buffers released generation=17', self.logs[-2])
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_PAIRED']
        self.event('CONNECTION_READY')
        self.run_body('disconnect_test_poll')
        self.event('APPLICATIONS_READY', 4)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect', 'connect', 'refresh', 'start'])
        self.assertFalse(any('PASS' in line for line in self.logs))
        # Recovery must not enter the worker barrier again, even with a new pool.
        self.values.update(owned=3, pool_generation=18)
        self.run_body('disconnect_test_startup_checkpoint', stage=10)
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
        self.event('STREAM_STARTED')
        self.assertIn('PASS startup cancelled', self.logs[-1])
        self.assertIn('late_start=0 stage=10', self.logs[-1])

    def test_changed_generation_and_release_timeout_abort_without_reconnect(self):
        for failure in ('generation', 'timeout'):
            with self.subTest(failure=failure):
                self.setUp()
                self.reach_late_checkpoint()
                self.closed()
                self.values['busy'] = 0
                self.run_body('disconnect_test_poll')
                if failure == 'generation':
                    self.values.update(pool_generation=18, owned=0)
                else:
                    self.values['now'] += self.timeout + 1
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.run_body('disconnect_test_poll')
                self.assertEqual(self.calls, ['disconnect'])

    def test_cancelled_setup_closed_error_ack_and_actual_restarted_event_required(self):
        self.request_disconnect()
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('STREAM_FAILED', -7)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.event('CONNECTION_CLOSED')
        self.values['now'] += self.timeout * 2
        self.run_body('disconnect_test_poll')  # Ordinary native error/OK is user paced.
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
        self.assertIn('PASS startup cancelled', self.logs[-1])
        self.assertIn('late_start=0', self.logs[-1])
        self.values['now'] += self.timeout * 2
        self.event('STREAM_STOPPED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(len(self.calls), 4)
        self.assertIn('PASS', self.logs[-1])

    def test_closed_before_cancel_or_duplicate_failure_cannot_pass(self):
        for sequence in (('CONNECTION_CLOSED',), ('STREAM_STOPPED',),
                         ('STREAM_STARTED',), ('STREAM_FAILED', 'STREAM_FAILED')):
            with self.subTest(sequence=sequence):
                self.setUp()
                self.request_disconnect()
                self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
                for event in sequence:
                    self.event(event, -7 if event == 'STREAM_FAILED' else 0)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.calls, ['disconnect'])

    def test_wrong_cancel_or_closed_target_result_rejects_recovery(self):
        for event, kwargs in (
                ('STREAM_FAILED', dict(result=-7, app_id=43)),
                ('STREAM_FAILED', dict(result=-7, address='other')),
                ('CONNECTION_CLOSED', dict(address='other')),
                ('CONNECTION_CLOSED', dict(result=-1))):
            with self.subTest(event=event, kwargs=kwargs):
                self.setUp()
                self.request_disconnect()
                self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
                if event == 'CONNECTION_CLOSED':
                    self.event('STREAM_FAILED', -7)
                self.event(event, **kwargs)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertEqual(self.calls, ['disconnect'])

    def test_real_non_cancel_failures_are_not_accepted_as_successful_cancellation(self):
        for result in (-1, -10, 123):
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

    def test_actual_stage_callback_publishes_before_test_and_never_injects_interrupt(self):
        source = (ROOT / 'src/moonlight/stream_session.cpp').read_text()
        callback = source.split('static void on_stage_start(int stage)', 1)[1].split('static void on_stage_complete', 1)[0]
        self.assertIn('#ifdef MOONLIGHT_STARTUP_DISCONNECT_TEST\n    disconnect_test_startup_checkpoint(stage);\n#endif', callback)
        unit = c_parser.CParser().parse('void on_stage_start(int stage)' + strip_directives(callback))
        called = []
        values = dict(stage=4, LiGetStageName=lambda stage: 'RTSP handshake',
                      publish_common_startup=lambda: called.append('publish'),
                      disconnect_test_startup_checkpoint=lambda stage: called.append(stage))
        execute(unit.ext[0].body, values)
        self.assertEqual(called, ['publish', 4])
        runtime = (ROOT / 'tests/runtime' / self.source_name).read_text()
        self.assertNotIn('LiInterruptConnection(', runtime)
        self.assertNotIn('LiStopConnection(', runtime)
        common = (ROOT / 'third_party/moonlight-common-c/src/Connection.c').read_text()
        audio_init = common.index('ListenerCallbacks.stageComplete(STAGE_AUDIO_STREAM_INIT);')
        hook = common.index('ListenerCallbacks.stageStarting(STAGE_RTSP_HANDSHAKE);')
        handshake = common.index('err = performRtspHandshake(serverInfo);')
        self.assertLess(audio_init, hook)
        self.assertLess(hook, handshake)
        self.assertIn('callbacks->stageStarting = on_stage_start;', source)
        self.assertNotIn('disconnect_test_pending_checkpoint(application_id);', callback)

    def test_late_stage_follows_successful_video_setup_before_started(self):
        common = (ROOT / 'third_party/moonlight-common-c/src/Connection.c').read_text()
        markers = ('err = startVideoStream(renderContext, drFlags);',
                   'ListenerCallbacks.stageComplete(STAGE_VIDEO_STREAM_START);',
                   'ListenerCallbacks.stageStarting(STAGE_AUDIO_STREAM_START);',
                   'err = startAudioStream(audioContext, arFlags);',
                   'ListenerCallbacks.connectionStarted();')
        positions = [common.index(marker) for marker in markers]
        self.assertEqual(positions, sorted(positions))
        video = (ROOT / 'third_party/moonlight-common-c/src/VideoStream.c').read_text()
        start = video.split('int startVideoStream(', 1)[1]
        self.assertLess(start.index('err = VideoCallbacks.setup('), start.index('VideoCallbacks.start();'))
        renderer = (ROOT / 'src/moonlight/vita_video_renderer.c').read_text()
        header = (ROOT / 'src/moonlight/vita_video_renderer.h').read_text()
        for source in (renderer, header):
            guarded = source.split('#ifdef MOONLIGHT_STARTUP_DISCONNECT_TEST\n', 1)[1].split('#endif', 1)[0]
            self.assertIn('int moonlight_video_test_owned_frame_buffers(void)', guarded)

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
        self.assertIn('MOONLIGHT_STARTUP_DISCONNECT_STAGE=${MOONLIGHT_STARTUP_DISCONNECT_STAGE}', branch)
        self.assertIn('set(MOONLIGHT_STARTUP_DISCONNECT_STAGE "4" CACHE STRING', cmake)
        self.assertIn('NOT MOONLIGHT_STARTUP_DISCONNECT_STAGE MATCHES "^(4|10)$"', cmake)
        self.assertIn('-DMOONLIGHT_STARTUP_DISCONNECT_STAGE=4', script)


class FrameBufferObserver(unittest.TestCase):
    def test_partial_slot_metadata_cannot_report_zero_owned(self):
        source = (ROOT / 'src/moonlight/vita_video_renderer.c').read_text()
        start = source.index('int moonlight_video_test_owned_frame_buffers(void)')
        body = source[start:source.index('\n}', start) + 2]
        # Normalize the loop increment/declaration for the tiny serial C interpreter.
        body = body.replace('for (int i = 0;', 'int i; for (i = 0;').replace('++i', 'i = i + 1')
        function = c_parser.CParser().parse(body).ext[0].body
        for mask in range(512):
            memblocks = [i + 1 if mask & (1 << (3 * i)) else -1 for i in range(3)]
            pointers = [int(bool(mask & (1 << (3 * i + 1)))) for i in range(3)]
            mappings = [int(bool(mask & (1 << (3 * i + 2)))) for i in range(3)]
            with self.subTest(mask=mask):
                values = dict(VITA_VIDEO_FRAME_COUNT=3, s_frame_memblocks=memblocks,
                              s_frame_buffers=pointers, s_frame_gpu_mapped=mappings)
                with self.assertRaises(Returned) as result:
                    execute(function, values)
                self.assertEqual(result.exception.value, sum(
                    memblocks[i] >= 0 or pointers[i] or mappings[i] for i in range(3)))


if __name__ == '__main__':
    unittest.main()
