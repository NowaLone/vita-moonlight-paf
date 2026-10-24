"""Exercise CI trust gates in isolation; never execute setup or build commands."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest


HELPERS = (Path(__file__).resolve().parents[1] / 'scripts/ci_setup.sh').read_text().split(
    '\n# Setup starts here.', 1)[0]


class CiSetup(unittest.TestCase):
    def run_helper(self, command, *args):
        return subprocess.run(['bash', '-c', HELPERS + '\n' + command, 'ci-test', *args],
                              capture_output=True, text=True)

    def test_archive_gate_stops_before_next_command_on_missing_or_corrupt_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'sdk archive.tar.bz2'
            expected = hashlib.sha256(b'expected SDK bytes').hexdigest()
            for contents in (b'expected SDK bytes', b'wrong SDK bytes', None):
                with self.subTest(contents=contents):
                    if contents is None:
                        archive.unlink()
                    else:
                        archive.write_bytes(contents)
                    result = self.run_helper('verify_archive "$1" "$2"\necho NEXT_STEP',
                                             expected, str(archive))
                    accepted = contents == b'expected SDK bytes'
                    self.assertEqual(result.returncode == 0, accepted, result.stderr)
                    self.assertEqual('NEXT_STEP' in result.stdout, accepted)

    def test_openssl_pin_rejects_recipe_drift_without_rewriting(self):
        source = 'git+https://github.com/d3m3vilurr/vita-openssl.git'
        commit = 'c' * 40
        with tempfile.TemporaryDirectory() as directory:
            recipe = Path(directory) / 'VITABUILD'
            original = 'source=("' + source + '")\nsha256sums=(\'SKIP\')\n'
            result = self.run_helper('pin_openssl_recipe "$1" "$2"', str(recipe), commit)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(recipe.exists())
            recipe.write_text(original)
            result = self.run_helper('pin_openssl_recipe "$1" "$2"', str(recipe), commit)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(recipe.read_text(), original.replace(source, source + '#commit=' + commit))
            for text, revision in ((original, 'HEAD'), (original, 'x' * 40),
                                   (original.replace(source, source + '#branch=master'), commit),
                                   (original + original, commit)):
                with self.subTest(text=text, revision=revision):
                    recipe.write_text(text)
                    result = self.run_helper('pin_openssl_recipe "$1" "$2"\necho NEXT_STEP',
                                             str(recipe), revision)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertNotIn('NEXT_STEP', result.stdout)
                    self.assertEqual(recipe.read_text(), text)


if __name__ == '__main__':
    unittest.main()
