"""Non-build checks of the actual pairing verification helper (requires pycparser).

These exercise return-code logic with mocked EVP results, not OpenSSL or Vita runtime.
Run: python3 -m unittest discover -s tests -p 'test_*.py'
"""
from pathlib import Path
import itertools
import unittest

from pycparser import c_ast


SOURCE = (Path(__file__).resolve().parents[1] /
          'src/backend/legacy/legacy_gamestream.c').read_text()


def evaluate(node, results):
    if isinstance(node, c_ast.ID):
        return results[node.name]
    if isinstance(node, c_ast.Constant):
        return int(node.value)
    if isinstance(node, c_ast.FuncCall):
        return results[node.name.name]
    if isinstance(node, c_ast.BinaryOp):
        if node.op == '&&':
            return int(bool(evaluate(node.left, results)) and
                       bool(evaluate(node.right, results)))
        if node.op == '==':
            return int(evaluate(node.left, results) == evaluate(node.right, results))
    raise AssertionError(f'Unsupported expression node: {node}')


class PairingVerification(unittest.TestCase):
    def test_only_successful_evp_results_accept_proof(self):
        from test_pairing_errors import Harness
        for values in itertools.product((-1, 0, 1, 2), repeat=3):
            with self.subTest(results=values):
                harness = Harness()
                for name, result in zip(('EVP_DigestVerifyInit', 'EVP_DigestVerifyUpdate', 'EVP_DigestVerifyFinal'), values):
                    harness.values[name] = lambda *args, result=result: result
                accepted = harness.invoke('verify_signature', 1, 16, 2, 256, 'PEM') == harness.code('OK')
                self.assertEqual(accepted, values == (1, 1, 1))

    def test_authenticate_before_wrong_pin_classification(self):
        body = SOURCE.split('static int pair(', 1)[1]
        self.assertLess(body.index('step_result = verify_signature('),
                        body.index('memcmp(challenge_response, expected_response_hash'))


if __name__ == '__main__':
    unittest.main()
