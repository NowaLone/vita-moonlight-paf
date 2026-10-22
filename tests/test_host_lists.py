"""Interpret actual snapshot, list-refresh, identity and row-token lookup source.

Vectors, allocation, widgets and serial service reads are modeled; no C++/PAF/Vita run.
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
    """Value-initialized row storage, distinct from the host snapshot buffer."""


def host(number, **fields):
    entry = dict(host_id=f'uuid-{number}\0', mac='\0', name='Same name\0',
                 internal=f'192.0.2.{number}\0', external='\0', port=47989)
    entry.update(fields)
    # The interpreter needs C's integer value for character-array subscripts.
    return {key: [ord(c) for c in value] if isinstance(value, str) else value
            for key, value in entry.items()}


def normalize(source):
    source = strip_directives(re.sub(r'//[^\n]*', '', source))
    source = re.sub(r'template<class Service>\s*', '', source)
    source = source.replace('inline ', '').replace('Service &service', 'int service')
    source = re.sub(r'const MoonlightHost &(\w+)', r'const MoonlightHost *\1', source)
    source = source.replace('paf::vector<MoonlightHost> &hosts', 'Vector hosts')
    source = source.replace('MoonlightApp *app = MoonlightApp::Instance()', 'int app = get_app()')
    source = source.replace('app->Hosts()', '1')
    source = source.replace('MoonlightApp::Instance()->Discovery()', '2')
    source = source.replace('MoonlightApp::Instance()->Hosts().Delete(', 'delete_host(')
    source = source.replace('MoonlightApp::Instance()->Connection().ShowError(', 'show_error(')
    source = source.replace('service.GetHosts(', 'get_hosts(service, ')
    source = re.sub(r'paf::ui::ListView \*list = static_cast<paf::ui::ListView \*>\(\s*'
                    r'root->FindChild\("list_view_main"\)\)', 'int list = get_list()', source)
    # Do not turn the SDK's unsafe count constructor into an idealized allocation.
    # Empty construction + resize is the production path; sized construction must fail parsing.
    source = source.replace('paf::vector<HostRowState> rows;', 'Vector rows = make_rows();')
    source = re.sub(r'paf::vector<\w+> (\w+);', r'Vector \1 = make_vector();', source)
    source = re.sub(r'(?:list|m_list)->GetCellNum\(0\)', 'get_cells()', source)
    source = re.sub(r'(?:list|m_list)->(DeleteCell|InsertCell)\(', r'\1(', source)
    source = source.replace('list->GetCellNum(0)', 'get_cells()')
    source = re.sub(r'(\w+)\.size\(\)', r'vec_size(\1)', source)
    source = re.sub(r'(\w+)\.(size|data|resize|clear|swap)\(([^)]*)\)',
                    lambda m: f'vec_{m[2]}({m[1]}' + (', ' + m[3] if m[3] else '') + ')', source)
    return source


class HostLists(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.main = (ROOT / 'src/pages/page_main.cpp').read_text()
        cls.search = (ROOT / 'src/pages/page_search.cpp').read_text()
        helper = (ROOT / 'include/pages/host_list.h').read_text()
        helper = helper[helper.index('template<class Service>'):helper.rindex('\n}')]
        functions = [helper]
        for page, source in (('Main', cls.main), ('Search', cls.search)):
            names = ('RefreshHosts', 'FindHostIndex')
            if page == 'Main':
                names += ('AllHostsSelected', 'ToggleSelectAll', 'UpdateSelectionCount', 'DeleteSelectedHosts')
            for name in names:
                start = re.search(r'^(?:int|void|bool) ' + page + '::' + name + r'\(', source, re.M).start()
                end = source.index('\n}', start) + 2
                body = source[start:end].replace(page + '::', page + '_')
                functions.append(body.replace(' const\n', '\n'))
        lookup = cls.search[cls.search.index('    int saved_count ='):]
        lookup = lookup[:lookup.index('    if (canonical_index < 0)')]
        functions.append('int canonical_lookup(Vector saved_hosts, MoonlightHost host) {'
                         + lookup + 'return canonical_index; }')
        preamble = '''typedef int bool; typedef unsigned int uint32_t;
        typedef int Vector; typedef struct {
            char host_id[33], mac[18], name[256], internal[256], external[256]; int port;
        } MoonlightHost;'''
        unit = c_parser.CParser().parse(preamble + normalize('\n'.join(functions)))
        cls.functions = {item.decl.name: item for item in unit.ext if isinstance(item, c_ast.FuncDef)}

    def setUp(self):
        self.original_expression, self.original_execute = runtime.expression, runtime.execute
        self.trace, self.cells = [], 0
        self.saved, self.discovered = [], []
        self.getter = self.get_hosts
        self.values = dict(root=1, m_list=1, m_connecting=0, m_host_selected=0,
                           m_host_count=0, m_hosts=[], m_rows=[], m_host_tokens=[],
                           m_next_host_token=0, m_selection_mode=0, m_selected_count=0,
                           NULL=0, true=1, false=0,
                           get_app=lambda: 1, get_list=lambda: 1,
                           make_vector=lambda: [], make_rows=RowVector,
                           vec_size=len, vec_data=lambda x: x,
                           vec_resize=self.resize, vec_clear=lambda x: x.clear(), vec_swap=self.swap,
                           get_hosts=lambda service, out, capacity: self.getter(service, out, capacity),
                           strcmp=lambda a, b: 0 if a == b else 1,
                           get_cells=lambda: self.cells, DeleteCell=self.delete, InsertCell=self.insert,
                           HideStatus=lambda: self.trace.append('hide'),
                           ShowIdleStatus=lambda: self.trace.append('idle'),
                           ShowEmptyState=lambda: self.trace.append('empty'),
                           UpdateSelectionCount=lambda: self.invoke('Main_UpdateSelectionCount'),
                           UpdateSelectionActionBar=lambda: self.trace.append('count'),
                           AllHostsSelected=lambda: self.invoke('Main_AllHostsSelected'),
                           RefreshHosts=lambda: self.invoke('Main_RefreshHosts'),
                           delete_host=self.delete_host, ExitSelectionMode=self.exit_selection,
                           show_error=lambda *args: self.trace.append('error'))
        for name in self.functions:
            self.values[name] = lambda *args, name=name: self.invoke(name, *args)

    def resize(self, vector, count):
        vector[count:] = []
        for _ in range(count - len(vector)):
            vector.append(dict(token=0, checkbox_token=0, selected=False, checkbox=0)
                          if isinstance(vector, RowVector) else None)

    def swap(self, left, right):
        left[:], right[:] = right[:], left[:]

    def get_hosts(self, service, out, capacity):
        entries = self.saved if service == 1 else self.discovered
        if out == 0:
            return len(entries)
        out[:min(capacity, len(entries))] = copy.deepcopy(entries[:capacity])
        return min(capacity, len(entries)) if service == 1 else len(entries)

    def delete(self, segment, start, count):
        self.assertEqual(self.values['m_host_count'], 0)  # old callbacks invalid before delete
        self.assertEqual(count, self.cells)
        self.trace.append(('delete', count))
        self.cells -= count

    def insert(self, segment, start, count):
        self.assertEqual(len(self.values['m_hosts']), count)
        self.trace.append(('insert', count))
        self.cells += count

    def delete_host(self, entry):
        self.trace.append(('delete_host', entry['host_id']))
        self.saved[:] = [candidate for candidate in self.saved if candidate['host_id'] != entry['host_id']]
        return 0

    def exit_selection(self):
        # Native action-bar transitions are outside this serial data/selection check.
        self.values['m_selection_mode'] = 0
        self.values['m_selected_count'] = 0
        self.invoke('Main_RefreshHosts')

    def expression(self, node, values):
        if isinstance(node, c_ast.BinaryOp) and node.op == '<=':
            return self.expression(node.left, values) <= self.expression(node.right, values)
        if isinstance(node, c_ast.UnaryOp) and node.op == '++':
            value = (self.expression(node.expr, values) + 1) & 0xffffffff
            values[node.expr.name] = value
            return value
        return self.original_expression(node, values)

    def execute(self, node, values):
        if isinstance(node, c_ast.Continue):
            raise ContinueLoop()
        elif isinstance(node, c_ast.For):
            self.execute(node.init, values)
            iterations = 0
            while self.expression(node.cond, values):
                iterations += 1
                self.assertLess(iterations, 1000)
                try:
                    self.execute(node.stmt, values)
                except ContinueLoop:
                    pass
                except runtime.BreakSwitch:
                    break
                self.execute(node.next, values)
        elif isinstance(node, c_ast.Assignment) and isinstance(node.lvalue, c_ast.ArrayRef):
            self.assertEqual(node.op, '=')
            self.expression(node.lvalue.name, values)[self.expression(node.lvalue.subscript, values)] = self.expression(node.rvalue, values)
        elif isinstance(node, c_ast.DeclList):
            for declaration in node.decls:
                self.execute(declaration, values)
        elif isinstance(node, c_ast.UnaryOp):
            self.expression(node, values)
        else:
            self.original_execute(node, values)

    def invoke(self, name, *args):
        # Page members are shared; helper arguments/locals have their own C scope.
        values = self.values if name.startswith(('Main_', 'Search_')) else dict(self.values)
        function = self.functions[name]
        parameters = function.decl.type.args.params if function.decl.type.args else []
        values.update(zip((arg.name for arg in parameters), args))
        with patch.object(runtime, 'expression', self.expression), patch.object(runtime, 'execute', self.execute):
            try:
                self.execute(function.body, values)
            except runtime.Returned as result:
                return result.value

    def test_both_pages_publish_more_than_eight_and_shrink_to_empty(self):
        for page, attribute, count in (('Main', 'saved', 40), ('Search', 'discovered', 16)):
            self.setUp()
            setattr(self, attribute, [host(i) for i in range(count)])
            self.invoke(page + '_RefreshHosts')
            self.assertEqual((self.values['m_host_count'], self.cells), (count, count))
            setattr(self, attribute, [host(2)])
            self.invoke(page + '_RefreshHosts')
            self.assertEqual((self.values['m_host_count'], self.cells), (1, 1))
            setattr(self, attribute, [])
            self.invoke(page + '_RefreshHosts')
            self.assertEqual((self.values['m_host_count'], self.cells), (0, 0))
            self.assertIn(('delete', count), self.trace)

    def test_discovery_growth_between_count_and_copy_retries_without_truncating(self):
        self.discovered = [host(i) for i in range(8)]
        original = self.getter
        def growing(service, out, capacity):
            if out != 0 and capacity == 8:
                self.discovered.extend(host(i) for i in range(8, 16))
            return original(service, out, capacity)
        self.getter = growing
        self.invoke('Search_RefreshHosts')
        self.assertEqual((self.values['m_host_count'], self.cells), (16, 16))
        self.assertEqual(self.values['m_hosts'][-1], host(15))

    def test_failed_query_or_copy_preserves_old_cells_and_tokens(self):
        for page in ('Main', 'Search'):
            for fail_copy in (False, True):
                self.setUp()
                self.saved = self.discovered = [host(i) for i in range(12)]
                self.invoke(page + '_RefreshHosts')
                old = copy.deepcopy(self.values['m_rows'] if page == 'Main' else self.values['m_host_tokens'])
                original = self.getter
                self.getter = lambda service, out, capacity: (
                    -1 if bool(out != 0) == fail_copy else original(service, out, capacity))
                self.invoke(page + '_RefreshHosts')
                self.assertEqual((self.values['m_host_count'], self.cells), (12, 12))
                self.assertEqual(self.values['m_rows'] if page == 'Main' else self.values['m_host_tokens'], old)

    def test_selection_follows_uuid_after_reorder_and_does_not_select_new_pc(self):
        self.saved = [host(i) for i in range(12)]
        self.invoke('Main_RefreshHosts')
        self.values['m_selection_mode'] = 1
        self.values['m_rows'][10]['selected'] = True
        self.saved = [host(10, internal='192.0.2.50\0'), host(99), host(1)]
        self.invoke('Main_RefreshHosts')
        self.assertEqual([row['selected'] for row in self.values['m_rows']], [True, False, False])
        self.assertTrue(all(row['checkbox'] == 0 for row in self.values['m_rows']))

    def test_conflicting_uuid_and_mac_do_not_match_reused_address_or_name(self):
        for right in (host(2, internal='192.0.2.1\0'),
                      host(1, host_id='\0', mac='bb\0')):
            left = host(1, mac='aa\0')
            self.assertFalse(self.invoke('same_host_identity', left, right))
        self.assertTrue(self.invoke('same_host_identity', host(1), host(1, host_id='\0')))
        self.assertFalse(self.invoke('same_host_identity', host(1), host(1, host_id='\0', port=48000)))

    def test_old_row_tokens_are_rejected_on_both_pages_after_refresh(self):
        for page in ('Main', 'Search'):
            self.setUp()
            self.saved = self.discovered = [host(i) for i in range(12)]
            self.invoke(page + '_RefreshHosts')
            old = self.values['m_rows'][10]['token'] if page == 'Main' else self.values['m_host_tokens'][10]
            self.assertEqual(self.invoke(page + '_FindHostIndex', old), 10)
            if page == 'Main':
                checkbox = self.values['m_rows'][10]['checkbox_token']
                tokens = [row[key] for row in self.values['m_rows'] for key in ('token', 'checkbox_token')]
                self.assertEqual(len(tokens), len(set(tokens)))
                self.assertEqual(self.invoke('Main_FindHostIndex', checkbox), 10)
            self.invoke(page + '_RefreshHosts')
            self.assertEqual(self.invoke(page + '_FindHostIndex', old), -1)
            self.assertEqual(self.invoke(page + '_FindHostIndex', 0), -1)
            if page == 'Main':
                self.assertEqual(self.invoke('Main_FindHostIndex', checkbox), -1)

    def test_canonical_lookup_searches_all_saved_hosts_by_address_and_port(self):
        saved = [host(i) for i in range(20)]
        self.assertEqual(self.invoke('canonical_lookup', saved, host(18)), 18)
        self.assertEqual(self.invoke('canonical_lookup', saved, host(18, port=48000)), -1)
        self.assertEqual(self.invoke('canonical_lookup', saved, host(99)), -1)

    def test_select_all_clear_all_and_delete_include_rows_after_eight(self):
        self.saved = [host(i) for i in range(20)]
        self.invoke('Main_RefreshHosts')
        self.values['m_selection_mode'] = 1
        self.invoke('Main_ToggleSelectAll')
        self.assertEqual(self.values['m_selected_count'], 20)
        self.assertTrue(self.invoke('Main_AllHostsSelected'))
        self.invoke('Main_ToggleSelectAll')
        self.assertEqual(self.values['m_selected_count'], 0)
        self.assertFalse(self.invoke('Main_AllHostsSelected'))
        self.values['m_rows'][18]['selected'] = True
        self.invoke('Main_UpdateSelectionCount')
        self.invoke('Main_DeleteSelectedHosts')
        self.assertEqual((self.values['m_host_count'], self.cells), (19, 19))
        self.assertEqual([row[1] for row in self.trace if isinstance(row, tuple) and row[0] == 'delete_host'],
                         [host(18)['host_id']])

    def test_callbacks_bind_only_page_and_resolve_current_row_token(self):
        for source in (self.main, self.search):
            self.assertNotIn('HostButtonContext', source)
            self.assertIn('FindHostIndex(button->GetName().GetIDHash())', source)
            self.assertRegex(source, r'OnHostButton,\s*this\)')
        self.assertIn('checkbox->SetName(m_rows[param.cell_index].checkbox_token)', self.main)
        self.assertIn('FindHostIndex(checkbox->GetName().GetIDHash())', self.main)
        self.assertNotIn('kMaxHosts', self.search)

    def test_unsafe_paf_count_constructor_is_not_hidden_by_the_test_normalizer(self):
        unsafe = 'paf::vector<HostRowState> rows(hosts.size());'
        normalized = normalize(unsafe)
        self.assertIn('paf::vector<HostRowState>', normalized)
        # The old normalizer silently modeled this as safe allocation and missed the crash.
        self.assertNotIn('make_rows', normalized)
        self.assertNotRegex(self.main, r'paf::vector<HostRowState>\s+rows\s*\(')


if __name__ == '__main__':
    unittest.main()
