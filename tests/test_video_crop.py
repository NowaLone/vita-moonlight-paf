"""Interpret actual crop/presentation code with byte pointers; no GPU, PAF or builds."""
from array import array
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives


class VideoCrop(unittest.TestCase):
    def test_exact_surface_skips_copy_and_larger_surfaces_keep_center_crop(self):
        source = (ROOT / 'src/moonlight/vita_video_renderer.c').read_text()
        start = source.index('        if (s_center_region) {', source.index('static int vita_video_submit('))
        crop = source[start:source.index('        s_frame_index =', start)]
        crop = crop.replace('++row', 'row = row + 1')
        body = c_parser.CParser().parse(strip_directives(
            'typedef unsigned int size_t; void present(void) {' + crop + '}')).ext[-1].body
        original_expression, original_execute = runtime.expression, runtime.execute

        def expression(node, values):
            if isinstance(node, c_ast.Constant):
                return int(node.value.rstrip('uUlL'), 0)
            if isinstance(node, c_ast.BinaryOp) and node.op in ('*', '/', '+'):
                left, right = expression(node.left, values), expression(node.right, values)
                if node.op == '*':
                    return left * right
                if node.op == '/':
                    return left // right
                return (left[0], left[1] + right) if isinstance(left, tuple) else left + right
            return original_expression(node, values)

        def execute(node, values):
            if isinstance(node, c_ast.For):
                execute(node.init, values)
                rows = 0
                while expression(node.cond, values):
                    rows += 1
                    self.assertLessEqual(rows, 544)
                    execute(node.stmt, values)
                    execute(node.next, values)
            else:
                original_execute(node, values)

        with patch.object(runtime, 'expression', expression), patch.object(runtime, 'execute', execute):
            for width, height in ((960, 544), (976, 544), (960, 560)):
                for generation in (0, 19):
                    with self.subTest(width=width, height=height, stats=generation):
                        pixels = array('I', range(width * height))
                        self.assertEqual(pixels.itemsize, 4)
                        original = pixels.tobytes()
                        storage = bytearray(original)
                        pointer = (storage, 0)
                        copies, presentations, stats = [], [], []

                        def memmove(destination, source, count):
                            self.assertIs(destination[0], source[0])
                            copies.append(count)
                            destination[0][destination[1]:destination[1] + count] = bytes(
                                source[0][source[1]:source[1] + count])

                        values = dict(s_center_region=1, s_frame_width=width, s_frame_height=height,
                                      VITA_SCREEN_WIDTH=960, VITA_SCREEN_HEIGHT=544,
                                      s_frame_buffers=[pointer], s_frame_index=0, generation=generation,
                                      decode_end_us=60, vblank_us=0, LiGetMicroseconds=lambda: 100,
                                      memmove=memmove, moonlight_video_present=lambda *args: presentations.append(args),
                                      moonlight_stats_prepared=lambda *args: stats.append(args))
                        execute(body, values)
                        x, y = (width - 960) // 2, (height - 544) // 2
                        expected = b''.join(original[((row + y) * width + x) * 4:
                                                    ((row + y) * width + x + 960) * 4]
                                            for row in range(544))
                        self.assertEqual(storage[:960 * 544 * 4], expected)
                        self.assertEqual(storage[960 * 544 * 4:], original[960 * 544 * 4:])
                        self.assertEqual(copies, [] if (width, height) == (960, 544) else [960 * 4] * 544)
                        self.assertEqual(presentations, [(pointer, 960, 544, 960, generation)])
                        self.assertEqual(stats, [(generation, 40, 0, 960, 544, 960)] if generation else [])


if __name__ == '__main__':
    unittest.main()
