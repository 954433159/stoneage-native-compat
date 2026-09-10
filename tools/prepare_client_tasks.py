"""Prepare, but never deploy, the verified TASK v3 sparse-ID resource repair.

Only build/task-safe.bin and evidence/task-resource-verification.json may be
created. Both known local/original inputs are read and hash checked. Existing
artifacts are accepted only when their bytes already match; nothing is replaced.
Check-only also accepts the exact reviewed repair in the local client copy.
The target loader and task-state evidence is in docs/task-resource-audit-20260909.md.
"""
import argparse
from dataclasses import dataclass, replace
import hashlib
import json
from pathlib import Path
import re
import struct

if __package__:
    from .project_paths import ORIGINAL_CLIENT
else:
    from project_paths import ORIGINAL_CLIENT


ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / "client/data/task.bin"
ORIGINAL = ORIGINAL_CLIENT / "data/task.bin"
CANDIDATE = ROOT / "build/task-safe.bin"
EVIDENCE = ROOT / "evidence/task-resource-verification.json"
EXPECTED_SHA256 = "f203a617f0300959dc2d5d9d2b7856277c2f66aa34376e022f58af1039b2e1f2"
MAX_TASK_ID = 1024  # Target activeTasks[1025], 44 bytes per task.
MAX_SUBTASKS = 1024  # Conservative preparation limit; no unbounded allocation.
INSERTED_IDS = (3, 4, 5, 6)


class TaskFormatError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise TaskFormatError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class SubTask:
    sub_id: int
    name: bytes
    info: bytes
    requirement: bytes
    map_no: int
    x: int
    y: int


@dataclass(frozen=True)
class Task:
    task_id: int
    order: int
    guild: int
    auto_trigger: int
    name: bytes
    info: bytes
    requirement: bytes
    reward: bytes
    subtasks: tuple[SubTask, ...]

    @property
    def count(self):
        return len(self.subtasks)


@dataclass(frozen=True)
class TaskResource:
    version: int
    tasks: tuple[Task, ...]


class Reader:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def take(self, size):
        require(0 <= size <= len(self.data) - self.offset,
                f"Truncated or invalid field at offset 0x{self.offset:x}")
        result = self.data[self.offset:self.offset + size]
        self.offset += size
        return result

    def integer(self):
        return struct.unpack("<i", self.take(4))[0]

    def string(self):
        value = self.take(self.integer())
        require(b"\0" not in value, "Embedded NUL would change target string semantics")
        return value


def parse(data, *, allow_sparse_ids=False):
    reader = Reader(data)
    require(reader.take(5) == b"TASK\x03", "Only verified TASK version 3 is supported")
    count = reader.integer()
    require(1 <= count <= MAX_TASK_ID + 1, "Task count exceeds the target table")
    tasks, task_ids, orders = [], set(), set()
    for _ in range(count):
        tid, order, guild, auto = (reader.integer() for _ in range(4))
        require(1 <= tid <= MAX_TASK_ID, "Task ID exceeds the target table")
        require(tid not in task_ids, "Duplicate task ID")
        require(0 <= order < count and order not in orders, "Invalid or duplicate task order")
        require(guild in (0, 1) and auto in (0, 1), "Invalid task flags")
        task_ids.add(tid)
        orders.add(order)
        name, info, requirement, reward = (reader.string() for _ in range(4))
        sub_count = reader.integer()
        require(0 <= sub_count <= MAX_SUBTASKS, "Subtask count exceeds preparation limit")
        subs, sub_ids = [], set()
        for _ in range(sub_count):
            sid = reader.integer()
            require(1 <= sid <= MAX_SUBTASKS, "Subtask ID exceeds preparation limit")
            require(sid not in sub_ids, "Duplicate subtask ID")
            require(allow_sparse_ids or sid <= sub_count,
                    f"Task {tid} subtask {sid} exceeds allocation count {sub_count}")
            sub_ids.add(sid)
            sub_name, sub_info, sub_requirement = (reader.string() for _ in range(3))
            map_no, x, y = (reader.integer() for _ in range(3))
            subs.append(SubTask(sid, sub_name, sub_info, sub_requirement, map_no, x, y))
        tasks.append(Task(tid, order, guild, auto, name, info, requirement, reward, tuple(subs)))
    require(reader.offset == len(data), "Trailing bytes after final task record")
    return TaskResource(3, tuple(tasks))


def _integer(value):
    return struct.pack("<i", value)


def _string(value):
    return _integer(len(value)) + value


def _sub_bytes(sub):
    return (_integer(sub.sub_id)
            + b"".join(_string(s) for s in (sub.name, sub.info, sub.requirement))
            + b"".join(_integer(n) for n in (sub.map_no, sub.x, sub.y)))


def _task_header(task):
    # Deliberately excludes the subtask count, the one existing scalar we change.
    return (b"".join(_integer(n) for n in (task.task_id, task.order,
                                           task.guild, task.auto_trigger))
            + b"".join(_string(s) for s in (task.name, task.info, task.requirement, task.reward)))


def _task_bytes(task):
    return _task_header(task) + _integer(task.count) + b"".join(map(_sub_bytes, task.subtasks))


def serialize(resource):
    require(resource.version == 3, "Only TASK version 3 can be serialized")
    return b"TASK\x03" + _integer(len(resource.tasks)) + b"".join(map(_task_bytes, resource.tasks))


def _placeholder(sid):
    # TK checks finished, not canDo: no recursive evaluation. After native
    # prefix synchronization this is hidden when pending, skipped when done.
    return SubTask(sid, b"", b"", f"1|TK|60-{sid}".encode("ascii"), 0, 0, 0)


def fill_sparse_slots(resource):
    require([t.task_id for t in resource.tasks].count(60) == 1, "Expected exactly one task 60")
    output = []
    for task in resource.tasks:
        if task.task_id != 60:
            require(all(s.sub_id <= task.count for s in task.subtasks),
                    "Unexpected sparse task; only the verified task 60 repair is permitted")
            output.append(task)
            continue
        require([s.sub_id for s in task.subtasks] == [1, 2, 7, 8],
                "Task 60 no longer has the verified sparse layout")
        by_id = {s.sub_id: s for s in task.subtasks}
        by_id.update((sid, _placeholder(sid)) for sid in INSERTED_IDS)
        output.append(replace(task, subtasks=tuple(by_id[sid] for sid in range(1, 9))))
    candidate = replace(resource, tasks=tuple(output))
    require(parse(serialize(candidate)) == candidate, "Candidate does not strictly round-trip")
    return candidate


def loader_audit(resource):
    """Model the target's malloc(count+1)*32, indexed writes and string pool.

    Actual subtask fields occupy bytes 0..30; offset 31 is padding. String
    pointers use a separate malloc(file_size) pool with an extra NUL per string.
    This is a bounds model of the audited instructions, not client execution.
    """
    allocations, violations, string_pool_used = [], [], 0
    for task in resource.tasks:
        size = (task.count + 1) * 32 if task.count else 0
        allocations.append({"task_id": task.task_id, "count": task.count,
                            "bytes": size, "max_sub_id": max((s.sub_id for s in task.subtasks), default=0)})
        strings = [task.name, task.info, task.requirement, task.reward]
        for sub in task.subtasks:
            start, end = sub.sub_id * 32, sub.sub_id * 32 + 31
            if start < 0 or end > size:
                violations.append({"task_id": task.task_id, "sub_id": sub.sub_id,
                                   "allocation_bytes": size, "write_start": start,
                                   "write_end_exclusive": end})
            strings.extend((sub.name, sub.info, sub.requirement))
        string_pool_used += sum(len(value) + 1 for value in strings)
    size = len(serialize(resource))
    return {"model": "target 0x6c8250 TASK v3 loader; no native execution",
            "allocations": allocations, "out_of_bounds": violations,
            "string_pool_bytes": size, "string_pool_used": string_pool_used,
            "string_pool_in_bounds": string_pool_used <= size}


def _satisfies(requirement, tasks, completed_tasks, finished, level, rebirth):
    """Only the target's PL/TK condition subset needed by this resource."""
    if not requirement:
        return True
    tokens = requirement.decode("ascii").split("|")
    require(tokens[0].isdigit(), "Unsupported requirement count in verification model")
    count = int(tokens[0])
    require(len(tokens) >= 1 + count * 2, "Truncated task requirement")
    for index in range(count):
        kind, value = tokens[index * 2 + 1:index * 2 + 3]
        if kind == "TK":
            for reference in value.split(","):
                match = re.fullmatch(r"(\d+)(?:-(\d+))?", reference)
                require(match is not None, "Unsupported TK reference in verification model")
                tid = int(match[1])
                if match[2] is None:
                    if tid not in tasks or tid not in completed_tasks:
                        return False
                else:
                    sid = int(match[2])
                    require(tid in tasks and 1 <= sid <= tasks[tid].count,
                            "Out-of-range TK reference in verification model")
                    if not finished.get((tid, sid), False):
                        return False
        elif kind == "PL":
            comparisons = value.split(",")
            require(len(comparisons) == 2, "Unsupported PL requirement in verification model")
            for actual, text in zip((level, rebirth), comparisons):
                match = re.fullmatch(r">=(-?\d+)", text)
                require(match is not None, "Only audited >= PL comparisons are modeled")
                if actual < int(match[1]):
                    return False
        else:
            raise TaskFormatError("Unsupported condition in verification model")
    return True


def visible_subtask(resource, task_id, progress, *, completed_tasks=(), level=120, rebirth=0):
    """Model 0x56c8a0 sync -> 0x6b9600 conditions -> 0x56c054 draw.

    A false canDo stops the scan even when finished is true. Thus permanent
    false fillers are wrong; self-finished fillers can be skipped after sync.
    """
    require(not loader_audit(resource)["out_of_bounds"], "Cannot model an unsafe allocation")
    tasks = {t.task_id: t for t in resource.tasks}
    require(task_id in tasks, "Unknown task in visibility verification")
    task = tasks[task_id]
    completed = set(completed_tasks)
    # Receiver clears each loaded task/subtask, then applies prefix progress.
    finished = {(t.task_id, s.sub_id): (t.task_id == task_id and s.sub_id <= progress)
                for t in resource.tasks for s in t.subtasks}
    if task.guild or task_id in completed or not _satisfies(
            task.requirement, tasks, completed, finished, level, rebirth):
        return None
    steps = {s.sub_id: s for s in task.subtasks}
    for sid in range(1, task.count + 1):
        sub = steps.get(sid)
        if sub is None or not _satisfies(sub.requirement, tasks, completed, finished, level, rebirth):
            break
        if not finished[(task_id, sid)]:
            return sid
    return None


def verify(before, after):
    require(before.version == after.version, "Resource version changed")
    require(len(before.tasks) == len(after.tasks), "Task count changed")
    task_rows, sub_rows, inserted = [], [], []
    for old, new in zip(before.tasks, after.tasks):
        require(_task_header(old) == _task_header(new), "Existing task fields changed")
        require(old.task_id == 60 or old == new, "Unrelated task changed")
        new_subs = {s.sub_id: s for s in new.subtasks}
        require(len(new_subs) == new.count, "Candidate contains duplicate subtask IDs")
        for sub in old.subtasks:
            require(new_subs.get(sub.sub_id) == sub, "Existing subtask fields changed")
            old_digest, new_digest = sha256(_sub_bytes(sub)), sha256(_sub_bytes(new_subs[sub.sub_id]))
            sub_rows.append({"task_id": old.task_id, "sub_id": sub.sub_id,
                             "before_sha256": old_digest, "after_sha256": new_digest,
                             "unchanged": old_digest == new_digest})
        old_ids = {s.sub_id for s in old.subtasks}
        extras = sorted(set(new_subs) - old_ids)
        if old.task_id == 60:
            require(old.count == 4 and new.count == 8 and extras == list(INSERTED_IDS),
                    "Unexpected task 60 count or inserted IDs")
            for sid in extras:
                require(new_subs[sid] == _placeholder(sid), "Placeholder semantics changed")
            inserted.extend(extras)
        else:
            require(not extras, "Unexpected inserted subtask")
        task_rows.append({"task_id": old.task_id, "order": old.order,
                          "before_count": old.count, "after_count": new.count,
                          "header_before_sha256": sha256(_task_header(old)),
                          "header_after_sha256": sha256(_task_header(new)),
                          "existing_fields_unchanged": True})
    require(inserted == list(INSERTED_IDS), "Expected placeholder repair is absent")
    require(not loader_audit(after)["out_of_bounds"], "Candidate still writes out of bounds")
    require(loader_audit(after)["string_pool_in_bounds"], "Candidate string pool overflows")
    return {"original_records_preserved": True, "inserted_sub_ids": inserted,
            "task_records": task_rows, "subtask_records": sub_rows}


def _summary(data, resource):
    return {"sha256": sha256(data), "bytes": len(data), "parsed_eof": len(data),
            "strict_round_trip": serialize(resource) == data,
            "task_count": len(resource.tasks),
            "subtask_count": sum(t.count for t in resource.tasks),
            "loader": loader_audit(resource)}


def build_candidate(data):
    require(sha256(data) == EXPECTED_SHA256, "Input task.bin SHA256 does not match the audited target")
    before = parse(data, allow_sparse_ids=True)
    require(serialize(before) == data, "Input does not losslessly round-trip")
    after = fill_sparse_slots(before)
    candidate = serialize(after)
    parsed_after = parse(candidate)
    preservation = verify(before, parsed_after)
    expected = {-1: 1, 0: 1, 1: 2, 2: None, 3: None, 4: None,
                5: None, 6: 7, 7: 8, 8: None, 9: None, 1024: None}
    visibility = []
    for progress, visible in expected.items():
        observed = visible_subtask(parsed_after, 60, progress, completed_tasks={4})
        require(observed == visible, "Candidate changed the verified progression model")
        visibility.append({"progress": progress, "expected_visible_sub_id": visible,
                           "observed_visible_sub_id": observed, "passed": True})
    report = {"candidate_only": True, "deployed": False,
              "before": _summary(data, before), "after": _summary(candidate, parsed_after),
              "preservation": preservation,
              "visibility_model": {"source": "audited target native instructions; not GUI acceptance",
                                   "level": 120, "rebirth": 0, "completed_tasks": [4],
                                   "cases": visibility},
              "pending_validation": "Root review and Shitou GUI retry; WER causality is not proven by this model"}
    return candidate, report


def _check_artifact(path, data):
    require(path in (CANDIDATE, EVIDENCE), "Output path is outside the two approved artifacts")
    require(path.parent.resolve() == path.parent, "Output directory is redirected")
    require(not path.is_symlink(), "Output path is a symbolic link")
    if path.exists():
        require(path.is_file() and path.read_bytes() == data,
                "Existing artifact differs; refusing to overwrite it")


def _create_artifact(path, data):
    _check_artifact(path, data)
    if path.exists():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as output:
        output.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", choices=("local", "original"), default="local")
    parser.add_argument("--check-only", action="store_true", help="Verify entirely in memory without output files")
    args = parser.parse_args()
    paths = {"local": LOCAL, "original": ORIGINAL}
    sources = {name: path.read_bytes() for name, path in paths.items()}
    candidate, report = build_candidate(sources["original"])
    require(sources["local"] in (sources["original"], candidate),
            "Local task resource is neither the audited original nor the reviewed repair")
    local_applied = sources["local"] == candidate
    if not args.check_only:
        require(not local_applied,
                "Repair is already applied; use --check-only or tools/verify_prepared.py to verify it")
        candidate, report = build_candidate(sources[args.source])
    report["inputs"] = {name: {"path": str(path), "sha256": sha256(sources[name]),
                               "unchanged": True} for name, path in paths.items()}
    report["outputs"] = {"candidate": str(CANDIDATE), "verification": str(EVIDENCE)}
    evidence = (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    # Validate both destinations before publishing either artifact.
    if not args.check_only:
        _check_artifact(CANDIDATE, candidate)
        _check_artifact(EVIDENCE, evidence)
        _create_artifact(CANDIDATE, candidate)
        _create_artifact(EVIDENCE, evidence)
    for name, path in paths.items():
        require(path.read_bytes() == sources[name], f"{name} input changed during verification")
    print(json.dumps({"candidate_only": True, "wrote_artifacts": not args.check_only,
                      "local_state": "applied" if local_applied else "original",
                      "before_sha256": EXPECTED_SHA256,
                      "after_sha256": sha256(candidate), "candidate_bytes": len(candidate),
                      "existing_subtasks_preserved": len(report["preservation"]["subtask_records"]),
                      "inserted_ids": list(INSERTED_IDS), "bounds_passed": True,
                      "visibility_cases_passed": len(report["visibility_model"]["cases"])}, ensure_ascii=False))


if __name__ == "__main__":
    main()
