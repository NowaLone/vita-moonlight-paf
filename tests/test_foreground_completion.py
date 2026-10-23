"""Interpret the actual terminal-event predicate; no PAF or compilation required."""
import re
import unittest
from pycparser import c_ast, c_parser
from test_pairing_verification import evaluate
from test_xml_source import ROOT, strip_directives


def evaluate_predicate(node, values):
    if isinstance(node, c_ast.TernaryOp):
        branch = node.iftrue if evaluate_predicate(node.cond, values) else node.iffalse
        return evaluate_predicate(branch, values)
    if isinstance(node, c_ast.BinaryOp) and node.op == '>=':
        return int(evaluate_predicate(node.left, values) >= evaluate_predicate(node.right, values))
    if isinstance(node, c_ast.BinaryOp) and node.op == '||':
        return int(bool(evaluate_predicate(node.left, values)) or
                   bool(evaluate_predicate(node.right, values)))
    return evaluate(node, values)


class ForegroundCompletion(unittest.TestCase):
    def test_completed_job_distinguishes_zero_apps_from_failure(self):
        source = (ROOT / 'src/services/connection_service.cpp').read_text()
        body = source.split('bool ConnectionService::GetCompletedJobEvent(', 1)[1]
        body = body.split('void ConnectionService::DialogPollTask(', 1)[0]
        expressions = re.findall(r'event->type\s*=\s*(.*?);', body, re.S)
        values = dict(MOONLIGHT_EVENT_CONNECTION_READY=10, MOONLIGHT_EVENT_CONNECTION_FAILED=11,
                      MOONLIGHT_EVENT_APPLICATIONS_READY=20, MOONLIGHT_EVENT_APPLICATIONS_FAILED=21)
        predicates = [c_parser.CParser().parse('int check(void) { return ' + expression + '; }')
                      .ext[0].body.block_items[0].expr for expression in expressions]
        self.assertEqual(len(predicates), 2)
        for result in (-1002, -1, 0, 1):
            with self.subTest(result=result):
                environment = dict(values, m_worker_result=result)
                self.assertEqual(evaluate_predicate(predicates[0], environment),
                                 10 if result == 0 else 11)
                self.assertEqual(evaluate_predicate(predicates[1], environment),
                                 20 if result >= 0 else 21)

    def test_only_the_owned_operations_terminal_events_complete_it(self):
        header = (ROOT / 'include/services/connection_service.h').read_text()
        operation_body = re.search(r'enum ForegroundOperation\s*{(.*?)}', header, re.S).group(1)
        source = (ROOT / 'src/services/connection_service.cpp').read_text()
        predicate = 'static bool completes_operation(' + source.split(
            'static bool completes_operation(', 1)[1].split('class ConnectJob', 1)[0]
        predicate = predicate.replace('ConnectionService::', '')
        types = strip_directives((ROOT / 'include/moonlight/types.h').read_text())
        unit = c_parser.CParser().parse(
            'typedef unsigned int uint32_t; typedef unsigned short uint16_t; typedef int bool;' + types +
            'typedef enum {' + operation_body + '} ForegroundOperation;' + predicate)
        values = {'false': 0}
        for declaration in unit.ext:
            if not isinstance(declaration, c_ast.Typedef):
                continue
            enum = declaration.type.type
            if isinstance(enum, c_ast.Enum) and declaration.name in (
                    'MoonlightEventType', 'ForegroundOperation'):
                value = -1
                for member in enum.values.enumerators:
                    value = int(member.value.value) if member.value else value + 1
                    values[member.name] = value
        cases = unit.ext[-1].body.block_items[0].stmt.block_items
        returns = {case.expr.name: case.stmts[0].expr
                   for case in cases if isinstance(case, c_ast.Case)}
        default = next(case.stmts[0].expr for case in cases
                       if isinstance(case, c_ast.Default))
        expected = {
            'Operation_None': set(),
            'Operation_Connect': {'MOONLIGHT_EVENT_CONNECTION_READY',
                                  'MOONLIGHT_EVENT_CONNECTION_FAILED'},
            'Operation_Applications': {'MOONLIGHT_EVENT_APPLICATIONS_READY',
                                       'MOONLIGHT_EVENT_APPLICATIONS_FAILED'},
            'Operation_Launch': {'MOONLIGHT_EVENT_STREAM_STARTED',
                                 'MOONLIGHT_EVENT_STREAM_FAILED',
                                 'MOONLIGHT_EVENT_STREAM_STOPPED'},
            'Unknown_operation': set(),
        }
        events = [name for name in values if name.startswith('MOONLIGHT_EVENT_')]
        for operation, terminal_events in expected.items():
            for event in events:
                with self.subTest(operation=operation, event=event):
                    actual = evaluate_predicate(returns.get(operation, default),
                                                dict(values, type=values[event]))
                    self.assertEqual(bool(actual), event in terminal_events)


if __name__ == '__main__':
    unittest.main()
