"""Interpret the active-stream scenario; no compilation, PAF, transport or real threads."""
import re
import unittest

from test_disconnect_runtime import ScenarioHarness
from test_xml_source import ROOT


class ActiveDisconnectRuntime(ScenarioHarness, unittest.TestCase):
    source_name = 'disconnect_active.cpp'

    def setUp(self):
        super().setUp()
        source = (ROOT / 'tests/runtime' / self.source_name).read_text()
        self.hold = int(re.search(r'kStreamHoldUs\s*=\s*(\d+)', source).group(1))
        self.values.update(kStreamHoldUs=self.hold, s_saw_stopped=0, s_application_id=-1)
        self.results['start'] = 0
        self.starts = []
        self.values['start_request'] = lambda app_id: (
            self.starts.append(app_id) or self.calls.append('start') or self.results['start'])

    def event(self, kind, result=0, address='test-host', app_id=42):
        self.run_body('disconnect_test_event', event=dict(
            type=self.values['MOONLIGHT_EVENT_' + kind], result=result,
            address=address, application_id=app_id))

    def begin_stream(self):
        self.arm()
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
        self.event('STREAM_STARTED')

    def request_disconnect(self):
        self.begin_stream()
        self.values['now'] += self.hold
        self.run_body('disconnect_test_poll')

    def closed(self):
        # Workers may have already updated the adapter before PAF delivers queued STOPPED.
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('STREAM_STOPPED')
        self.event('CONNECTION_CLOSED')

    def reconnect(self):
        self.closed()
        self.run_body('disconnect_test_poll')
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_PAIRED']
        self.event('CONNECTION_READY')
        self.run_body('disconnect_test_poll')

    def test_waits_for_selected_stream_and_nonblocking_hold(self):
        self.arm()
        self.values['now'] = self.timeout * 2
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.assertNotIn('FAIL', self.logs[-1])
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
        self.event('STREAM_STARTED')
        self.values['now'] += self.hold - 1
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.values['now'] += 1
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])

    def test_single_active_cycle_waits_for_closed_and_actual_restarted_event(self):
        self.request_disconnect()
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('STREAM_STOPPED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.event('CONNECTION_CLOSED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect', 'connect'])
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_PAIRED']
        self.event('CONNECTION_READY')
        self.run_body('disconnect_test_poll')
        self.event('APPLICATIONS_READY', 4)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect', 'connect', 'refresh', 'start'])
        self.assertEqual(self.starts, [42])
        self.assertFalse(any('PASS' in line for line in self.logs))
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
        self.event('STREAM_STARTED')
        self.assertIn('PASS active', self.logs[-1])
        self.values['now'] += self.timeout * 2
        self.run_body('disconnect_test_poll')
        self.event('STREAM_STOPPED')
        self.assertEqual(len(self.calls), 4)
        self.assertIn('PASS active', self.logs[-1])

    def test_closed_without_stopped_aborts_even_if_state_disconnected(self):
        self.request_disconnect()
        self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
        self.event('CONNECTION_CLOSED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.assertIn('FAIL', self.logs[-1])

    def test_bad_stop_or_closed_never_reconnects(self):
        for kind, kwargs in (
                ('STREAM_STOPPED', dict(result=-1)),
                ('STREAM_STOPPED', dict(app_id=43)),
                ('STREAM_STOPPED', dict(address='other-host')),
                ('CONNECTION_CLOSED', dict(result=-1)),
                ('CONNECTION_CLOSED', dict(address='other-host'))):
            with self.subTest(kind=kind, kwargs=kwargs):
                self.setUp()
                self.request_disconnect()
                self.values['state'] = self.values['MOONLIGHT_CONNECTION_DISCONNECTED']
                if kind == 'CONNECTION_CLOSED':
                    self.event('STREAM_STOPPED')
                self.event(kind, **kwargs)
                self.run_body('disconnect_test_poll')
                self.assertEqual(self.calls, ['disconnect'])
                self.assertIn('FAIL', self.logs[-1])

    def test_busy_defers_then_timeout_prevents_more_actions(self):
        self.begin_stream()
        self.values.update(now=self.hold, busy=1)
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.values.update(now=self.timeout + 1, busy=0)
        self.run_body('disconnect_test_poll')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.assertIn('FAIL', self.logs[-1])

    def test_disconnect_rejection_or_early_stop_aborts(self):
        self.begin_stream()
        self.event('STREAM_STOPPED')
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, [])
        self.assertIn('FAIL', self.logs[-1])
        self.setUp()
        self.results['disconnect'] = -1
        self.request_disconnect()
        self.closed()
        self.run_body('disconnect_test_poll')
        self.assertEqual(self.calls, ['disconnect'])
        self.assertIn('FAIL', self.logs[-1])

    def test_reconnect_rejects_changed_trust_or_host(self):
        for kwargs in (dict(), dict(address='other-host')):
            with self.subTest(kwargs=kwargs):
                self.setUp()
                self.request_disconnect()
                self.closed()
                self.run_body('disconnect_test_poll')
                self.values['state'] = self.values['MOONLIGHT_CONNECTION_READY']
                self.event('CONNECTION_READY', **kwargs)
                self.run_body('disconnect_test_poll')
                self.assertEqual(self.calls, ['disconnect', 'connect'])
                self.assertIn('FAIL', self.logs[-1])

    def test_restart_rejection_failed_event_and_wrong_app_never_pass(self):
        for failure in ('rejected', 'failed', 'wrong_app'):
            with self.subTest(failure=failure):
                self.setUp()
                self.request_disconnect()
                self.reconnect()
                self.event('APPLICATIONS_READY', 4)
                self.results['start'] = -1 if failure == 'rejected' else 0
                self.run_body('disconnect_test_poll')
                if failure == 'failed':
                    self.event('STREAM_FAILED', result=-8)
                elif failure == 'wrong_app':
                    self.values['state'] = self.values['MOONLIGHT_CONNECTION_STREAMING']
                    self.event('STREAM_STARTED', app_id=43)
                self.run_body('disconnect_test_poll')
                self.assertIn('FAIL', self.logs[-1])
                self.assertFalse(any('PASS' in line for line in self.logs))
                self.assertEqual(self.starts, [42])

    def test_user_connect_and_shutdown_abort(self):
        for action in ('disconnect_test_capture_host', 'disconnect_test_shutdown'):
            with self.subTest(action=action):
                self.setUp()
                self.begin_stream()
                self.run_body(action)
                self.values['now'] += self.hold
                self.run_body('disconnect_test_poll')
                self.assertEqual(self.calls, [])
                self.assertIn('FAIL', self.logs[-1])


if __name__ == '__main__':
    unittest.main()
