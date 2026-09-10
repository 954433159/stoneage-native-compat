"""A vanished PID or a previous run's log must not prove a successful save."""
from contextlib import ExitStack
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import runtime


class RuntimeStopTests(unittest.TestCase):
    def scenario(self, name, history, new, owned=True):
        with tempfile.TemporaryDirectory(prefix='stone-stop-marker-') as folder, ExitStack() as stack:
            root = Path(folder)
            (root / 'logs').mkdir()
            (root / (name + '.pid')).write_text('123')
            log = root / 'logs' / (name + '.log')
            log.write_text(history, encoding='utf-8')
            stack.enter_context(patch.object(runtime, 'RUNTIME', root))
            stack.enter_context(patch.object(runtime, 'alive', side_effect=[owned, False]))
            def signal_boundary(*args):
                with log.open('a', encoding='utf-8') as output:
                    output.write(new)
            kill = stack.enter_context(patch.object(runtime.os, 'kill', side_effect=signal_boundary))
            runtime.stop_process(name)
            self.assertEqual(kill.call_count, int(owned))

    def test_new_native_gmsv_completion_is_accepted(self):
        self.scenario('gmsv', 'old log\n', 'GMSV正常停止，原生数据保存和连接关闭已完成\n')

    def test_previous_native_completion_cannot_mask_a_crash(self):
        with self.assertRaisesRegex(RuntimeError, 'save-completion marker'):
            self.scenario('gmsv', 'GMSV正常停止，原生数据保存和连接关闭已完成\n', 'crash\n')

    def test_saac_exit_without_save_completion_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'save-completion marker'):
            self.scenario('saac', '', 'SAAC正常关闭\n')

    def test_saac_save_completion_is_accepted(self):
        self.scenario('saac', '', 'SAAC正常停止，家族、据点和备忘资料已保存\n')

    def test_nonproject_pid_is_not_signalled(self):
        self.scenario('gmsv', '', '', owned=False)

    def test_http_does_not_require_native_marker(self):
        self.scenario('http', '', '')


if __name__ == '__main__':
    unittest.main()
