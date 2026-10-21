# Conversion status

This repository contains an in-progress handwritten C++17/nanobind conversion
of napari **0.9.2**, pinned to commit
`595639295a153c84bcb127a9e07d801355440a35`. It preserves Python access and
plugin integration. All M0–M9 completion gates remain open.

The current finite checkpoint moves 13 shared Labels/indexing edit, write and
cache policies and 11 expressions into handwritten C++. Existing Python
functions and aliases remain the public entry points. Actual layer dictionaries,
backend data and raw/view arrays retain their identities.

Validation completed on Windows with CPython 3.12 and PyQt6:

- 606 distinct passing cases across 31 processes, including 9 real Viewer/canvas
  cases with NumPy, Zarr and TensorStore.
- Zero test failures, errors, skips, duplicate case IDs or tracked input/binary
  changes during those runs.
- Independent source, compatibility, runtime evidence, accounting and checkpoint
  reviews passed.
- The responsibility audit contains 692 valid records, 6,193 unreviewed entries
  and zero invalid records. Of the reviewed records, 667 remain partial.
- Cleanup covered 75 settled process receipts; no new disposable files were
  found. Required build assets and genuine caches were preserved locally.

These results qualify this finite checkpoint. Full class/module/state ownership,
frontend editing, real plugin save/reload, cancellation/close, shutdown/lifetime,
other installed configurations and final release parity remain required work.
Individual process reports retain nanobind shutdown diagnostics; passing tests
do not establish clean application shutdown.

The next work covers frontend paint/fill/polygon and mouse control, required
property and exception policies, helper paths with Python fallbacks, and the
brush grid-construction policy. Its source and contract proposals are under
review; no next-stage native result is claimed.

See the [execution checklist](remaining-conversion-checklist.md),
[master plan](full-conversion-plan.md), and [build instructions](../README.md).
The machine-readable snapshot is [conversion-status.json](conversion-status.json).

Raw host-specific reports, binary snapshots, SDKs, virtual environments, caches
and build outputs stay in the local ignored `.artifacts` and build directories.
Historical documentation links into `.artifacts` refer to that local evidence
archive. This source snapshot does not contain a complete release-evidence index.

The full test suite currently depends on some ignored local before-snapshots
and receipts that `tools/fetch_upstream.py` does not recreate. Consequently,
clean-clone CI cannot yet reproduce all local checks. The CI workflow remains
enabled; portable test/evidence packaging is required follow-up work.
