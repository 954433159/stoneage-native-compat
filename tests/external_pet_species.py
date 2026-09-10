"""Exercise native fitting and real public catalog reconciliation without live writes."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from prepare_external_pets import build, fit_parameters, inverse_stats, raw_stats, MARKER


class ExternalSpeciesTests(unittest.TestCase):
    def test_native_stat_inverse(self):
        for values in ([3100, 4500, 2700, 3000], [0, 10, 500, 2], [520000, 12000, 3000, 4200]):
            self.assertTrue(all(abs(a-b)<1e-6 for a,b in zip(values, inverse_stats(raw_stats(values)))))

    def test_large_birth_multiplier_is_not_silently_clamped(self):
        observed = json.loads((ROOT/'evidence/external-pets-20260910/growth-observed.json').read_text(encoding='utf-8'))
        fit = fit_parameters(observed['observations']['843'])
        self.assertGreater(fit['initial'], 100)
        self.assertLess(fit['maximum_140_relative_error'], .10)

    def test_full_pack_preserves_ids_and_quarantines_mismatch(self):
        recipes = json.loads((ROOT/'native/resource-patches.json').read_text(encoding='utf-8'))
        pack, patches = build([p for p in recipes if not p.get('id','').startswith(MARKER)])
        self.assertEqual(len(pack['entries']), 514)
        by_id = {row['external_id']: row for row in pack['entries']}
        self.assertEqual(by_id[1016]['status'], 'quarantined')
        self.assertIsNone(by_id[1016]['native_id'])
        self.assertEqual(by_id[1060]['native_id'], 335)
        self.assertEqual(by_id[3]['native_id'], 3)
        self.assertEqual(by_id[6]['native_id'], 6)
        self.assertEqual(by_id[777]['native_id'], 777)
        new = [r for r in pack['entries'] if r['status']=='new_native_fitted_template']
        self.assertGreater(len(new), 130)
        self.assertEqual(len(new), len({r['native_id'] for r in new}))
        self.assertNotEqual(by_id[393]['native_id'], 393, 'existing different species cannot be overwritten')
        self.assertEqual(len({by_id[i]['native_id'] for i in range(5878,5882)}), 4, 'four elemental Rubin variants cannot be merged by name')
        self.assertTrue(patches[0]['after'].startswith(patches[0]['before']+'\n'))


if __name__=='__main__':
    unittest.main()
