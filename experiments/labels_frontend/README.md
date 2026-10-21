# Labels frontend experiment

This directory contains the handwritten C++ draft for 50 frontend bodies and
seven lazy expressions, read-only canonical binding validation, and experimental
declaration-point capture for 50 functions and four defining classes.
It is **not enabled in napari**. Building this
target does not install or replace either production extension.

The translation unit consolidates handwritten source fragments; it is not
generated C++ from Python application bodies. The Python shell files contain
argument, closure and generator transport. Compiler controls compile byte-verified
source without executing it. Test oracles use the current Python implementations.
The manifest pins the source inputs and local draft provenance; it does not
attest release parity or independent source approval.

Use an editable checkout installation of this repository with its normal GUI/test
dependencies, nanobind and CMake. The experiment requires the normal CPython
3.11–3.13 SDK including internal headers. Windows/CPython 3.12 exercises the
full packaged suite; 3.11/3.13 exercise selected isolated controls. Other
platforms and full frame compatibility remain open.

From the repository root, configure and build separately:

```powershell
.venv/Scripts/cmake.exe -S experiments/labels_frontend -B build/labels-frontend-experiment -G "Visual Studio 17 2022" -A x64 -DPython_EXECUTABLE="$PWD/.venv/Scripts/python.exe"
.venv/Scripts/cmake.exe --build build/labels-frontend-experiment --config Release
$env:NAPARIXX_FRONTEND_PROBE_DIR="$PWD/build/labels-frontend-experiment/Release"
$env:PYTHONDONTWRITEBYTECODE="1"
.venv/Scripts/python.exe -B experiments/labels_frontend/run_tests.py -q
```

The focused suite contains 72 binding/snapshot checks, 24 expression/frame/order
checks, 31 capture/preparation/publication checks, ten batch-protocol checks and
five native-first declaration checks, 14 NumPy grid-provider checks and one
isolated grid comprehension frame check (157 cases in total).
Twenty-six scenarios use fresh import child
processes for the six real defining imports. The runner denies access to ignored
`.artifacts` files. Its compiler
profiles are derived for this checkout and interpreter from the manifest-pinned
inputs, rather than reusing host-specific frozen profile filenames.

The snapshot alone validates current registered bindings; an identical function
installed before that snapshot can pass. The experimental import collector adds
an innermost native declaration decorator and captures actual functions/classes
at exact frame/code/instruction positions before existing decorators. Removing
the injected decorators must recover the original AST, including source locations.
Captured function and class identities reject later identical substitutions.

This import collector executes the existing Python implementations as controlled
oracles. It is not the application's default loader and does not establish full
native application ownership. Closed capture markers preserve later adapter
factory behavior without enrolling new canonical producers. Weak markers avoid
owning code cycles, and temporary session references are cleared even when import
frames survive through provider tracebacks. The original loader metadata and
genuine source caches are preserved.

Captured declarations now feed native preparation of all 50 replacements and
seven expression transports. Original compiler controls and actual argument/cell
layouts are checked before preparation, then captured identities are checked
again before returning the plan. Preparation preserves the existing functions,
descriptors, defaults and aliases. The plan owns its capture references through
Python GC; prepared CodeTypes remain usable after the plan and scopes are released.
Capture instrumentation and original controls use the same verified source buffer.

Producer-qualified plans expose `publish_experiment()`. In controlled child
processes it publishes all 50 prepared CodeTypes into the existing functions;
aliases, descriptors, closures and defaults keep their identities. A real Labels
paint result matches the original, including generic numerical dispatch with
the optimized backend disabled. This is experimental publication, not enabled
application startup or complete frontend acceptance.

The batch protocol sends all audit notifications, then function-watcher
notifications, before any writes. Observers see the old cohort. It revalidates
after callbacks and once more with automatic GC paused; the subsequent GIL-held
write phase cannot allocate or drop last references. Version invalidation follows
the matching CPython setter, including the 3.13 function-version cache and rare
event counter. A rejected preflight makes no publisher-owned code changes;
foreign callback changes are retained. Watchers can receive notifications before
an abort caused by a later callback. This proposed observer contract differs from
serial Python setters and still needs effective compatibility review.

The primitive is limited to normal GIL builds of CPython 3.11–3.13. Its batch
core passed seven checks on3.11 and ten on3.13; those repeats are separate from the
157-case Windows/CPython3.12 focused suite. Watchers do not exist on3.11.
Final canonical lookup rejects nonexact/ambiguous dictionary keys without calling
their Python equality hooks. General foreign-key namespace compatibility remains
required work; this preflight restriction is not a final dependency exemption.

Default startup integration, full observer/namespace compatibility,
complete target/parent locals and tracing/release
ABI, provider/default-grid conversion, effective independent review, and installed
Viewer/plugin/shutdown qualification remain required. Legacy callable transports
remain explicit probe interfaces and are not approved default application paths.
An optional native-first import experiment now substitutes validated transport
CodeTypes into module constants before function creation. Its native factories
enroll their children directly. CREATE watchers and call profiling verify that
all 50 selected functions begin with native code and none of their original
bodies execute. Five fresh-process cases cover import, metadata, real painting
and generic dispatch, generator completion, and table/scope release. Function
docstrings are compiled into the inert transports so constant indices and
generator return values remain correct. Table construction/release also passes
on3.11/3.13 without importing the application modules.

This is still an isolated import experiment. Unselected module/class/decorator
policies remain Python, and original loader metadata is restored afterward;
production startup/reload integration and complete module ownership are not
implemented. The original observation mode remains available for before controls.
No default application import path has been changed.

The grid-provider experiment admits the actual NumPy `nd_grid.__getitem__` and
`numeric.indices` functions with complete compiler profiles and defining-module
identity checks, then publishes handwritten bodies through the same batch core.
Aliases retain their genuine function identity. The current provider sources
are byte-pinned in `grid_provider_controls.json` and tested with NumPy 2.5.3.
Grid tests run in separate processes; the normal test process and application
provider remain untouched. Sparse/dense, complex/single-slice, invalid input,
exception-global and foreign numeric callback, alias, brush, GC and rejection
controls pass. No blanket NumPy dependency exemption is introduced.

On 3.11 the sparse-grid comprehension now has a genuine function/frame with a
handwritten C++ body. Its function is created before evaluating `zip` and its
iterator, preserving the comprehension's own builtins snapshot. On 3.12/3.13
the inlined comprehension retains the parent frame. An isolated stdlib-only
control compares values and callback exceptions against the byte-pinned source,
including callbacks that replace builtins and `zip` during argument/iterator
evaluation. It passes on Windows CPython 3.11.11, 3.12.8 and 3.13.8; profile
guards reject original grid-body execution during native runs. The 3.11/3.13
controls use synthetic numeric leaves, not a full NumPy runtime qualification.
Run `grid_comprehension_child.py` with the corresponding probe directory on
`PYTHONPATH` to reproduce those controls.

The 3.11 comprehension's `.0`, `_x` and `_t` slots now belong to the genuine
frame. The native body reads and writes those slots directly instead of keeping
independent C++ local values. Three additional controls compare callback local
inspection, writes/deletion through `PyFrame_LocalsToFast`, and retained frame
contents with the source. They demonstrate the previous missing-local defect
and pass on 3.11.11 after the fix. This does not qualify the grid parent locals,
the inlined 3.12/3.13 locals, source tracing or complete release behavior.

Grid target/frame locals, tracing, exact temporary release/exception fidelity
and precise `arange`/`empty`/`result_type`/reshape
primitive approvals remain required before acceptance. Other provider versions
and installed configurations are unqualified. The adapted policies retain the
[NumPy source notice](../../licenses/NumPy-BSD-3-Clause.txt).

The implementation was checked against the pinned
[3.11 setter](https://github.com/python/cpython/blob/v3.11.11/Objects/funcobject.c),
[3.12 setter/watchers](https://github.com/python/cpython/blob/v3.12.8/Objects/funcobject.c),
and [3.13 setter/version cache](https://github.com/python/cpython/blob/v3.13.8/Objects/funcobject.c).
Private interpreter headers and exported internals are not a stable ABI promise.

Keep reusable build dependencies and evidence. Remove only settled per-target
compiler/linker intermediates after checking their exact paths.
