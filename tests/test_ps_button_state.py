"""Serial source checks of PS reset/tap timing; no Vita, ShellUtil or thread execution."""
import re
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_host_persistence import function, run
from test_xml_source import ROOT, strip_directives


class PsButtonState(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / 'src/moonlight/vita_input.c').read_text()
        process = function(cls.source, 'vita_input_process')
        timing = process[process.index('    if (s_ps_button_capture) {'):]
        timing = timing[:timing.index('    back_buttons = vita_read_back_touch(&s_back);')]
        # Controller bit masking is outside this timing model; retain its call position.
        timing = timing.replace('s_pad.buttons &= ~SCE_CTRL_PSBUTTON;', 'clear_ps_bit();')
        code = function(cls.source, 'reset_ps_button_state')
        code += '\nvoid step(void) {\n' + timing + '\n}'
        code = re.sub(r'(\d+)ull\b', r'\1', strip_directives(code))
        unit = c_parser.CParser().parse('typedef unsigned long long uint64_t;' + code)
        cls.bodies = {node.decl.name: node.body for node in unit.ext
                      if isinstance(node, c_ast.FuncDef)}

    def setUp(self):
        self.now = 1000000
        self.stops = []
        self.values = dict(false=0, true=1, s_ps_button_capture=1,
                           s_ignore_ps_until_release=1, s_ps_button_locked=1,
                           s_ps_button_release_time=900000, s_ps_press_time=800000,
                           s_ps_single_pending=1, s_ps_physical_old=1,
                           s_ps_host_pulse_state=2, s_shell_ps_edge=1,
                           sceKernelGetSystemTimeWide=lambda: self.now,
                           moonlight_stream_stop=lambda: self.stops.append('stop'),
                           clear_ps_bit=lambda: None)

    def step(self, pressed, elapsed=0):
        self.now += elapsed
        self.values.update(ps_pressed=pressed, ps_pressed_old=self.values['s_ps_physical_old'])
        original = runtime.expression

        def expression(node, values):
            if isinstance(node, c_ast.BinaryOp) and node.op == '<=':
                return original(node.left, values) <= original(node.right, values)
            return original(node, values)

        with patch.object(runtime, 'expression', expression):
            run(self.bodies['step'], self.values)

    def test_reset_discards_pending_tap_without_releasing_overlay_guard_or_lock(self):
        run(self.bodies['reset_ps_button_state'], self.values)
        self.step(False, 1000000)
        self.assertEqual(self.stops, [])
        self.assertEqual(self.values['s_ps_host_pulse_state'], 0)
        self.assertEqual(self.values['s_shell_ps_edge'], 0)
        self.assertEqual(self.values['s_ignore_ps_until_release'], 1)
        self.assertEqual(self.values['s_ps_button_locked'], 1)
        for caller in ('vita_input_start', 'vita_input_stop', 'vita_input_suppress_for_system_ui'):
            self.assertIn('reset_ps_button_state();', function(self.source, caller))
        process = function(self.source, 'vita_input_process')
        suppression = process[process.index('    if (s_ignore_ps_until_release) {'):]
        self.assertLess(suppression.index('reset_ps_button_state();'),
                        suppression.index('if (!raw_ps_pressed)'))

    def test_fresh_single_double_and_held_taps_after_reset(self):
        for gesture in ('single', 'double', 'hold', 'release_after_hold'):
            with self.subTest(gesture=gesture):
                self.setUp()
                run(self.bodies['reset_ps_button_state'], self.values)
                self.step(True)
                if gesture == 'hold':
                    self.step(True, 500000)
                self.step(False, 500000 if gesture == 'release_after_hold' else 10000)
                if gesture == 'double':
                    self.step(True, 250000)
                    self.assertEqual(self.stops, ['stop'])
                else:
                    self.step(False, 250000)
                    self.assertEqual(self.values['s_ps_host_pulse_state'], 0)
                    self.step(False, 1)
                    self.assertEqual(self.values['s_ps_host_pulse_state'], int(gesture == 'single'))
                    self.assertEqual(self.stops, [])


if __name__ == '__main__':
    unittest.main()
