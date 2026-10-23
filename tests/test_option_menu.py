"""Interpret menu callbacks and delete-before-notify order; no native PAF or build."""
import re
import unittest

from pycparser import c_ast, c_parser
from test_host_persistence import function, run
from test_xml_source import ROOT


class OptionMenuCallbacks(unittest.TestCase):
    def test_each_action_destroys_menu_before_notifying_and_handles_missing_data(self):
        source = (ROOT / 'src/option_menu.cpp').read_text()
        header = (ROOT / 'include/option_menu.h').read_text()
        names = ['OnDismiss', 'OnSettings', 'OnDelete']
        if 'void OptionMenu::CloseAndNotify(' in source:
            names.append('CloseAndNotify')
        code = '\n'.join(function(source, 'OptionMenu::' + name) for name in names)
        code = code.replace('OptionMenu::', '').replace('delete menu;', 'destroy(menu);')
        code = code.replace('paf::ui::Handler', 'void').replace('paf::ui::Event', 'void')
        code = re.sub(r'\(void\)(?:type|self|event);', '', code)
        enums = '\n'.join(re.findall(r'enum (?:EventType|ButtonType)\s*{.*?};', header, re.S))
        preamble = '''typedef int int32_t, EventType;
            typedef void (*EventCb)(int, int, void *);
            typedef struct { EventCb m_cb; void *m_userdata; } OptionMenu;'''
        unit = c_parser.CParser().parse(preamble + enums + code)
        functions = {node.decl.name: node for node in unit.ext if isinstance(node, c_ast.FuncDef)}
        constants = {}
        for node in unit.ext:
            if isinstance(node, c_ast.Decl) and isinstance(node.type, c_ast.Enum):
                value = -1
                for entry in node.type.values.enumerators:
                    value = int(entry.value.value) if entry.value else value + 1
                    constants[entry.name] = value

        for name, kind, button in (('OnDismiss', 'Event_Close', -1),
                                   ('OnSettings', 'Event_Button', constants['Button_Settings']),
                                   ('OnDelete', 'Event_Button', constants['Button_Delete'])):
            for mode in ('null', 'no_callback', 'callback'):
                with self.subTest(action=name, mode=mode):
                    calls = []
                    menu = dict(m_userdata=91)

                    def destroy(target):
                        self.assertIs(target, menu)
                        calls.append('destroy')
                        target.clear()

                    def callback(event, selected, data):
                        self.assertEqual(menu, {})
                        calls.append((event, selected, data))

                    menu['m_cb'] = callback if mode == 'callback' else 0
                    values = dict(constants, destroy=destroy)

                    def invoke(symbol, *args):
                        node = functions[symbol]
                        local = dict(values)
                        local.update({param.name: arg for param, arg in
                                      zip(node.decl.type.args.params, args)})
                        return run(node.body, local)

                    values['CloseAndNotify'] = lambda *args: invoke('CloseAndNotify', *args)
                    invoke(name, 0, 0, 0, 0 if mode == 'null' else menu)
                    expected = [] if mode == 'null' else ['destroy']
                    if mode == 'callback':
                        expected.append((constants[kind], button, 91))
                    self.assertEqual(calls, expected)


if __name__ == '__main__':
    unittest.main()
