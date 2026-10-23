"""Interpret mDNS name/record bounds with byte arrays; no Vita sockets or C compilation."""
import ast
import re
import struct
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_host_persistence import run
from test_xml_source import ROOT, strip_directives


def dns(name):
    return b''.join(bytes([len(part)]) + part.encode() for part in name.split('.')) + b'\0'


class MdnsBounds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'src/backend/legacy/mdns/udp_sniffer_vita.c').read_text()
        start = source.index('static int decode_dns_name(')
        decoder = source[start:source.index('\n}', start) + 2]
        start = source.index('    int qdcount = 0, ancount = 0;')
        records = source[start:source.index('\n}', start) + 2]
        code = decoder + '\nvoid parse_packet(const unsigned char *pkt, int msglen) {\n' + records
        # Model memcpy's pointer offsets as byte indices; the count/bounds remain actual source.
        code = code.replace('memcpy(out + outpos, msg + pos + 1, c)',
                            'copy_bytes(out, outpos, msg, pos + 1, c)')
        code = re.sub(r'\+\+(\w+)', r'\1 = \1 + 1', code)
        code = code.replace('strcmp(rrname + len - suffixlen, suffix)',
                            'compare_suffix(rrname, len - suffixlen, suffix)')
        code = re.sub(r'//[^\n]*', '', strip_directives(code))
        unit = c_parser.CParser().parse('typedef unsigned int size_t;' + code)
        cls.functions = {node.decl.name: node for node in unit.ext if isinstance(node, c_ast.FuncDef)}

    def setUp(self):
        self.calls = []
        original, original_execute = runtime.expression, runtime.execute

        def expression(node, values):
            if isinstance(node, c_ast.Constant) and node.type == 'char':
                return ord(ast.literal_eval(node.value))
            if isinstance(node, c_ast.Constant) and node.type == 'int':
                return int(node.value, 0)
            if isinstance(node, c_ast.UnaryOp) and node.op == 'sizeof':
                return len(expression(node.expr, values))
            if isinstance(node, c_ast.BinaryOp) and node.op in ('&', '|', '<<', '<='):
                a, b = expression(node.left, values), expression(node.right, values)
                return {'&': lambda: a & b, '|': lambda: a | b,
                        '<<': lambda: a << b, '<=': lambda: a <= b}[node.op]()
            return original(node, values)

        def execute(node, values):
            if isinstance(node, c_ast.DeclList):
                for declaration in node.decls:
                    execute(declaration, values)
            elif isinstance(node, c_ast.Decl) and isinstance(node.type, c_ast.ArrayDecl):
                values[node.name] = [0] * expression(node.type.dim, values)
            elif isinstance(node, c_ast.Assignment) and isinstance(node.lvalue, c_ast.ArrayRef):
                expression(node.lvalue.name, values)[expression(node.lvalue.subscript, values)] = expression(node.rvalue, values)
            elif isinstance(node, c_ast.Assignment) and node.op == '+=':
                values[node.lvalue.name] += expression(node.rvalue, values)
            else:
                original_execute(node, values)

        self.expression, self.execute = expression, execute

    def invoke(self, name, *args):
        node = self.functions[name]
        values = {param.name: arg for param, arg in zip(node.decl.type.args.params, args)}
        text = lambda data: data if isinstance(data, str) else bytes(data[:data.index(0)]).decode()

        def copy(out, offset, data, start, count):
            for i in range(count):
                out[offset + i] = data[start + i]

        def format_ip(out, size, fmt, *args):
            encoded = (fmt % args).encode() + b'\0'
            for i, value in enumerate(encoded):
                out[i] = value

        values.update(NULL=0, decode_dns_name=lambda *args: self.invoke('decode_dns_name', *args),
                      copy_bytes=copy, strlen=lambda data: len(text(data)),
                      strstr=lambda data, needle: int(needle in text(data)),
                      compare_suffix=lambda data, offset, suffix: 0 if text(data)[offset:] == suffix else 1,
                      snprintf=format_ip,
                      update_sunshine=lambda *args: self.calls.append('ptr'),
                      update_sunshine_ip_by_target=lambda name, ip: self.calls.append(text(ip)),
                      check_and_print_ready_entries_vita=lambda: self.calls.append('notify'))
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            return run(node.body, values)

    def test_literal_and_compressed_names_preserve_consumed_offsets(self):
        literal = dns('foo.local')
        packet = literal + b'\xc0\0' + b'\x03bar\xc0\4'
        for offset, consumed, name in ((0, len(literal), 'foo.local'),
                                       (len(literal), 2, 'foo.local'),
                                       (len(literal) + 2, 6, 'bar.local')):
            out = [0] * 256
            self.assertEqual(self.invoke('decode_dns_name', packet, len(packet), offset, out, len(out)), consumed)
            self.assertEqual(bytes(out[:out.index(0)]).decode(), name)

    def test_bad_pointers_labels_cycles_and_short_output_are_rejected(self):
        for packet in (b'\xc0', b'\xc0\xff', b'\xc0\0', b'\xc0\2\xc0\0',
                       b'\x03ab', b'\x03abc', b'\x40a\0'):
            with self.subTest(packet=packet):
                self.assertLess(self.invoke('decode_dns_name', packet, len(packet), 0, [0] * 256, 256), 0)
        self.assertLess(self.invoke('decode_dns_name', dns('foo.local'), 11, 0, [0] * 4, 4), 0)

    def test_records_reject_truncated_question_header_and_rdata(self):
        header = lambda questions, answers: struct.pack('!6H', 0, 0, questions, answers, 0, 0)
        record = lambda kind, length, data: dns('pc.local') + struct.pack('!HHIH', kind, 1, 60, length) + data
        good = header(0, 1) + record(1, 4, bytes((192, 0, 2, 1)))
        self.invoke('parse_packet', good, len(good))
        self.assertEqual(self.calls, ['192.0.2.1', 'notify'])
        # A question followed by PTR, SRV and A answers, with a compressed SRV target.
        service = dns('pc._nvstream._tcp.local')
        srv = b'\0\0\0\0' + struct.pack('!H', 47989) + b'\xc0\x0c'
        good = (header(1, 3) + dns('pc.local') + struct.pack('!HH', 1, 1)
                + record(12, len(service), service)
                + service + struct.pack('!HHIH', 33, 1, 60, len(srv)) + srv
                + record(1, 4, bytes((192, 0, 2, 1))))
        self.calls.clear()
        self.invoke('parse_packet', good, len(good))
        self.assertEqual(self.calls, ['ptr', 'ptr', '192.0.2.1', 'notify'])
        for packet in (header(1, 0) + dns('pc.local'), header(0, 1) + dns('pc.local'),
                       header(0, 1) + b'\xc0\x0c', header(0, 1) + record(1, 4, b'\x01'),
                       header(0, 1) + record(12, 1, b'\x02'),
                       header(0, 1) + record(33, 6, b'\0' * 6)):
            with self.subTest(packet=packet):
                self.calls.clear()
                self.invoke('parse_packet', packet, len(packet))
                self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
