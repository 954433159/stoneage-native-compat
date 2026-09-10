"""Regression for received-byte framing, species identity and provenance."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from import_reference_pet_observations import decode_frames, import_frames, import_ranking_frames


def frame(kind, number, payload):
    raw = f'1789040000|{kind}|{number}|{payload}'.encode('utf-8')
    return str(len(raw)).encode() + b':' + raw


def detail(name='甲'):
    fields = ['D', '100001', name, '0', '100', '0', '0',
              '5.1000', '6.2000', '6.1000', '7.2000']
    fields += [str(i) for i in range(24)] + ['0']
    fields += ['浴血狂袭'] + [''] * 6 + ['实际说明'] + [''] * 6
    return '|'.join(fields) + '|'


class Observations(unittest.TestCase):
    def setUp(self):
        self.catalog = {'1': {'name': '甲', 'index': 10}}
        self.catalog_frame = frame('catalog', 0, 'L|1|甲|10|')
        self.bytes = self.catalog_frame + frame('detail', 10, detail())

    def test_byte_length_not_character_length(self):
        records, consumed = decode_frames(self.bytes)
        self.assertEqual(consumed, len(self.bytes))
        self.assertEqual(len(records), 2)

    def test_live_tail_is_preserved_but_corruption_rejected(self):
        records, consumed = decode_frames(self.bytes + b'200:partial', live=True)
        self.assertEqual(consumed, len(self.bytes))
        self.assertEqual(len(records), 2)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            decode_frames(self.bytes + b'200:partial')
        for invalid in (b'x:abc', b'0:', b'99999999:x', b'1:\xff'):
            with self.assertRaises(ValueError):
                decode_frames(invalid, live=True)

    def test_actual_interleaved_stats_and_skill_descriptions(self):
        result = import_frames(self.bytes, self.catalog)
        pet = result['observations']['10'][0]['pet']
        self.assertEqual(pet['hp'], dict(zip(map(str, range(1, 7)), range(0, 24, 4))))
        self.assertEqual(pet['quick']['6'], 23)
        self.assertEqual(pet['growthMax'], '6.2000')
        self.assertEqual(pet['skillNames']['1'], '浴血狂袭')
        self.assertEqual(pet['skillDescs']['1'], '实际说明')
        self.assertEqual(result['frame_imports'][0]['detail_frames'], 1)

    def test_last_empty_skill_field_is_not_a_terminator(self):
        actual = self.catalog_frame + frame('detail', 10, detail()[:-1])
        pet = import_frames(actual, self.catalog)['observations']['10'][0]['pet']
        self.assertEqual(pet['skillDescs']['7'], '')

    def test_unrequested_or_wrong_species_cannot_become_evidence(self):
        for bad in (frame('detail', 10, detail()),
                    self.catalog_frame + frame('detail', 11, detail()),
                    self.catalog_frame + frame('detail', 10, detail('乙')),
                    frame('catalog', 0, 'L|1|乙|10|')):
            with self.assertRaises(ValueError):
                import_frames(bad, self.catalog)

    def test_reimport_is_idempotent_and_manual_observations_survive(self):
        result = import_frames(self.bytes, self.catalog)
        result['observations']['10'].insert(0, {'captured_at': 'earlier', 'pet': {'name': '甲'}})
        again = import_frames(self.bytes, self.catalog, result)
        self.assertEqual(len(again['observations']['10']), 2)
        self.assertEqual(again['observations']['10'][0]['captured_at'], 'earlier')
        self.assertEqual(len(again['frame_imports']), 1)
        self.assertEqual(len(result['observations']['10']), 2, 'input must not be mutated')

    def test_mismatched_reply_is_quarantined_not_imported_as_the_pet(self):
        raw = self.catalog_frame + frame('conflict', 10, detail('乙'))
        result = import_frames(raw, self.catalog)
        self.assertEqual(result['observations'], {})
        self.assertEqual(result['conflicts']['10'][0]['requested_name'], '甲')
        self.assertEqual(result['conflicts']['10'][0]['returned_pet']['name'], '乙')
        with self.assertRaises(ValueError):
            import_frames(self.catalog_frame + frame('conflict', 10, detail('甲')), self.catalog)

    def test_rank_individuals_blessing_and_empty_response_are_distinct(self):
        fields = ['S', '1', '100001', '0', '100', '0', '0', '100', '10', '0', '0',
                  '140', '1600', '310', '240', '210', '1', '70', '15', '12', '8',
                  '1500', '300', '240', '210', '0', '0']
        raw = self.catalog_frame + frame('rank', 10, '|'.join(fields))
        result = import_ranking_frames(raw)
        pet = result['observations']['10'][0]['pet']
        self.assertEqual(pet['without_blessing'], [1500, 300, 240, 210])
        self.assertEqual(pet['blessing'], [100, 10, 0, 0])
        self.assertEqual(pet['observed_growth_without_blessing'][1], round(285 / 139, 6))
        self.assertFalse(pet['use_as_species_parameters'])
        empty = import_ranking_frames(self.catalog_frame + frame('rank', 10, 'S|0|'))
        self.assertFalse(empty['observations']['10'][0]['pet']['available'])
        absent = import_ranking_frames(self.catalog_frame + frame('empty', 10, 'P|1|0|0|0|0|0|0|0'))
        self.assertEqual(absent['observations']['10'][0]['pet']['reason'], 'all_seven_slots_absent_in_actual_P_reply')
        with self.assertRaises(ValueError):
            import_ranking_frames(self.catalog_frame + frame('empty', 10, 'P|1|1|0|0|0|0|0|0'))
        with self.assertRaises(ValueError):
            import_ranking_frames(raw + frame('rank', 10, '|'.join(fields) + '|unexpected-private-field'))


if __name__ == '__main__':
    unittest.main()
