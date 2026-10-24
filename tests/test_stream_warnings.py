"""Interpret warning/counter/UI bodies with mocked time, locks and PAF; no builds or Vita."""
import copy
from collections import defaultdict
import re
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives

STATS = (ROOT / 'src/moonlight/stream_stats.c').read_text()
PAGE = (ROOT / 'src/pages/page_stream.cpp').read_text()
SESSION = (ROOT / 'src/moonlight/stream_session.cpp').read_text()
NONE, CONNECTION, LOW, STALLED = range(4)


def function(source, name):
    start = re.search(r'^(?:static |extern "C" )?(?:void|int|uint32_t|MoonlightStreamWarning) '
                      + name + r'\(', source, re.M).start()
    return source[start:source.index('\n}', start) + 2]


class Harness:
    def __init__(self):
        code = '\n'.join(function(STATS, name) for name in (
            'begin_update', 'moonlight_stats_begin', 'moonlight_stats_end',
            'moonlight_stats_configure_video', 'moonlight_stats_connection_status',
            'moonlight_stats_received', 'stream_warning'))
        code += function(SESSION, 'on_status_update')
        ui = function(PAGE, 'Stream::UpdateWarning').replace('Stream::', '')
        ui = ui.replace('const MoonlightStreamStats &stats', 'const MoonlightStreamStats *stats')
        ui = ui.replace('common::transition::Type_Reset', '0')
        ui = ui.replace('g_plugin->GetString', 'get_label').replace('paf::wstring(label)', 'label')
        ui = re.sub(r'(m_warning_\w+)->(Hide|Show|SetString)\(([^)]*)\)', r'widget_\2(\1, \3)', ui)
        header = strip_directives((ROOT / 'include/moonlight/stream_stats.h').read_text())
        preamble = ('typedef unsigned int uint32_t; typedef unsigned long long uint64_t;'
                    'typedef int MoonlightSettings; typedef int wchar_t;')
        unit = c_parser.CParser().parse(preamble + header + strip_directives(code + ui))
        self.bodies = {node.decl.name: node.body for node in unit.ext if isinstance(node, c_ast.FuncDef)}
        self.now, self.locks, self.widgets, self.timings = 0, set(), [], []
        self.values = dict(s_mutex='stats', s_stream_mutex='stream',
                           s_session=defaultdict(int), s_current=defaultdict(int),
                           s_previous=defaultdict(int), s_start_us=0, s_bucket_start_us=0,
                           s_last_video_us=0, s_last_frame=0, s_has_last_frame=0,
                           m_warning=NONE, m_warning_until_us=0,
                           m_warning_text='text', m_warning_background='background', g_plugin=1,
                           s_stream_thread_running=1, s_stream_stop_requested=0,
                           s_stream_cancel_requested=0, s_stream_stats_generation=1,
                           NULL=0, CONN_STATUS_POOR=1, TIMING_REASSEMBLY=0)
        for key, value in zip(('NONE', 'CONNECTION', 'LOW_BITRATE', 'STALLED'), range(4)):
            self.values['MOONLIGHT_STREAM_WARNING_' + key] = value
        self.values.update(LiGetMicroseconds=lambda: self.now, advance_window=lambda now: None,
                           pthread_mutex_lock=self.lock, pthread_mutex_unlock=self.unlock,
                           memset=lambda name, value, size: self.values[name].clear(),
                           note_timing=lambda *args: self.timings.append(args),
                           get_label=lambda key: key,
                           widget_Hide=lambda *args: self.widgets.append(('hide', *args)),
                           widget_Show=lambda *args: self.widgets.append(('show', *args)),
                           widget_SetString=lambda *args: self.widgets.append(('text', *args)))
        for name in ('begin_update', 'moonlight_stats_connection_status'):
            self.values[name] = lambda *args, name=name: self.invoke(name, generation=args[0],
                                                   **({'poor': args[1]} if len(args) > 1 else {}))

    def lock(self, name):
        assert name not in self.locks
        self.locks.add(name)

    def unlock(self, name):
        assert name in self.locks
        self.locks.remove(name)

    def expression(self, node, values):
        if isinstance(node, c_ast.Assignment):
            self.execute(node, values)
            return self.expression(node.lvalue, values)
        if isinstance(node, c_ast.Constant) and node.type != 'string':
            return float(node.value) if node.type in ('float', 'double') else int(node.value.rstrip('uUlL'), 0)
        if isinstance(node, c_ast.BinaryOp) and node.op == '*':
            return self.expression(node.left, values) * self.expression(node.right, values)
        return self.original_expression(node, values)

    def execute(self, node, values):
        if isinstance(node, c_ast.UnaryOp) and node.op in ('++', 'p++'):
            node = c_ast.Assignment('+=', node.expr, c_ast.Constant('int', '1'))
        if isinstance(node, c_ast.Assignment):
            if isinstance(node.lvalue, c_ast.UnaryOp) and node.lvalue.op == '*':
                values[self.expression(node.lvalue.expr, values)] = self.expression(node.rvalue, values)
                return
            if node.op == '+=':
                node = c_ast.Assignment('=', node.lvalue, c_ast.BinaryOp('+', node.lvalue, node.rvalue))
        self.original_execute(node, values)

    original_expression = staticmethod(runtime.expression)
    original_execute = staticmethod(runtime.execute)

    def invoke(self, name, **parameters):
        self.values.update(parameters)
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            try:
                runtime.execute(self.bodies[name], self.values)
            except runtime.Returned as result:
                return result.value

    def begin(self, enabled=0):
        return self.invoke('moonlight_stats_begin', settings=defaultdict(int, show_fps=enabled, bitrate=10000, fps=60))


class StreamWarnings(unittest.TestCase):
    def test_policy_static_scene_thresholds_startup_stop_and_stall(self):
        h = Harness()
        base = dict(active=1, has_video=1, video_age_us=0, connection_poor=0,
                    missing_percent=0.0, bitrate_kbps=10000, payload_mbps=0.1)
        cases = (({}, NONE), ({'connection_poor': 1}, LOW),
                 ({'missing_percent': 5.0}, LOW), ({'missing_percent': 4.99}, NONE),
                 ({'connection_poor': 1, 'payload_mbps': 5.0}, CONNECTION),
                 ({'connection_poor': 1, 'bitrate_kbps': 0}, CONNECTION),
                 ({'video_age_us': 999999}, NONE), ({'video_age_us': 1000000}, STALLED),
                 ({'has_video': 0}, STALLED), ({'active': 0, 'connection_poor': 1}, NONE))
        for changes, expected in cases:
            with self.subTest(changes=changes):
                self.assertEqual(h.invoke('stream_warning', stats=dict(base, **changes), elapsed_us=3000000), expected)
                self.assertEqual(h.invoke('stream_warning', elapsed_us=2999999), NONE)

    def test_receive_counters_without_detailed_stats_and_generation_rejection(self):
        for enabled in (0, 1):
            h = Harness()
            generation = h.begin(enabled)
            self.assertEqual(h.invoke('moonlight_stats_configure_video', format=1, width=960,
                                     height=544, fps=60, receive_generation='receive_token'),
                             generation if enabled else 0)
            self.assertEqual(h.values['receive_token'], generation)
            h.now = 4000000
            for frame in (10, 12):
                h.invoke('moonlight_stats_received', generation=generation, frame_number=frame,
                         bytes=1000, idr=1, host_latency=10, receive_us=1, enqueue_us=2)
            self.assertEqual(h.values['s_current']['bytes'], 2000)
            self.assertEqual(h.values['s_current']['missing'], 1)
            self.assertEqual(h.values['s_last_video_us'], h.now)
            self.assertEqual(len(h.timings), 2 if enabled else 0)
            before = copy.deepcopy(h.values['s_current'])
            h.invoke('moonlight_stats_received', generation=generation + 1, frame_number=100)
            self.assertEqual(h.values['s_current'], before)
            h.invoke('moonlight_stats_end', generation=generation)
            h.invoke('moonlight_stats_received', generation=generation)
            self.assertEqual(h.values['s_current'], before)
            self.assertEqual(h.locks, set())
            self.assertNotEqual(h.begin(enabled), generation)
            self.assertEqual(h.values['s_current'], {})
            self.assertEqual(h.values['s_session']['connection_poor'], 0)

    def test_status_callback_stop_guard_recovery_and_stale_token(self):
        h = Harness()
        generation = h.begin()
        h.values['s_stream_stats_generation'] = generation
        h.invoke('on_status_update', status=1)
        self.assertEqual(h.values['s_session']['connection_poor'], 1)
        h.invoke('on_status_update', status=0)
        self.assertEqual(h.values['s_session']['connection_poor'], 0)
        for guard in ('s_stream_stop_requested', 's_stream_cancel_requested'):
            h.values[guard] = 1
            h.invoke('on_status_update', status=1)
            self.assertEqual(h.values['s_session']['connection_poor'], 0)
            h.values[guard] = 0
        h.invoke('moonlight_stats_connection_status', generation=generation + 1, poor=1)
        self.assertEqual(h.values['s_session']['connection_poor'], 0)
        self.assertEqual(h.locks, set())

    def test_ui_hold_refresh_change_and_immediate_stop(self):
        h = Harness()
        h.invoke('UpdateWarning', stats=dict(active=1, warning=CONNECTION), now_us=10000000)
        self.assertEqual(h.widgets[0], ('text', 'text', 'msg_stream_connection_poor'))
        self.assertEqual(len(h.widgets), 3)
        h.invoke('UpdateWarning', now_us=11000000)
        self.assertEqual(len(h.widgets), 3)  # No repeated SetString/Show while unchanged.
        h.invoke('UpdateWarning', stats=dict(active=1, warning=NONE), now_us=13999999)
        self.assertEqual(len(h.widgets), 3)
        h.invoke('UpdateWarning', now_us=14000000)
        self.assertEqual(h.widgets[-2:], [('hide', 'text', 0), ('hide', 'background', 0)])
        h.invoke('UpdateWarning', stats=dict(active=1, warning=LOW), now_us=15000000)
        self.assertEqual(h.widgets[-3][2], 'msg_stream_low_bitrate')
        h.invoke('UpdateWarning', stats=dict(active=1, warning=STALLED), now_us=15250000)
        self.assertEqual(h.widgets[-3][2], 'msg_stream_video_stalled')
        h.invoke('UpdateWarning', stats=dict(active=0, warning=STALLED), now_us=15500000)
        self.assertEqual(h.values['m_warning'], NONE)
        self.assertEqual(h.widgets[-1], ('hide', 'background', 0))

    def test_resource_and_poll_wiring(self):
        ui = ET.parse(ROOT / 'cxml/vita_moonlight_ui.xml').getroot()
        page = ui.find("./pagetable/page[@id='page_stream']")
        self.assertIsNotNone(page.find("./plane[@id='plane_stream_warning']"))
        self.assertIsNotNone(page.find("./text[@id='text_stream_warning']"))
        labels = {item.get('id') for item in ET.parse(ROOT / 'cxml/locale/en.xml').getroot()}
        for key in ('msg_stream_connection_poor', 'msg_stream_low_bitrate', 'msg_stream_video_stalled'):
            self.assertIn(key, labels)
        self.assertEqual(PAGE.count('Register(StatisticsTask, this)'), 1)
        self.assertEqual(PAGE.count('Unregister(StatisticsTask, this)'), 1)
        update = function(PAGE, 'Stream::UpdateStatistics')
        self.assertLess(update.index('UpdateWarning(stats, now_us);', update.index('return;')),
                        update.index('if (!stats.enabled'))
        self.assertIn('now_us + 250000ull', update)
        self.assertIn('now_us + 1000000ull', update)
        renderer = (ROOT / 'src/moonlight/vita_video_renderer.c').read_text()
        self.assertIn('moonlight_stats_received(s_receive_generation,', renderer)
        self.assertIn('out->warning = stream_warning(out, now - s_start_us);', STATS)


if __name__ == '__main__':
    unittest.main()
