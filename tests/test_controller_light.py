"""Non-build source interpretation of RGB state/PAF handoff and asset checks.

Mutex scheduling, PAF, float colors and pointer/struct copies are modeled. These
checks do not compile C++, run common-c, render PAF or establish Vita performance.
"""
import copy
import importlib.util
import itertools
import re
import struct
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zlib

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives

STREAM = (ROOT / 'src/moonlight/stream_session.cpp').read_text()
PAGE = (ROOT / 'src/pages/page_stream.cpp').read_text()
OFF, DS4, DUALSENSE = 0, 1, 2


def function(source, name):
    start = re.search(r'^(?:static |extern "C" )?(?:int|void) ' + name + r'\(', source, re.M).start()
    return source[start:source.index('\n}', start) + 2].replace('extern "C" ', '')


def black(mode=OFF, generation=1):
    return dict(mode=mode, generation=generation, r=0, g=0, b=0)


class LightHarness:
    original_expression = staticmethod(runtime.expression)
    original_execute = staticmethod(runtime.execute)

    def __init__(self):
        code = function(STREAM, 'on_controller_led')
        code += function(STREAM, 'moonlight_stream_get_controller_light')
        # Interpret the actual accepted-start reset, excluding unrelated setup calls.
        reset = STREAM[STREAM.index('    s_controller_light.generation ='):]
        reset = reset[:reset.index('    pthread_mutex_unlock(&s_stream_mutex);')]
        code += 'void begin(void) {\n' + reset + '\n}'
        ui = function(PAGE, 'Stream::UpdateControllerLight').replace('Stream::', '')
        ui = ui.replace('MoonlightApp::Instance()->Connection().GetControllerLight', 'get_light')
        ui = ui.replace('MoonlightControllerLight()', 'zero_light()')
        ui = ui.replace('common::transition::Type_Reset', '0')
        ui = re.sub(r'const math::v4 color\(([^;]+)\);', r'int color = make_color(\1);', ui)
        ui = re.sub(r'(m_light_\w+)->(Hide|Show|SetColor)\(([^)]*)\)', r'widget_\2(\1, \3)', ui)
        code += ui
        preamble = 'typedef unsigned int uint32_t; typedef unsigned short uint16_t; typedef unsigned char uint8_t;'
        header = strip_directives((ROOT / 'include/moonlight/controller_light.h').read_text())
        code = re.sub(r'//[^\n]*', '', strip_directives(code))
        unit = c_parser.CParser().parse(preamble + header + code)
        self.bodies = {item.decl.name: item.body for item in unit.ext if isinstance(item, c_ast.FuncDef)}
        self.locked = False
        self.polls = 0
        self.visible = set()
        self.colors = {}
        self.widgets = []
        self.values = dict(MOONLIGHT_CONTROLLER_LIGHT_OFF=OFF,
                           MOONLIGHT_CONTROLLER_LIGHT_DS4=DS4,
                           MOONLIGHT_CONTROLLER_LIGHT_DUALSENSE=DUALSENSE,
                           s_stream_thread_running=1, s_stream_stop_requested=0,
                           s_stream_cancel_requested=0, s_stream_context=dict(started=1),
                           s_controller_light=black(DS4), s_stream_mutex=1,
                           m_controller_light=black(), m_controller_light_generation=1,
                           m_light_ds4='ds4', m_light_left='left', m_light_right='right')
        self.values.update(pthread_mutex_lock=self.lock, pthread_mutex_unlock=self.unlock,
                           get_light=self.get_light, zero_light=lambda: black(generation=0),
                           make_color=lambda *rgba: rgba,
                           widget_Hide=self.hide, widget_Show=self.show,
                           widget_SetColor=self.color)

    def lock(self, mutex):
        assert not self.locked
        self.locked = True

    def unlock(self, mutex):
        assert self.locked
        self.locked = False

    def expression(self, node, values):
        if isinstance(node, c_ast.Constant):
            if node.type in ('float', 'double'):
                return float(node.value.rstrip('fF'))
            if node.type == 'unsigned int':
                return int(node.value.rstrip('uU'))
        if isinstance(node, c_ast.BinaryOp) and node.op == '/':
            return self.expression(node.left, values) / self.expression(node.right, values)
        if isinstance(node, c_ast.UnaryOp) and node.op == '*':
            return self.expression(node.expr, values)
        return self.original_expression(node, values)

    def execute(self, node, values):
        if isinstance(node, c_ast.Assignment):
            value = self.expression(node.rvalue, values)
            if isinstance(node.lvalue, c_ast.UnaryOp) and node.lvalue.op == '*':
                target = self.expression(node.lvalue.expr, values)
                target.clear()
                target.update(copy.deepcopy(value))
                return
            if isinstance(node.lvalue, c_ast.ID) and isinstance(value, dict):
                values[node.lvalue.name] = copy.deepcopy(value)
                return
        self.original_execute(node, values)

    def invoke(self, name, **parameters):
        self.values.update(parameters)
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            try:
                runtime.execute(self.bodies[name], self.values)
            except runtime.Returned as result:
                return result.value

    def get_light(self, pointer):
        self.polls += 1
        # A C struct pointer points to the caller's local storage.
        out = black()  # Non-null C pointer, even when all members are zero.
        result = self.invoke('moonlight_stream_get_controller_light', out=out)
        self.values[pointer] = out
        return result

    def hide(self, widget, transition):
        assert not self.locked
        self.widgets.append(('hide', widget))
        self.visible.discard(widget)

    def show(self, widget, transition):
        assert not self.locked
        self.widgets.append(('show', widget))
        self.visible.add(widget)

    def color(self, widget, color):
        assert not self.locked
        self.widgets.append(('color', widget))
        self.colors[widget] = color

    def rgb(self, r, g, b, controller=0):
        self.invoke('on_controller_led', controller=controller, r=r, g=g, b=b)
        assert not self.locked


class ControllerLight(unittest.TestCase):
    def test_modes_preserve_rgb_and_ignore_other_controller_ids(self):
        for mode in (OFF, DS4, DUALSENSE):
            h = LightHarness()
            h.values['s_controller_light']['mode'] = mode
            h.rgb(255, 64, 7, controller=1)
            self.assertEqual(h.values['s_controller_light'], black(mode))
            h.rgb(255, 64, 7)
            self.assertEqual(tuple(h.values['s_controller_light'][c] for c in 'rgb'),
                             (0, 0, 0) if mode == OFF else (255, 64, 7))

    def test_stop_cancel_and_finished_sessions_reject_feedback(self):
        for running, stopped, cancelled in itertools.product((0, 1), repeat=3):
            h = LightHarness()
            h.values.update(s_stream_thread_running=running, s_stream_stop_requested=stopped,
                            s_stream_cancel_requested=cancelled)
            h.rgb(9, 8, 7)
            self.assertEqual(h.values['s_controller_light']['r'], 9 if running and not stopped and not cancelled else 0)

    def test_snapshot_gates_visibility_on_actual_started_lifetime(self):
        for running, started, stopped, cancelled in itertools.product((0, 1), repeat=4):
            h = LightHarness()
            h.values.update(s_stream_thread_running=running, s_stream_context=dict(started=started),
                            s_stream_stop_requested=stopped, s_stream_cancel_requested=cancelled)
            out = black()
            self.assertEqual(h.invoke('moonlight_stream_get_controller_light', out=out), 0)
            self.assertEqual(out['mode'], DS4 if running and started and not stopped and not cancelled else OFF)
            self.assertEqual(h.values['s_controller_light']['mode'], DS4)  # snapshot never mutates session
            self.assertFalse(h.locked)
        self.assertEqual(h.invoke('moonlight_stream_get_controller_light', out=0), -1)

    def test_accepted_restart_resets_rgb_generation_and_invalid_setting(self):
        for mode in (-1, OFF, DS4, DUALSENSE, 3, 99):
            h = LightHarness()
            h.rgb(200, 50, 255)
            h.invoke('begin', settings=dict(controller_light=mode))
            self.assertEqual(h.values['s_controller_light'], black(mode if mode in (OFF, DS4, DUALSENSE) else OFF, 2))

    def test_ui_selects_shape_and_repeated_feedback_does_not_touch_widgets(self):
        for mode, shapes in ((DS4, {'ds4'}), (DUALSENSE, {'left', 'right'})):
            h = LightHarness()
            h.values['s_controller_light']['mode'] = mode
            h.rgb(255, 64, 0)
            h.invoke('UpdateControllerLight')
            self.assertEqual(h.visible, shapes)
            self.assertTrue(all(h.colors[shape] == (1.0, 64 / 255, 0.0, 1.0) for shape in shapes))
            count = len(h.widgets)
            h.invoke('UpdateControllerLight')
            self.assertEqual(len(h.widgets), count)
            h.rgb(255, 64, 0)
            h.invoke('UpdateControllerLight')
            self.assertEqual(len(h.widgets), count)

    def test_black_stop_and_stale_generation_hide_the_overlay(self):
        for cause in ('black', 'stop', 'cancel', 'ended', 'stale'):
            h = LightHarness()
            h.rgb(10, 30, 255)
            h.invoke('UpdateControllerLight')
            self.assertEqual(h.visible, {'ds4'})
            if cause == 'black': h.rgb(0, 0, 0)
            if cause == 'stop': h.values['s_stream_stop_requested'] = 1
            if cause == 'cancel': h.values['s_stream_cancel_requested'] = 1
            if cause == 'ended': h.values['s_stream_context']['started'] = 0
            if cause == 'stale': h.values['s_controller_light']['generation'] = 2
            h.invoke('UpdateControllerLight')
            self.assertEqual(h.visible, set(), cause)

    def test_disabled_page_does_not_poll_and_missing_widgets_are_safe(self):
        h = LightHarness()
        h.values['m_controller_light_generation'] = 0
        h.invoke('UpdateControllerLight')
        self.assertEqual((h.polls, h.widgets), (0, []))
        h.values.update(m_controller_light_generation=1, m_light_ds4=0, m_light_left=0, m_light_right=0)
        h.rgb(20, 40, 60)
        h.invoke('UpdateControllerLight')
        self.assertEqual(h.widgets, [])

    def test_callback_capability_service_and_paused_video_wiring(self):
        self.assertIn('callbacks->setControllerLED = on_controller_led;', STREAM)
        callback = function(STREAM, 'on_controller_led')
        self.assertNotIn('paf::', callback)
        self.assertNotIn('vita_debug_log', callback)
        task = function(PAGE, 'Stream::PresentTask')
        self.assertLess(task.index('UpdateControllerLight()'), task.index('if (frame.valid)'))
        inputs = (ROOT / 'src/moonlight/vita_input.c').read_text()
        self.assertIn('capabilities |= LI_CCAP_RGB_LED;', inputs)
        block = inputs[inputs.index('    MoonlightControllerLight light;'):inputs.index('    if (s_touchscreen_mode == 1)', inputs.index('    MoonlightControllerLight light;'))]
        self.assertIn('light.mode != MOONLIGHT_CONTROLLER_LIGHT_OFF', block)
        for path, call in (('src/services/connection_service.cpp', 'm_backend.GetControllerLight(out)'),
                           ('src/backend/legacy_moonlight_adapter.cpp', 'moonlight_api_get_controller_light(out)'),
                           ('src/moonlight/api.cpp', 'moonlight_stream_get_controller_light(out)')):
            self.assertIn('return ' + call + ';', (ROOT / path).read_text())

    def test_settings_localization_and_overlay_layer_geometry(self):
        ui = ET.parse(ROOT / 'cxml/vita_moonlight_ui.xml').getroot()
        page = ui.find(".//page[@id='page_stream']")
        ids = [child.get('id') for child in page]
        self.assertLess(ids.index('plane_stream_video'), ids.index('plane_stream_light_ds4'))
        self.assertLess(ids.index('plane_stream_light_right'), ids.index('plane_stream_statistics'))
        settings = ET.parse(ROOT / 'cxml/moonlight_settings.xml').getroot()
        setting = settings.find(".//list[@id='controller_light']")
        self.assertEqual(setting.get('default_value'), '0')
        self.assertEqual([item.get('value') for item in setting], ['0', '1', '2'])
        # Existing locale strings contain literal '&'/'<->' in attributes; the
        # resource tool accepts these. Check IDs without rewriting that artwork.
        strings = set(re.findall(r'\bid="([^"]+)"', (ROOT / 'cxml/locale/en.xml').read_text()))
        self.assertTrue(all(item.get('title') in strings for item in setting))
        self.assertIn(setting.get('hint'), strings)
        for suffix, size in (('ds4', '200, 36'), ('left', '24, 448'), ('right', '24, 448')):
            plane = page.find("./plane[@id='plane_stream_light_" + suffix + "']")
            self.assertEqual(plane.find('layout_hint').get('size'), size)
            style = ui.find(".//style_plane[@id='" + plane.get('style') + "']")
            self.assertEqual(style.get('color'), '1, 1, 1, 1')
            self.assertEqual(style.find('planeobj').get('color'), '1, 1, 1, 1')

    def test_source_masks_are_reproducible_white_alpha_and_mirrored(self):
        spec = importlib.util.spec_from_file_location('light_textures', ROOT / 'tools/generate_controller_light_textures.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            module.TEXTURES = __import__('pathlib').Path(directory)
            module.main()
            decoded = {}
            for suffix, size in (('ds4', (200, 36)), ('left', (16, 128)), ('right', (16, 128))):
                name = 'controller_light_' + suffix + '.png'
                original = (ROOT / 'cxml/texture' / name).read_bytes()
                self.assertEqual((module.TEXTURES / name).read_bytes(), original)
                width, height, depth, color = struct.unpack('>IIBB', original[16:26])
                self.assertEqual((width, height, depth, color), (*size, 8, 6))
                offset, payload = 8, bytearray()
                while offset < len(original):
                    length = struct.unpack('>I', original[offset:offset + 4])[0]
                    if original[offset + 4:offset + 8] == b'IDAT':
                        payload.extend(original[offset + 8:offset + 8 + length])
                    offset += length + 12
                pixels = zlib.decompress(payload)
                rows = [pixels[y * (width * 4 + 1) + 1:(y + 1) * (width * 4 + 1)] for y in range(height)]
                self.assertTrue(all(row[x:x + 3] == b'\xff\xff\xff' for row in rows for x in range(0, width * 4, 4)))
                alpha = [list(row[3::4]) for row in rows]
                # The DS4 bar can face either way; its outer halo fades at an edge.
                self.assertLess(min(max(alpha[0]), max(alpha[-1])), 3)
                self.assertGreater(max(map(max, alpha)), 100)
                decoded[suffix] = alpha
            self.assertEqual(decoded['left'], [list(reversed(row)) for row in decoded['right']])


if __name__ == '__main__':
    unittest.main()
