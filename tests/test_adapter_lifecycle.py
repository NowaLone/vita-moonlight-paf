"""Interpret adapter methods with lifecycle callbacks before/after API return.

No C++ compilation, PAF execution or real threads; this tests ordering semantics only.
"""
import re
import unittest

from pycparser import c_ast, c_parser
from test_xml_source import ROOT, strip_directives


class Returned(Exception):
    def __init__(self, value):
        self.value = value


class BreakSwitch(Exception):
    pass


def expression(node, values):
    if isinstance(node, c_ast.ID):
        return values[node.name]
    if isinstance(node, c_ast.Constant):
        return int(node.value)
    if isinstance(node, c_ast.Cast):
        return expression(node.expr, values)
    if isinstance(node, c_ast.StructRef):
        return expression(node.name, values)[node.field.name]
    if isinstance(node, c_ast.ArrayRef):
        return expression(node.name, values)[expression(node.subscript, values)]
    if isinstance(node, c_ast.UnaryOp):
        if node.op == '&' and isinstance(node.expr, c_ast.ID):
            return node.expr.name
        if node.op == 'sizeof':
            # Size is only passed to mocked memory helpers, never used for layout checks.
            return 1
        operand = expression(node.expr, values)
        if node.op == '!':
            return int(not operand)
        if node.op == '-':
            return -operand
    if isinstance(node, c_ast.BinaryOp):
        left = expression(node.left, values)
        if node.op == '&&':
            return int(bool(left) and bool(expression(node.right, values)))
        if node.op == '||':
            return int(bool(left) or bool(expression(node.right, values)))
        right = expression(node.right, values)
        if node.op == '==':
            return int(left == right)
        if node.op == '!=':
            return int(left != right)
    if isinstance(node, c_ast.FuncCall):
        if node.name.name == 'vita_debug_log':
            return None
        arguments = [] if node.args is None else [
            expression(arg, values) for arg in node.args.exprs]
        return values[node.name.name](*arguments)
    if isinstance(node, c_ast.TernaryOp):
        branch = node.iftrue if expression(node.cond, values) else node.iffalse
        return expression(branch, values)
    raise AssertionError(f'Unsupported expression: {node}')


def execute(node, values):
    if node is None:
        return
    if isinstance(node, c_ast.Compound):
        for item in node.block_items or []:
            execute(item, values)
    elif isinstance(node, c_ast.Decl):
        values[node.name] = expression(node.init, values) if node.init else None
    elif isinstance(node, c_ast.Assignment):
        assert node.op == '=' and isinstance(node.lvalue, c_ast.ID)
        values[node.lvalue.name] = expression(node.rvalue, values)
    elif isinstance(node, c_ast.If):
        execute(node.iftrue if expression(node.cond, values) else node.iffalse, values)
    elif isinstance(node, c_ast.Return):
        raise Returned(expression(node.expr, values) if node.expr else None)
    elif isinstance(node, c_ast.Break):
        raise BreakSwitch()
    elif isinstance(node, c_ast.While):
        iterations = 0
        while expression(node.cond, values):
            iterations += 1
            assert iterations < 100, 'Mocked loop did not terminate'
            execute(node.stmt, values)
    elif isinstance(node, c_ast.For):
        execute(node.init, values)
        iterations = 0
        while node.cond is None or expression(node.cond, values):
            iterations += 1
            assert iterations < 100, 'Mocked loop did not terminate'
            try:
                execute(node.stmt, values)
            except BreakSwitch:
                break
            execute(node.next, values)
    elif isinstance(node, c_ast.Switch):
        selected = expression(node.cond, values)
        active = False
        try:
            for case in node.stmt.block_items:
                active = active or isinstance(case, c_ast.Default) or (
                    expression(case.expr, values) == selected)
                if active:
                    for item in case.stmts or []:
                        execute(item, values)
        except BreakSwitch:
            pass
    elif isinstance(node, c_ast.FuncCall):
        expression(node, values)
    else:
        raise AssertionError(f'Unsupported statement: {node}')


class AdapterLifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'src/backend/legacy_moonlight_adapter.cpp').read_text()
        types = (ROOT / 'include/moonlight/types.h').read_text()
        enums = '\n'.join(re.search(
            r'typedef enum ' + name + r'\s*{.*?}\s*' + name + ';', types, re.S).group()
            for name in ('MoonlightConnectionState', 'MoonlightEventType'))
        functions = []
        for name in ('StartApplication', 'StopApplication', 'DisconnectHost',
                     'ConnectHost', 'PairCurrentHost', 'ForwardEvent'):
            start = source.index('LegacyMoonlightAdapter::' + name + '(')
            end = source.index('\n}', start) + 2
            result_type = 'void ' if name == 'ForwardEvent' else 'int '
            functions.append(result_type + source[start:end].replace(
                'LegacyMoonlightAdapter::', '').replace('MoonlightHost &host', 'MoonlightHost *host'))
        unit = c_parser.CParser().parse(strip_directives(
            'typedef struct { int type; } MoonlightEvent;'
            'typedef struct { char internal[256]; } MoonlightHost;'
            'typedef int MoonlightCancellation;' + enums + '\n'.join(functions)))
        cls.functions = {item.decl.name: item.body for item in unit.ext
                         if isinstance(item, c_ast.FuncDef)}
        cls.constants = {'false': 0, 'true': 1}
        for declaration in unit.ext:
            if isinstance(declaration, c_ast.Typedef) and isinstance(declaration.type.type, c_ast.Enum):
                value = -1
                for member in declaration.type.type.values.enumerators:
                    value = expression(member.value, cls.constants) if member.value else value + 1
                    cls.constants[member.name] = value

    def test_api_acceptance_does_not_replace_lifecycle_events(self):
        paired = self.constants['MOONLIGHT_CONNECTION_PAIRED']
        streaming = self.constants['MOONLIGHT_CONNECTION_STREAMING']
        disconnected = self.constants['MOONLIGHT_CONNECTION_DISCONNECTED']
        cases = (
            ('StartApplication', 'moonlight_api_start_application', paired,
             {'STREAM_STARTED': streaming, 'STREAM_FAILED': paired, 'STREAM_STOPPED': paired}),
            ('StopApplication', 'moonlight_api_stop_application', streaming,
             {'STREAM_STOPPED': paired, 'STREAM_FAILED': paired}),
            ('DisconnectHost', 'moonlight_api_disconnect_host', paired,
             {'CONNECTION_CLOSED': disconnected}),
            ('DisconnectHost', 'moonlight_api_disconnect_host', streaming,
             {'STREAM_STOPPED': paired, 'CONNECTION_CLOSED': disconnected}),
        )
        for method, api, initial, events in cases:
            for event, terminal in events.items():
                for timing in ('before_return', 'after_return'):
                    with self.subTest(method=method, event=event, timing=timing):
                        values = dict(self.constants, m_connection_state=initial,
                                      m_has_current_host=1, m_event_callback=0, application_id=42)

                        def deliver():
                            values['event'] = {'type': self.constants['MOONLIGHT_EVENT_' + event]}
                            execute(self.functions['ForwardEvent'], values)

                        def backend(*args):
                            if timing == 'before_return':
                                deliver()
                            return 0

                        values[api] = backend
                        with self.assertRaises(Returned) as returned:
                            execute(self.functions[method], values)
                        self.assertEqual(returned.exception.value, 0)
                        self.assertEqual(values['m_connection_state'],
                                         terminal if timing == 'before_return' else initial)
                        if timing == 'after_return':
                            deliver()
                            self.assertEqual(values['m_connection_state'], terminal)
                        self.assertEqual(values['m_has_current_host'], event != 'CONNECTION_CLOSED')

    def test_synchronous_failure_preserves_state(self):
        for method, api, state in (
                ('StartApplication', 'moonlight_api_start_application', 'PAIRED'),
                ('StopApplication', 'moonlight_api_stop_application', 'STREAMING'),
                ('DisconnectHost', 'moonlight_api_disconnect_host', 'PAIRED')):
            for result in (-8, -1, 12):
                with self.subTest(method=method, result=result):
                    initial = self.constants['MOONLIGHT_CONNECTION_' + state]
                    values = dict(self.constants, m_connection_state=initial,
                                  m_has_current_host=1, application_id=42)
                    values[api] = lambda *args: result
                    with self.assertRaises(Returned) as returned:
                        execute(self.functions[method], values)
                    self.assertEqual(returned.exception.value, result)
                    self.assertEqual(values['m_connection_state'], initial)

    def test_connect_or_pair_return_cannot_resurrect_a_closed_host(self):
        for method, api in (
                ('ConnectHost', 'moonlight_api_connect_host'),
                ('PairCurrentHost', 'moonlight_api_pair_current_host')):
            with self.subTest(method=method):
                values = dict(self.constants, m_has_current_host=1, m_event_callback=0,
                              m_connection_state=self.constants['MOONLIGHT_CONNECTION_READY'],
                              host={'internal': [1]}, pin=1, cancel=0)
                values['moonlight_api_get_connection_state'] = lambda: self.constants[
                    'MOONLIGHT_CONNECTION_PAIRED']

                def backend(*args):
                    # The API releases admission as it returns; another thread can
                    # complete disconnect before the adapter caller resumes.
                    for event in ('CONNECTION_READY' if method == 'ConnectHost'
                                  else 'PAIRING_FINISHED', 'CONNECTION_CLOSED'):
                        values['event'] = {'type': self.constants['MOONLIGHT_EVENT_' + event]}
                        execute(self.functions['ForwardEvent'], values)
                    return 0

                values[api] = backend
                with self.assertRaises(Returned) as returned:
                    execute(self.functions[method], values)
                self.assertEqual(returned.exception.value, 0)
                self.assertEqual(values['m_connection_state'],
                                 self.constants['MOONLIGHT_CONNECTION_DISCONNECTED'])
                self.assertFalse(values['m_has_current_host'])

    def test_connection_ready_publishes_actual_pairing_state(self):
        for state in ('READY', 'PAIRED'):
            with self.subTest(state=state):
                expected = self.constants['MOONLIGHT_CONNECTION_' + state]
                values = dict(self.constants, m_has_current_host=0, m_event_callback=0,
                              m_connection_state=self.constants['MOONLIGHT_CONNECTION_DISCONNECTED'],
                              event={'type': self.constants['MOONLIGHT_EVENT_CONNECTION_READY']})
                values['moonlight_api_get_connection_state'] = lambda: expected
                execute(self.functions['ForwardEvent'], values)
                self.assertTrue(values['m_has_current_host'])
                self.assertEqual(values['m_connection_state'], expected)

    def test_no_host_rejects_without_calling_api(self):
        for method in ('StartApplication', 'StopApplication'):
            with self.subTest(method=method):
                initial = self.constants['MOONLIGHT_CONNECTION_DISCONNECTED']
                values = dict(self.constants, m_connection_state=initial,
                              m_has_current_host=0, application_id=42)
                # No backend stub is installed: a call would fail the test.
                with self.assertRaises(Returned) as returned:
                    execute(self.functions[method], values)
                self.assertEqual(returned.exception.value, -1)
                self.assertEqual(values['m_connection_state'], initial)


if __name__ == '__main__':
    unittest.main()
