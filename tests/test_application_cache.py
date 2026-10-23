"""Interpret cache invalidation/read guards; no network, allocator or native execution."""
import unittest

from pycparser import c_parser
from test_host_persistence import function, run
from test_xml_source import ROOT, strip_directives


class ApplicationCache(unittest.TestCase):
    def test_invalidation_shorter_refresh_and_empty_list_never_expose_stale_entries(self):
        source = (ROOT / 'src/moonlight/api.cpp').read_text()
        cached = function(source, 'moonlight_api_get_applications')
        cached = cached[:cached.index('    if (!s_has_current_host')]
        cached = cached.replace('HostControlRequest request(false);', 'int request = acquired;')
        cached = cached.replace('request.Acquired()', 'request')
        # memcpy size is modeled in application elements, without C layout/pointer arithmetic.
        cached = cached.replace('sizeof(*out) * count', 'count') + '    return -99;\n}'
        preamble = 'typedef int MoonlightApplication;'
        parse = lambda code: c_parser.CParser().parse(preamble + strip_directives(code)).ext[-1].body
        read = parse(cached)
        invalidate = parse(function(source, 'invalidate_applications'))
        copied = []
        values = dict(false=0, acquired=1, capacity=2, out=[-1, -1, -1],
                      s_applications_valid=1, s_application_count=3, s_applications=[10, 20, 30])

        def copy(out, apps, count):
            self.assertTrue(values['s_applications_valid'])
            copied.append(count)
            out[:count] = apps[:count]

        values.update(memcpy=copy, memset=lambda apps, *args: apps.__setitem__(slice(None), [0] * len(apps)))
        self.assertEqual(run(read, values), 2)
        self.assertEqual(values['out'], [10, 20, -1])
        run(invalidate, values)
        self.assertEqual(values['s_application_count'], 0)
        values['out'] = [-1, -1, -1]
        self.assertEqual(run(read, values), -1)
        self.assertEqual(values['out'], [-1, -1, -1])
        self.assertEqual(copied, [2])
        # A successful shorter list may leave old bytes beyond its count.
        values.update(s_applications_valid=1, s_application_count=1, s_applications=[40, 20, 30], capacity=3)
        self.assertEqual(run(read, values), 1)
        self.assertEqual(values['out'], [40, -1, -1])
        values.update(s_application_count=0, out=[-1, -1, -1])
        self.assertEqual(run(read, values), 0)
        self.assertEqual(values['out'], [-1, -1, -1])
        values['acquired'] = 0
        self.assertEqual(run(read, values), -1)
        self.assertEqual(copied, [2, 1])


if __name__ == '__main__':
    unittest.main()
