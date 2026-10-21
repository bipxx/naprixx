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
isolated grid comprehension frame check, one indices-local check and one grid
parent/inlined-local check, one temporary-lifetime check and one native line
transport check, one native opcode transport check and one getter line check (163 cases
in total).
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
163-case Windows/CPython3.12 focused suite. Watchers do not exist on3.11.
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
and pass on 3.11.11 after the fix. This finite check does not qualify source
tracing or complete release behavior.

`indices` now stores its nine source locals in the genuine parent frame and
reads them again at the corresponding source operand evaluations. The inert
shell passes only its dispatch entry; it does not retain argument copies in a
native call tuple. Isolated controls compare dense/sparse/empty results,
callback local inspection and mutation at `len`, `arange`, reshape lookup/call,
errors and retained frames. Nine controls pass on 3.12.8 and eleven on 3.11.11
and 3.13.8. The additional two controls cover checked deletion on 3.11 and the
interpreter proxy's rejection of deletion on 3.13. These synthetic numeric
controls do not qualify full NumPy on 3.11/3.13. Run
`grid_indices_locals_child.py` with the matching probe on `PYTHONPATH`.

The initial 3.12 deletion fixture incorrectly expected `UnboundLocalError` after
clearing a definitely initialized argument through `PyFrame_LocalsToFast`.
The source's unchecked `LOAD_FAST` instead consumed a NULL slot on that release
build. The failing observation is retained locally; that low-level deletion
behavior requires an explicit ABI decision and remains unqualified. It is not
covered by the nine passing 3.12 controls.

`nd_grid.__getitem__` now uses genuine frame slots for its fourteen source
locals on 3.11 and sixteen on 3.12/3.13. `grid_getitem.inc` holds that handwritten
policy. The inlined comprehension saves and clears `_x`/`_t` after `GET_ITER`,
then restores them in the source compiler's normal/error orders. The shell
passes only its dispatch entry. Ten isolated source comparisons pass on 3.11
and twelve on 3.12/3.13, covering callback-visible locals and handled exceptions,
sparse/dense/complex/single-slice results, mutation during key/result/comprehension
callbacks, and old-slot restoration on normal completion and errors. The prior
native binary fails the new parent-local oracle. The runner
`grid_getitem_locals_child.py` uses synthetic numeric leaves; full provider and
frame ABI acceptance remain separate.

Eleven temporary-lifetime comparisons also pass on 3.11/3.12/3.13. They retain
failures from the prior native binary and fix observable finalizer differences:
the star-call callable is released before assigning `typ`, exhausted iterators
before the following source assignment/return, a partial comprehension's
iterator before its abandoned list, and the temporary `arange` callable before
appending its result. Finalizers can mutate actual frame locals or discover
the partial list through GC, so these differences changed dtype, returned
values and list contents. Error controls retain finalizer order and exception
context; assertions require the relevant finalizers to have run before comparison.
`grid_temporary_lifetime_child.py` reproduces these finite comparisons using
synthetic numeric leaves. It does not certify all temporary/error/GC/shutdown
behavior or source tracing.

The four latest controls cover transient `isinstance` operands/classes, a
self-removing `isinstance` callable, and a transient comparison operand. Native
call arguments and comparison operands now release before the conditional truth
test, as the source does. Prior extra C++ owners delayed finalizers, changed
the truth result and produced different grid values; the prior binary fails
three of these controls. The fourth retains the already-correct callable timing.

`grid_trace_comparison.py` is a strict, separate tracing diagnostic. Run it with
the matching probe on `PYTHONPATH`. It currently exits 1 on 3.11/3.12/3.13:
policy values match, but native transport events differ from source policy
events. For example, the 3.12 grid line-trace comparison records 65 source
events and 3 native events. Opcode observation is now complete on all three
minors: the setup requests opcodes before installing the tracer on 3.12 and
installs each frame's callback before enabling its opcode monitoring on 3.13.
These requirements follow the [CPython tracing issue](https://github.com/python/cpython/issues/114480)
and the pinned [3.13 frame setters](https://github.com/python/cpython/blob/v3.13.8/Objects/frameobject.c).
The initial incomplete observations are retained locally. Native source-event/
position transport remains required. The diagnostic is not counted as a passing test
in the finite 163-case policy suite.

An opt-in `indices` line-transport pilot now closes that function's normal line
event comparison on 3.11/3.12/3.13. `grid_line_trace.inc` forwards the user's trace
callback, filters thin-shell line/opcode events, and supplies source line/offset
events from the handwritten native statements. Its code transport retains
source line metadata over an inert NOP-padded shell, not executable original
policy. `grid_line_transport.py` derives checked positions from compile-only
metadata. Twelve controls compare line events, offsets, local names, mutation,
line disabling, trace identity/restoration, global disabling and callback-error
propagation against source. Prior local/lifetime controls remain separate.

Run `grid_line_child.py` with the matching probe on `PYTHONPATH`. For the strict
diagnostic, add `--line-transport`: the `indices` line row passes, but the overall
command still exits 1 because opcode and `getitem` event parity remain open.
The explicit `LineProbe`/native call adapter is required; no default application
tracer or startup path is installed. Debugger jumps raise `NotImplementedError`
when detected. Opcode delivery, complete exception/traceback and out-of-event
positions, profiling/monitoring, C trace callbacks and general observer/reentry
compatibility remain unqualified. This pilot does not accept the full frame ABI.

The opt-in indices pilot also supplies opcode events at native operand
boundaries. `grid_indices.inc` remains the handwritten algorithm;
`grid_opcode_trace.inc` uses checked compiler instruction metadata solely to
schedule observer events. It does not execute original Python policy. Hooks
precede native global/local loads, stores, calls, arithmetic, slicing and branch
decisions. Tests compare every event's line/offset/local names and resulting
values with source, including mutation before loads, at calls after operand
capture, and before stores. The 3.13 control also mutates a fused-load operand.
Fifteen controls pass on 3.11/3.12 and sixteen on 3.13, retaining the twelve
line/state controls with opcode delivery enabled.

This includes the 3.11 opcode callback already queued when a line callback
disables tracing, 3.13's separate truth conversion and fused instructions, and
the cleanup markers skipped by iterator exhaustion. Original compiler line
numbers are frozen into the observer metadata; padded-shell cache positions
cannot supply them reliably. Initial cast, cleanup, trace-disable and 3.13
metadata failures are retained locally.

Run `grid_opcode_child.py` with the matching probe on `PYTHONPATH`. Add
`--opcode-transport` to the strict diagnostic to pass both indices line and
opcode rows on all three minors. The overall command still exits 1 for
`getitem`; default activation, complete error/out-of-event positions, jumps,
profiling/monitoring and general observer ABI remain open. The earlier line-only
adapter remains available for its narrower controls.

The line adapter now also covers normal `nd_grid.__getitem__` paths and its
genuine 3.11 comprehension frame. Return positions are selected per branch.
Thirteen controls pass on 3.11 and fourteen on 3.12/3.13: sparse/dense/empty and
complex grids, local/callback mutation, line/global disabling, restoration and
custom iterator forwarding. The 3.13 inlined comprehension's second `GET_ITER`
is now performed after clearing saved locals and allocating the list; a foreign
iterator can redirect that call. A traced Python iterator's `StopIteration`
event is reported before suppression. Both differences were caught by the
source comparisons; their initial failures are retained locally.

Run `grid_getter_line_child.py` with the matching probe on `PYTHONPATH`.
With either explicit adapter, the strict normal-path getter line row now
passes on all three minors. `--opcode-transport` also retains both passing
indices rows, but the overall diagnostic still exits 1 for getter opcode
events. Handled-exception/fallback tracing, complete traceback/out-of-event
positions, jumps, profiling/monitoring/general observer ABI and permanent
activation remain open. The original Python policies are never executed in
the native comparisons.

Run `grid_getter_opcode_child.py` for sixteen getter opcode controls on 3.11
and seventeen on 3.12/3.13. These retain the normal line/state comparisons and
add mutations at local-load, call and local-store events. Opcode presence is
asserted; the original policies remain test oracles only. Native hooks cover
container creation, canonical local operations, calls, indexing, branching and
comprehension frames. The 3.11 line at comprehension GET_ITER is delivered
before the operation; a queued opcode at the try NOP is delivered once when
global tracing is disabled. The 3.13 conditional comparison converts truth
inside COMPARE_OP before its branch event. General error-path observer and
complete observer/lifetime compatibility are still open.

The fallback pilot additionally compares twenty-two controls on each supported
minor. Caught length errors retain the source traceback before entering the
handler. Escaping regular/complex `arange` errors retain the active handled
exception, source event/return positions and the caller-visible traceback and
context chain. The thin transport traceback is removed even when an exception
callback disables tracing; 3.11 requires cleanup after its trace callback.
Unmatched RuntimeError/SystemExit errors preserve the original traceback while
reporting the handler RERAISE position for the return event. Missing exception
class names and invalid handler classes retain the handled exception context
and their own lookup/matching positions.
These controls use synthetic numeric leaves and do not qualify general
exception, observer or lifetime compatibility.

Run `grid_fallback_opcode_child.py` to enable opcode comparison for the same
twenty-two exception controls on each minor. Native handler entry, exception
class matching, successful return and error unwind now deliver source events
around changes to the active handled exception. The comparisons include
PUSH_EXC_INFO, CHECK_EXC_MATCH, POP_EXCEPT and RERAISE, caller-visible
context chains and restoration of an ambient caller exception. Opcode presence
is required. General exception/callback failures, complete source positions,
observer ABI and lifetime behavior remain unqualified.

Run `grid_observer_error_child.py` for fourteen trace-callback failure
controls on each minor. Line/opcode failures retain their projected source
frame; exception-event failures omit it, as do 3.11 return-event failures.
The comparisons check traceback/context chains, disabled global/local tracing,
ambient caller state and TypeError callbacks entering the source fallback.
An opcode callback failing before PUSH_EXC_INFO leaves the pending error as
handled state in the source interpreter. That state is compared explicitly,
and only then reset between oracles with the public
[handled-exception setter](https://docs.python.org/3.12/c-api/exceptions.html#c.PyErr_SetHandledException).
This preserves the observed interpreter quirk; it is not full observer or
lifetime approval. General failures and out-of-event frame positions remain open.

Run `grid_provider_traced_position_child.py` for sixteen provider-position
controls on each minor: eight policies with one invocation and eight with
eight invocations. Canonical source positions remain active during native
operations and Python calls, including when no opcode event is requested;
physical interpreter positions are restored at the native boundary. The
passing controls require opcode instrumentation, which keeps source operations
deoptimized. They compare positions inside provider callbacks, not only trace
events. Existing exception, trace-disable and callback-error controls remain.

Run `grid_position_reuse_child.py` for one hundred native calls inside one
untraced transport scope. Each native frame entry resets its disposable
instruction cursor, so a recycled frame address cannot inherit a completed
cursor. The retained initial failure demonstrates this lifecycle defect.

Run `grid_provider_position_child.py` without flags for the strict thirty-two
case comparison, including untraced first-call and repeated-call histories.
It currently exits 1 on 3.11 for warm-up inlining; the measured 3.12/3.13
cohorts exit 0. Policy results agree in
all measured cases. Preserve those untraced failures as required ABI work.
Adaptive specialization, general out-of-event/dead-frame positions and complete
provider/frame qualification are not accepted by these controls.

On 3.12, `grid_adaptive_position.inc` owns per-code counters and guarded
Python getter/subscript position state through the code-extra API. It follows
first-use delay, cooldown, failed-specialization retry/backoff, type/function
version invalidation and the lookup-hook transition after replacing a getter.
No original executable code body or owning application references are retained.
The three `grid_adaptive_mutation_child.py` controls compare forty invocations
each after same-code assignment and getter replacement. Five
`grid_adaptive_lifetime_child.py` controls check original-code release, prepared
code lifetime through an alias, collection after the final alias, and release
of both original and prepared code after warming.
The CPython rules derive from pinned
[specialize.c](https://github.com/python/cpython/blob/v3.12.8/Python/specialize.c),
[function versions](https://github.com/python/cpython/blob/v3.12.8/Objects/funcobject.c)
and [counter macros](https://github.com/python/cpython/blob/v3.12.8/Python/ceval_macros.h);
[Python license notices](../../licenses/Python-PSF.txt) are retained.

`grid_hot_handoff_child.py` compares seven already-warmed handoffs on 3.12,
including scalar/complex, sparse/dense and invalidated getter caches. It runs
eight original calls before cutover and four calls afterward; a profile guard
rejects any original policy body after cutover. Preparation captures active
Python getter/subscript cache kinds and their scalar version guards. A valid
captured getter cache can identify the direct slot without another generic
call. Neither the original code nor cached application functions gain owning
references from the native observer state. Other cache families remain open.

`grid_property_position_child.py` compares eight exact-property scenarios,
with forty calls after cutover per scenario: cold and already-warmed properties,
same-code invalidation, getter replacement, a generic `__getattr__` hook, and
cold/warm RuntimeError plus warm TypeError failures. The 3.12 observer captures
and checks property getter version guards as well as ordinary getter guards.
Attribute failures retain their projected source position through native unwind;
the controls compare caller traceback and exception-context chains. Original
policy bodies are rejected after cutover. All eight comparisons pass on 3.12
and 3.13; 3.11 positions remain open. These synthetic numeric leaves do not
qualify other descriptors, receiver lifetimes, instrumentation transitions or
full property/observer ABI compatibility.

The property comparison also accepts `--lines`, `--opcodes`, `--toggle`,
`--warm-opcodes` and `--skip-attributes`. These compare line/opcode/exception
and return events, locals names, ambient exception state, provider positions,
and every result/context chain across forty calls per scenario. Toggle alternates
opcode, line-only and absent tracing; the last option changes tracing on calls
that fail before any attribute operation. Warm-opcodes hands off a function
already instrumented by eight original calls. On 3.12, the native observer
uses the code-wide active-instruction monitor table and resets counters when
instruction instrumentation is removed, including at entry on early-failing
paths. These finite transitions do not qualify arbitrary monitoring tools,
in-call changes, other threads or full observer ABI.

`--inside-disable`, `--inside-local-off`, `--inside-enable` and
`--inside-replace` change the tracer from the first property callback in each
invocation. They compare the remaining source events, callback identity,
positions, all forty outcomes and exception-context chains. Starting or replacing
a tracer is detected by the native driver before its next source operation or
exception dispatch; the latest user tracer is retained on return. In 3.12,
exception dispatch also synchronizes code-wide instrumentation before unwind.
The controls cover this specific provider-callback transition, not general
trace-object lifetimes, audit hooks, C tracers, reentry or monitoring tools.

`grid_retained_frame_child.py --traced-only` compares sixteen post-completion
frame observations across getter/indices success and provider-error paths with
line or opcode tracing. The actual retained frame keeps its source completion
position after the return callback; no new owning frame references are added.
Ordinary calls and CALL_FUNCTION_EX preserve their failure-site position through
native exception reporting, including the indices entry boundary. The strict
command without flags also checks eight untraced observations. Those retained
positions still expose the transport shim and remain required work. The finite
traced result does not qualify full dead-frame, traceback, lifetime or observer
ABI compatibility.

The opt-in `LineProbe(..., completion=True)` / `--completion-transport`
prototype uses a result-or-error carrier on the canonical VM operand stack.
C++ chooses the endpoint and outcome kind; an inert compile-only bridge supplies
VM transport. Lambda bodies in that template are replaced by a native selector
and are never executed or retained by the prepared function. Normal completion
transfers the result back to its stack slot and jumps to source RETURN_VALUE.
Error completion transfers the exception and saved instruction position for
RERAISE 1, preserving the source traceback without another transport frame.
The carrier protocol is versioned separately from the source-policy payload.
No owning frame keeper or application-state mirror is added.
`grid_completion_controls_child.py` explicitly selects this prototype for the
existing source comparisons; the default protocol stays unchanged.

The twenty-four adapter cases and eight untraced direct-call cases compare
retained frame positions, values and owned-frame traceback/context chains.
`--ambient` also checks direct calls inside an existing exception handler and
restoration of that caller's handled exception. A body-local native driver
supplies source positions when no explicit adapter is active. The strict
`--direct-call --direct-traces` diagnostic preserves the remaining direct tracing
event gap: transport events outside that body-local scope remain observable.
VM-stack/carrier GC and lifetime review, reentry, C profiling/monitoring, other
provider/SDK configurations and independent ABI approval remain required before
integration or default activation.

`grid_reentry_child.py` compares sixty-four nested calls entered from line
or opcode trace callbacks, with original or converted outer callers and direct
or adapter invocation of the nested getter/indices. Metadata advances and
tracebacks are built while Python suppresses recursive observer delivery; native
line/opcode/exception callbacks stay suppressed in that interval. The controls
compare parent events/state, nested values, provider and retained positions,
exception-context chains and caller handled-exception restoration. Explicit GC
runs while the outer call is suspended, and weak references check release of
each nested prepared code object after its frame holders leave scope.

A profile guard cannot enforce original-body exclusion inside a suppressed trace
callback. This fixture instead checks converted-code admission and preserves the
normal runtime guards outside that callback. Installing an otherwise ineffective
profile guard changes the suspended caller's instrumentation on the checked
interpreter; a pure-source control reproduces that change without the C++ module.
The nested fixture therefore uses the same observer setup for both oracles.
`grid_trace_state_probe` is a read-only diagnostic of that setup. These finite
checks do not approve general reentry, active-carrier GC, audit hooks, cross-thread
observers or the complete VM-stack/lifetime ABI.

`grid_carrier_gc_child.py` checks nine further source/native cases at completion.
The native profiler stops at the first selector C_CALL, before the VM-stack
carrier is unwrapped. A restricted read-only diagnostic verifies carrier,
result/exception and endpoint identity across explicit collection and a nested
call to the same converted function. Four weak-referenceable results must stay
alive there and be released after the fixture drops its holders. The suspended
frame and caller handled exception must survive the nested call. The source
oracle uses its RETURN profile event; this is a lifetime/value comparison and
does not claim profile-event parity. Profiling callbacks suppress nested profile
guards, so original-body exclusion there uses static converted-code admission.
These finite cases do not approve arbitrary profile failures, monitoring,
cross-thread collection or the complete VM-stack/carrier lifetime ABI.

`grid_carrier_unwind_child.py` adds eighteen cyclic-owner controls: four result
families and two provider errors, with normal completion or a profiler failure
at the first selector C_CALL or C_RETURN. Cyclic witnesses survive collection
while completion is suspended, then must be collected after normal return or
exceptional unwind. Each call also reenters the same function and preserves an
ambient handled exception. Successful source returns receive the same cycle;
original provider exceptions are accessible through the native carrier only,
so those two error cases do not claim symmetric cycle injection. Source
profilers fail at RETURN. Outcomes and release checks agree, while profile-event
and exception-chain parity remain unaccepted. This is finite lifetime evidence,
not approval of arbitrary profilers or the complete observer/GC ABI.

`grid_eval_scope.inc` is a separate, explicitly installed PEP 523 experiment.
It wraps the previous evaluator and creates a native trace-driver scope around
the entire genuine selected frame, including transport entry and return. Import
does not install it. Duplicate installation, removal during active evaluation
and removal after another hook replaced it are rejected; explicit removal
restores the previous evaluator. The previous evaluator still executes every
frame; the handwritten policies execute selected application operations.

`grid_eval_scope_child.py` compares twenty-four direct getter/indices cases with
line/opcode tracing or no tracing. With the same hook installed for both source
and native oracles, all cases agree on the checked interpreters. Comparing an
unhooked original source to the hooked native candidate retains four event and
three retained/error-position differences on 3.11/3.12; the twenty-four cases
agree on 3.13. Evaluator hooks disable Python-call inlining on earlier versions,
so native CALL positions now distinguish that interpreter configuration.
These baseline failures remain required work. This interpreter-wide candidate
does not establish transparent effects on unselected/plugin frames, hook
composition, thread/subinterpreter behavior, audit/profiling, specialization or
complete lifetime/observer compatibility, and is not enabled by application
startup. Same-hook comparisons do not establish unmodified-source parity.

This remains an experimental 3.12 observer component. The 3.11 quickening path,
other specialization/descriptor kinds and their specialized-input handoff,
instrumentation transitions, PEP 523, cross-thread behavior and complete
error/GC/lifetime qualification still require implementation and review.

Grid source tracing, unchecked-slot deletion, exact temporary release/exception fidelity
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
