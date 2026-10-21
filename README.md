# naparixx

A source fork of **napari 0.9.2** that moves application implementation into
**C++17**, exposed to the Python API through **nanobind**. The distribution is
`napari==0.9.2+naparixx.2`; the native core is version 0.3.0.

This is an in-progress development snapshot. The latest reviewed native
Labels edit/cache checkpoint passed **606 distinct cases across 31 processes**
on Windows with CPython 3.12 and PyQt6. The full application conversion and
all M0–M9 completion gates remain open. See the [current conversion status](docs/conversion-status.md)
and [remaining checklist](docs/remaining-conversion-checklist.md).

The [Labels frontend experiment](experiments/labels_frontend/README.md) publishes
the next handwritten draft and a standalone 96-case suite. It is built separately
and is not enabled in the application.

The handwritten application port includes dimension/camera/grid state, viewer
navigation, transform math, slice coordinate planning, image/multiscale/point/
vector/surface slice requests, layer loading and asynchronous slice scheduling.
Layer-list extent, unit and naming decisions also run in C++. Python facades
retain napari's public classes, validation, events, serialization and plugin
entry points.

EventedModel class preparation, property-dependency discovery, helper transitions
and change-journal decisions now run in handwritten C++. Python retains Pydantic
class construction, public schema/property metadata and GC-visible callback
references. Field and event-state ownership remain part of the unfinished
model conversion.

Event/EventEmitter/EmitterGroup and tracing now forward their owned control
flow to handwritten C++. Independent pinned-source tests cover callback order,
blocking, reentry, exception cleanup, weak-reference lifetime and tracing.
Callback/payload storage remains visible to Python GC; native state authority
and wider shutdown contracts remain open. The [full conversion plan](docs/full-conversion-plan.md)
records the current gaps, implementation order and milestone acceptance checks.

Transform construction, property transitions, cache decisions, chain control and
slice/expansion methods now also forward to handwritten C++. Independent pinned
transform, utility and unit sources verify 130 application comparisons. Public
arrays and cache dictionaries remain GC-visible Python storage; authoritative
native storage remains unfinished.

Typed, evented, nested and selectable lists, evented sets and selection now
forward their owned control to handwritten C++. The nested/set/selection batch
adds 67 native entries and 309 independent pinned-source comparisons, including
provider hooks, generator protocols, events and GC. Ordinary nested-move history
runs in the independent C++ core. Mutable stores and external references remain
visible to Python GC; native state authority remains open.

Typed and evented dictionaries, including namespace access, also forward all
23 owned method bodies to handwritten C++. There are 217 comparisons against
independent original dictionary implementations, covering mutation and event
order, equality/None behavior, child subscriptions, namespace exception causes,
provider/subclass hooks, live views, copies and GC. All eight container
implementation modules now use native control; authoritative storage and
broader compatibility contracts remain unfinished.

Model copying/delay lifecycle, Camera/Grid synchronization and copy transitions,
camera cache/direction/orientation transitions and tuple normalization now also
forward 15 owned bodies to handwritten C++. There are 139 focused checks against
unchanged prior integration bodies and real model/provider behavior. These
preserve Python schemas, external protocols and existing numerical/event
services; model state authority and shutdown ownership remain unfinished.

Camera/Grid now have a native adapter connecting their primitive state to the
live public field dictionary. Direct dictionary writes/replacement and native
updates stay consistent in the tested paths, including copies, restores, shared
dictionary aliases, missing fields, watcher exhaustion and GC. These paths have
47 new checks. Broader provider/private/reflection contracts and complete model
authority remain open.

Grid's eight original application methods and the fork's visibility helper now
forward to handwritten C++ control, with a GC-visible native iterator. The six
reproduced early-return, large-stride, iterator-mutation and subclass-hook gaps
are repaired. Native owners retain wide integers without decimal conversion
limits, and failed native updates preserve the prior state. The 202 Grid checks
compare unchanged pinned method bodies and exercise protocols, copies and GC;
their model/event dependencies are shared. Full native state authority and
release parity remain open.

Camera/Grid field records now own their adapter references in native fields
with explicit Python GC traversal and clearing. Their owner, dictionary and
weak-model views are read-only; 29 new checks cover retained owners, cycles and
finalizer reentry. Both Dims records now also own native GC-visible dictionary
handles with read-only views. Their 25 new checks cover retained records,
restored aliases and replacement/cache/finalizer behavior. Broader model
authority and wider GUI lifecycle contracts remain open in the
[conversion plan](docs/full-conversion-plan.md).

LayerList's 37 original control bodies now forward to handwritten C++ entries
for mutation, selection/linking, units/extents, events and save dispatch. Its
native iterator and batching scope retain GC-visible references. Forty new
comparisons pass against the pinned bodies; native storage and the dependent
layer/context/IO services remain incomplete.

Five collection/Grid iterator types now retain suspended references in native
fields with explicit GC traversal and clearing. Their frame references are
read-only while the diagnostic dictionaries remain live and mutable. Forty
new ownership/protocol/GC checks pass; authoritative collection storage and
wider GUI lifecycle contracts remain open.

The current context/Qt lifetime packet repairs owned type-namespace
references and napari's QApplication focus-callback root. Native shutdown
releases the exact callback and shared menu actions, including the original
menu/Viewer reproductions. 83 new focused checks pass; see
[the selected validation evidence](docs/validation.md#context-namespace-and-qt-lifetime-checkpoint).
Broader native ownership and release parity remain open.

**The full application conversion is incomplete.** Most layer models, editing,
LayerList state ownership, Qt controllers, VisPy orchestration, IO, settings and
plugin management still contain Python implementation.
[migration.json](migration.json) records individual migrated responsibilities
and remaining work. No generated C++ or embedded Python implementation is used
to claim a completed native rewrite.

## Build and run

Use Python 3.11+ and a C++17 compiler: Visual Studio 2022 with the Desktop
development with C++ workload on Windows, or GCC/Clang on Linux/macOS.
The build frontend supplies CMake, nanobind and scikit-build-core.

```powershell
uv venv --python 3.12
uv pip install --python .venv/Scripts/python.exe -e ".[qt,test]"
.venv/Scripts/python.exe -m napari
```

`python -m pip install ".[qt,test]"` also works in an activated environment.
On Linux/macOS, use `.venv/bin/python`. This fork installs as the `napari`
distribution, replacing upstream napari in that environment. Its wheel bundles
the application, builtin plugin manifest, resources and compiled extension.

Existing scripts use the same imports; native application models are active
automatically:

```python
import napari
import numpy as np

viewer = napari.Viewer()
viewer.add_image(np.random.default_rng(0).random((16, 128, 128)))
viewer.dims.set_current_step(0, 5)
napari.run()
```

`python -m naparixx` launches the same application. `import naparixx as napari`
remains available. `naparixx.backend_status()` reports the application core and
numerical dispatch. `enable()` and `disable()` toggle numerical dispatch for
verification; converted application paths always use C++. Callables keep their
identity through these switches, including references cached by plugins. There
is no runtime method or module-alias replacement. Disabling dispatch does not
restore the original Python implementations of converted application paths.

## Architecture and coverage

`cpp/application.cpp`, `cpp/layer_core.cpp`, `cpp/slicing.cpp`,
`cpp/change_journal.cpp` and `cpp/collection_core.cpp` build the independent
`naparixx_core` C++ library with no Python, nanobind or Qt dependency.
Handwritten binding files adapt public Python models, arrays, units, events and
executor services to C++. The source fork calls the bindings explicitly.

| Application area | C++ responsibility |
| --- | --- |
| Dimensions | One persistent owner of standard validated fields; range normalization, point clipping, resizing, axis order, slider coordinates, margins/thickness, rolling, focus and center steps |
| Camera | State, orientation, view/up directions, handedness, nD embedding and saved 2D/3D views |
| Grid | Occupied groups, positive/negative strides, hidden layers, shape, positions, viewbox contents and spacing |
| Canvas | Available viewbox dimensions after grid gaps |
| Viewer navigation | Fit-to-view center/zoom, rotated bounds and synchronized/independent camera transitions |
| Transform math | Rotation/shear construction, embedding, matrix inverse/composition/decomposition and coordinate application |
| Transform objects | Construction/metadata validation, setters, cache decisions, chain control, slicing/replacement/expansion and subclass/provider dispatch |
| Typed/evented lists | Validation, lookup, mutation, child subscription, move planning, reverse and event decisions; list/provider storage and other container algorithms remain partial |
| Slice planning | Axis selection, resizing, orthogonality, world/data coordinates, units and half-even indexing |
| Image slices | Single/multiscale data, tiles, thumbnails, projection and lazy indexing context management |
| Geometry slices | Point projection, vector visibility/fade and surface vertex/face/value/color/texture selection |
| Layer lists | Extent/step aggregation, common units, axis labels, unique names and duplicate validation |
| Loading and scheduling | Request IDs, stale completions, submission, cancellation, completion/error handling and idle waiting |

The binding boundary uses NumPy/SciPy primitives for dtype-specific operations,
Pint for unit objects, and existing array protocols for lazy storage. It keeps
Python executor services and GC-visible array/event references at the binding
boundary. These dependencies are
retained; the independent core uses C++ containers and arithmetic.

The native Dims storage is a development checkpoint. Selected comparisons now
preserve live dictionary identity/mutation/order, field deletion and tested
trusted values and plugin schemas. Primitive playback/scroll/validation flags
now have native state and truth decisions, preserving private-dictionary aliases.
Broader native-authority, numeric/provider and shutdown contracts remain open.
The dictionary observers and their
fallback need the supported interpreter/platform matrix; Camera/Grid retain
Python/native snapshots. The detailed
remaining gaps, milestone exits and full handwritten conversion plan are in
[docs/full-conversion-plan.md](docs/full-conversion-plan.md). `migration.json` keeps
`complete_application_port=false`.

Nine stable numerical dispatch sites cover affine and scale/translate calls,
point slicing, contrast range, brush interpolation, label ray picking, segment
normals, plane projection and point box selection. The first three now also
use C++ with dispatch disabled. The other six retain Python fallbacks for data
representations their native kernels do not support. Native output buffers own
their storage, and pure numerical loops release the GIL.

## Development and verification

```powershell
uv pip install --python .venv/Scripts/python.exe -e ".[qt,dev,test-upstream]"
.venv/Scripts/python.exe tools/fetch_upstream.py
.venv/Scripts/python.exe tools/audit_conversion.py
.venv/Scripts/python.exe -m pytest
.venv/Scripts/python.exe tools/run_upstream_tests.py
.venv/Scripts/python.exe -m build
```

Rebuild after C++ changes by repeating the editable installation command.
The audit retains source-hashed decisions in `docs/conversion-reviews.json`;
`--check-complete` rejects an incomplete review inventory. Its current failing
status is expected. Review consistency and behavior/release parity are separate
requirements; see [the review schema](docs/conversion-review.md).
The verified baseline is recorded in [upstream.json](upstream.json); the
reference checkout in `.upstream/napari` is excluded from distributions.

The application core can be built and tested without Python or Qt. The following
Windows commands install the CMake tools in the development environment:

```powershell
uv pip install --python .venv/Scripts/python.exe cmake
.venv/Scripts/cmake.exe -S . -B build/core-tests -DNAPARIXX_BUILD_PYTHON=OFF -DNAPARIXX_BUILD_CORE_TESTS=ON
.venv/Scripts/cmake.exe --build build/core-tests --config Release
.venv/Scripts/ctest.exe --test-dir build/core-tests -C Release --output-on-failure
```

See [docs/validation.md](docs/validation.md) for measured checks and limits,
and [docs/migration.md](docs/migration.md) for the application-port roadmap.
The selected Qt widget contracts can also be checked with
`python tools/run_upstream_qt_tests.py` in an environment with a Qt binding and
pytest-qt; this does not replace the full upstream suite.
No whole-application performance improvement is claimed. BSD notices and
upstream attribution are retained in `licenses/`.

The M0 native Qt prototype is an optional development target. It requires a Qt
SDK matching the active Python binding's runtime and does not yet replace the
Viewer's controllers. Configure its build and run its interoperability checks:

```powershell
uv pip install --python .venv/Scripts/python.exe -e ".[qt,dev]" --config-setting=cmake.define.NAPARIXX_BUILD_QT=ON --config-setting=cmake.define.NAPARIXX_BUILD_QT5=OFF --config-setting=cmake.define.CMAKE_PREFIX_PATH=C:/path/to/matching/Qt6/SDK
.venv/Scripts/python.exe -m pytest tests/test_qt_interop.py -q
.venv/Scripts/python.exe tools/audit_conversion.py
```

`naparixx.qt_host.NativeWidgetHost` exposes this prototype. Its Python code
adapts Qt wrapper identity/ownership; C++ owns the window, dock registry and
central widget. The independent Qt tests also build with
`NAPARIXX_BUILD_PYTHON=OFF`, `NAPARIXX_BUILD_QT=ON` and
`NAPARIXX_BUILD_CORE_TESTS=ON`. CTest needs the matching SDK runtime and platform
plugins available. For PyQt5, use a separate environment with `[pyqt5,dev]`,
`NAPARIXX_BUILD_QT5=ON`, `NAPARIXX_BUILD_QT=OFF` and a matching Qt5 SDK. The
same eight interop tests pass with PyQt5 5.15.11/Qt 5.15.2, PyQt6 6.11.0/Qt
6.11.2 and PySide6 6.11.2 on Windows CPython 3.12. Broader plugin/platform
contracts and full Viewer integration remain open. Existing release artifacts
precede this opt-in target and the new native EventedModel change journal.
