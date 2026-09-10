"""In-memory checks for the target TASK v3 resource candidate; no live writes."""
from dataclasses import replace
import importlib
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]


def number(value):
    return struct.pack("<i", value)


def string(value):
    return number(len(value)) + value


def subrecord(sid, *, requirement=b"", info=b"step details"):
    return (number(sid) + string(b"step " + str(sid).encode())
            + string(info) + string(requirement)
            + number(100 + sid) + number(sid) + number(sid + 1))


def taskrecord(tid, order, subids, *, first_requirement=b""):
    return (b"".join(map(number, (tid, order, 0, 0)))
            + string("原有任务".encode()) + string(b"task info")
            + string(b"") + string(b"reward unchanged")
            + number(len(subids))
            + b"".join(subrecord(sid, requirement=first_requirement if n == 0 else b"")
                       for n, sid in enumerate(subids)))


def resource(*tasks):
    return b"TASK\x03" + number(len(tasks)) + b"".join(tasks)


def sparse_fixture():
    return resource(taskrecord(60, 0, (1, 2, 7, 8),
                               first_requirement=b"2|TK|4|PL|>=120,>=0"),
                    taskrecord(4, 1, (1,)))


class ClientTaskTests(unittest.TestCase):
    def setUp(self):
        try:
            self.tool = importlib.import_module("tools.prepare_client_tasks")
        except ModuleNotFoundError:
            self.fail("The guarded TASK candidate preparation tool is not implemented")

    def repaired(self):
        before = self.tool.parse(sparse_fixture(), allow_sparse_ids=True)
        return before, self.tool.fill_sparse_slots(before)

    def test_original_sparse_ids_exceed_native_allocation(self):
        with self.assertRaises(self.tool.TaskFormatError):
            self.tool.parse(sparse_fixture())
        original = self.tool.parse(sparse_fixture(), allow_sparse_ids=True)
        audit = self.tool.loader_audit(original)
        self.assertEqual([v["sub_id"] for v in audit["out_of_bounds"]], [7, 8])
        self.assertEqual(audit["allocations"][0]["bytes"], 160)

    def test_fill_preserves_original_ids_and_raw_record_fields(self):
        before, after = self.repaired()
        old, new = before.tasks[0], after.tasks[0]
        self.assertEqual(new.count, 8)
        self.assertEqual([s.sub_id for s in new.subtasks], list(range(1, 9)))
        self.assertEqual(before.tasks[1], after.tasks[1])
        for existing in old.subtasks:
            self.assertEqual(existing, next(s for s in new.subtasks
                                            if s.sub_id == existing.sub_id))
        self.assertEqual(replace(old, subtasks=()), replace(new, subtasks=()))
        self.assertEqual(self.tool.loader_audit(after)["out_of_bounds"], [])
        encoded = self.tool.serialize(after)
        self.assertEqual(self.tool.parse(encoded), after)
        self.assertEqual(self.tool.serialize(self.tool.parse(encoded)), encoded)

    def test_inserted_slots_have_only_the_native_self_finished_condition(self):
        _, after = self.repaired()
        for step in after.tasks[0].subtasks[2:6]:
            self.assertEqual((step.name, step.info, step.map_no, step.x, step.y),
                             (b"", b"", 0, 0, 0))
            self.assertEqual(step.requirement, f"1|TK|60-{step.sub_id}".encode())

    def test_hidden_slots_do_not_render_or_block_finished_prefix(self):
        _, after = self.repaired()
        expected = {-1: 1, 0: 1, 1: 2, 2: None, 3: None, 4: None,
                    5: None, 6: 7, 7: 8, 8: None, 9: None, 1024: None}
        for progress, visible in expected.items():
            with self.subTest(progress=progress):
                self.assertEqual(self.tool.visible_subtask(
                    after, 60, progress, completed_tasks={4}, level=120), visible)

    def test_new_player_and_completed_task_stay_hidden(self):
        _, after = self.repaired()
        self.assertIsNone(self.tool.visible_subtask(after, 60, 0,
                                                   completed_tasks={4}, level=4))
        self.assertIsNone(self.tool.visible_subtask(after, 60, 0, level=120))
        for progress in range(9):
            self.assertIsNone(self.tool.visible_subtask(
                after, 60, progress, completed_tasks={4, 60}, level=120))

    def test_progress_reset_does_not_leave_a_placeholder_visible(self):
        _, after = self.repaired()
        # Each call models the receiver's reset, prefix update, and canDo pass.
        for progress, expected in [(8, None), (0, 1), (7, 8), (2, None), (6, 7)]:
            self.assertEqual(self.tool.visible_subtask(
                after, 60, progress, completed_tasks={4}), expected)

    def test_unconditional_empty_slots_would_create_a_visible_blank_step(self):
        _, after = self.repaired()
        task = after.tasks[0]
        unconditional = replace(task, subtasks=tuple(
            replace(s, requirement=b"") if 3 <= s.sub_id <= 6 else s
            for s in task.subtasks))
        unsafe_ui = replace(after, tasks=(unconditional, after.tasks[1]))
        self.assertEqual(self.tool.visible_subtask(
            unsafe_ui, 60, 2, completed_tasks={4}), 3)

    def test_permanently_false_slots_would_block_original_step_seven(self):
        _, after = self.repaired()
        task = after.tasks[0]
        blocked = replace(task, subtasks=tuple(
            replace(s, requirement=b"1|TK|60") if 3 <= s.sub_id <= 6 else s
            for s in task.subtasks))
        unsafe_ui = replace(after, tasks=(blocked, after.tasks[1]))
        self.assertIsNone(self.tool.visible_subtask(
            unsafe_ui, 60, 6, completed_tasks={4}))

    def test_verification_rejects_changes_to_existing_content(self):
        before, after = self.repaired()
        task = after.tasks[0]
        changed = replace(task, subtasks=(replace(task.subtasks[0], info=b"changed"),)
                          + task.subtasks[1:])
        tampered = replace(after, tasks=(changed, after.tasks[1]))
        with self.assertRaises(self.tool.TaskFormatError):
            self.tool.verify(before, tampered)

    def test_verification_covers_every_existing_record(self):
        before, after = self.repaired()
        report = self.tool.verify(before, after)
        self.assertTrue(report["original_records_preserved"])
        self.assertEqual(len(report["subtask_records"]), 5)
        self.assertEqual(report["inserted_sub_ids"], [3, 4, 5, 6])
        self.assertTrue(all(row["unchanged"] for row in report["subtask_records"]))

    def test_every_truncated_fixture_is_rejected(self):
        raw = resource(taskrecord(60, 0, (1, 2)))
        for end in range(len(raw)):
            with self.subTest(end=end), self.assertRaises(self.tool.TaskFormatError):
                self.tool.parse(raw[:end])

    def test_trailing_bytes_and_unsupported_version_are_rejected(self):
        raw = resource(taskrecord(60, 0, (1,)))
        for changed in (raw + b"trailing", raw[:4] + b"\x02" + raw[5:]):
            with self.assertRaises(self.tool.TaskFormatError):
                self.tool.parse(changed)

    def test_duplicate_task_ids_orders_and_sub_ids_are_rejected(self):
        cases = (resource(taskrecord(60, 0, (1, 1))),
                 resource(taskrecord(60, 0, (1,)), taskrecord(60, 1, (1,))),
                 resource(taskrecord(60, 0, (1,)), taskrecord(4, 0, (1,))))
        for raw in cases:
            with self.assertRaises(self.tool.TaskFormatError):
                self.tool.parse(raw, allow_sparse_ids=True)

    def test_invalid_lengths_ids_and_allocation_limits_are_rejected(self):
        raw = resource(taskrecord(60, 0, (1,)))
        # The first title's length follows the 9-byte header and four integers.
        for bad_length in (-1, 0x7fffffff):
            changed = raw[:25] + number(bad_length) + raw[29:]
            with self.assertRaises(self.tool.TaskFormatError):
                self.tool.parse(changed)
        for tid, sid in ((0, 1), (1025, 1), (60, 0), (60, -1)):
            with self.assertRaises(self.tool.TaskFormatError):
                self.tool.parse(resource(taskrecord(tid, 0, (sid,))),
                                allow_sparse_ids=True)

    def test_source_hash_guard_rejects_non_target_input(self):
        with self.assertRaises(self.tool.TaskFormatError):
            self.tool.build_candidate(sparse_fixture())

    def test_live_and_arbitrary_output_paths_are_rejected(self):
        for path in (self.tool.LOCAL, self.tool.ORIGINAL, ROOT / "build/unapproved.bin"):
            with self.subTest(path=path.name), self.assertRaises(self.tool.TaskFormatError):
                self.tool._check_artifact(path, b"must not write")

    def test_other_sparse_tasks_are_not_silently_rewritten(self):
        raw = resource(taskrecord(60, 0, (1, 2, 7, 8)), taskrecord(4, 1, (1, 3)))
        parsed = self.tool.parse(raw, allow_sparse_ids=True)
        with self.assertRaises(self.tool.TaskFormatError):
            self.tool.fill_sparse_slots(parsed)

    def test_verified_target_candidate_round_trips_and_preserves_all_records(self):
        raw = self.tool.ORIGINAL.read_bytes()
        candidate, report = self.tool.build_candidate(raw)
        self.assertEqual(report["before"]["sha256"], self.tool.EXPECTED_SHA256)
        self.assertEqual(report["before"]["task_count"], 30)
        self.assertEqual(report["after"]["task_count"], 30)
        self.assertEqual(report["after"]["subtask_count"],
                         report["before"]["subtask_count"] + 4)
        self.assertTrue(report["preservation"]["original_records_preserved"])
        self.assertEqual(report["after"]["loader"]["out_of_bounds"], [])
        self.assertEqual(self.tool.serialize(self.tool.parse(candidate)), candidate)


if __name__ == "__main__":
    unittest.main()
