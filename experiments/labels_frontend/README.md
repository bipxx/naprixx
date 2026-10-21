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
3.11–3.13 SDK including internal headers. Only Windows/CPython 3.12 has exercised
this packaged target; other platforms and full frame compatibility remain open.

From the repository root, configure and build separately:

```powershell
.venv/Scripts/cmake.exe -S experiments/labels_frontend -B build/labels-frontend-experiment -G "Visual Studio 17 2022" -A x64 -DPython_EXECUTABLE="$PWD/.venv/Scripts/python.exe"
.venv/Scripts/cmake.exe --build build/labels-frontend-experiment --config Release
$env:NAPARIXX_FRONTEND_PROBE_DIR="$PWD/build/labels-frontend-experiment/Release"
$env:PYTHONDONTWRITEBYTECODE="1"
.venv/Scripts/python.exe -B experiments/labels_frontend/run_tests.py -q
```

The focused suite contains 72 binding/snapshot checks, 24 expression/frame/order
checks and 18 producer-capture checks. Nine producer scenarios use fresh child
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

No code publication operation is exposed. Integrating the producer mechanism with
native preparation/default startup, atomic handoff under audit/reentrant hooks,
complete target/parent locals and tracing/release
ABI, provider/default-grid conversion, effective independent review, and installed
Viewer/plugin/shutdown qualification remain required. Legacy callable transports
remain explicit probe interfaces and are not approved default application paths.

Keep reusable build dependencies and evidence. Remove only settled per-target
compiler/linker intermediates after checking their exact paths.
