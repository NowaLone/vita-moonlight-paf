"""Interpret actual stream startup/stop helpers with serialized mutex/common-c mocks.

No C++ compilation, platform calls or real concurrency. Exercises cancellation boundaries,
including common-c resetting its global flag and success racing a pending interruption.
"""
import re
import unittest

from pycparser import c_ast, c_parser
from test_adapter_lifecycle import Returned, execute
from test_xml_source import ROOT, strip_directives

SOURCE = (ROOT / 'src/moonlight/stream_session.cpp').read_text()


class StreamStartupInterrupt(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        functions = []
        for name in ('interrupt_common_startup_locked', 'publish_common_startup',
                     'finish_common_startup', 'on_connection_started', 'moonlight_stream_stop'):
            start = re.search(r'^(?:static |extern "C" )(?:void|int) ' + name + r'\(', SOURCE, re.M).start()
            source = SOURCE[start:SOURCE.index('\n}', start) + 2].replace('extern "C" ', '')
            functions.append(source)
        preamble = 'typedef struct { int started; } StreamContext;'
        unit = c_parser.CParser().parse(preamble + strip_directives('\n'.join(functions)))
        cls.functions = {item.decl.name: item.body for item in unit.ext if isinstance(item, c_ast.FuncDef)}

    def setUp(self):
        self.values = dict(s_stream_mutex=1, s_stream_thread_running=1,
                           s_stream_cancel_requested=0, s_stream_stop_requested=0,
                           s_common_starting=0, s_start_interrupt_sent=0,
                           s_stream_context=dict(started=0), result=0,
                           MOONLIGHT_EVENT_STREAM_STARTED=7)
        self.calls, self.logs = [], []
        self.locked = False
        self.interrupted = False
        self.values['pthread_mutex_lock'] = self.lock
        self.values['pthread_mutex_unlock'] = self.unlock
        self.values['LiInterruptConnection'] = self.interrupt
        self.values['LiStopConnection'] = self.stop
        self.values['vita_input_start'] = lambda: self.record_unlocked('input_start')
        self.values['emit_event'] = lambda kind, result: self.record_unlocked('STARTED')
        self.values['vita_debug_log'] = lambda fmt, *args: self.logs.append(fmt % args)
        self.values['interrupt_common_startup_locked'] = lambda: self.run_body('interrupt_common_startup_locked')

    def lock(self, mutex):
        self.assertFalse(self.locked)
        self.locked = True

    def unlock(self, mutex):
        self.assertTrue(self.locked)
        self.locked = False

    def interrupt(self):
        self.assertTrue(self.locked)
        self.interrupted = True
        self.calls.append('interrupt')

    def stop(self):
        self.record_unlocked('stop')

    def record_unlocked(self, label):
        self.assertFalse(self.locked)
        self.calls.append(label)

    def run_body(self, name, **parameters):
        self.values.update(parameters)
        try:
            execute(self.functions[name], self.values)
        except Returned as result:
            return result.value

    def test_cancel_before_entry_is_replayed_after_common_c_reset(self):
        self.assertEqual(self.run_body('moonlight_stream_stop'), 0)
        self.assertEqual(self.calls, [])
        self.interrupted = False  # Actual common-c reset before the first stage callback.
        self.run_body('publish_common_startup')
        self.assertTrue(self.interrupted)
        self.assertEqual(self.calls, ['interrupt'])
        self.assertEqual(self.run_body('finish_common_startup', result=-1), -7)
        self.assertEqual(self.calls, ['interrupt'])  # Failure already unwound in common-c.
        self.assertEqual(self.values['s_common_starting'], 0)
        self.assertEqual(self.values['s_start_interrupt_sent'], 0)

    def test_cancel_after_publication_interrupts_once_and_keeps_owner_reserved(self):
        self.run_body('publish_common_startup')
        self.assertEqual(self.calls, [])
        self.run_body('moonlight_stream_stop')
        self.run_body('moonlight_stream_stop')
        self.run_body('publish_common_startup')
        self.assertEqual(self.calls, ['interrupt'])
        self.assertEqual(self.values['s_stream_thread_running'], 1)
        self.assertEqual(self.values['s_stream_cancel_requested'], 1)
        self.assertEqual(self.run_body('finish_common_startup', result=-1), -7)
        self.assertEqual(self.calls, ['interrupt'])

    def test_cancelled_success_suppresses_input_event_and_owner_stops_once_before_failure(self):
        self.run_body('publish_common_startup')
        self.run_body('moonlight_stream_stop')
        self.run_body('on_connection_started')
        self.assertEqual(self.values['s_stream_context']['started'], 0)
        self.assertEqual(self.calls, ['interrupt'])
        self.assertEqual(self.run_body('finish_common_startup', result=0), -7)
        self.assertEqual(self.calls, ['interrupt', 'stop'])
        self.assertIn('result=0', self.logs[-1])
        self.assertEqual(self.values['s_stream_thread_running'], 1)

    def test_success_commit_before_stop_retains_ordinary_active_teardown(self):
        self.run_body('publish_common_startup')
        self.run_body('on_connection_started')
        self.assertEqual(self.calls, ['input_start', 'STARTED'])
        self.assertEqual(self.values['s_stream_context']['started'], 1)
        self.run_body('moonlight_stream_stop')
        self.assertEqual(self.calls, ['input_start', 'STARTED'])
        self.assertEqual(self.run_body('finish_common_startup', result=0), 0)
        self.assertEqual(self.values['s_stream_stop_requested'], 1)

    def test_uncancelled_failure_and_pre_callback_validation_errors_preserve_result(self):
        for published, cancel, result in ((0, 0, -3), (1, 0, -10), (0, 1, -2)):
            with self.subTest(published=published, cancel=cancel, result=result):
                self.setUp()
                if published:
                    self.run_body('publish_common_startup')
                if cancel:
                    self.run_body('moonlight_stream_stop')
                self.assertEqual(self.run_body('finish_common_startup', result=result), result)
                self.assertEqual(self.calls, [])

    def test_stop_after_scope_exit_cannot_interrupt_or_free_old_common_c_state(self):
        self.run_body('publish_common_startup')
        self.assertEqual(self.run_body('finish_common_startup', result=-1), -1)
        self.run_body('moonlight_stream_stop')
        self.assertEqual(self.calls, [])
        self.assertEqual(self.values['s_stream_thread_running'], 1)
        self.values['s_stream_thread_running'] = 0
        self.values['s_stream_cancel_requested'] = 0
        self.assertEqual(self.run_body('moonlight_stream_stop'), 0)
        self.assertEqual(self.values['s_stream_cancel_requested'], 0)
        self.assertEqual(self.calls, [])

    def test_boundary_orderings_before_and_after_started_commit(self):
        sequences = (
            ('cancel', 'reset', 'publish', 'ready', 'return'),
            ('reset', 'cancel', 'publish', 'ready', 'return'),
            ('reset', 'publish', 'cancel', 'ready', 'return'),
            ('reset', 'publish', 'ready', 'cancel', 'return'),
            ('reset', 'publish', 'ready', 'return', 'cancel'))
        for sequence in sequences:
            with self.subTest(sequence=sequence):
                self.setUp()
                result = None
                for action in sequence:
                    if action == 'cancel':
                        self.run_body('moonlight_stream_stop')
                    elif action == 'reset':
                        self.interrupted = False
                    elif action == 'publish':
                        self.run_body('publish_common_startup')
                    elif action == 'ready':
                        self.run_body('on_connection_started')
                    else:
                        result = self.run_body('finish_common_startup', result=0)
                if sequence.index('cancel') < sequence.index('ready'):
                    self.assertEqual(result, -7)
                    self.assertEqual(self.calls, ['interrupt', 'stop'])
                else:
                    self.assertEqual(result, 0)
                    self.assertEqual(self.calls, ['input_start', 'STARTED'])
                self.assertFalse(self.locked)

    def test_actual_common_c_scope_order_and_worker_cleanup_remain_single_owner(self):
        common = (ROOT / 'third_party/moonlight-common-c/src/Connection.c').read_text()
        reset = common.index('    ConnectionInterrupted = false;')
        first_callback = common.index('ListenerCallbacks.stageStarting(STAGE_PLATFORM_INIT);')
        self.assertLess(reset, first_callback)
        start = SOURCE.index('    result = LiStartConnection(')
        finish = SOURCE.index('    return finish_common_startup(result);', start)
        worker = SOURCE.index('static void *stream_thread_main')
        self.assertLess(start, finish)
        self.assertLess(finish, worker)
        self.assertIn('publish_common_startup();', SOURCE.split('static void on_stage_start', 1)[1].split('\n}', 1)[0])
        reset_flags = SOURCE.split('    s_stream_thread_running = 1;', 1)[1].split('pthread_mutex_unlock', 1)[0]
        self.assertIn('s_common_starting = 0;', reset_flags)
        self.assertIn('s_start_interrupt_sent = 0;', reset_flags)
        # Terminal event/bookkeeping still precedes owner release; host cleanup waits for it.
        terminal = SOURCE.index('static void finish_stream_worker(')
        owner_release = SOURCE.index('s_stream_thread_running = 0;', terminal)
        self.assertLess(SOURCE.index('callback(type, result, userdata);', terminal), owner_release)
        api = (ROOT / 'src/moonlight/api.cpp').read_text()
        cleanup = api.split('static void *disconnect_cleanup_worker', 1)[1]
        self.assertLess(cleanup.index('while (moonlight_stream_is_running())'), cleanup.index('legacy_gamestream_shutdown'))
        self.assertNotIn('LiStopConnection', api.split('int moonlight_api_disconnect', 1)[1].split('\n}', 1)[0])


    def test_failed_setup_queues_release_and_waits_for_safe_pool_generation(self):
        apps_source = (ROOT / 'src/pages/page_apps.cpp').read_text()
        failed = apps_source.split('case MOONLIGHT_EVENT_STREAM_FAILED: {', 1)[1].split('\n    }', 1)[0]
        failed = failed.replace('Stream::ScheduleFrameBufferRelease()', 'schedule_release()')
        failed = failed.replace('MoonlightApp::Instance()->Connection().ShowError', 'show_error')
        failed = failed.replace('apps->ShowEmptyState()', 'show_empty()').replace('        break;', '')
        preamble = ('typedef int bool; typedef struct { int m_launching; } Apps;'
                    'typedef struct { int result; } Event;')
        unit = c_parser.CParser().parse(preamble + 'void failed(Apps *apps, Event *event) {' + strip_directives(failed) + '}')
        calls = []
        values = dict(apps=dict(m_launching=1), event=dict(result=-7), false=0,
                      schedule_release=lambda: calls.append('schedule'),
                      show_error=lambda message, result: calls.append(('error', result)),
                      show_empty=lambda: calls.append('empty'))
        execute(unit.ext[-1].body, values)
        self.assertEqual(calls, ['schedule', ('error', -7), 'empty'])
        self.assertEqual(values['apps']['m_launching'], 0)

        page = (ROOT / 'src/pages/page_stream.cpp').read_text()
        task = page.split('void Stream::ReleaseFrameBuffersTask(void *userdata)', 1)[1].split('\n}', 1)[0] + '\n}'
        task = task.replace('common::MainThreadCallList::Unregister', 'unregister_task')
        task = task.replace('--s_release_delay;', 's_release_delay = s_release_delay - 1;')
        unit = c_parser.CParser().parse('typedef unsigned int uintptr_t; void release(void *userdata)' + strip_directives(task))
        for generation, instance in ((7, 0), (8, 0), (7, 1)):
            with self.subTest(generation=generation, instance=instance):
                actions = []
                values = dict(userdata=7, s_instance=instance, NULL=0, s_release_delay=3,
                              ReleaseFrameBuffersTask=99,
                              moonlight_video_get_frame_pool_generation=lambda: generation,
                              unregister_task=lambda *args: actions.append('unregister'),
                              moonlight_video_release_frame_buffers=lambda: actions.append('free'))
                for _ in range(3):
                    try:
                        execute(unit.ext[-1].body, values)
                    except Returned:
                        pass
                self.assertNotIn('free', actions)
                try:
                    execute(unit.ext[-1].body, values)
                except Returned:
                    pass
                self.assertEqual(actions.count('free'), int(generation == 7 and instance == 0))


if __name__ == '__main__':
    unittest.main()
