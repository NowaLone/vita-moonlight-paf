"""Interpret actual worker admission with serial pthread mocks; no builds or threads."""
import re
import unittest
from unittest.mock import patch

from pycparser import c_ast, c_parser
import test_adapter_lifecycle as runtime
from test_xml_source import ROOT, strip_directives


class GameStreamWorker(unittest.TestCase):
    def run_worker(self, failed_stage=None, failure=1, callback_result=0):
        source = (ROOT / 'src/backend/legacy/legacy_gamestream.c').read_text()
        record = re.search(r'typedef struct WorkerCall\s*{.*?}\s*WorkerCall;', source, re.S).group()
        functions = []
        for name in ('worker_entry', 'run_on_worker'):
            start = re.search(r'^static [^\n]+\b' + name + r'\(', source, re.M).start()
            functions.append(source[start:source.index('\n}', start) + 2])
        unit = c_parser.CParser().parse(strip_directives(
            'typedef int pthread_t, pthread_attr_t;' + record + '\n'.join(functions)))
        bodies = {node.decl.name: node for node in unit.ext if isinstance(node, c_ast.FuncDef)}
        header = (ROOT / 'src/backend/legacy/legacy_gamestream.h').read_text()
        values = dict(re.findall(r'^#define (LEGACY_GAMESTREAM_\w+) (-?\d+)', header, re.M))
        values = {key: int(value) for key, value in values.items()}
        calls, queued = [], []
        values.update(NULL=0, GAMESTREAM_WORKER_STACK=256 * 1024)
        original_expression, original_execute = runtime.expression, runtime.execute

        def expression(node, scope):
            if isinstance(node, c_ast.FuncCall) and isinstance(node.name, c_ast.StructRef):
                return expression(node.name, scope)(*[expression(arg, scope) for arg in node.args.exprs])
            return original_expression(node, scope)

        def execute(node, scope):
            if (isinstance(node, c_ast.Decl) and isinstance(node.type, c_ast.TypeDecl)
                    and node.type.type.names == ['WorkerCall']):
                scope[node.name] = {}
            else:
                original_execute(node, scope)

        def invoke(name, *args):
            node = bodies[name]
            scope = dict(values, **{param.name: arg for param, arg in
                                   zip(node.decl.type.args.params, args)})
            try:
                execute(node.body, scope)
            except runtime.Returned as result:
                return result.value

        def stage(name):
            calls.append(name)
            return failure if failed_stage == name else 0

        def create(thread, attr, entry, argument):
            result = stage('create')
            if not result:
                queued.append((entry, argument))
            return result

        def join(*args):
            calls.append('join')
            entry, argument = queued.pop()
            entry(argument)

        def callback(argument):
            self.assertEqual(argument, 73)
            calls.append('callback')
            return callback_result

        # Model addresses as mutable records for WorkerCall and opaque tokens for
        # pthread storage; only ownership/call order is checked, not ABI or races.
        def address(node, scope):
            if isinstance(node, c_ast.UnaryOp) and node.op == '&' and node.expr.name == 'call':
                return scope['call']
            return expression(node, scope)

        values.update(worker_entry=lambda argument: invoke('worker_entry', argument),
                      pthread_attr_init=lambda *args: stage('init'),
                      pthread_attr_setstacksize=lambda attr, size:
                          self.assertEqual(size, 256 * 1024) or stage('stack'),
                      pthread_attr_destroy=lambda *args: calls.append('destroy'),
                      pthread_create=create, pthread_join=join)
        with patch.object(runtime, 'expression', address), patch.object(runtime, 'execute', execute):
            result = invoke('run_on_worker', callback, 73)
        return result, calls, values['LEGACY_GAMESTREAM_INTERNAL_ERROR']

    def test_every_admission_failure_rejects_without_running_callback(self):
        for failed_stage, expected in (
                ('init', ['init']),
                ('stack', ['init', 'stack', 'destroy']),
                ('create', ['init', 'stack', 'create', 'destroy'])):
            for failure in (-1, 1, 12):
                with self.subTest(stage=failed_stage, result=failure):
                    result, calls, internal_error = self.run_worker(failed_stage, failure)
                    self.assertEqual(result, internal_error)
                    self.assertEqual(calls, expected)

    def test_success_joins_and_preserves_callback_result(self):
        for callback_result in (-13, 0, 17):
            with self.subTest(result=callback_result):
                result, calls, _ = self.run_worker(callback_result=callback_result)
                self.assertEqual(result, callback_result)
                self.assertEqual(calls, ['init', 'stack', 'create', 'destroy', 'join', 'callback'])


if __name__ == '__main__':
    unittest.main()
