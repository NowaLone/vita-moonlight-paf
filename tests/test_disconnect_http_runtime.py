"""HTTP mode reuses the pending controller; curl callbacks/options are mocked, not executed."""
import re
import unittest

from pycparser import c_ast, c_parser
import test_disconnect_pending_runtime as pending_suite
from test_adapter_lifecycle import Returned, execute
from test_xml_source import ROOT, strip_directives


class HttpDisconnectController(pending_suite.PendingDisconnectRuntime):
    log_prefix = '[DisconnectHttpTest]'
    checkpoint_description = 'during curl before response'


class Buffer:
    """A synthetic C char pointer for the interpreter's memcmp/pointer arithmetic."""
    def __init__(self, text, offset=0):
        self.text, self.offset = text, offset

    def __add__(self, count):
        return Buffer(self.text, self.offset + count)

    def __sub__(self, count):
        return self + -count

    def read(self, count):
        return self.text[self.offset:self.offset + count]


class HttpDisconnectTransfer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / 'tests/runtime/disconnect_http.c').read_text()
        header = strip_directives((ROOT / 'include/moonlight/http_disconnect_test.h').read_text())
        header = header.replace('extern "C" {', '').replace('\n}\n', '\n')
        code = re.sub(r'\b([01])L\b', r'\1', strip_directives(cls.source))
        preamble = 'typedef unsigned int size_t; typedef int CURL; typedef int CURLcode; typedef int curl_infotype;'
        unit = c_parser.CParser().parse(preamble + header + code)
        cls.functions = {item.decl.name: item.body for item in unit.ext if isinstance(item, c_ast.FuncDef)}

    def setUp(self):
        self.calls, self.logs, self.checkpoints = [], [], []
        self.transfer = dict(handle=0, application_id=0, active=0, matched_request=0, entered=0, response_seen=0)
        self.values = dict(handle=17, url='https://test:47984/resume?appid=42', transfer=self.transfer,
                           userdata=self.transfer, NULL=0, s_http_application_id=-1, available=1,
                           CURLE_OK=0, CURLINFO_HEADER_OUT=2, CURLINFO_HEADER_IN=1,
                           CURLOPT_DEBUGFUNCTION=1, CURLOPT_DEBUGDATA=2, CURLOPT_VERBOSE=3,
                           http_test_debug=99)
        self.fail_option = None
        self.values['curl_easy_setopt'] = self.setopt
        self.values['disconnect_test_pending_checkpoint_available'] = lambda: self.values['available']
        self.values['disconnect_test_pending_checkpoint'] = lambda app: self.checkpoints.append(app)
        self.values['vita_debug_log'] = lambda fmt, *args: self.logs.append(fmt % args)
        self.values['memset'] = lambda target, value, size: target.update({key: 0 for key in target})
        self.values['strncmp'] = lambda a, b, n: 0 if a[:n] == b[:n] else 1
        self.values['strstr'] = lambda a, b: 1 if b in a else 0
        self.values['memcmp'] = lambda a, b, n: 0 if a.read(n) == b[:n] else 1
        self.values['disconnect_http_test_end'] = lambda handle, transfer, result: self.run_body(
            'disconnect_http_test_end', handle=handle, transfer=transfer, result=result)

    def setopt(self, handle, option, value):
        self.calls.append((option, value))
        if option == self.fail_option and value != 0:
            return 48
        return 0

    def run_body(self, name, **parameters):
        self.values.update(parameters)
        try:
            execute(self.functions[name], self.values)
        except Returned as result:
            return result.value

    def begin(self, **parameters):
        self.run_body('disconnect_http_test_arm', application_id=42)
        return self.run_body('disconnect_http_test_begin', **parameters)

    def debug(self, kind=2, data='GET /resume?rikey=SECRET HTTP/1.1\r\nHost: test\r\n\r\n', handle=17):
        return self.run_body('http_test_debug', handle=handle, type=kind, data=Buffer(data), size=len(data))

    def test_targeted_callback_waits_for_complete_get_headers_before_response(self):
        self.assertEqual(self.begin(), 0)
        self.assertEqual([item[0] for item in self.calls], [1, 2, 3])
        self.assertEqual(self.debug(data='GET /resume?rikey=SECRET HTTP/1.1\r\n'), 0)
        self.assertEqual(self.checkpoints, [])
        self.assertEqual(self.debug(data='Host: test\r\n\r\n'), 0)
        self.assertEqual(self.checkpoints, [42])
        self.assertEqual(self.transfer['response_seen'], 0)
        self.debug(kind=1, data='HTTP/1.1 200 OK\r\n')
        self.assertEqual(self.transfer['response_seen'], 1)
        self.debug()
        self.assertEqual(self.checkpoints, [42])
        self.assertFalse(any('SECRET' in line or 'rikey' in line for line in self.logs))
        self.run_body('disconnect_http_test_end', result=0)
        self.assertEqual(self.calls[-3:], [(3, 0), (2, 0), (1, 0)])
        self.assertEqual(self.transfer['active'], 0)
        self.assertEqual(self.values['s_http_application_id'], -1)

    def test_response_before_checkpoint_prevents_false_pre_response_coverage(self):
        self.begin()
        self.debug(data='GET /resume?key=x HTTP/1.1\r\n')
        self.debug(kind=1, data='HTTP/1.1 400 Bad Request\r\n')
        self.debug(data='Host: test\r\n\r\n')
        self.assertEqual(self.checkpoints, [])
        self.assertEqual(self.transfer['response_seen'], 1)
        self.assertEqual(self.transfer['entered'], 0)

    def test_non_target_requests_and_finished_test_leave_curl_untouched(self):
        for url, available in (('http://test/launch?a=1', 1), ('https://test/serverinfo', 1),
                               ('https://test/applist', 1), ('https://test/resume?a=1', 0)):
            with self.subTest(url=url, available=available):
                self.setUp()
                self.values['available'] = available
                self.assertEqual(self.begin(url=url), 0)
                self.assertEqual(self.calls, [])
                self.run_body('disconnect_http_test_end', result=0)
                self.assertEqual(self.calls, [])

    def test_internal_handles_and_unrelated_debug_data_cannot_trigger(self):
        self.begin()
        self.debug(handle=99)
        self.debug(kind=0, data='GET /resume?secret')
        self.debug(data='CONNECT test:47984 HTTP/1.1\r\n\r\n')
        self.debug(data='GET /applist?x HTTP/1.1\r\n\r\n')
        self.debug(data='GET')
        self.assertEqual(self.checkpoints, [])
        self.assertTrue(all('secret' not in line for line in self.logs))

    def test_callback_setup_failures_disable_verbose_before_removing_sink(self):
        for option in (1, 2, 3):
            with self.subTest(option=option):
                self.setUp()
                self.fail_option = option
                self.assertEqual(self.begin(), 48)
                self.assertEqual(self.calls[-3:], [(3, 0), (2, 0), (1, 0)])
                self.assertEqual(self.transfer['active'], 0)

    def test_transport_error_still_removes_stack_userdata_and_does_not_claim_success(self):
        self.begin()
        self.debug()
        self.run_body('disconnect_http_test_end', result=28)
        self.assertIn('result=28', self.logs[-1])
        self.assertEqual(self.calls[-3:], [(3, 0), (2, 0), (1, 0)])

    def test_checkpoint_availability_is_atomic_and_excludes_used_or_released(self):
        source = (ROOT / 'tests/runtime/disconnect_pending.cpp').read_text()
        start = source.index('extern "C" int disconnect_test_pending_checkpoint_available(')
        body = source[start:source.index('\n}', start) + 2].replace('extern "C" ', '')
        unit = c_parser.CParser().parse(body)
        for used, released in ((0, 0), (1, 0), (0, 1), (1, 1)):
            with self.subTest(used=used, released=released):
                values = dict(s_checkpoint_used=used, s_checkpoint_release=released, __ATOMIC_ACQUIRE=2)
                values['__atomic_load_n'] = lambda key, order: values[key]
                with self.assertRaises(Returned) as result:
                    execute(unit.ext[0].body, values)
                self.assertEqual(result.exception.value, int(not used and not released))

    def test_real_hook_order_scope_and_unchanged_progress_and_cancel_paths(self):
        legacy = (ROOT / 'src/backend/legacy/legacy_gamestream.c').read_text()
        start = legacy.index('static int http_request(')
        end = legacy.index('\nint legacy_gamestream_request(', start)
        body = legacy[start:end]
        begin = body.index('disconnect_http_test_begin(')
        perform = body.index('curl_result = curl_easy_perform(')
        cleanup = body.index('disconnect_http_test_end(')
        unlock = body.index('pthread_mutex_unlock', cleanup)
        self.assertLess(begin, perform)
        self.assertLess(perform, cleanup)
        self.assertLess(cleanup, unlock)
        self.assertIn('CURLOPT_XFERINFOFUNCTION, pairing_progress', body)
        stream = (ROOT / 'src/moonlight/stream_session.cpp').read_text()
        arm = stream.index('disconnect_http_test_arm(application_id);')
        self.assertLess(arm, stream.index('result = legacy_gamestream_request(', arm))
        self.assertIn('#if defined(MOONLIGHT_PENDING_DISCONNECT_TEST) && !defined(MOONLIGHT_HTTP_DISCONNECT_TEST)', stream)
        self.assertIn('    if (stream_cancel_requested()) {\n        vita_debug_log("[Stream] launch cancelled before RTSP setup");', stream)


if __name__ == '__main__':
    unittest.main()
