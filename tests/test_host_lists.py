"""Interpret Main's actual list/identity/selection logic with serial vector/UI mocks.

No C++ compilation, PAF rendering, native focus/scroll or device execution.
"""
import copy
import re
import unittest
from unittest.mock import patch
from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives


class ContinueLoop(Exception):
    pass


class RowVector(list):
    pass


def host(number, **fields):
    entry = dict(host_id=f'uuid-{number}\0', mac='\0', name='Same name\0',
                 internal=f'192.0.2.{number}\0', external='\0', port=47989)
    entry.update(fields)
    return {key: [ord(c) for c in value] if isinstance(value, str) else value
            for key, value in entry.items()}


def function(source, name):
    start = re.search(r'^(?:inline |static )?(?:int|void|bool) ' + re.escape(name) + r'\(', source, re.M).start()
    return source[start:source.index('\n}', start) + 2]


def normalize(source):
    source = strip_directives(re.sub(r'//[^\n]*', '', source))
    source = re.sub(r'template<class Service>\s*', '', source).replace('inline ', '')
    source = source.replace('Service &service', 'int service')
    source = re.sub(r'const MoonlightHost &(\w+)', r'const MoonlightHost *\1', source)
    source = source.replace('paf::vector<MoonlightHost> &hosts', 'Vector hosts')
    source = source.replace('MoonlightApp *app = MoonlightApp::Instance()', 'int app = get_app()')
    source = source.replace('app->Hosts().Add(', 'add_host(')
    source = source.replace('app->Hosts()', '1').replace('app->Discovery()', '2')
    source = source.replace('app->Connection().IsBusy()', 'is_busy()')
    source = source.replace('MoonlightApp::Instance()->Connection().IsBusy()', 'is_busy()')
    source = source.replace('Base::GetCurrent() == this', 'is_current()')
    source = source.replace('app->Connection().ShowError(', 'show_error(')
    source = source.replace('MoonlightApp::Instance()->Hosts().Delete(', 'delete_host(')
    source = source.replace('MoonlightApp::Instance()->Connection().ShowError(', 'show_error(')
    source = source.replace('service.GetHosts(', 'get_hosts(service, ')
    source = source.replace('paf::vector<HostRowState> rows;', 'Vector rows = make_rows();')
    source = re.sub(r'paf::vector<\w+> (\w+);', r'Vector \1 = make_vector();', source)
    source = re.sub(r'(\w+)\.size\(\)', r'vec_size(\1)', source)
    source = re.sub(r'(\w+)\.(size|data|resize|clear|swap|push_back|pop_back)\(([^)]*)\)',
                    lambda m: f'vec_{m[2]}({m[1]}' + (', ' + m[3] if m[3] else '') + ')', source)
    for method in ('DeleteCell', 'MoveCell', 'InsertCell'):
        source = source.replace('list->' + method, method)
    source = source.replace('static_cast<paf::ui::ListView *>(root->FindChild("list_view_main"))', 'get_list()')
    source = source.replace('static_cast<paf::ui::CheckBox *>(checkbox)->SetCheck(', 'set_check(checkbox, ')
    source = re.sub(r'paf::ui::(?:Widget|ListView|ListItem) \*', 'int ', source)
    source = source.replace('item->FindChild(', 'find_child(item, ')
    source = source.replace('list->GetListItem(0, ', 'get_item(list, ')
    source = source.replace('button->IsFocused()', 'is_focused(button)')
    source = source.replace('0.0f', '0')
    return source.replace('Main::', 'Main_').replace(' const\n', '\n')


class HostLists(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.main = (ROOT / 'src/pages/page_main.cpp').read_text()
        helper = (ROOT / 'include/pages/host_list.h').read_text()
        helper = helper[helper.index('template<class Service>'):helper.rindex('\n}')]
        functions = [helper]
        for name in ('CanDiscover', 'ReadHostRows', 'FindHostIndex', 'AllHostsSelected', 'UpdateSelectionCount', 'DeleteSelectedHosts',
                     'UpdateHostItem', 'ToggleHostSelection', 'ToggleSelectAll'):
            functions.append(function(cls.main, 'Main::' + name))
        capture = cls.main.index('    for (int i = 0; i < m_host_count; ++i)', cls.main.index('    uint32_t focused_token = 0;'))
        functions.append('void capture_focus(Vector cells) {' + cls.main[capture:cls.main.index('    /* Block native generation', capture)] + '}')
        # Interpret the actual row-template/token/selection preparation before any PAF edits.
        fragment = cls.main.split('    paf::vector<HostRowState> rows;', 1)[1].split('    paf::vector<uint32_t> cells;', 1)[0]
        functions.append('void prepare_rows(Vector hosts, Vector rows) {' + fragment + '}')
        edits = cls.main[cls.main.index('        if (edit == 1)'):]
        edits = edits[:edits.index('    }\n    list->UnblockSync();')]
        functions.append('void apply_cell_edit(int edit, int from, int to, Vector cells, Vector desired) {' + edits + '}')
        selection = function(cls.main, 'Main::SelectHost')
        selection = selection[:selection.index('    paf::ui::ListView *list')] + '\n}'
        functions.append(selection)
        focus = cls.main[cls.main.index('    if (focused_index >= 0'):]
        focus = focus[:focus.index('    if (m_selection_mode)')]
        focus = focus.replace('list->SetFocus', 'record_focus').replace(
            'paf::ui::ListView::FOCUS_ALIGN_TYPE_HEAD', '0')
        functions.append('void restore_focus(void) {' + focus + '}')
        preamble = '''typedef int bool; typedef unsigned int uint32_t; typedef int Vector;
        typedef struct {char host_id[33], mac[18], name[256], internal[256], external[256]; int port;} MoonlightHost;'''
        code = normalize('\n'.join(functions)).replace('*from =', 'set_from(').replace('*to =', 'set_to(')
        code = re.sub(r'(set_(?:from|to)\([^;]+);', r'\1);', code)
        unit = c_parser.CParser().parse(preamble + code)
        cls.functions = {item.decl.name: item for item in unit.ext if isinstance(item, c_ast.FuncDef)}

    def setUp(self):
        self.base_expression, self.base_execute = runtime.expression, runtime.execute
        self.saved, self.discovered, self.trace = [], [], []
        self.values = dict(root=1, m_connecting=0, m_host_count=0, m_saved_count=0,
                           m_hosts=[], m_saved_hosts=[], m_rows=[], m_next_host_token=0,
                           m_selection_mode=0, m_selected_count=0, m_ime_open=0,
                           m_ime_retry_pending=0, m_pending_action=0, Pending_None=0,
                           NULL=0, true=1, false=0, get_app=lambda: 1, is_busy=lambda: False, is_current=lambda: True,
                           make_vector=list, make_rows=RowVector,
                           vec_size=len, vec_data=lambda x: x, vec_resize=self.resize,
                           vec_clear=lambda x: x.clear(), vec_swap=self.swap, vec_pop_back=lambda x: x.pop(),
                           vec_push_back=lambda x, y: x.append(copy.deepcopy(y)),
                           get_hosts=self.get_hosts, strcmp=lambda a,b: 0 if a == b else 1,
                           HideStatus=lambda: self.trace.append('hide'),
                           SuspendBackground=lambda: self.trace.append('cancel'),
                           show_error=lambda *args: self.trace.append('error'),
                           add_host=self.add_host, delete_host=self.delete_host,
                           UpdateSelectionActionBar=lambda: None,
                           ExitSelectionMode=lambda: None)
        self.getter = self.get_hosts
        self.values['get_hosts'] = lambda *args: self.getter(*args)
        self.values['FindHostIndex'] = lambda token: self.invoke('Main_FindHostIndex', token)
        for name in self.functions:
            self.values[name] = lambda *args, name=name: self.invoke(name, *args)
            if name.startswith('Main_'):
                self.values[name[5:]] = self.values[name]

    def resize(self, vector, count):
        vector[count:] = []
        for _ in range(count - len(vector)):
            vector.append(dict(saved=False, selection_template=False, token=0,
                               checkbox_token=0, selected=False) if isinstance(vector, RowVector) else None)

    @staticmethod
    def swap(a, b):
        a[:], b[:] = b[:], a[:]

    def get_hosts(self, service, out, capacity):
        entries = self.saved if service == 1 else self.discovered
        if out == 0: return len(entries)
        out[:min(capacity, len(entries))] = copy.deepcopy(entries[:capacity])
        return min(capacity, len(entries)) if service == 1 else len(entries)

    def add_host(self, address, port, name):
        self.trace.append('add')
        self.saved.append(host(99, internal=''.join(map(chr, address)), port=port))
        # Simulate list changes during persistence: SelectHost must use its value copy.
        self.values['m_hosts'].clear()
        return 0

    def delete_host(self, entry):
        self.trace.append(('delete', copy.deepcopy(entry)))
        return 0

    def expression(self, node, values):
        if isinstance(node, c_ast.UnaryOp) and node.op in ('++', '--'):
            result = self.expression(node.expr, values) + (1 if node.op == '++' else -1)
            if node.expr.name == 'm_next_host_token': result &= 0xffffffff
            values[node.expr.name] = result
            return result
        if isinstance(node, c_ast.BinaryOp) and node.op == '<=':
            return self.expression(node.left, values) <= self.expression(node.right, values)
        return self.base_expression(node, values)

    def execute(self, node, values):
        if isinstance(node, c_ast.Continue): raise ContinueLoop()
        if isinstance(node, (c_ast.For, c_ast.While)):
            if isinstance(node, c_ast.For): self.execute(node.init, values)
            steps = 0
            while node.cond is None or self.expression(node.cond, values):
                steps += 1
                self.assertLess(steps, 1000)
                try: self.execute(node.stmt, values)
                except ContinueLoop: pass
                except runtime.BreakSwitch: break
                if isinstance(node, c_ast.For): self.execute(node.next, values)
        elif isinstance(node, c_ast.Assignment) and isinstance(node.lvalue, c_ast.ArrayRef):
            self.expression(node.lvalue.name, values)[self.expression(node.lvalue.subscript, values)] = copy.deepcopy(self.expression(node.rvalue, values))
        elif isinstance(node, c_ast.DeclList):
            for declaration in node.decls: self.execute(declaration, values)
        elif isinstance(node, c_ast.UnaryOp): self.expression(node, values)
        elif isinstance(node, c_ast.Decl) and node.init and isinstance(node.type, c_ast.TypeDecl) and node.type.type.names == ['MoonlightHost']:
            values[node.name] = copy.deepcopy(self.expression(node.init, values))
        else: self.base_execute(node, values)

    def invoke(self, name, *args):
        values = self.values if name.startswith(('Main_', 'prepare_rows', 'restore_focus', 'capture_focus')) else dict(self.values)
        parameters = self.functions[name].decl.type.args.params if self.functions[name].decl.type.args else []
        values.update(zip((arg.name for arg in parameters), args))
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            try: self.execute(self.functions[name].body, values)
            except runtime.Returned as result: return result.value

    def refresh(self):
        hosts, rows = [], RowVector()
        if not self.invoke('Main_ReadHostRows', hosts): return False
        self.invoke('prepare_rows', hosts, rows)
        self.values.update(m_hosts=hosts, m_rows=rows, m_host_count=len(hosts))
        return True

    def test_saved_first_full_lists_and_duplicate_names_are_distinct(self):
        self.saved = [host(i) for i in range(40)]
        self.discovered = [host(i, host_id='\0') for i in (2, 18, 50, 51)]
        self.refresh()
        self.assertEqual(self.values['m_host_count'], 42)
        self.assertEqual(self.values['m_hosts'][-2:], self.discovered[-2:])
        self.assertEqual(sum(row['saved'] for row in self.values['m_rows']), 40)

    def test_identity_uses_real_uuid_mac_routes_and_port_never_name(self):
        self.assertFalse(self.invoke('same_discovered_host', host(1), host(2, internal='192.0.2.1\0')))
        self.assertFalse(self.invoke('same_discovered_host', host(1, host_id='\0', mac='aa\0'), host(1, host_id='\0', mac='bb\0')))
        self.assertTrue(self.invoke('same_discovered_host', host(1), host(1, host_id='\0')))
        self.assertTrue(self.invoke('same_discovered_host', host(1, internal='old\0', external='192.0.2.2\0'), host(2, host_id='\0')))
        self.assertFalse(self.invoke('same_discovered_host', host(1), host(1, host_id='\0', port=48000)))
        self.assertFalse(self.invoke('same_discovered_host', host(1, internal='\0'), host(2, internal='\0')))

    def test_unchanged_and_reordered_hosts_keep_tokens_and_selection(self):
        self.saved = [host(i) for i in range(12)]
        self.values['m_selection_mode'] = True
        self.refresh()
        self.values['m_rows'][10]['selected'] = True
        original = copy.deepcopy(self.values['m_rows'])
        self.refresh()
        self.assertEqual(self.values['m_rows'], original)
        self.saved = [host(10, internal='changed\0'), host(99), host(1)]
        self.refresh()
        self.assertEqual([row['selected'] for row in self.values['m_rows']], [True, False, False])
        self.assertEqual(self.values['m_rows'][0]['token'], original[10]['token'])

    def test_temporary_to_saved_retemplates_only_that_pc_and_rejects_old_callback(self):
        self.saved, self.discovered = [host(1)], [host(2, host_id='\0')]
        self.refresh()
        first, old = [row['token'] for row in self.values['m_rows']]
        self.saved.append(host(2))
        self.refresh()
        self.assertEqual(self.values['m_host_count'], 2)
        self.assertEqual(self.values['m_rows'][0]['token'], first)
        self.assertTrue(self.values['m_rows'][1]['saved'])
        self.assertEqual(self.invoke('Main_FindHostIndex', old), -1)
        self.assertEqual(self.invoke('Main_FindHostIndex', 0), -1)

    def test_failed_snapshot_preserves_published_rows_and_growth_retries(self):
        self.saved = [host(1)]
        self.refresh()
        old = copy.deepcopy(self.values['m_rows'])
        self.getter = lambda *args: -1
        self.assertFalse(self.refresh())
        self.assertEqual(self.values['m_rows'], old)
        self.discovered = [host(i, host_id='\0') for i in range(8)]
        def growing(service, out, capacity):
            if service == 2 and out != 0 and capacity == 8:
                self.discovered += [host(i, host_id='\0') for i in range(8, 16)]
            return self.get_hosts(service, out, capacity)
        self.getter = growing
        self.refresh()
        self.assertEqual(self.values['m_host_count'], 16)

    def test_delete_mode_only_contains_saved_and_deleted_pc_can_reappear(self):
        self.saved, self.discovered = [host(1)], [host(1, host_id='\0'), host(2, host_id='\0')]
        self.values['m_selection_mode'] = True
        self.refresh()
        self.assertEqual(self.values['m_host_count'], 1)
        self.values['m_rows'][0]['selected'] = True
        self.invoke('Main_UpdateSelectionCount')
        self.assertTrue(self.invoke('Main_AllHostsSelected'))
        self.invoke('Main_DeleteSelectedHosts')
        self.assertEqual(len(self.trace), 1)
        self.saved = []
        self.values['m_selection_mode'] = False
        self.refresh()
        self.assertEqual(self.values['m_host_count'], 2)
        self.assertFalse(any(row['saved'] for row in self.values['m_rows']))

    def test_renamed_checkboxes_follow_row_and_select_all_without_rebuilding(self):
        self.saved = [host(i) for i in range(12)]
        self.values['m_selection_mode'] = True
        self.refresh()
        # SetName replaces the template ID: live children are keyed only by tokens.
        items = {i: {row['checkbox_token']: dict(checked=False)}
                 for i, row in enumerate(self.values['m_rows'])}
        self.values.update(get_list=lambda: 1, get_item=lambda _, i: items.get(i, 0),
                           find_child=lambda item, name: item.get(name, 0),
                           set_check=lambda checkbox, checked, *_: checkbox.update(checked=bool(checked)),
                           set_child_text=lambda *_: None, set_host_availability=lambda *_: None)
        row = self.values['m_rows'][10]
        checkbox = items[10][row['checkbox_token']]
        self.invoke('Main_ToggleHostSelection', 10)
        self.assertTrue(checkbox['checked'])
        self.assertEqual(self.values['m_selected_count'], 1)
        self.invoke('Main_ToggleHostSelection', 10)
        self.assertFalse(checkbox['checked'])
        self.invoke('Main_ToggleSelectAll')
        self.assertTrue(all(child['checked'] for item in items.values() for child in item.values()))
        self.assertEqual(self.values['m_selected_count'], 12)
        self.invoke('Main_ToggleSelectAll')
        self.assertFalse(any(child['checked'] for item in items.values() for child in item.values()))
        # A virtualized row has no widget; its model must still retain selection.
        items.pop(10)
        self.invoke('Main_ToggleSelectAll')
        self.assertTrue(row['selected'])
        replacement = dict(checked=False)
        items[10] = {row['checkbox_token']: replacement}
        self.invoke('Main_UpdateHostItem', items[10], 10)
        self.assertTrue(replacement['checked'])

    def test_focus_capture_finds_renamed_row_button(self):
        self.saved = [host(i) for i in range(12)]
        self.refresh()
        items = [{row['token']: dict(focused=i == 10)}
                 for i, row in enumerate(self.values['m_rows'])]
        self.values.update(list=1, get_item=lambda _, i: items[i],
                           find_child=lambda item, name: item.get(name, 0),
                           is_focused=lambda button: button['focused'],
                           focused_index=-1, focused_token=0, focused_host=host(99))
        self.invoke('capture_focus', [])
        self.assertEqual(self.values['focused_index'], 10)
        self.assertEqual(self.values['focused_token'], self.values['m_rows'][10]['token'])

    def test_selection_captures_value_before_save_and_resolves_full_canonical_list(self):
        self.saved = [host(i) for i in range(20)]
        self.discovered = [host(50, host_id='\0')]
        self.refresh()
        self.invoke('Main_SelectHost', 20)
        self.assertEqual(self.values['host']['internal'], host(50)['internal'])
        self.assertEqual(self.trace, ['cancel', 'add'])
        self.setUp()
        self.discovered = [host(50, host_id='\0')]
        self.refresh()
        self.values['add_host'] = lambda *args: -5
        self.invoke('Main_SelectHost', 0)
        self.assertEqual(self.trace, ['cancel', 'error'])
        self.assertFalse(self.values['m_connecting'])

    def test_cell_edits_are_minimal_for_unchanged_append_remove_move(self):
        for current, wanted, expected in (([1,2], [1,2], []), ([1,2], [1,2,3], [3]),
                                         ([1,2,3], [1,3], [1]), ([1,2,3], [3,1,2], [2]),
                                         ([1,2], [3,1,2], [3]), ([1,2], [1,3,2], [3]),
                                         ([1,2], [2], [1]), ([1,2], [1], [1]),
                                         ([1], [], [1]), ([], [1], [3])):
            edits, native_calls = [], []
            for method in ('DeleteCell', 'MoveCell', 'InsertCell'):
                self.values[method] = lambda *args, method=method: native_calls.append((method, args))
            while True:
                output = {}
                self.values['set_from'] = lambda x: output.update(from_index=x)
                self.values['set_to'] = lambda x: output.update(to_index=x)
                edit = self.invoke('next_host_cell_edit', current, len(current), wanted, len(wanted), 0, 0)
                if not edit: break
                edits.append(edit)
                self.invoke('apply_cell_edit', edit, output.get('from_index', 0),
                            output.get('to_index', 0), current, wanted)
            self.assertEqual(current, wanted)
            self.assertEqual(edits, expected)
            self.assertEqual([call[0] for call in native_calls],
                             [{1: 'DeleteCell', 2: 'MoveCell', 3: 'InsertCell'}[edit] for edit in expected])
            for method, args in native_calls:
                if method != 'MoveCell': self.assertEqual(args[2], 1)

    def test_focus_replacement_uses_same_identity_then_nearest_and_keeps_retained_focus(self):
        self.saved = [host(i) for i in range(12)]
        self.refresh()
        calls = []
        self.values['record_focus'] = lambda segment, index, align, timer: calls.append(index)
        self.values.update(focused_index=10, focused_host=host(10), focused_token=9999)
        self.invoke('restore_focus')
        self.assertEqual(calls, [10])
        self.values['focused_host'] = host(99)
        self.saved = [host(1),host(2)]
        self.refresh()
        self.invoke('restore_focus')
        self.assertEqual(calls, [10,1])
        self.values['focused_token'] = self.values['m_rows'][0]['token']
        self.invoke('restore_focus')
        self.assertEqual(calls, [10,1])
        self.values.update(m_hosts=[], m_rows=[], m_host_count=0)
        self.invoke('restore_focus')
        self.assertEqual(calls, [10,1])

    def test_background_eligibility_blocks_every_conflicting_flow_and_resumes(self):
        flags = dict(m_list_ready=1, m_system_ui_overlaid=0, m_connecting=0,
                     m_selection_mode=0, m_settings_suspended=0, m_ime_open=0,
                     m_ime_retry_pending=0, m_pending_action=0, m_delete_dialog_open=0)
        self.values.update(flags)
        self.assertTrue(self.invoke('Main_CanDiscover'))
        for key, initial in flags.items():
            self.values[key] = not initial
            self.assertFalse(self.invoke('Main_CanDiscover'), key)
            self.values[key] = initial
        for key in ('is_busy', 'is_current'):
            self.values[key] = (lambda: True) if key == 'is_busy' else (lambda: False)
            self.assertFalse(self.invoke('Main_CanDiscover'))
            self.values[key] = (lambda: False) if key == 'is_busy' else (lambda: True)
        self.assertTrue(self.invoke('Main_CanDiscover'))

    def test_saved_and_discovered_cxml_columns_match_without_a_temporary_dot(self):
        import xml.etree.ElementTree as ET
        xml = ET.parse(ROOT / 'cxml/vita_moonlight_ui.xml').getroot()
        saved = xml.find(".//template[@id='template_host_list_item']")
        temporary = xml.find(".//template[@id='template_host_list_item_discovered']")
        self.assertIsNone(temporary.find(".//plane[@id='plane_host_availability']"))
        self.assertIsNotNone(saved.find(".//plane[@id='plane_host_availability']"))
        for ident in ('text_host_title', 'text_host_desc', 'plane_host_pc'):
            self.assertEqual(saved.find(f".//*[@id='{ident}']/layout_hint").attrib,
                             temporary.find(f".//*[@id='{ident}']/layout_hint").attrib)
        self.assertIsNone(xml.find(".//page[@id='page_search_pcs']"))
        self.assertIsNone(xml.find(".//button[@id='btn_search_pcs']"))

    def test_paf_bindings_focus_fallback_and_return_routes(self):
        self.assertIn('FindHostIndex(button->GetName().GetIDHash())', self.main)
        self.assertIn('next_host_cell_edit', self.main)
        self.assertNotIn('std::vector', self.main)
        self.assertNotIn('#include <vector>', self.main)
        self.assertNotRegex(self.main, r'paf::vector<uint32_t>\s+(cells|desired)\s*\(')
        self.assertIn('focused_index < m_host_count ? focused_index : m_host_count - 1', self.main)
        self.assertIn('app->Hosts().CheckAvailability(main, main->m_saved_hosts)', self.main)
        self.assertNotIn('page_search', (ROOT / 'CMakeLists.txt').read_text())
        for file in ('page_apps.cpp', 'page_pairing.cpp'):
            source = (ROOT / 'src/pages' / file).read_text()
            self.assertNotIn('Search', source)
            self.assertIn('main->RestoreEventCallback()', source)
        self.assertNotRegex(self.main, r'paf::vector<HostRowState>\s+rows\s*\(')


if __name__ == '__main__': unittest.main()
