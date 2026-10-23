"""Interpret fit/cover size arithmetic; no decoder, PAF, build or Vita execution."""
import re
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives


class VideoScaling(unittest.TestCase):
    def test_fit_cover_rounding_and_wide_cross_products(self):
        source = (ROOT / 'src/moonlight/vita_video_renderer.c').read_text()
        names = ('align_decoder_resolution', 'divide_round_up', 'vita_video_calculate_output_size')
        functions = []
        for name in names:
            start = re.search(r'^static [^\n]+\b' + name + r'\(', source, re.M).start()
            functions.append(source[start:source.index('\n}', start) + 2])
        constants = re.search(r'enum\s*{\s*VITA_SCREEN_WIDTH.*?};', source, re.S).group()
        unit = c_parser.CParser().parse(strip_directives(
            'typedef unsigned long long uint64_t;' + constants + '\n'.join(functions)))
        bodies = {node.decl.name: node for node in unit.ext if isinstance(node, c_ast.FuncDef)}
        original = runtime.expression

        def expression(node, values):
            if isinstance(node, c_ast.Constant):
                return int(node.value.rstrip('uUlL'), 0)
            if isinstance(node, c_ast.Cast):
                value = expression(node.expr, values)
                names = node.to_type.type.type.names
                return value & ((1 << (64 if names == ['uint64_t'] else 32)) - 1)
            if isinstance(node, c_ast.UnaryOp) and node.op == '~':
                return ~expression(node.expr, values)
            if isinstance(node, c_ast.BinaryOp) and node.op in ('*', '/', '&'):
                left, right = expression(node.left, values), expression(node.right, values)
                if node.op == '*':
                    return left * right
                return left // right if node.op == '/' else left & right
            return original(node, values)

        values = {}
        for item in unit.ext:
            if isinstance(item, c_ast.Decl) and isinstance(item.type, c_ast.Enum):
                values.update({entry.name: expression(entry.value, values)
                               for entry in item.type.values.enumerators})

        def invoke(name, *args):
            node = bodies[name]
            local = dict(values, **{param.name: arg for param, arg in
                                   zip(node.decl.type.args.params, args)})
            try:
                runtime.execute(node.body, local)
            except runtime.Returned as result:
                if name != 'vita_video_calculate_output_size':
                    return result.value
            return local['s_frame_width'], local['s_frame_height']

        values.update({name: lambda *args, name=name: invoke(name, *args) for name in names})
        # Zero dimensions, exact ratio, narrow/wide sources, rounding, and cross
        # products beyond UINT32_MAX. Expected dimensions include uint32_t casts.
        cases = (
            (0, 544, (960, 544), (960, 544)),
            (960, 0, (960, 544), (960, 544)),
            (0, 0, (960, 544), (960, 544)),
            (960, 544, (960, 544), (960, 544)),
            (1920, 1088, (960, 544), (960, 544)),
            (960, 540, (960, 544), (976, 544)),
            (1280, 720, (960, 544), (976, 544)),
            (1920, 1080, (960, 544), (976, 544)),
            (1024, 768, (736, 544), (960, 720)),
            (720, 1280, (320, 544), (960, 1712)),
            (3840, 1080, (960, 272), (1936, 544)),
            (1, 1, (544, 544), (960, 960)),
            (4294967295, 4294967295, (544, 544), (960, 960)),
            (4294967295, 1, (960, 16), (4294966752, 544)),
            (1, 4294967295, (16, 544), (960, 4294966336)),
        )
        with patch.object(runtime, 'expression', expression):
            for width, height, fit, cover in cases:
                for mode, expected in enumerate((fit, cover)):
                    with self.subTest(width=width, height=height, cover=mode):
                        values['s_center_region'] = mode
                        self.assertEqual(invoke('vita_video_calculate_output_size', width, height), expected)


if __name__ == '__main__':
    unittest.main()
