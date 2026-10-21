# Responsibility reviews

`tools/audit_conversion.py` discovers the pinned and current source trees, then
validates persistent decisions with `tools/conversion_review.py`. It does not
classify code as native merely because it calls nanobind. The inventory includes
`napari`, `napari_builtins`, the added adapters, module-level behavior and nested
definitions. It verifies the upstream commit and rejects tracked source changes.

```powershell
.venv/Scripts/python.exe tools/audit_conversion.py
.venv/Scripts/python.exe tools/audit_conversion.py --check-complete
```

The first command writes `.artifacts/responsibility-register.json`, including
review issues, and permits an incomplete development port. The second returns
status 1 for unreviewed, invalid or incomplete responsibilities. A manifest
claim of `complete_application_port=true` also enables this rejection check.
Neither command changes the manifest or marks a milestone complete. Keep the
full [migration plan](migration.md) and its behavior/release gates separate.

## Persistent files and identity

`docs/conversion-reviews.json` has `schema_version: 1`, the pinned
`upstream_commit`, a `records` object and an optional `retired_modules` object.
`docs/conversion-dependencies.json` uses the same version/commit and an
`operations` object. CLI `--reviews`, `--dependencies` and `--output` options
select other files when needed.

Inventory `review_key` values identify module or qualified definition and an
occurrence number, for example:

```text
naparixx/qt_slot.py:module
naparixx/qt_slot.py:function:Slot.decorate.trampoline:1
napari/components/example.py:function:Model.value:2
```

The occurrence distinguishes repeated names, including property getter/setter
definitions. Moving lines preserves the key. Changing the source still makes
the old review stale. Renaming or removing a definition leaves an orphan review
that must be resolved; the check does not silently discard it.

Every module and definition needs its own record. `classification` is one of
`native`, `facade`, `adapter`, `dependency`, `tooling`, `partial` or `unconverted`.
A current review may honestly classify implementation as partial/unconverted;
that increments the reviewed count while preventing completion. Runtime code
cannot be excluded as tooling; the current tooling scope is benchmarks.

## Required record contents

Each record includes:

- `source_sha256`: the inventory module hash. It hashes decoded UTF-8 source
  with normalized newlines, so module imports/globals also invalidate definition
  reviews. Definitions additionally need their inventory `body_sha256` AST hash.
- `reason`, `reviewed_by` and a timezone-bearing ISO `reviewed_at`: the explicit
  source-level decision and its author/time.
- `coverage`: explanations for each required concern. Module keys are
  `module_execution`, `state`, `inheritance`, `exports`, `callbacks`, `fallbacks`
  and `calls`. Definition keys are `implementation`, `decorators` and `calls`.
- `implementation`: fingerprint objects with workspace-relative POSIX `path`
  and raw-byte `sha256`. Native reviews require at least one C++ source/header
  and its named `symbol`. Include relevant headers, adapters and test sources
  so changing those files also invalidates the review.
- `evidence`: fingerprint objects with `format: "junit"` and exact `cases`
  identifiers in `classname::name` form. Reports must exist, match their hashes,
  contain passing selected cases and have no failures/errors. A fresh report
  hash cannot turn a failed, skipped or unrelated case into supporting evidence.
  Completed classifications require evidence; partial/unconverted reviews may
  record unfinished work without claiming passing tests.
- `calls`: all observed call targets and retained C++/Python calls, with
  `target`, `kind` and `purpose`. Calls not in the AST inventory also require a
  `source` fingerprint identifying the inspected call site.

Fingerprints resolve inside the workspace; missing files, outside paths and
stale hashes invalidate the record. Evidence stored in `.artifacts` must be
restored from its original artifact before validating on another machine.
The gate never regenerates reports or updates review hashes to make them pass.

## Calls and external operations

Use these call kinds:

| Kind | Required information and restriction |
| --- | --- |
| `application` | `responsibility` points to the reviewed inventory key. A completed caller cannot delegate to an unreviewed, invalid, partial or unconverted target. Known local/qualified targets must point to the corresponding definition. |
| `native` | A declared `naparixx._native`, `native`, `_qt_native` or `_qt5_native` entry. The review still needs its real C++ sources and evidence. |
| `dependency` | `dependency_id` identifies one approved operation whose exact callable matches the target. |
| `builtin` | The target names an actual Python builtin. A known local application definition cannot be disguised as that builtin. |
| `protocol` | `receiver` identifies the external callback/provider or Python reference/metadata object. An imported operation or known application target cannot be disguised as a provider protocol. |

Each approved dependency operation records `callable`, `owner`, `purpose`, a
source-hashed `adapter` and exact passing JUnit `evidence`. Wildcards and napari
application callables are rejected as external dependencies. A retained vendor
dependency classification also lists its approved `dependency_ids`.

Deleted baseline modules require `retired_modules` records with the original
`baseline_sha256`, a reason, completed `replacements` responsibility keys and
passing evidence. Runtime modules require replacements; development tooling
can have a reviewed retirement without a runtime replacement.

## What this establishes

The gate verifies the consistency, coverage and freshness of source-level
attestations and their recorded evidence. It does not prove algorithm equivalence,
that an arbitrary C++ symbol implements the claimed behavior, or that a reviewer
has declared every C++ callback. Import resolution is conservative and does not
fully resolve lexical shadowing, inherited/dynamic dispatch or runtime exports.
Review those behaviors and the native bodies directly before approving records.

The output therefore always keeps `semantic_and_release_parity_proven=false`,
even when the review consistency check passes. M9 still requires full upstream
fixtures, real plugins, application workflows, shutdown/thread/GPU ownership,
installed wheel/platform checks and performance evidence.

The initial real review covers the Qt slot metadata adapter: one module and four
definitions, with three approved QtPy/CPython operations and fresh PyQt5, PyQt6
and PySide6 slot ownership checks. It is an allowed boundary review, not a new
C++ application conversion. All application milestones remain open.

Two subsequent records review the `_get_field_dependents` and
`_update_dependents_from_property_code` facades and their handwritten C++
implementations. They declare metadata/provider protocols, native recursive
helper delegation and the scoped CPython warnings operation, with exact
passing cases from the Python 3.11 and 3.12 model selectors. The inventory now
at that checkpoint had seven reviewed records; this does not close metaclass/helper dependencies,
whole-model ownership, broader lifetime contracts or any application milestone.

The event batch adds bounded facade reviews for `_is_pos_arg` and
`_shorten_fname`, naming their handwritten C++ implementations and retained
parameter/string/module-metadata protocols. Existing metadata reviews now
fingerprint the expanded stub and fresh passing Python 3.11/3.12 evidence.
The event checkpoint records nine reviewed and 6,712 unreviewed responsibilities.

The transform batch adds bounded reviews for the constant `Transform` and
`ScaleTranslate` diagonal properties. Their original descriptors forward to
named handwritten C++ boolean returns; independent pinned-source cases check
values and subclass overrides on Python 3.11/3.12. The restored prior stub hash
and AST comparison establish that the 64 added native declarations preserve all
earlier declarations. Four model/event/tracing reviews keep their unchanged
implementation fingerprints and refresh their stub/evidence fingerprints using
current named passing cases.

The current gate records eleven reviewed, 6,710 unreviewed, zero invalid and zero
issues. Native model/event/transform authority, inherited collection behavior,
broader dependency/provider reviews, GUI shutdown retention and all milestone
exits remain open. These two property reviews do not classify the transform
module or application as fully converted.
