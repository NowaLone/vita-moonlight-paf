"""Interpret remote-Quit consent, startup guards and response verification.

Native dialogs, network/XML calls and cancellation are serial mocks. No build,
PAF/native-dialog execution, real host, threads or Vita behavior is validated.
"""
import ast
import re
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives


def function(source, name):
    start = re.search(r'^(?:static )?(?:int|bool|void) ' + re.escape(name) + r'\(', source, re.M).start()
    return source[start:source.index('\n}', start) + 2]


class Ref:
    def __init__(self, values, name):
        self.values, self.name = values, name

    def get(self):
        return self.values[self.name]

    def set(self, value):
        self.values[self.name] = value


class Scope:
    def __init__(self, shared, local):
        self.shared, self.local = shared, local

    def __getitem__(self, name):
        return self.local[name] if name in self.local else self.shared[name]

    def __setitem__(self, name, value):
        (self.shared if name.startswith(('m_', 's_')) else self.local)[name] = value

    def __contains__(self, name):
        return name in self.local or name in self.shared


class Harness:
    original_expression = staticmethod(runtime.expression)
    original_execute = staticmethod(runtime.execute)

    def __init__(self):
        service = (ROOT / 'src/services/connection_service.cpp').read_text()
        header = (ROOT / 'include/services/connection_service.h').read_text()
        operation = re.search(r'enum ForegroundOperation\s*{(.*?)}', header, re.S).group(1)
        types = (ROOT / 'include/moonlight/types.h').read_text()
        connection_enum = re.search(r'typedef enum MoonlightConnectionState\s*{.*?}\s*MoonlightConnectionState;', types, re.S).group()
        event_enum = re.search(r'typedef enum MoonlightEventType\s*{.*?}\s*MoonlightEventType;', types, re.S).group()
        result_enum = re.search(r'enum\s*{\s*MOONLIGHT_STREAM_APP_CONFLICT.*?};', types, re.S).group()
        methods = ('IsBusy', 'ShowError', 'ConfirmQuitAndStart', 'TryShowError',
                   'PrepareCompletion', 'DialogPollTask')
        bodies = '\n'.join(function(service, 'ConnectionService::' + name) for name in methods)
        bodies = bodies.replace('ConnectionService::', '').replace('service->', '')
        bodies = bodies.replace('const MoonlightEvent &event', 'const MoonlightEvent *event')
        bodies = bodies.replace(') const\n', ')\n').replace('paf::common::MainThreadCallList::', '')
        bodies = bodies.replace('event.', 'event->')
        bodies += function(service, 'completes_operation').replace('ConnectionService::', '')
        stream = (ROOT / 'src/moonlight/stream_session.cpp').read_text()
        launch = function(stream, 'load_launch_session')
        # Execute startup through endpoint choice; exclude key generation and the launch request.
        launch = launch[:launch.index('    if (sceKernelGetRandomNumber')]
        bodies += launch + '    return resume;\n}'
        native = (ROOT / 'src/backend/legacy/legacy_gamestream.c').read_text()
        bodies += function(native, 'legacy_gamestream_quit_app')
        api = (ROOT / 'src/moonlight/api.cpp').read_text()
        bodies += function(api, 'on_stream_event')
        preamble = '''typedef int bool, int32_t, ConnectionService, SceChar8;
        typedef unsigned int size_t;
        typedef enum {''' + operation + '''} ForegroundOperation;
        typedef struct { int type, result, application_id, running_application_id; } MoonlightEvent;
        typedef struct { int buttonId, result; } SceMsgDialogResult;
        typedef struct { int buttonType; char *msg; void *buttonParam; } SceMsgDialogUserMessageParam;
        typedef struct { int mode; void *userMsgParam; } SceMsgDialogParam;
        typedef struct { int curl, paired, server_certificate, current_game; } LegacyGameStreamServer;
        typedef struct { int unused; } MoonlightSettings;
        typedef struct { int status_code; char status_message[256]; } MoonlightGameStreamResponse;
        '''
        unit = c_parser.CParser().parse(strip_directives(preamble + connection_enum + event_enum + result_enum + bodies))
        self.functions = {item.decl.name: item for item in unit.ext if isinstance(item, c_ast.FuncDef)}
        self.values = dict(NULL=0, true=1, false=0, this=1, __ATOMIC_ACQUIRE=1,
                           m_operation=0, m_progress_dialog_slot=-1, m_error_pending=0,
                           m_error_dialog_open=0, m_dialog_task_registered=0, m_worker_done=1,
                           m_confirm_application_id=0, m_confirm_running_id=0,
                           m_error_message=[0] * 512, SCE_COMMON_DIALOG_STATUS_FINISHED=2,
                           SCE_COMMON_DIALOG_ERROR_BUSY=-99, SCE_MSG_DIALOG_BUTTON_TYPE_YESNO=3,
                           SCE_MSG_DIALOG_BUTTON_TYPE_OK=1, SCE_MSG_DIALOG_BUTTON_ID_YES=1,
                           SCE_MSG_DIALOG_MODE_USER_MSG=1, SCE_COMMON_DIALOG_RESULT_OK=0, MOONLIGHT_XML_OK=0,
                           MOONLIGHT_XML_OUT_OF_MEMORY=-20, MOONLIGHT_XML_SERVER_ERROR=-21)
        for declaration in unit.ext:
            if isinstance(declaration, c_ast.Typedef) and isinstance(declaration.type.type, c_ast.Enum):
                enum = declaration.type.type
            elif isinstance(declaration, c_ast.Decl) and isinstance(declaration.type, c_ast.Enum):
                enum = declaration.type
            else:
                continue
            value = -1
            for member in enum.values.enumerators:
                value = self.expression(member.value, self.values) if member.value else value + 1
                self.values[member.name] = value
        legacy_header = (ROOT / 'src/backend/legacy/legacy_gamestream.h').read_text()
        for name, value in re.findall(r'^#define (LEGACY_GAMESTREAM_\w+) (-?\d+)', legacy_header, re.M):
            self.values[name] = int(value)
        self.calls, self.errors = [], []
        self.button, self.get_result, self.term_result, self.init_result, self.status = 1, 0, 0, 0, 2
        self.dialog_result = 0
        self.values.update(
            __atomic_load_n=lambda ref, order: ref.get(),
            CloseWaitDialog=self.close_wait,
            ShowConnectionError=lambda: self.errors.append('connect'),
            Register=lambda *args: self.calls.append('register'),
            Unregister=lambda *args: self.calls.append('unregister'),
            StartConfirmed=self.start,
            memset=self.memset, snprintf=self.snprintf, strncpy=self.strncpy,
            sceMsgDialogParamInit=lambda param: None,
            sceMsgDialogInit=self.init_dialog,
            sceMsgDialogGetStatus=lambda: self.status,
            sceMsgDialogGetResult=self.read_dialog,
            sceMsgDialogTerm=self.term_dialog,
            vita_debug_log=lambda *args: None)
        for name in self.functions:
            self.values[name] = lambda *args, name=name: self.invoke(name, *args)

    def invoke(self, name, *args):
        declaration = self.functions[name].decl.type.args
        parameters = declaration.params if declaration else []
        local = dict(zip((arg.name for arg in parameters), args))
        if name == 'ShowError':
            local.setdefault('result', 0)
        values = Scope(self.values, local)
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            try:
                self.execute(self.functions[name].body, values)
            except runtime.Returned as result:
                return result.value

    def expression(self, node, values):
        if isinstance(node, c_ast.Constant):
            if node.type == 'char':
                return ord(ast.literal_eval(node.value))
            if node.type in ('int', 'long int'):
                return int(node.value.rstrip('L'), 0)
        if isinstance(node, c_ast.UnaryOp) and node.op == '&' and isinstance(node.expr, c_ast.ID):
            return Ref(values, node.expr.name)
        if isinstance(node, c_ast.BinaryOp) and node.op == '<=':
            return int(self.expression(node.left, values) <= self.expression(node.right, values))
        return self.original_expression(node, values)

    def execute(self, node, values):
        if (isinstance(node, c_ast.Decl) and isinstance(node.type, c_ast.TypeDecl)
                and isinstance(node.type.type, c_ast.IdentifierType)
                and node.type.type.names[0].startswith(('SceMsg', 'MoonlightGameStreamResponse'))):
            values[node.name] = dict(buttonId=0, result=0, status_code=0, status_message=[0])
        elif isinstance(node, c_ast.Assignment) and isinstance(node.lvalue, c_ast.ArrayRef):
            self.expression(node.lvalue.name, values)[self.expression(node.lvalue.subscript, values)] = self.expression(node.rvalue, values)
        elif isinstance(node, c_ast.Cast):
            self.expression(node, values)
        else:
            self.original_execute(node, values)

    def memset(self, pointer, value, size):
        target = pointer.get() if isinstance(pointer, Ref) else pointer
        if isinstance(target, list):
            target[:] = [value] * len(target)
        elif isinstance(target, dict):
            for key in target:
                target[key] = value

    def strncpy(self, target, text, size):
        target[:len(text)] = [ord(char) for char in text[:size]]

    def snprintf(self, target, size, fmt, message, result):
        self.strncpy(target, fmt % (message, result & 0xFFFFFFFF), size - 1)

    def close_wait(self):
        self.calls.append('close_wait')
        self.values['m_progress_dialog_slot'] = -1

    def init_dialog(self, param):
        self.calls.append(('dialog', param.get()['userMsgParam'].get()['buttonType']))
        return self.init_result

    def read_dialog(self, result):
        self.calls.append('get_result')
        result.get().update(buttonId=self.button, result=self.dialog_result)
        return self.get_result

    def term_dialog(self):
        self.calls.append('term')
        return self.term_result

    def start(self, application, running):
        self.calls.append(('start', application, running, self.values['m_confirm_application_id'], self.values['m_confirm_running_id']))
        self.values['m_progress_dialog_slot'] = 7
        return 0

    def failed_launch(self, result=-8, selected=42, running=99):
        self.values.update(m_operation=self.values['Operation_Launch'], m_progress_dialog_slot=7)
        event = dict(type=self.values['MOONLIGHT_EVENT_STREAM_FAILED'], result=result,
                     application_id=selected, running_application_id=running)
        return self.invoke('PrepareCompletion', event)

    def poll(self):
        return self.invoke('DialogPollTask', 1)


class RemoteQuit(unittest.TestCase):
    def test_conflict_closes_busy_before_yes_no_and_copies_both_ids(self):
        h = Harness()
        self.assertTrue(h.failed_launch())
        self.assertEqual(h.calls, ['close_wait', 'register'])
        self.assertEqual(h.values['m_operation'], h.values['Operation_None'])
        self.assertEqual((h.values['m_confirm_application_id'], h.values['m_confirm_running_id']), (42, 99))
        self.assertTrue(h.invoke('IsBusy'))
        h.status = 1
        h.poll()
        self.assertEqual(h.calls[-1], ('dialog', h.values['SCE_MSG_DIALOG_BUTTON_TYPE_YESNO']))
        self.assertTrue(h.values['m_error_dialog_open'])

    def test_yes_restarts_once_after_get_and_term_and_clears_ids_before_retry(self):
        h = Harness()
        h.failed_launch()
        h.poll()
        self.assertEqual(h.calls[-3:], ['get_result', 'term', ('start', 42, 99, 0, 0)])
        h.poll()
        self.assertEqual(sum(isinstance(call, tuple) and call[0] == 'start' for call in h.calls), 1)

    def test_synchronous_retry_failure_shows_an_ordinary_error_without_reusing_consent(self):
        h = Harness()
        h.failed_launch()
        def fail_start(application, running):
            self.assertEqual((h.values['m_confirm_application_id'], h.values['m_confirm_running_id']), (0, 0))
            h.calls.append(('failed_start', application, running))
            return -3
        h.values['StartConfirmed'] = fail_start
        h.poll()
        self.assertEqual(h.calls[-3:], ['get_result', 'term', ('failed_start', 42, 99)])
        self.assertTrue(h.values['m_error_pending'])
        self.assertFalse(h.values['m_error_dialog_open'])
        h.status = 1
        h.poll()
        self.assertEqual(h.calls[-1], ('dialog', h.values['SCE_MSG_DIALOG_BUTTON_TYPE_OK']))

    def test_no_missing_consent_or_get_failure_never_starts(self):
        for button, get_result, consent, outcome in ((0, 0, 42, 0), (1, -1, 42, 0), (1, 0, 0, 0),
                (1, 0, 42, 1), (1, 0, 42, 2)):
            with self.subTest(button=button, get_result=get_result, consent=consent, outcome=outcome):
                h = Harness()
                h.failed_launch()
                h.button, h.get_result, h.dialog_result = button, get_result, outcome
                h.values['m_confirm_application_id'] = consent
                h.poll()
                self.assertFalse(any(isinstance(call, tuple) and call[0] == 'start' for call in h.calls))
                self.assertEqual((h.values['m_confirm_application_id'], h.values['m_confirm_running_id']), (0, 0))
                self.assertFalse(h.invoke('IsBusy'))

    def test_term_failure_retains_dialog_and_starts_only_after_successful_cleanup(self):
        h = Harness()
        h.failed_launch()
        h.term_result = -1
        h.poll()
        self.assertTrue(h.values['m_error_dialog_open'])
        self.assertNotIn(('start', 42, 99, 0, 0), h.calls)
        h.term_result = 0
        h.poll()
        self.assertEqual(h.calls[-3:], ['get_result', 'term', ('start', 42, 99, 0, 0)])

    def test_dialog_busy_retries_but_init_failure_discards_consent(self):
        for failure in (-99, -1):
            with self.subTest(failure=failure):
                h = Harness()
                h.failed_launch()
                h.init_result = failure
                h.poll()
                self.assertEqual(bool(h.values['m_error_pending']), failure == -99)
                self.assertEqual(h.values['m_confirm_application_id'], 42 if failure == -99 else 0)
                self.assertFalse(h.values['m_error_dialog_open'])
                self.assertFalse(any(isinstance(call, tuple) and call[0] == 'start' for call in h.calls))

    def test_unidentified_conflict_and_other_failures_use_ordinary_error(self):
        for result, selected, running in ((-8, 42, 0), (-8, 42, 42), (-8, 0, 99), (-14, 42, 99), (-7, 42, 99)):
            with self.subTest(result=result, selected=selected, running=running):
                h = Harness()
                h.failed_launch(result, selected, running)
                h.status = 1
                h.poll()
                self.assertEqual(h.calls[-1], ('dialog', h.values['SCE_MSG_DIALOG_BUTTON_TYPE_OK']))
                self.assertEqual(h.values['m_confirm_application_id'], 0)

    def test_repeated_error_does_not_replace_pending_or_open_confirmation(self):
        for opened in (False, True):
            with self.subTest(opened=opened):
                h = Harness()
                h.failed_launch()
                if opened:
                    h.status = 1
                    h.poll()
                original = list(h.values['m_error_message'])
                h.invoke('ShowError', 'Unrelated error', -1)
                self.assertEqual(h.values['m_error_message'], original)
                self.assertEqual((h.values['m_confirm_application_id'], h.values['m_confirm_running_id']), (42, 99))

    def launch(self, fresh=99, expected=99, cancel_at=0, refresh_result=0, quit_result=0):
        h = Harness()
        server = dict(curl=1, current_game=123)
        checks = []
        def cancelled():
            checks.append(1)
            return len(checks) == cancel_at
        def refresh(host):
            h.calls.append('refresh')
            host['current_game'] = fresh
            return refresh_result
        def quit_app(host):
            h.calls.append('quit')
            if quit_result == 0:
                host['current_game'] = 0
            return quit_result
        h.values.update(stream_cancel_requested=cancelled, legacy_gamestream_refresh_server_info=refresh,
                        legacy_gamestream_quit_app=quit_app)
        result = h.invoke('load_launch_session', server, 'uniqueid', 42, expected, 1, 1, 256, 1, 1)
        return h, result

    def test_fresh_snapshot_blocks_unconfirmed_and_stale_quit_but_resumes_same_app(self):
        for fresh, expected, result, calls in ((99, 0, -8, ['refresh']),
                (99, 98, -8, ['refresh']), (42, 99, 1, ['refresh']),
                (0, 99, 0, ['refresh']), (99, 99, 0, ['refresh', 'quit'])):
            with self.subTest(fresh=fresh, expected=expected):
                h, actual = self.launch(fresh, expected)
                self.assertEqual(actual, result)
                self.assertEqual(h.calls, calls)

    def test_cancel_refresh_and_quit_failures_stop_launch(self):
        for cancel, refresh, quit_result, expected, calls in ((1, 0, 0, -7, []),
                (2, 0, 0, -7, ['refresh']), (3, 0, 0, -7, ['refresh', 'quit']),
                (0, -10, 0, -10, ['refresh']), (0, 0, -14, -14, ['refresh', 'quit'])):
            with self.subTest(cancel=cancel, refresh=refresh, quit_result=quit_result):
                h, actual = self.launch(cancel_at=cancel, refresh_result=refresh, quit_result=quit_result)
                self.assertEqual(actual, expected)
                self.assertEqual(h.calls, calls)

    def test_only_a_conflict_with_a_different_host_app_carries_quit_identity(self):
        for event, result, active, expected in (
                ('STREAM_FAILED', -8, 99, 99), ('STREAM_FAILED', -8, 42, 0),
                ('STREAM_FAILED', -8, 0, 0), ('STREAM_FAILED', -14, 99, 0),
                ('STREAM_STARTED', -8, 99, 0), ('STREAM_STOPPED', -8, 99, 0)):
            with self.subTest(event=event, result=result, active=active):
                h = Harness()
                h.values.update(s_stream_pending=1, s_stream_active=0, s_connection_state=0,
                                s_stream_application_id=42, s_current_host=dict(id=7, internal='host'),
                                s_server=dict(current_game=active), emit=lambda *args: h.calls.append(args))
                kind = h.values['MOONLIGHT_EVENT_' + event]
                h.invoke('on_stream_event', kind, result, 0)
                self.assertEqual(h.calls, [(kind, result, 7, 42, 'host', 0, 0, expected)])
                self.assertEqual(h.values['s_stream_application_id'], 42 if event == 'STREAM_STARTED' else -1)

    def test_quit_requires_paired_pin_and_verifies_stopped_state_after_cancel(self):
        for paired, pin, active, expected in ((0, 1, 0, 'WRONG_STATE'),
                (1, 0, 0, 'WRONG_STATE'), (1, 1, 99, 'QUIT_FAILED'), (1, 1, 0, 'OK')):
            with self.subTest(paired=paired, pin=pin, active=active):
                h = Harness()
                server = dict(curl=1, paired=paired, server_certificate=pin, current_game=99)
                h.values.update(s_error=[], set_error=lambda message: h.errors.append(message),
                                free=lambda body: h.calls.append(('free', body)))
                def request(host, endpoint, query, timeout, body, length):
                    h.calls.append(('request', endpoint, query, timeout))
                    body.set('response')
                    length.set(8)
                    return 0
                def refresh(host):
                    h.calls.append('refresh')
                    host['current_game'] = active
                    return 0
                h.values.update(legacy_gamestream_request=request,
                                moonlight_xml_parse_cancel=lambda *args: 0,
                                legacy_gamestream_refresh_server_info=refresh)
                self.assertEqual(h.invoke('legacy_gamestream_quit_app', server),
                                 h.values['LEGACY_GAMESTREAM_' + expected])
                self.assertEqual(h.calls, [('request', 'cancel', 0, 30), ('free', 'response'), 'refresh']
                                 if paired and pin else [])

    def test_quit_transfer_parse_and_refresh_failures_preserve_error_and_free_response(self):
        for transfer, parsed, status, refreshed, expected in (
                (-10, 0, 0, 0, 'SECURITY_ERROR'),
                (0, -20, 0, 0, 'OUT_OF_MEMORY'),
                (0, -21, 401, 0, 'NOT_PAIRED'),
                (0, -21, 503, 0, 'FAILED'),
                (0, -22, 0, 0, 'INVALID'),
                (0, 0, 0, -10, 'SECURITY_ERROR')):
            with self.subTest(transfer=transfer, parsed=parsed, status=status, refreshed=refreshed):
                h = Harness()
                def request(host, endpoint, query, timeout, body, length):
                    body.set('response')
                    length.set(8)
                    return transfer
                def parse(body, length, response):
                    h.calls.append('parse')
                    response.get().update(status_code=status, status_message=[0])
                    return parsed
                def refresh(host):
                    h.calls.append('refresh')
                    return refreshed
                h.values.update(s_error=[], set_error=lambda message: h.errors.append(message),
                                free=lambda body: h.calls.append(('free', body)),
                                legacy_gamestream_request=request, moonlight_xml_parse_cancel=parse,
                                legacy_gamestream_refresh_server_info=refresh)
                server = dict(curl=1, paired=1, server_certificate=1, current_game=99)
                self.assertEqual(h.invoke('legacy_gamestream_quit_app', server),
                                 h.values['LEGACY_GAMESTREAM_' + expected])
                self.assertEqual(h.calls.count(('free', 'response')), 1)
                self.assertEqual(h.calls.count('parse'), transfer == 0)
                self.assertEqual(h.calls.count('refresh'), transfer == 0 and parsed == 0)


if __name__ == '__main__':
    unittest.main()
