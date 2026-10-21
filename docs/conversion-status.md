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

The frontend candidate has handwritten drafts for all 50 selected bodies and
seven lazy generator-expression policies. Its latest isolated revision uses
source-named generator frames and shared closure cells, and creates expression
functions before evaluating their iterables. It passed 24 focused cases and 77
earlier regression cases on CPython 3.12. A dependency-free 19-case harness also
passed on each of CPython 3.11 and 3.13; these repeats are separate runtime scopes.
The [local candidate evidence](../.artifacts/labels-native-frontend-edit-owner-expression-order-stage/progress.json)
records the sources, binaries, results and remaining limits. Cleanup removed
38,851,185 bytes of settled compiler/linker intermediates while preserving
reusable dependencies and all 2,845 accepted inputs.

This candidate is not installed or accepted. Canonical module/class/decorator
provenance, atomic handoff, full fast-local/tracing/release compatibility,
default grid and required provider policies, independent effective source/ABI
review, and real Viewer/shutdown qualification remain required.

The [tracked frontend experiment](../experiments/labels_frontend/README.md) now
publishes the handwritten draft and a read-only C++ binding snapshot. Its 72 new
checks cover all 50 current targets, changed-binding rejection and a GC cycle.
The packaged suite combines these with 24 retained expression checks: all 96
passed on its standalone Windows/CPython 3.12 Release build, including a run
with access to `.artifacts` denied. These repeated runs are not additional
distinct cases. The snapshot validates current bindings; authenticating the
historical defining execution and publishing replacements atomically remain open.
This packet removed another 21,830,189 bytes of settled target intermediates.

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
