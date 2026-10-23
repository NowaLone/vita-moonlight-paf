"""Non-build C grammar checks; not a compiler or a parser runtime test."""
from pathlib import Path
import re
import unittest
from pycparser import c_parser

ROOT = Path(__file__).resolve().parents[1]


def strip_directives(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'^\s*#.*$', '', text, flags=re.M)


class XmlSource(unittest.TestCase):
    def test_parser_and_fixture_c_grammar(self):
        types = strip_directives((ROOT / 'include/moonlight/types.h').read_text())
        header = strip_directives((ROOT / 'include/moonlight/gamestream_xml.h').read_text())
        header = header.replace('extern "C" {', '').replace('\n}\n', '\n')
        preamble = 'typedef unsigned int size_t; typedef unsigned int uint32_t; typedef unsigned short uint16_t; '
        preamble += 'typedef void *XML_Parser; typedef char XML_Char;'
        for path in ('src/moonlight/gamestream_xml.c', 'tests/gamestream_xml_test.c'):
            source = strip_directives((ROOT / path).read_text()).replace('XMLCALL', '')
            c_parser.CParser().parse(preamble + types + header + source, filename=path)


if __name__ == '__main__':
    unittest.main()
