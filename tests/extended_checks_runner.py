"""Check that the default runner cannot execute a database acceptance driver."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('extended_checks', ROOT / 'tools/run_extended_checks.py')
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)


class RunnerIsolation(unittest.TestCase):
    def test_default_plan_only_compiles_database_drivers(self):
        plan = runner.build_steps('cc')
        database = [step for step in plan if step.kind == 'database-compile']
        self.assertEqual({step.name for step in database}, {'market-sql-build', 'storage-sql-build'})
        for step in database:
            self.assertEqual(step.command[0], 'cc')
        for step in plan:
            self.assertNotIn(Path(step.command[0]).name, {path.name for path in runner.SQL_OUTPUTS.values()})
            self.assertFalse(any(Path(arg).name == 'run_feature_database_checks.py' for arg in step.command))

    def test_inherited_database_credentials_are_removed(self):
        with patch.dict(os.environ, {'STONE_MARKET_TEST_PASSWORD': 'not-a-real-secret',
                                    'STONE_STORAGE_TEST_DATABASE': 'not-a-real-db', 'MYSQL_PWD': 'not-a-real-secret',
                                    'MARIADB_HOST': 'not-a-host', 'PYTHONOPTIMIZE': '1'}):
            environment = runner.child_environment()
        self.assertFalse(any(key.startswith(('STONE_MARKET_TEST_', 'STONE_STORAGE_TEST_', 'MYSQL_', 'MARIADB_'))
                             for key in environment))
        self.assertEqual(environment['PYTHONOPTIMIZE'], '0')
        self.assertEqual(environment['STONE_CHECK_OUTPUT_DIR'], str(runner.OUTPUT))

    def test_accidental_database_step_is_blocked_before_process_creation(self):
        (ROOT / 'build/checks').mkdir(parents=True, exist_ok=True)
        commands = [[str(runner.SQL_OUTPUTS['market'])],
                    [sys.executable, str(ROOT / 'tools/run_feature_database_checks.py'), 'storage']]
        with tempfile.TemporaryDirectory(prefix='runner-guard-', dir=ROOT / 'build/checks') as directory:
            with patch.object(runner, 'OUTPUT', Path(directory)):
                for command in commands:
                    with patch.object(runner.subprocess, 'run') as execute:
                        report = runner.execute([runner.Step('accidental-db', 'test', command)])
                        self.assertEqual(report['status'], 'failed')
                        self.assertIn('separate explicit', report['error'])
                        execute.assert_not_called()


if __name__ == '__main__':
    unittest.main()
