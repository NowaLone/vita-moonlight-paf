"""Actual-expression checks with mocked X509 comparison, without TLS or compilation."""
import itertools
import re
import unittest
from pycparser import c_parser
from test_pairing_verification import SOURCE, evaluate
from test_xml_source import ROOT, strip_directives


class ServerTrust(unittest.TestCase):
    def test_trust_helpers_and_pairing_c_grammar(self):
        header = strip_directives((ROOT / 'src/backend/legacy/legacy_gamestream.h').read_text())
        header = header.replace('extern "C" {', '').replace('\n}\n', '\n')
        types = 'typedef unsigned int size_t; typedef int bool; typedef unsigned short uint16_t;'
        types += 'typedef long long curl_off_t;'
        types += strip_directives((ROOT / 'include/moonlight/cancellation.h').read_text())
        types += 'typedef int X509, X509_STORE_CTX, SSL_CTX, CURL, CURLcode, FILE, BIO, ASN1_BIT_STRING;'
        types += 'typedef struct { char *memory; size_t size; } HttpBuffer;'
        helpers = SOURCE.split('static int verify_pinned_certificate(', 1)[1]
        helpers = 'static int verify_pinned_certificate(' + helpers.split('static int http_request(', 1)[0]
        transfer = 'static int http_request(' + SOURCE.split('static int http_request(', 1)[1].split('int legacy_gamestream_request(', 1)[0]
        pair = 'static int pair(' + SOURCE.split('static int pair(', 1)[1].split('typedef struct InitArgs', 1)[0]
        for source in (helpers, transfer, pair):
            c_parser.CParser().parse(types + header + strip_directives(source))

    def test_only_present_equal_certificates_are_trusted(self):
        body = SOURCE.split('static int verify_pinned_certificate(', 1)[1]
        body = body.split('static CURLcode configure_pinned_tls(', 1)[0]
        expression = re.search(r'int matches\s*=\s*(.*?);', body, re.S).group(1)
        tree = c_parser.CParser().parse(
            'typedef void X509; int check(void) { int matches = ' + expression + '; }')
        expr = tree.ext[1].body.block_items[0].init
        for expected, peer, comparison in itertools.product((0, 1), (0, 1), (-1, 0, 1)):
            with self.subTest(expected=expected, peer=peer, comparison=comparison):
                self.assertEqual(evaluate(expr, dict(expected=expected, peer=peer, X509_cmp=comparison)),
                                 int(bool(expected and peer and comparison == 0)))

    def test_pairing_pin_is_temporary_until_https_challenge_succeeds(self):
        pair = SOURCE.split('static int pair(', 1)[1].split('typedef struct InitArgs', 1)[0]
        self.assertLess(pair.index('if (!verify_signature('), pair.index('server->server_certificate = candidate'))
        self.assertLess(pair.index('memcmp(challenge_response, expected_response_hash'),
                        pair.index('server->server_certificate = candidate'))
        self.assertLess(pair.index('GameStream rejected HTTPS pairing challenge'),
                        pair.index('save_pinned_certificate(server)'))
        self.assertEqual(pair.count('server->server_certificate = previous'), 2)


if __name__ == '__main__':
    unittest.main()
