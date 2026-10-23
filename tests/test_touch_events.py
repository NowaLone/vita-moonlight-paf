"""Interpret touch down/move/up tracking; Vita sampling and Moonlight transport are mocked."""
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_host_persistence import function, run
from test_xml_source import ROOT, strip_directives


class TouchEvents(unittest.TestCase):
    def test_stationary_move_to_origin_release_and_reused_ids(self):
        source = (ROOT / 'src/moonlight/vita_touch.c').read_text()
        names = ('send_touch_event', 'process_touch_points') if 'static void process_touch_points(' in source else (
            'process_ds4_touch', 'process_tablet')
        names += ('release_touch_state',)
        code = '\n'.join(function(source, name) for name in names)
        code = code.replace('++i', 'i = i + 1')
        preamble = 'typedef int bool;'
        unit = c_parser.CParser().parse(preamble + strip_directives(code))
        bodies = {node.decl.name: node for node in unit.ext if isinstance(node, c_ast.FuncDef)}
        original = runtime.expression
        original_execute = runtime.execute

        def expression(node, values):
            if isinstance(node, c_ast.Constant) and node.type in ('float', 'double'):
                return float(node.value.rstrip('fF'))
            if isinstance(node, c_ast.BinaryOp) and node.op in ('*', '/'):
                left, right = expression(node.left, values), expression(node.right, values)
                return left * right if node.op == '*' else (
                    left // right if isinstance(left, int) and isinstance(right, int) else left / right)
            return original(node, values)

        def execute(node, values):
            if isinstance(node, c_ast.Decl) and isinstance(node.type, c_ast.ArrayDecl):
                values[node.name] = [0] * expression(node.type.dim, values)
            elif isinstance(node, c_ast.Assignment) and isinstance(node.lvalue, c_ast.ArrayRef):
                expression(node.lvalue.name, values)[expression(node.lvalue.subscript, values)] = expression(node.rvalue, values)
            else:
                original_execute(node, values)

        for mode in (1, 3):
            with self.subTest(mode=mode), patch.object(runtime, 'expression', expression), patch.object(runtime, 'execute', execute):
                events = []
                points = [dict(active=0, x=0, y=0, id=0) for _ in range(6)]
                values = dict(SCE_TOUCH_MAX_REPORT=6, VITA_TOUCH_WIDTH=960.0, VITA_TOUCH_HEIGHT=544.0,
                              true=1, false=0, s_mode=mode, s_points=points,
                              LI_TOUCH_EVENT_DOWN=1, LI_TOUCH_EVENT_MOVE=2, LI_TOUCH_EVENT_UP=3,
                              LI_TOUCH_EVENT_CANCEL_ALL=4, LI_ROT_UNKNOWN=0,
                              s_left_mouse_down=0, s_two_finger_active=0)

                def controller(controller, kind, identity, x, y, pressure):
                    events.append((kind, identity, x, y, pressure))

                values.update(LiSendControllerTouchEvent=controller,
                              LiSendTouchEvent=lambda kind, identity, x, y, pressure, *args:
                                  events.append((kind, identity, x, y, pressure)))

                def invoke(name, *args):
                    node = bodies[name]
                    parameters = node.decl.type.args.params
                    local = dict(values, **{param.name: arg for param, arg in zip(parameters, args)})
                    local['memset'] = lambda pointer, *args: pointer.__setitem__(slice(None), [0] * len(pointer))
                    return run(node.body, local)

                if 'send_touch_event' in bodies:
                    values['send_touch_event'] = lambda *args: invoke('send_touch_event', *args)
                name = 'process_touch_points' if 'process_touch_points' in bodies else (
                    'process_ds4_touch' if mode == 1 else 'process_tablet')

                def frame(*reports):
                    values['s_front'] = dict(reportNum=len(reports), report=[
                        dict(id=identity, x=x * 2, y=y * 2) for identity, x, y in reports])
                    invoke(name)
                    result = list(events)
                    events.clear()
                    return result

                self.assertEqual(frame((2, 96, 54)), [(1, 2, 96 / 960, 54 / 544, 1.0)])
                self.assertEqual(frame((2, 96, 54)), [])
                self.assertEqual(frame((2, 0, 0)), [(2, 2, 0.0, 0.0, 1.0)])
                self.assertEqual(frame(), [(3, 2, 0.0, 0.0, 0.0)])
                self.assertEqual(frame(), [])
                self.assertEqual(frame((2, 96, 54)), [(1, 2, 96 / 960, 54 / 544, 1.0)])
                self.assertEqual(frame((9, 48, 27)), [
                    (1, 0, 48 / 960, 27 / 544, 1.0), (3, 2, 96 / 960, 54 / 544, 0.0)])
                invoke('release_touch_state')
                self.assertEqual(events, [(3, 0, 48 / 960, 27 / 544, 0.0)] if mode == 1 else [
                    (4, 0, 0.0, 0.0, 0.0)])
                self.assertEqual(points, [0] * 6)


if __name__ == '__main__':
    unittest.main()
