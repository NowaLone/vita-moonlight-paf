"""Interpret actual pairing helpers/guards with injected failures, without a build.

OpenSSL, curl, Expat, allocation and mutexes are mocks. This is not crypto/TLS,
compiler/ABI, threading, native-dialog or Vita validation.
"""
import ast
import itertools
import re
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_discovery_lifecycle import Scope
from test_xml_source import ROOT, strip_directives


SOURCE = (ROOT / 'src/backend/legacy/legacy_gamestream.c').read_text()
API = (ROOT / 'src/moonlight/api.cpp').read_text()


def function(source, name):
    start = re.search(r'^(?:static )?(?:int|bool|void|size_t) ' + name + r'\(', source, re.M).start()
    return source[start:source.index('\n}', start) + 2]


class Ref:
    def __init__(self, values, name):
        self.values, self.name = values, name

    def get(self):
        return self.values[self.name]

    def set(self, value):
        self.values[self.name] = value

    def __getitem__(self, key):
        return self.get()[key]

    def __setitem__(self, key, value):
        self.get()[key] = value


class Memory:
    def __init__(self, size):
        self.data = [0] * size

    def __getitem__(self, index):
        return self.data[index]

    def __setitem__(self, index, value):
        self.data[index] = value

    def text(self):
        return ''.join(chr(value) for value in itertools.takewhile(bool, self.data))

    def write(self, text):
        self.data[:len(text) + 1] = [ord(c) for c in text] + [0]


class Harness:
    original_expression = staticmethod(runtime.expression)
    original_execute = staticmethod(runtime.execute)

    def __init__(self):
        header = (ROOT / 'src/backend/legacy/legacy_gamestream.h').read_text()
        types = (ROOT / 'include/moonlight/types.h').read_text()
        enum = re.search(r'typedef enum MoonlightPairingResult\s*{.*?}\s*MoonlightPairingResult;', types, re.S).group()
        names = ('read_pairing_response', 'read_pairing_certificate', 'get_certificate_signature',
                 'verify_signature', 'encrypt_ecb', 'decrypt_ecb', 'rsa_sign', 'http_request',
                 'http_write_callback', 'http_buffer_init', 'http_buffer_free', 'pair')
        bodies = '\n'.join(function(SOURCE, name) for name in names)
        bodies += '\n' + '\n'.join(function(API, name) for name in
                                   ('is_pairing_network_error', 'classify_pairing_error'))
        # Exclude the opt-in HTTP test instrumentation, as in a production build.
        bodies = re.sub(r'#ifdef MOONLIGHT_HTTP_DISCONNECT_TEST.*?#endif', '', bodies, flags=re.S)
        buffer_type = re.search(r'typedef struct HttpBuffer\s*{.*?}\s*HttpBuffer;', SOURCE, re.S).group()
        preamble = 'typedef int bool, CURL, CURLcode, MoonlightCancellation, X509, EVP_PKEY, EVP_MD_CTX, EVP_CIPHER_CTX, BIO;'
        preamble += 'typedef unsigned int size_t; typedef unsigned short uint16_t;'
        preamble += 'typedef struct { int length; unsigned char *data; } ASN1_BIT_STRING;'
        header = header.replace('extern "C" {', '').replace('\n}\n', '\n')
        code = strip_directives(preamble + enum + header + buffer_type + bodies)
        code = re.sub(r'\b(\d+)L\b', r'\1', code)
        unit = c_parser.CParser().parse(code)
        self.functions = {item.decl.name: item for item in unit.ext if isinstance(item, c_ast.FuncDef)}
        self.values = dict(NULL=0, true=1, false=0, SIGNATURE_LEN=256,
                           MIN_SUPPORTED_GFE_VERSION=3, MAX_SUPPORTED_GFE_VERSION=7,
                           s_http_mutex=1, s_error=Memory(256), s_cert=2, s_last_http_result=0)
        for name, value in re.findall(r'^#define (LEGACY_GAMESTREAM_\w+) (-?\d+)', header, re.M):
            self.values[name] = int(value)
        for item in unit.ext:
            if isinstance(item, c_ast.Typedef) and isinstance(item.type.type, c_ast.Enum):
                value = -1
                for member in item.type.type.values.enumerators:
                    value = self.expression(member.value, self.values) if member.value else value + 1
                    self.values[member.name] = value
        # Synthetic distinct values: the interpreted source only compares these constants.
        for index, name in enumerate(dict.fromkeys(re.findall(r'\b(?:CURLE_|CURLOPT_|CURLINFO_|CURL_HTTP_|ERR_R_)\w+', bodies) + ['CURLE_WRITE_ERROR']), 1):
            self.values[name] = index
        self.values['CURLE_OK'] = 0
        self.calls = []
        for name in self.functions:
            self.values[name] = lambda *args, name=name: self.invoke(name, *args)
        self.values.update(
            set_error=lambda text: self.values['s_error'].write(text),
            strlen=lambda text: len(text.text() if isinstance(text, Memory) else text),
            strcmp=lambda a, b: 0 if (a.text() if isinstance(a, Memory) else a) == b else 1,
            strncmp=lambda a, b, n: 0 if a[:n] == b[:n] else 1,
            BIO_s_mem=lambda: 1, BIO_new=lambda kind: 11,
            BIO_write=lambda bio, data, n: n, BIO_free=lambda obj: self.calls.append(('bio_free', obj)),
            PEM_read_bio_X509=lambda *args: 12,
            ERR_clear_error=lambda: None, ERR_peek_last_error=lambda: 0, ERR_GET_REASON=lambda code: code,
            X509_get_pubkey=lambda cert: 13, X509_free=lambda obj: self.calls.append(('cert_free', obj)),
            EVP_sha256=lambda: 1, EVP_aes_128_ecb=lambda: 1, EVP_MD_CTX_create=lambda: 14,
            EVP_MD_CTX_destroy=lambda obj: self.calls.append(('context_free', obj)),
            EVP_PKEY_free=lambda obj: self.calls.append(('key_free', obj)),
            EVP_DigestVerifyInit=lambda *args: 1, EVP_DigestVerifyUpdate=lambda *args: 1,
            EVP_DigestVerifyFinal=lambda *args: 1,
            EVP_CIPHER_CTX_new=lambda: 15,
            EVP_CIPHER_CTX_free=lambda obj: self.calls.append(('cipher_free', obj)),
            EVP_EncryptInit_ex=lambda *args: 1, EVP_DecryptInit_ex=lambda *args: 1,
            EVP_CIPHER_CTX_set_padding=lambda *args: 1,
            EVP_EncryptUpdate=lambda *args: 1, EVP_DecryptUpdate=lambda *args: 1,
            OPENSSL_malloc=lambda n: 16, OPENSSL_free=lambda obj: self.calls.append(('signature_free', obj)),
            EVP_DigestSignInit=lambda *args: 1, EVP_DigestSignUpdate=lambda *args: 1,
            EVP_DigestSignFinal=self.sign_final, s_private_key=17,
            memcpy=lambda *args: None, free=lambda obj: self.calls.append(('free', obj)),
            malloc=lambda n: Memory(n), realloc=lambda obj, n: Memory(n),
            pthread_mutex_lock=lambda *args: self.calls.append(('lock', 1)),
            pthread_mutex_unlock=lambda *args: self.calls.append(('unlock', 1)),
            moonlight_is_cancelled=lambda cancel: 0,
            curl_easy_setopt=lambda *args: 0, pairing_progress=1,
            curl_easy_perform=lambda *args: 0, curl_easy_getinfo=lambda *args: 0,
            curl_easy_strerror=lambda code: 'mock curl failure',
            legacy_gamestream_last_http_error=lambda: self.values['s_last_http_result'])

    def code(self, suffix):
        return self.values['LEGACY_GAMESTREAM_' + suffix]

    def sign_final(self, context, output, length):
        length.set(256)
        return 1

    def expression(self, node, values):
        if isinstance(node, c_ast.Constant) and node.type == 'char':
            return ord(ast.literal_eval(node.value))
        if isinstance(node, c_ast.UnaryOp):
            if node.op == '&' and isinstance(node.expr, c_ast.ID):
                return Ref(values, node.expr.name)
            if node.op == '*':
                return self.expression(node.expr, values).get()
            if node.op == 'sizeof':
                value = self.expression(node.expr, values)
                return len(value.data) if isinstance(value, Memory) else 1
        if isinstance(node, c_ast.BinaryOp) and node.op in ('*', '/', '%'):
            a, b = self.expression(node.left, values), self.expression(node.right, values)
            return {'*': lambda: a * b, '/': lambda: a // b, '%': lambda: a % b}[node.op]()
        return self.original_expression(node, values)

    def execute(self, node, values):
        if isinstance(node, c_ast.Decl) and isinstance(node.type, c_ast.ArrayDecl):
            values[node.name] = Memory(self.expression(node.type.dim, values))
        elif isinstance(node, c_ast.Assignment) and isinstance(node.lvalue, c_ast.UnaryOp):
            assert node.op == '=' and node.lvalue.op == '*'
            self.expression(node.lvalue.expr, values).set(self.expression(node.rvalue, values))
        elif isinstance(node, c_ast.Assignment) and isinstance(node.lvalue, c_ast.ArrayRef):
            self.expression(node.lvalue.name, values)[self.expression(node.lvalue.subscript, values)] = self.expression(node.rvalue, values)
        elif isinstance(node, c_ast.Cast):
            self.expression(node, values)
        else:
            self.original_execute(node, values)

    def run(self, body, values):
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            try:
                self.execute(body, values)
            except runtime.Returned as result:
                return result.value

    def invoke(self, name, *args):
        definition = self.functions[name]
        params = definition.decl.type.args.params
        values = Scope(self.values, dict(zip((p.name for p in params), args)))
        return self.run(definition.body, values)

    def classify(self, result):
        return self.invoke('classify_pairing_error', result)


class PairingErrors(unittest.TestCase):
    def test_classifier_uses_typed_results_independent_of_diagnostic_text(self):
        h = Harness()
        expected = {'OK': 'SUCCESS', 'INCORRECT_PIN': 'ERROR_INCORRECT_PIN', 'SECURITY_ERROR': 'ERROR_SECURITY',
                    'OUT_OF_MEMORY': 'ERROR_INTERNAL', 'INTERNAL_ERROR': 'ERROR_INTERNAL',
                    'FAILED': 'ERROR_PROTOCOL', 'INVALID': 'ERROR_PROTOCOL', 'NOT_PAIRED': 'ERROR_PROTOCOL'}
        for text in ('Incorrect pairing PIN', 'MITM attack detected', 'changed diagnostic'):
            h.values['s_error'].write(text)
            for legacy, public in expected.items():
                with self.subTest(text=text, legacy=legacy):
                    self.assertEqual(h.classify(h.code(legacy)), h.values['MOONLIGHT_PAIRING_' + public])
        for name in ('TIMEDOUT', 'COULDNT_CONNECT', 'COULDNT_RESOLVE_HOST', 'RECV_ERROR', 'SEND_ERROR', 'GOT_NOTHING', 'SSL_CONNECT_ERROR', 'OUT_OF_MEMORY'):
            code = 'CURLE_OPERATION_TIMEDOUT' if name == 'TIMEDOUT' else 'CURLE_' + name
            h.values['s_last_http_result'] = h.values[code]
            expected = 'INTERNAL' if name == 'OUT_OF_MEMORY' else 'NETWORK'
            self.assertEqual(h.classify(h.code('FAILED')), h.values['MOONLIGHT_PAIRING_ERROR_' + expected])
            self.assertEqual(h.classify(h.code('INTERNAL_ERROR')), -1005)

    def test_evp_verification_rejects_every_non_success_and_frees_owners(self):
        for init, update, final in itertools.product((-1, 0, 1), repeat=3):
            with self.subTest(init=init, update=update, final=final):
                h = Harness()
                h.values.update(EVP_DigestVerifyInit=lambda *a: init, EVP_DigestVerifyUpdate=lambda *a: update,
                                EVP_DigestVerifyFinal=lambda *a: final)
                expected = 'INTERNAL_ERROR' if init != 1 or update != 1 or final < 0 else 'OK' if final == 1 else 'SECURITY_ERROR'
                self.assertEqual(h.invoke('verify_signature', 1, 16, 2, 256, 'PEM'), h.code(expected))
                for kind in ('context_free', 'key_free', 'cert_free'):
                    self.assertEqual(sum(call[0] == kind for call in h.calls), 1)

    def test_verification_allocation_and_certificate_failures_keep_cause(self):
        for function_name, result, expected in (
                ('BIO_new', 0, 'OUT_OF_MEMORY'), ('BIO_write', -1, 'INTERNAL_ERROR'),
                ('PEM_read_bio_X509', 0, 'SECURITY_ERROR'), ('X509_get_pubkey', 0, 'SECURITY_ERROR'),
                ('EVP_MD_CTX_create', 0, 'OUT_OF_MEMORY')):
            with self.subTest(function_name=function_name):
                h = Harness()
                h.values[function_name] = lambda *args: result
                legacy = h.invoke('verify_signature', 1, 16, 2, 256, 'PEM')
                self.assertEqual(legacy, h.code(expected))
                self.assertNotEqual(h.classify(legacy), -1001)
        for name in ('PEM_read_bio_X509', 'X509_get_pubkey'):
            h = Harness()
            h.values[name] = lambda *a: 0
            h.values['ERR_peek_last_error'] = lambda: h.values['ERR_R_MALLOC_FAILURE']
            self.assertEqual(h.invoke('verify_signature', 1, 16, 2, 256, 'PEM'), h.code('OUT_OF_MEMORY'))

    def test_aes_and_signing_execution_failures_are_internal_with_cleanup(self):
        for name, failure in (('encrypt_ecb', 'EVP_EncryptInit_ex'), ('decrypt_ecb', 'EVP_DecryptUpdate')):
            h = Harness()
            h.values[failure] = lambda *args: -1
            self.assertEqual(h.invoke(name, 1, 16, 2, 3), h.code('INTERNAL_ERROR'))
            self.assertEqual(h.calls, [('cipher_free', 15)])
            h.values['EVP_CIPHER_CTX_new'] = lambda: 0
            self.assertEqual(h.invoke(name, 1, 16, 2, 3), h.code('OUT_OF_MEMORY'))
        for stage in ('EVP_MD_CTX_create', 'EVP_DigestSignInit', 'OPENSSL_malloc', 'EVP_DigestSignFinal'):
            h = Harness()
            # Fail finalization only on the second call, after the signature allocation.
            calls = []
            def fail_final(*args):
                calls.append(1)
                return h.sign_final(*args) if len(calls) == 1 else 0
            h.values[stage] = fail_final if stage == 'EVP_DigestSignFinal' else lambda *a: 0
            values = dict(signature=0, length=0)
            result = h.invoke('rsa_sign', 1, 16, Ref(values, 'signature'), Ref(values, 'length'))
            expected = 'OUT_OF_MEMORY' if stage in ('EVP_MD_CTX_create', 'OPENSSL_malloc') else 'INTERNAL_ERROR'
            self.assertEqual(result, h.code(expected))
            self.assertEqual(values['signature'], 0)
            if stage == 'EVP_DigestSignFinal':
                self.assertIn(('signature_free', 16), h.calls)

    def test_certificate_signature_failures_keep_cause_through_pair_guard(self):
        for failure, expected in (('ok', 'OK'), ('bio', 'OUT_OF_MEMORY'),
                                  ('pem', 'SECURITY_ERROR'), ('length', 'SECURITY_ERROR')):
            h = Harness()
            h.values['X509_get0_signature'] = lambda output, alg, cert: output.set(
                dict(length=0 if failure == 'length' else 256, data=Memory(256)))
            if failure == 'bio':
                h.values['BIO_new'] = lambda *args: 0
            if failure == 'pem':
                h.values['PEM_read_bio_X509'] = lambda *args: 0
            out = dict(length=0)
            self.assertEqual(h.invoke('get_certificate_signature', 'PEM', Memory(256), 256,
                                     Ref(out, 'length')), h.code(expected))
            self.assertEqual(out['length'], 256 if failure == 'ok' else 0)
        def compounds(node):
            if isinstance(node, c_ast.Compound):
                yield node.block_items or []
            for _, child in node.children():
                yield from compounds(child)
        for items in compounds(h.functions['pair'].body):
            for index, node in enumerate(items[:-1]):
                if (isinstance(node, c_ast.Assignment) and isinstance(node.rvalue, c_ast.FuncCall)
                        and node.rvalue.name.name == 'get_certificate_signature'):
                    for failure in ('OUT_OF_MEMORY', 'INTERNAL_ERROR', 'SECURITY_ERROR'):
                        h = Harness()
                        h.values['get_certificate_signature'] = lambda *args: h.code(failure)
                        values = Scope(h.values, dict(plaincert=Memory(8), response=dict(memory=1, size=0),
                            server_certificate_signature=Memory(256), server_certificate_signature_length=0))
                        self.assertEqual(h.run(c_ast.Compound([node, items[index + 1]]), values), h.code(failure))
                        self.assertEqual(sum(c[0] == 'free' for c in h.calls), 1)
                    return
        self.fail('pair signature guard not found')

    def test_xml_status_fields_and_allocator_errors_propagate(self):
        for failure_at in ('status', 'paired', 'plaincert', None):
            for failure in ('OUT_OF_MEMORY', 'INVALID', 'FAILED'):
                h = Harness()
                def status(*args):
                    if failure_at == 'status':
                        args[2].update(status_code=409, status_message='pending')
                        return h.code(failure)
                    return 0
                def field(body, size, name, output, capacity):
                    output.write('1' if name == 'paired' else '41')
                    return h.code(failure) if name == failure_at else 0
                h.values.update(xml_status_ok=status, xml_find_value=field)
                details = dict(status_code=0, status_message='')
                result = h.invoke('read_pairing_response', dict(memory='XML', size=3), details, 'plaincert', Memory(32), 32)
                self.assertEqual(result, 0 if failure_at is None else h.code(failure))
                if failure_at == 'status':
                    self.assertEqual(details['status_code'], 409)
        h = Harness()
        h.values.update(xml_status_ok=lambda *args: 0,
                        xml_find_value=lambda body, size, name, output, capacity: output.write('0') or 0)
        self.assertEqual(h.invoke('read_pairing_response', dict(memory='XML', size=3), {}, 0, 0, 0), h.code('FAILED'))

    def test_transfer_allocation_faults_are_internal_and_cancel_has_priority(self):
        for failure in ('ok', 'realloc', 'curl_oom', 'setup', 'initial_alloc', 'network', 'tls', 'cancel'):
            with self.subTest(failure=failure):
                h = Harness()
                h.values['http_write_callback'] = 1  # curl option gets a function pointer.
                buffer = dict(memory=Memory(1), size=0, allocation_failed=0)
                def transfer(*args):
                    if failure in ('realloc', 'cancel'):
                        h.values['realloc'] = lambda *args: 0
                        self.assertEqual(h.invoke('http_write_callback', 1, 1, 10, buffer), 0)
                        self.assertEqual(buffer['allocation_failed'], 1)
                    return h.values[{'realloc': 'CURLE_WRITE_ERROR', 'cancel': 'CURLE_WRITE_ERROR',
                                     'curl_oom': 'CURLE_OUT_OF_MEMORY', 'network': 'CURLE_COULDNT_CONNECT',
                                     'tls': 'CURLE_PEER_FAILED_VERIFICATION'}.get(failure, 'CURLE_OK')]
                h.values['curl_easy_perform'] = transfer
                if failure == 'setup':
                    h.values['curl_easy_setopt'] = lambda handle, option, value: 1 if option == h.values['CURLOPT_XFERINFOFUNCTION'] else 0
                if failure == 'initial_alloc':
                    h.values['malloc'] = lambda n: 0
                if failure == 'cancel':
                    h.values['moonlight_is_cancelled'] = lambda token: int(bool(buffer['allocation_failed']))
                result = h.invoke('http_request', dict(curl=1, server_certificate=12), 'http://host/pair', buffer, 15, 0)
                expected = {'ok': 'OK', 'network': 'FAILED', 'tls': 'SECURITY_ERROR', 'setup': 'INTERNAL_ERROR',
                            'cancel': 'CANCELLED'}.get(failure, 'OUT_OF_MEMORY')
                self.assertEqual(result, h.code(expected))
                self.assertEqual(sum(c[0] == 'lock' for c in h.calls), sum(c[0] == 'unlock' for c in h.calls))
                if failure in ('realloc', 'curl_oom', 'setup', 'initial_alloc'):
                    self.assertEqual(h.classify(result), -1005)
                if failure == 'realloc':
                    # A new request must clear the previous response's failure marker.
                    h.values['curl_easy_perform'] = lambda *args: 0
                    self.assertEqual(h.invoke('http_request', dict(curl=1, server_certificate=12),
                                             'http://host/pair', buffer, 15, 0), 0)
                    self.assertEqual(buffer['allocation_failed'], 0)

    def test_pair_caller_guards_propagate_results_and_release_response(self):
        h = Harness()
        nodes = h.functions['pair'].body.block_items
        # Interpret actual call-and-failure-guard pairs, preserving the caller's cleanup.
        targets = ('read_pairing_response', 'read_pairing_certificate', 'encrypt_ecb', 'decrypt_ecb', 'verify_signature', 'rsa_sign')
        last_response = max(i for i, node in enumerate(nodes) if isinstance(node, c_ast.Assignment)
                            and isinstance(node.rvalue, c_ast.FuncCall) and node.rvalue.name.name == 'read_pairing_response')
        checked = 0
        for index, node in enumerate(nodes[:-1]):
            if not isinstance(node, c_ast.Assignment) or not isinstance(node.rvalue, c_ast.FuncCall):
                continue
            name = node.rvalue.name.name
            if name not in targets:
                continue
            for error in ('OUT_OF_MEMORY', 'INTERNAL_ERROR', 'SECURITY_ERROR', 'INVALID'):
                with self.subTest(helper=name, index=index, error=error):
                    h = Harness()
                    h.values[name] = lambda *args: h.code(error)
                    values = Scope(h.values, dict(response=dict(memory=1, size=0), error={}, result=Memory(8192),
                        challenge_data=Memory(16), challenge_enc=Memory(16), challenge_response_enc=Memory(64),
                        challenge_response_length=48, challenge_response=Memory(64), aes_key=Memory(32),
                        challenge_response_hash=Memory(32), challenge_response_hash_enc=Memory(32),
                        pairing_secret=Memory(272), plaincert=Memory(8192), signature=0, signature_length=0,
                        client_secret=Memory(16), server=dict(server_certificate=12), previous=21, candidate=12))
                    # Pointer arithmetic here is only an argument to the mocked verifier.
                    if name == 'verify_signature':
                        values.local['pairing_secret'] = 100
                    if name == 'read_pairing_response':
                        values.local['response'] = dict(memory=1, size=0)
                        h.values[name] = lambda *args: h.code(error)
                    result = h.run(c_ast.Compound([node, nodes[index + 1]]), values)
                    self.assertEqual(result, h.code(error))
                    self.assertEqual(sum(c[0] == 'free' for c in h.calls), 1)
                    if index == last_response:
                        self.assertEqual(values.local['server']['server_certificate'], 21)
                        self.assertIn(('cert_free', 12), h.calls)
                    checked += 1
        self.assertEqual(checked, 44)  # 5 responses, final cert, 2 encryptions, decrypt, verify, sign.

    def test_pair_rng_and_pin_guards_have_distinct_results(self):
        h = Harness()
        def calls(node, name):
            return (isinstance(node, c_ast.FuncCall) and node.name.name == name) or any(
                calls(child, name) for _, child in node.children())
        def conditions(node):
            if isinstance(node, c_ast.If):
                yield node
            for _, child in node.children():
                yield from conditions(child)
        rng_checks = [node for node in conditions(h.functions['pair'].body) if calls(node.cond, 'RAND_bytes')]
        self.assertEqual(len(rng_checks), 3)
        for index, node in enumerate(rng_checks):
            h = Harness()
            h.values['RAND_bytes'] = lambda *args: 0
            values = Scope(h.values, dict(response=dict(memory=1, size=0), salt_data=Memory(16),
                                         challenge_data=Memory(16), client_secret=Memory(16)))
            self.assertEqual(h.run(node, values), h.code('INTERNAL_ERROR'))
            self.assertEqual(h.classify(h.code('INTERNAL_ERROR')), -1005)
            self.assertEqual(sum(c[0] == 'free' for c in h.calls), int(index != 0))
        pin = next(node for node in conditions(h.functions['pair'].body) if calls(node.cond, 'memcmp'))
        h.values['memcmp'] = lambda *args: 1
        values = Scope(h.values, dict(response=dict(memory=1, size=0), challenge_response=1,
                                     expected_response_hash=2, hash_length=32))
        self.assertEqual(h.run(pin, values), h.code('INCORRECT_PIN'))
        self.assertEqual(h.classify(h.code('INCORRECT_PIN')), -1001)

    def test_api_failure_event_preserves_category_and_409_and_suppresses_cancel(self):
        api = function(API, 'moonlight_api_pair_current_host')
        # Start after unchanged RAII/admission checks, end before the success/persistence path.
        api = api[api.index('    if (moonlight_is_cancelled(cancel))'):api.index('    update_host_from_server();')]
        tree = c_parser.CParser().parse(strip_directives('int api_failure(void) { ' + api + ' }'))
        for legacy, code, reason, expected in (
                ('FAILED', 409, 'A pairing session with this uniqueid already exists', -1003),
                ('OUT_OF_MEMORY', 0, '', -1005), ('INTERNAL_ERROR', 0, '', -1005),
                ('SECURITY_ERROR', 0, '', -1004), ('INCORRECT_PIN', 0, '', -1001),
                ('CANCELLED', 0, '', -1006)):
            h = Harness()
            events = []
            def pair(server, pin, error, cancel):
                error.get().update(status_code=code, status_message=reason)
                return h.code(legacy)
            h.values.update(legacy_gamestream_pair=pair, disconnect_pending=lambda: 0,
                            legacy_gamestream_error=lambda: 'diagnostic',
                            emit=lambda *args: events.append(args), MOONLIGHT_EVENT_PAIRING_FAILED=8,
                            s_server=1, s_current_host=dict(id=7, internal='host'))
            values = Scope(h.values, dict(result=0, pin='1234', cancel=0,
                                         server_error=dict(status_code=0, status_message='')))
            self.assertEqual(h.run(tree.ext[0].body, values), expected)
            if legacy == 'CANCELLED':
                self.assertEqual(events, [])
            else:
                self.assertEqual(events, [(8, expected, 7, -1, 'host', code, reason)])


if __name__ == '__main__':
    unittest.main()
