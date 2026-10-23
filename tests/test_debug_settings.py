"""Interpret settings callbacks/startup with stale storage; no native AppSettings/build."""
import re
import unittest

from pycparser import c_parser
from test_host_persistence import function, run
from test_xml_source import ROOT


def body(source, name):
    code = function(source.replace('int32_t ', 'int '), name)
    code = code.replace('LegacyMoonlightAdapter::', '')
    code = re.sub(r'//[^\n]*', '', code)
    return c_parser.CParser().parse(code).ext[-1].body


class DebugSettings(unittest.TestCase):
    def test_startup_loads_settings_before_host_initialization_and_propagates_errors(self):
        source = (ROOT / 'src/backend/legacy_moonlight_adapter.cpp').read_text()
        startup = body(source, 'LegacyMoonlightAdapter::Initialize')
        for failure in (None, 'settings', 'store', 'api'):
            with self.subTest(failure=failure):
                calls = []

                def initialize(stage):
                    calls.append(stage)
                    return -9 if stage == failure else 0

                values = dict(false=0, MOONLIGHT_CONNECTION_DISCONNECTED=0,
                              moonlight_settings_init=lambda: initialize('settings'),
                              legacy_device_store_init=lambda: initialize('store'),
                              moonlight_api_init=lambda: initialize('api'))
                self.assertEqual(run(startup, values), -9 if failure else 0)
                stages = ['settings', 'store', 'api']
                self.assertEqual(calls, stages[:stages.index(failure) + 1] if failure else stages)

    def test_toggle_uses_new_value_even_while_storage_still_has_previous_value(self):
        source = (ROOT / 'src/moonlight/settings.cpp').read_text()
        pressed = body(source, 'on_press')
        for previous, new_value in ((1, '0'), (0, '1'), (1, '1'), (0, '0')):
            with self.subTest(previous=previous, new_value=new_value):
                state = dict(enabled=previous)
                calls = []

                def apply_stored():
                    state['enabled'] = previous
                    calls.append('apply-stored')

                values = dict(element_id='debug_log', new_value=new_value,
                              strcmp=lambda a, b: 0 if a == b else 1,
                              vita_debug_log_set_enabled=lambda value: state.update(enabled=value),
                              moonlight_settings_apply_runtime=apply_stored,
                              emit=lambda *args: calls.append('changed'),
                              MOONLIGHT_EVENT_SETTINGS_CHANGED=1, SCE_OK=0)
                self.assertEqual(run(pressed, values), 0)
                self.assertEqual(state['enabled'], int(new_value))
                self.assertEqual(calls, ['changed'])

    def test_close_applies_committed_settings_before_logging_and_notifying(self):
        source = (ROOT / 'src/moonlight/settings.cpp').read_text()
        closed = body(source, 'on_term')
        for enabled in (0, 1):
            calls, logs = [], []
            state = dict(enabled=1 - enabled)

            def apply_stored():
                calls.append('apply')
                state['enabled'] = enabled

            def log(tag):
                calls.append('memory')
                if state['enabled']:
                    logs.append(tag)

            values = dict(result=-7, moonlight_settings_apply_runtime=apply_stored,
                          moonlight_log_memory_budget=log, MOONLIGHT_EVENT_SETTINGS_CLOSED=2,
                          emit=lambda *args: calls.append(args))
            run(closed, values)
            self.assertEqual(calls, ['apply', 'memory', (2, -7)])
            self.assertEqual(logs, ['settings-closed'] if enabled else [])

    def test_other_presses_and_missing_toggle_value_are_safe(self):
        source = (ROOT / 'src/moonlight/settings.cpp').read_text()
        pressed = body(source, 'on_press')
        for element, value in (('fps', '60'), (None, None), ('debug_log', None)):
            calls = []
            values = dict(element_id=element, new_value=value,
                          strcmp=lambda a, b: 0 if a == b else 1,
                          vita_debug_log_set_enabled=lambda value: calls.append(('enabled', value)),
                          moonlight_settings_apply_runtime=lambda: calls.append('apply'),
                          emit=lambda *args: calls.append('changed'),
                          MOONLIGHT_EVENT_SETTINGS_CHANGED=1, SCE_OK=0)
            self.assertEqual(run(pressed, values), 0)
            self.assertEqual(calls, ['changed'] if element == 'debug_log' else ['apply', 'changed'])


if __name__ == '__main__':
    unittest.main()
