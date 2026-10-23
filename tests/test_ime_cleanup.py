"""Interpret Main's IME result/cleanup prefix with serial dialog/call-list mocks."""
import unittest

from pycparser import c_parser
from test_host_persistence import function, run
from test_xml_source import ROOT


class ImeCleanup(unittest.TestCase):
    def test_cleanup_precedes_error_cancel_or_address_processing(self):
        source = (ROOT / 'src/pages/page_main.cpp').read_text()
        code = function(source, 'Main::HandleAddPcImeResult').replace('Main::', '')
        code = code[:code.index('    char address[')] + '\n}'
        code = code.replace('MoonlightApp::Instance()->Connection().ShowError', 'show_error')
        code = code.replace('paf::common::MainThreadCallList::Unregister', 'unregister')
        body = c_parser.CParser().parse(
            'typedef struct { int button; } SceImeDialogResult;' + code).ext[-1].body
        for status in (-1, 0):
            for button in (0, 1, 2):
                for registered in (False, True):
                    with self.subTest(status=status, button=button, registered=registered):
                        calls = []
                        values = dict(false=0, this=42, ImePollTask=7, m_ime_open=1,
                                      m_ime_task_registered=registered,
                                      SCE_IME_DIALOG_BUTTON_CLOSE=1, SCE_IME_DIALOG_BUTTON_ENTER=2)

                        def get_result(pointer):
                            calls.append('get')
                            if status == 0:
                                values[pointer]['button'] = button
                            return status

                        values.update(
                            memset=lambda pointer, *args: values.update({pointer: dict(button=0)}),
                            sceImeDialogGetResult=get_result,
                            sceImeDialogTerm=lambda: calls.append('term'),
                            unregister=lambda callback, owner: calls.append(('unregister', callback, owner)),
                            show_error=lambda message: calls.append(('error', message)),
                            ShowIdleStatus=lambda: calls.append('idle'))
                        run(body, values)
                        expected = ['get', 'term']
                        if registered:
                            expected.append(('unregister', 7, 42))
                        if status < 0:
                            expected.append(('error', 'Unable to read the PC address.'))
                        elif button == 1:
                            expected.append('idle')
                        elif button != 2:
                            expected.append(('error', 'Enter PC hostname or IP address.'))
                        self.assertEqual(calls, expected)
                        self.assertFalse(values['m_ime_open'])
                        self.assertFalse(values['m_ime_task_registered'])


if __name__ == '__main__':
    unittest.main()
