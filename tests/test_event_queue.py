"""Interpret the real queue/dispatch source; no compiler, PAF, allocator or threads.

C struct assignment, array writes, addresses and the queue's small loops are modeled.
The payload tests preserve pointer identity while copying inline event fields by value.
"""
import ast
import copy
import re
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as c_runtime
from test_xml_source import ROOT, strip_directives


class Buffer:
    def __init__(self, text='', size=256):
        self.data = list(text.encode()) + [0]
        self.data = (self.data + [0] * size)[:size]

    def __deepcopy__(self, memo):
        return self  # A C address, not an inline struct member.

    def __getitem__(self, index):
        return self.data[index]

    def __setitem__(self, index, value):
        self.data[index] = value

    def text(self):
        end = self.data.index(0) if 0 in self.data else len(self.data)
        return bytes(self.data[:end]).decode()


def event_value(kind=0, result=0, address=0):
    return dict(type=kind, result=result, address=address, host_id=7, application_id=42,
                pairing_pin=[ord(c) for c in '1234\0'], server_status_code=409,
                server_status_message=[ord(c) for c in 'pending session\0'])


def slot():
    return dict(event=event_value(), address=Buffer(), sequence=0)


class EventQueue(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        header = strip_directives((ROOT / 'include/app/event_queue.h').read_text())
        header = header.replace('extern "C" {', '').replace('\n}\n', '\n')
        types = strip_directives((ROOT / 'include/moonlight/types.h').read_text())
        source = strip_directives((ROOT / 'src/app/event_queue.c').read_text())
        app = (ROOT / 'src/app/moonlight_app.cpp').read_text()
        dispatch = app[app.index('void MoonlightApp::ProcessEventTask('):]
        dispatch = dispatch.replace('MoonlightApp::', '')
        dispatch = dispatch.replace('paf::thread::RMutex::main_thread_mutex.Lock()', 'main_lock()')
        dispatch = dispatch.replace('paf::thread::RMutex::main_thread_mutex.Unlock()', 'main_unlock()')
        dispatch = dispatch.replace('app->m_connection.GetCompletedJobEvent', 'job_completion')
        dispatch = dispatch.replace('app->m_connection.PrepareCompletion', 'prepare_completion')
        cls.unit = c_parser.CParser().parse(
            'typedef unsigned short uint16_t; typedef unsigned long long uint64_t; typedef int bool;'
            + types + header + source +
            'typedef void (*MoonlightAppEventCallback)(MoonlightEvent *, void *);'
            'typedef struct { MoonlightEventQueue m_events; MoonlightAppEventCallback m_event_callback;'
            'void *m_event_userdata; } MoonlightApp;' + strip_directives(dispatch))
        cls.functions = {item.decl.name: item for item in cls.unit.ext if isinstance(item, c_ast.FuncDef)}
        cls.constants = dict(NULL=0, false=0, true=1)
        for declaration in cls.unit.ext:
            enum = declaration.type.type if isinstance(declaration, c_ast.Typedef) else (
                declaration.type if isinstance(declaration, c_ast.Decl) else None)
            if isinstance(enum, c_ast.Enum) and enum.values:
                value = -1
                for member in enum.values.enumerators:
                    value = c_runtime.expression(member.value, cls.constants) if member.value else value + 1
                    cls.constants[member.name] = value

    def setUp(self):
        self.queue = dict(ordered=[slot() for _ in range(16)], snapshots=[slot(), slot()],
                          snapshot_pending=[0, 0], pending=slot())
        self.base_expression = c_runtime.expression
        self.base_execute = c_runtime.execute
        self.library = dict(self.constants, strncpy=self.strncpy, sce_paf_strncpy=self.strncpy)
        for name in self.functions:
            self.library[name] = lambda *args, name=name: self.invoke(name, *args)
        self.invoke('moonlight_event_queue_reset', self.queue)

    @staticmethod
    def strncpy(destination, source, count):
        ended = False
        for i in range(count):
            value = 0 if ended else source[i]
            ended = ended or value == 0
            destination[i] = value

    def expression(self, node, values):
        if isinstance(node, c_ast.UnaryOp):
            if node.op in ('&', '*'):
                return self.expression(node.expr, values)
            if node.op == 'sizeof':
                return len(self.expression(node.expr, values).data)
            if node.op in ('++', '--'):
                value = self.expression(node.expr, values) + (1 if node.op == '++' else -1)
                self.assign(node.expr, value, values)
                return value
        if isinstance(node, c_ast.Constant) and node.type == 'char':
            return ord(ast.literal_eval(node.value))
        if isinstance(node, c_ast.BinaryOp) and node.op == '%':
            return self.expression(node.left, values) % self.expression(node.right, values)
        return self.base_expression(node, values)

    def assign(self, target, value, values):
        value = copy.deepcopy(value)
        if isinstance(target, c_ast.ID):
            values[target.name] = value
        elif isinstance(target, c_ast.StructRef):
            self.expression(target.name, values)[target.field.name] = value
        elif isinstance(target, c_ast.ArrayRef):
            self.expression(target.name, values)[self.expression(target.subscript, values)] = value
        elif isinstance(target, c_ast.UnaryOp) and target.op == '*':
            destination = self.expression(target.expr, values)
            destination.clear()
            destination.update(value)
        else:
            self.fail(f'Unsupported lvalue {target}')

    def execute(self, node, values):
        if isinstance(node, c_ast.Assignment):
            self.assertEqual(node.op, '=')
            self.assign(node.lvalue, self.expression(node.rvalue, values), values)
        elif isinstance(node, c_ast.DeclList):
            for declaration in node.decls:
                self.execute(declaration, values)
        elif isinstance(node, c_ast.UnaryOp):
            self.expression(node, values)
        elif isinstance(node, c_ast.Decl) and not node.init:
            if isinstance(node.type, c_ast.ArrayDecl):
                values[node.name] = Buffer(size=self.expression(node.type.dim, values))
            elif isinstance(node.type, c_ast.TypeDecl) and node.type.type.names == ['MoonlightEvent']:
                values[node.name] = event_value()
            else:
                values[node.name] = None
        else:
            self.base_execute(node, values)

    def invoke(self, name, *arguments):
        function = self.functions[name]
        values = dict(self.library)
        values.update(zip((param.name for param in function.decl.type.args.params), arguments))
        with patch.object(c_runtime, 'expression', self.expression), patch.object(c_runtime, 'execute', self.execute):
            try:
                self.execute(function.body, values)
            except c_runtime.Returned as result:
                return result.value

    def event(self, name, result=0, address=0):
        return event_value(self.constants['MOONLIGHT_EVENT_' + name], result, address)

    def push(self, name, result=0, address=0):
        return self.invoke('moonlight_event_queue_push', self.queue, self.event(name, result, address))

    def peek(self):
        result = event_value()
        ready = self.invoke('moonlight_event_queue_peek', self.queue, result)
        return result if ready else None

    def ack(self):
        self.invoke('moonlight_event_queue_acknowledge', self.queue)

    def drain(self):
        events = []
        while (event := self.peek()) is not None:
            events.append(event)
            self.ack()
            self.assertLess(len(events), 30)
        return events

    def test_discovery_burst_cannot_displace_pairing_stream_and_close(self):
        expected = ['PAIRING_FINISHED', 'STREAM_STARTED', 'STREAM_STOPPED', 'CONNECTION_CLOSED']
        for n in range(200):
            self.assertEqual(self.push('HOSTS_CHANGED', n), 0)
            self.assertEqual(self.push('SETTINGS_CHANGED', n), 0)
            if n in (30, 70, 110, 150):
                self.assertEqual(self.push(expected[(n - 30) // 40]), 0)
        actual = self.drain()
        self.assertEqual([e['type'] for e in actual[:4]],
                         [self.constants['MOONLIGHT_EVENT_' + kind] for kind in expected])
        self.assertEqual([e['result'] for e in actual[4:]], [199, 199])
        self.assertIsNone(self.peek())

    def test_failures_and_scan_boundaries_remain_ordered_and_are_not_coalesced(self):
        kinds = ['HOST_SCAN_STARTED', 'HOST_SCAN_FAILED', 'HOST_SCAN_FINISHED',
                 'PAIRING_REQUIRED', 'PAIRING_FAILED', 'STREAM_FAILED', 'SETTINGS_CLOSED']
        for n, name in enumerate(kinds):
            for occurrence in range(2):
                self.assertEqual(self.push(name, 2 * n + occurrence), 0)
        self.assertEqual([e['result'] for e in self.drain()], list(range(14)))

    def test_latest_snapshot_position_preserves_lifecycle_order(self):
        for kind, value in [('HOSTS_CHANGED', 1), ('PAIRING_REQUIRED', 2), ('HOSTS_CHANGED', 3),
                            ('PAIRING_FINISHED', 4), ('SETTINGS_CHANGED', 5), ('SETTINGS_CHANGED', 6),
                            ('STREAM_STARTED', 7), ('HOSTS_CHANGED', 8), ('SETTINGS_CLOSED', 9)]:
            self.push(kind, value)
        self.assertEqual([e['result'] for e in self.drain()], [2, 4, 6, 7, 8, 9])

    def test_pending_event_owns_payload_while_producers_update_or_wrap_slots(self):
        address = Buffer('original-host')
        event = self.event('PAIRING_FAILED', -1003, address)
        self.assertEqual(self.invoke('moonlight_event_queue_push', self.queue, event), 0)
        event['pairing_pin'][0] = 0
        event['server_status_message'][0] = 0
        address[0] = ord('X')
        retained = self.peek()
        self.assertEqual(retained['address'].text(), 'original-host')
        self.assertEqual(retained['pairing_pin'][0], ord('1'))
        self.assertEqual(retained['server_status_message'][0], ord('p'))
        for n in range(16):
            self.assertEqual(self.push('STREAM_STARTED', n, Buffer('other-host')), 0)
            self.push('HOSTS_CHANGED', n, Buffer('latest'))
        for _ in range(3):
            self.assertEqual(self.peek(), retained)
        self.ack()
        self.assertEqual([e['result'] for e in self.drain()], list(range(16)) + [15])

    def test_snapshot_update_during_pending_delivery_remains_for_next_ack(self):
        self.push('HOSTS_CHANGED', 1, Buffer('one'))
        first = self.peek()
        self.push('HOSTS_CHANGED', 2, Buffer('two'))
        self.assertEqual(self.peek()['result'], 1)
        self.assertEqual(first['address'].text(), 'one')
        self.ack()
        self.assertEqual(self.peek()['result'], 2)
        self.assertEqual(self.peek()['address'].text(), 'two')
        self.ack()
        self.assertIsNone(self.peek())

    def test_explicit_ordered_overload_does_not_overwrite_accepted_events(self):
        for n in range(16):
            self.assertEqual(self.push('STREAM_STOPPED', n), 0)
        self.assertEqual(self.push('STREAM_FAILED', -7), -1)
        self.assertEqual(self.push('HOSTS_CHANGED', 200), 0)
        self.assertEqual(self.push('SETTINGS_CHANGED', 300), 0)
        self.assertEqual([e['result'] for e in self.drain()], list(range(16)) + [200, 300])

    def test_reset_and_repeated_wrap_do_not_replay_acknowledged_or_abandoned_events(self):
        for n in range(100):
            self.push('STREAM_STARTED', n)
            self.push('STREAM_STOPPED', n)
            self.assertEqual([e['result'] for e in self.drain()], [n, n])
        self.push('PAIRING_FINISHED')
        self.peek()
        self.push('HOSTS_CHANGED')
        self.push('CONNECTION_CLOSED')
        self.invoke('moonlight_event_queue_reset', self.queue)
        self.assertIsNone(self.peek())
        self.push('PAIRING_FAILED', -1001)
        self.assertEqual([e['result'] for e in self.drain()], [-1001])

    def test_null_and_overlong_addresses_are_copied_and_terminated(self):
        self.push('STREAM_FAILED', -7, Buffer('a' * 300, size=301))
        self.assertEqual(self.peek()['address'].text(), 'a' * 255)
        self.ack()
        self.push('STREAM_STOPPED')
        self.assertEqual(self.peek()['address'], 0)

    def dispatch(self, prepare, callback, fallback=lambda event: 0):
        locked = [False]

        def lock():
            self.assertFalse(locked[0])
            locked[0] = True

        def unlock():
            self.assertTrue(locked[0])
            locked[0] = False

        def prepare_unlocked(event):
            self.assertFalse(locked[0])
            return prepare(event)

        def callback_unlocked(event, userdata):
            self.assertFalse(locked[0])
            self.assertEqual(self.queue['pending_valid'], 0)
            callback(event)

        self.library.update(main_lock=lock, main_unlock=unlock, prepare_completion=prepare_unlocked,
                            job_completion=fallback, disconnect_test_poll=lambda app: None,
                            disconnect_test_event=lambda app, event: None, moonlight_log_memory_budget=lambda label: None)
        self.invoke('ProcessEventTask', dict(m_events=self.queue, m_event_callback=callback_unlocked,
                                            m_event_userdata=0))
        self.assertFalse(locked[0])

    def test_dispatch_waits_for_worker_without_replaying_or_losing_following_close(self):
        self.push('STREAM_FAILED', -7, Buffer('host'))
        self.push('CONNECTION_CLOSED')
        seen = []
        self.dispatch(lambda event: 0, lambda event: seen.append(event['type']))
        self.assertEqual(seen, [])
        self.push('HOSTS_CHANGED', 3)
        self.dispatch(lambda event: 1, lambda event: seen.append(event['type']))
        self.assertEqual(seen, [self.constants['MOONLIGHT_EVENT_' + name]
                               for name in ('STREAM_FAILED', 'CONNECTION_CLOSED', 'HOSTS_CHANGED')])
        self.dispatch(lambda event: 1, lambda event: seen.append(event['type']))
        self.assertEqual(len(seen), 3)

    def test_dispatch_ack_precedes_callback_and_keeps_snapshot_enqueued_by_callback(self):
        self.push('HOSTS_CHANGED', 1)
        seen = []

        def callback(event):
            seen.append(event['result'])
            if len(seen) == 1:
                self.push('HOSTS_CHANGED', 2)
                self.push('STREAM_STARTED', 3)

        self.dispatch(lambda event: 1, callback)
        self.assertEqual(seen, [1, 2, 3])

    def test_producer_during_worker_wait_cannot_replace_the_extracted_completion(self):
        self.push('CONNECTION_READY', 42, Buffer('selected-host'))
        seen = []

        def waiting(event):
            for n in range(100):
                self.push('HOSTS_CHANGED', n, Buffer('discovered-host'))
            for n in range(16):
                self.assertEqual(self.push('STREAM_STOPPED', n), 0)
            self.assertEqual(event['result'], 42)
            self.assertEqual(event['address'].text(), 'selected-host')
            return 0

        self.dispatch(waiting, lambda event: seen.append(event['result']))
        self.assertEqual(seen, [])
        self.dispatch(lambda event: 1, lambda event: seen.append(event['result']))
        self.assertEqual(seen, [42, 99] + list(range(16)))

    def test_retained_connect_job_fallback_still_completes_once_when_queue_empty(self):
        completed = [False]
        seen = []

        def fallback(event):
            if completed[0]: return 0
            event.update(self.event('CONNECTION_READY'))
            return 1

        def callback(event):
            completed[0] = True
            seen.append(event['type'])

        self.dispatch(lambda event: 1, callback, fallback)
        self.assertEqual(seen, [self.constants['MOONLIGHT_EVENT_CONNECTION_READY']])


if __name__ == '__main__':
    unittest.main()
