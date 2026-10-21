# Full conversion plan

The target is all napari-owned application implementation in handwritten C++,
with nanobind preserving the Python API and plugin access. The baseline is
napari 0.9.2, commit `595639295a153c84bcb127a9e07d801355440a35`.
Keep this baseline fixed until parity is established.

**The conversion is incomplete. All M0–M9 milestones remain open.** The
[migration history](migration.md) and [manifest](../migration.json) describe
implemented responsibilities. Whole-body native dispatch proves only its tested
scope; it does not establish complete module or application ownership.

## Remaining work checklist and effort

Use the [remaining conversion plan and checklist](remaining-conversion-checklist.md)
for the M0–M9 work breakdown, owner exits, platform/provider decisions, cleanup
and suggested model effort. This planning update preserves all existing
completion gates. The current task card remains the next-action authority;
the older packet table below is the broader roadmap.

Resolve the current owner's blocking M0 decisions before implementation.
Use the selected Sol model at High for ordinary C++/nanobind migration,
Medium for defined mechanical work, and Extra High for difficult ownership
or compatibility decisions. Ultra is useful when independent implementation,
contracts and review can run in parallel. These are recommendations, not
changes to the active model or client configuration.

## Goal contract for Sol Ultra

The active goal's stored label is `implement the plan`. This contract
supplies its concrete outcome and execution policy. The exposed tools cannot
rename the active objective or select/verify the model or reasoning effort.

For `/goal edit`, use this short objective and keep the detailed contract here:

```text
Complete the handwritten C++17/nanobind conversion of pinned napari 0.9.2, preserving its Python API, plugins and Qt/rendering behavior. Resume the current task card in docs/full-conversion-plan.md; finish each native workflow through installed validation. Complete only when all M0-M9 and final release gates pass.
```

```text
Complete the full handwritten C++17/nanobind port of napari 0.9.2,
commit 595639295a153c84bcb127a9e07d801355440a35. Preserve its Python API,
plugins, seven layers, documented fork differences and real Qt/rendering
workflows. C++ must own napari-owned algorithms, policy, canonical state and
scheduling. Keep only reviewed Python compatibility glue and declared external
primitives. Exclude generated C++, Cython, original-body production fallback
and independent mutable state mirrors.

Resume the current task card, staged work and accepted evidence. Make routine
reversible decisions autonomously and preserve others' edits. Finish the current
assigned owner's acceptance before selecting another owner, unless a concrete
blocker requires independent work. Choose cohesive workflow owners; keep their
required owned callees and state gaps on the same parent's backlog.

Delegate independent implementation, interfaces/tests and review with exclusive
files and concrete exits. The coordinator owns shared integration/accounting
and serializes build/application/GUI jobs. Hold shared inputs during disjoint
staging. Coalesce demonstrated defects, fix them, then integrate, build and
verify installed behavior before expanding scope.

Read the current card and only evidence needed for its next action. Every
preparation, probe or repeated check must name a changed input or unresolved
contract and the decision it will resolve; otherwise advance acceptance.
Review each coherent owner once, then recheck affected contracts after fixes.
Use upstream tests first; add focused tests for changed contracts or demonstrated
defects. Preserve failures and assertions; correct fixtures only for proven
setup defects. After two attempts on the same named blocker without new diagnostics, execute
a specific alternative or independent required action and retain the unresolved
requirement on the backlog. Do not restart settled preparation
or repeat settled reasoning while waiting for another agent.

Each continuation takes the next executable step: implement, review, integrate,
build, validate or accept. Tie reasoning to the decision being made. If waiting,
finish independent required work or wait for the result. Do not reopen this goal
review on automatic continuations without a new requirement or demonstrated
conflict. Resolve ambiguous compatibility boundaries as named M0 decisions;
preserve accepted behavior and all final gates.

Measure progress by runnable behavior, resolved blockers and actual execution.
Keep one compact card stating stage, owner, next tool action, required exit
and blocker. Link accepted checkpoints; update accounting once after acceptance.
Documentation, hashes and readiness alone do not establish a converted owner.
Reuse existing runners, indexes and accepted results. After a changed input,
execute the affected check and necessary downstream exits; preserve failed
evidence and refresh only changed fingerprints. Add a probe or receipt only
for an actual implementation, acceptance or reproducibility gap. Keep handoffs
to changed files, checks, findings and the next tool action; link large logs.
When that action is available, execute it. While waiting for a prerequisite,
perform required independent work or use a wait tool and yield; do not repeat
settled reasoning. Count attempts against the same named blocker even if a
probe is renamed. Read archived history only for a demonstrated need.

Complete only when every M0-M9 exit and final gate below passes for the same
final sources: frozen compatibility/workload tolerances, unchanged upstream
fixtures, real workflows/plugins/providers, fresh wheel/sdist installs,
numerical/image/performance parity and clean lifetime/GC/thread/cancellation/
close/GPU checks. Require tools/audit_conversion.py --check-complete and indexed
proof in docs/conversion-release-evidence.json before setting
migration.json.complete_application_port=true. Finite acceptance closes only
its proven scope. Report evidence, blockers and the next executable action;
keep unproven gates open and follow the goal lifecycle rules.
```

The contract fixes the outcome; the current card selects the next action.
The historical scope, milestone backlog and final gates below remain required.
A continuation resumes the current stage instead of reopening owner selection.

[Official Goals guidance](https://learn.chatgpt.com/docs/use-cases/follow-goals)
supports a verifiable outcome, checkpoints and validation loop.
[Official Ultra guidance](https://learn.chatgpt.com/docs/models) describes
parallel subagents for independent parts. Exclusive ownership, bounded reviews
and serialized integration are repository workflow choices.

Select the requested Sol model and Ultra in the client's model controls, where
available; in the CLI use `/model`. This document does not change or verify
those settings or substitute a different Sol generation.

<a id="current-task-card-actual-viewer-async-slice-delivery"></a>

### Current task card: Viewer lifecycle — Labels frontend editing

Parent outcome: finish native ownership of actual Viewer workflows, preserving
Qt/model/canvas identities, canonical storage, plugin callbacks, error order
and task stop-to-wait behavior.

#### Compact resume state

| Item | Current instruction |
| --- | --- |
| Accepted checkpoint | [Finite Labels edit/publication checkpoint](../.artifacts/labels-native-edit-publication-owner-production-checkpoint.json):606 distinct installed passes/31 processes, zero failures/errors/skips/duplicates/drift. Retains569 prior cases including9 real Viewer/canvas cases and adds37. Current-history before51 and prior-schema before258 remain separate scopes. CP312/Windows/PyQt6 only. |
| Current owner | Reviewed50-body/21-site selection and43 before cases; native-first50 experiment retained. Handwritten NumPy nd_grid.getitem/numeric.indices now have canonical read-only preparation, alias-preserving batch publication and runtime checks on2.5.3. Final156 distinct cases pass (142 retained+14 provider scenarios), including native-first brush comparison, handler/numeric callbacks, errors, aliases, GC and rejection. [Latest evidence](../.artifacts/labels-native-frontend-edit-owner-grid-runtime-stage/progress.json) retains exact source pins/notice and limitations.311/313 syntax is not provider runtime qualification. No default startup or frontend/provider acceptance. |
| Next tool action | Complete grid source/frame/temporary-release fidelity and precise primitive approvals, then required Pillow/skimage and remaining provider parents. Review native-first producer and observer/namespace compatibility, design permanent startup/reload ownership, and close source target/parent fast locals, tracing/callback/evaluation/error/local-release/GC/frame ABI before default enablement. Unselected application policies remain required. Preserve aliases/state. Obtain independent effective source/ABI approval before application integration, then reuse43 before/606 accepted regressions with real Viewer/plugin/save/reload/cancellation/shutdown checks. Do not repeat completed selection/baselines or unchanged finite probes. |
| Owners | Root owns the current staged implementation and shared integration/build/runtime, accounting/checkpoint and docs. The existing independent selection review covers source scope only; full effective native source/ABI review remains required. Shared application/build jobs remain serialized. |
| Required workflow exit | Finish frontend geometry/brush/flood/mouse, real plugin serialization/save/reload and actual cancellation/close. Preserve actual model/backend/raw/view/plugin/Qt identities and error/event order. Whole class/module/canonical/schema/private/provider parents remain required. |
| Lifetime | Per-process diagnostics are recorded separately in the [606-case review](../.artifacts/labels-native-edit-publication-owner-installed-review/installed-evidence-review.json); no total/cause/clean-shutdown claim. Prior [bounded history import/reload observation](../.artifacts/labels-edit-history-owner-lifetime-followup-review/bounded-runtime-observation-review.json) passed that scenario only. Candidate full generation/error/reentry/interpreter/GUI lifetime remains OPEN. |
| Release tooling | Reviewed checker/test targets are integrated and38 production tooling tests passed. M0 normalization/transport/applicability and actual final release evidence remain required OPEN. |
| Reuse and cleanup | [Cleanup](../.artifacts/labels-native-edit-publication-owner-production-cleanup-inventory.json) covers75 settled app/build/before/failure receipts, no new disposable junk/deletions and5038 unchanged genuine caches. Prior accepted binaries and all required SDK/env/providers/build/snapshots/stages/evidence remain preserved. |
| Full goal | All M0-M9 and complete_application_port remain OPEN/false. [Fresh audit](../.artifacts/labels-native-edit-publication-owner-accepted-responsibility-register.json) has692 valid reviews,0invalid and6193 unreviewed entries;667 records remain PARTIAL and63 dependency operations are narrowly reviewed. Counts do not measure conversion percentage. |

#### Supporting scope and evidence

First-refresh control is accepted within its finite installed scope. Twelve complete bodies
in four facades now dispatch to `cpp/layer_first_refresh.cpp` and
`cpp/scalar_first_refresh.cpp`. Fifteen exports, three GC-visible carriers and
nineteen readonly references preserve refresh/cache/context/units/request/response
evaluation, callback and error order. Reuse the native canonical Layer dictionary,
sole load state, slice plan and accepted numerical/event/VisPy operations.

The single installed receipt is
[first-refresh-control-checkpoint.json](../.artifacts/first-refresh-control-checkpoint.json),
SHA256 `5183c8f5cfa38ad622e4b8d36cafff6825ce090840dc277eebe9655b44848447`.
It links 747 runtime/54 original/144 preserved inputs, independent actual native/
facade/additive integration review, installed binaries, build, commands and reports.
All four jobs reached terminal exit 0: 51 first-refresh comparisons, 42 constructor/
seven-layer cases, 16 unchanged pinned cases and 10 actual Viewer GUI cases.
Total: **119 passes, no failures or skips**. No build/test process remains live.
This is finite control parity; full constructor/first-refresh/Viewer closure is open.

Preserve the [first-refresh pre-port checkpoint](../.artifacts/first-refresh-control-baseline-checkpoint.json)
and [raw before-image](../.artifacts/first-refresh-control-before/index.json).
Their creation-time 50-pass paired/16-pass reused-original scope and 744 runtime/
51 original count remain valid. The held fifty-case test is unchanged at
`f911bdb3bb669a35567f1bbb8312c5b7085806ef7781d06595830a66de773137`.
Three original inventory entries were supplemented because that test already
self-checked all four pinned source hashes.

Independent review found an exceptional context-manager lifetime defect before
installation. Preserve its [rejected draft receipt](../.artifacts/first-refresh-control-source-review-attempt1.json).
The corrected native code retains the saved exit callable through result truth
and handled-exception cleanup. One new [context lifetime regression](../tests/test_first_refresh_context_lifetime_native.py)
passed in explicit pinned/frozen-before test-only mode and installed current mode.
No production fallback or source-path substitution was added.
Earlier [constructor](../.artifacts/layer-construction-checkpoint.json) and
[namespace/publication](../.artifacts/layer-slice-publication-checkpoint.json)
receipts retain their historical 116-pass and 527-pass/one-existing-skip scopes.
Do not replay or recapture them for documentation maintenance.

**Accepted finite stage: slicing-input helpers with reviewed interop.**
Native code must own the actual shape/extent/projection/input/cache/context
policies feeding construction and first refresh. Preserve dynamic overrides and
fresh reads, real NumPy calls, transform/Extent aliases, genuine lexical super,
canonical `cached_property` storage, materializer closure-cell authority and
the actual `lru_cache(maxsize=1)` wrapper as sole cache. Preserve genuine generator
function/category and closure metadata through reviewed protocol delegation;
C++ must own iterator suspension, send/throw/close and both context scopes.
No second cache, copied transform or independent mutable provider/model mirror.

The finite candidate contains 31 complete definitions, including nested bodies:

| Owner | Accepted 31-definition responsibility |
| --- | --- |
| Root | Fresh affected before-image, common integration, serial runtime/build/GUI jobs and one checkpoint; no recapture of unchanged prior boundaries. |
| `/root/colormap_acceleration` | New `cpp/dask_indexing.cpp` and `cpp/layer_data_geometry.cpp`; `utils/_dask_utils.py`: `resize_dask_cache`, `_is_dask_data`, `configure_dask`, nested `dask_optimized_slicing`; `layers/base/base.py`: projection getter/setter, units getter, `_extent_world`, `_extent_world_augmented`, `extent`, `_extent_augmented`, `_data_to_world`; `layers/utils/layer_utils.py`: `get_extent_world`. Thirteen definitions. |
| `/root/colormap_inner_loops` | New scalar geometry/materializer native unit; `layers/_scalar_field/scalar_field.py`: `_make_level_materializer`/nested `_materializer`, `ScalarFieldBase.dtype`, `_get_ndim`, `_get_level_shapes`, `level_shapes`, `downsample_factors`, `_extent_data`, `_extent_data_augmented`, `_extent_level_data`, `_extent_level_data_augmented`, `_reset_thumbnail_level_data`; `layers/image/image.py`: `Image._get_level_shapes`; `layers/_multiscale_data.py`: dtype, shape, shapes, item and length operations. Eighteen definitions. |
| `/root/colormap_map_tests` | Independent bounded scope/native-authority/protocol review, one finite affected paired cohort; no production edits or shared runtime jobs. |

Independent bounded scope review accepts this exact 31-definition stage. The
fresh [ten-file before-image](../.artifacts/slicing-input-helpers-before/index.json),
SHA256 `d3829658525abec1e1d008fa8c57e328581561189fe41fba42df300721042148`,
captures all current bodies/common files against the accepted installed pair.
Preserve this boundary; the three planned native units were absent at creation.

The [accepted design gate](../.artifacts/slicing-input-helpers-design-gate.json),
SHA256 `49ac9b3ac9acd0c25250fcb412d5a96eacc2a5dd132afa645b6399e7730fe260`,
fixes 39 module exports, nine GC types and twenty readonly original-local
references. Independent review accepts the literal closure/generator/cache
handoffs and exact A13/B18 ownership. The held paired file contains thirteen
groups; its actual pre-port execution passed **56 cases, no failures or skips**.
The [baseline checkpoint](../.artifacts/slicing-input-helpers-baseline-checkpoint.json),
SHA256 `8dabb50a8aaef1c396ec4395b05a5c2f8f67a8db6c8d13d26f86d6b2ef812ea3`,
links 748 runtime/56 original/155 preserved inputs and the unchanged installed
pair. It reuses the valid 119-pass integration receipt and releases exactly the
31 owned definitions. The test remains held at
`c60e159a0fff74ee22988000ee999842e25e44ae7dbd2f8d82ccd752db65ef0d`.
The four installed jobs passed: new helpers 56, 35 constructors plus seven real
layers 42, unchanged originals 16 and actual GUI cases ten: total **124 passes,
no failures or skips**, all terminal exit 0. The constrained Qt6 build also
reached terminal exit 0. The [installed checkpoint](../.artifacts/slicing-input-helpers-checkpoint.json),
SHA256 `191e4f5d3e2cf1d32441a9be540d0e0143033beae9430c991943bc8196a4cab1`,
links the actual source/profile/binary/build/command/report evidence. No process
remains live. These counts prove only the recorded finite scope.
The prior controlled 51-case cohort does not exercise these new helpers, so keep
its locked historical source expectations intact without an unrelated replay.

**Next owner context:** the five frozen dataclasses need native authority,
constructors and remaining owned helpers. The compact resume state above is the
single source of the next executable action and its release conditions.
A13/B18 handwritten helper implementations, their facades
and independent actual patch/GC/common review have passed. Root integrated the
three native units/hooks and appended the accepted stub API, preserving its
entire 98,996-byte earlier prefix. The source-only
[integration proof](../.artifacts/slicing-input-helpers-integration-proof.json)
and [frozen inventory](../.artifacts/slicing-input-helpers-acceptance-inventory.json)
record 751 runtime, 56 original and 155 preserved inputs. The patch is installed
and its four jobs passed. Preserve these held sources and valid evidence until
the next owner's before-image/release gate; do not rerun the helper baseline or
repeat its source review. Accepted Dask control reads the genuine cache cell after
its first context manager enters; materialization captures `np.asarray`
before reading the genuine data cell. Both lazy expression boundaries retain
genuine generator category through reviewed protocol glue. Keep exclusive
facade-file ownership; no competing scalar writers. Reuse valid current evidence.
Immutable slice/view/response model authority is the next closure step. Its five
frozen dataclasses retain 29 canonical field references; the current SlicePlan
is a derived numeric cache, not their state owner. Independent source review
accepts a separate policy-free native namespace beneath those five classes,
with handwritten native constructors and genuine signature/default glue. Retain
the actual standard-library dataclass field/frozen/eq/hash/repr/replace protocols
as a reviewed external metadata boundary; discard the original constructor body.
Capture its saved object, exact factory sentinel and original default factory
metadata. Native constructors must call fresh saved-object `__setattr__` for
each field in source order, honoring subclass descriptors; the request-ID factory
runs after fourteen successful stores only when the sentinel was supplied.
Reuse the original semantic capture-cell objects in genuine signature glue.
Read the saved-object cell before every field's setter lookup and the sentinel/
factory cells at their original read points; a setter callback may replace them
between stores. Initial-value and per-invocation snapshots would both hide this.
For the ID store, capture the setter before evaluating the factory RHS. The
baseline must preserve these source-observed metadata/order expectations without
depending on CPython-private cell spellings.
The held cohort is independently source-reviewed at
`5ecc2b425a57c870aefd695a62bea00c78f9c704e766951d0a961510532561a2`.
The exact earlier versions and source-boundary images are preserved. Its explicit
descriptor warm and native allocation-candidate restoration loop exercise copy,
deepcopy and pickle without deleting caches. The actual first run produced
21 passing allocation/constructor-cell/descriptor/Generic/restoration/weakref/GC
cases plus five passing existing numerical/warning cases. Both unchanged
`copy_with` assertions fail on actual current classes after the pinned oracle
succeeds. The new live-field comparison also fails after all four current helpers
execute, exposing stale displayed/non-displayed dimensions, resized order and
slice coordinates. Its pinned expectations pass. These are preserved repair
targets, not setup errors or a passing whole baseline. The
[baseline/release checkpoint](../.artifacts/slice-dataclass-state-baseline-checkpoint.json),
SHA256 `811ff069320ccdc5d562d0a17494f4eb8f31eee3a4455b28fc04878ab88ed4a6`,
links 752 runtime / 58 original / 171 preserved inputs, unchanged installed
binaries, exact command, terminal exit 1, reports and independent diagnosis.
The passing new-boundary evidence releases the complete native owner; all named
installed checks must pass before acceptance. Preserve the existing
`cached_property` decorator, signature,
docstring/body and warmed dictionary entries as passive diagnostic/restoration
data. The new complete `data_slice` operation must reproduce the pinned fresh
reads, transform and margin aliases, scatter/error order and late constructor
lookup. Root forwards the old native `slice_input_data_slice` export directly
to the same complete C++ engine while retaining the existing binding signature
and keyword names. Its additional untouched source is captured in the
[supplemental before-image](../.artifacts/slice-dataclass-state-legacy-forward-before/index.json).
The subsequent [installed five-model checkpoint](../.artifacts/slice-dataclass-state-checkpoint.json),
SHA256 `1a2a40e06bb4d16915bddfa0ed585f70b6624548774727bec426c969b277c81b`,
accepts five handwritten constructors/29 canonical fields and thirteen complete
helpers within97passing affected checks/four terminal-exit-0 jobs. The constrained
Qt6 build passed;753runtime/58original/177preserved inputs stayed held. All three
failed baseline assertions now pass unchanged, including fresh `self.make_full`
overrides and warmed-plan/live-field authority. The 23exports/sixGC types/
21readonly-local design has actual source and installed evidence. Private
frames/code/MRO, cross-version metadata extraction and full workflow/lifetime
parity remain open. Preserve the genuine original cells without relying on
CPython-private names.

**Next complete owner:** scalar/Image/Labels request execution. Preserve the
[eight-file before-image](../.artifacts/scalar-slice-executor-before/index.json),
SHA256 `4c0c36f21ec53be873a5b237969800dc4e52588231a20c9b0346134ed91a5a9c`.
Its twelve bodies cover request invocation, single/multiscale assembly, level
rescaling, projection, order/bounds/point/thick-index helpers and Image/Labels
reducers. The preserved baseline exposed stale projection/order/view overrides,
recomputed multiscale order and late context exits. The reviewed native owner
now implements fresh overrides, original special-method lookup/suppression
leases and genuine source globals; installed parity remains unverified.
The settled13-export/two-GC-type/25readonly-local design reuses the accepted
canonical state. Image generators retain actual data/axis cells; on CPython3.12,
inlined comprehensions clear `_` on normal and exceptional cleanup. Raw typed
native extensions remain explicitly bounded; all twelve application facades
must execute source-exact operations. Self-based private whole-operation exports
forward to the same engine through a fresh legacy module-namespace boundary;
escaped facade/global identity there remains open. No request proxy or original
Python application-body fallback is permitted. The actual 57-case baseline
passed adoption checks and justified the complete implementation release with
all 22 failing assertions preserved. Source review is complete; the compact card
now directs build and installed acceptance.

**Required remaining owned callees:** MultiScaleData construction/backing-list
authority and protocol validation; level-lock/data-level/data properties and units
mutation; settings/EventedModel/EmitterGroup and remaining model validation/
construction, excluding the five accepted slice constructors and 29 canonical fields;
mode/name/clipping/locking, scalar plane/kernel/depiction and Image interpolation/
rendering configuration; naming/provenance/xarray inference, colormap/color/model
validation and actual subtype/highlight/Viewer callbacks. These responsibilities
cannot be declared external primitives. This stage does not close full workflows.

**Evidence limits:** original/GUI jobs retain 144-instance/six-type/97-function
shutdown diagnostics; isolated paired jobs do not prove clean teardown. Historical
Windows WMI `0x8007000e`, private frame/error-locals, arbitrary plugin inheritance,
cold startup, GPU cleanup, complete fixtures, platforms and outside-checkout wheels
remain open. The current Qt6 editable build uses scikit-build-core 0.12.2,
nanobind 3.1.0 and CMake 4.4.4. Preserve the reviewed earlier packaging failure;
fresh-wheel/sdist/release certification is unproved.

**Parallel M0 work:** source-reviewed native NPE2 SVG/Animation/OME-Zarr candidates
are in [m0-native-npe2-plugin-corpus.json](../.artifacts/m0-native-npe2-plugin-corpus.json).
The actual NPE1/adapted reader is in
[m0-legacy-npe1-plugin-corpus-v2.json](../.artifacts/m0-legacy-npe1-plugin-corpus-v2.json);
preserve applicability, failed tag-assumption and superseded receipts. The
[provisional API source scan](../.artifacts/m0-plugin-api-source-scan.json) and
[independent source review](../.artifacts/m0-plugin-api-source-scan-review.json)
verify all 36 runtime Python members of the four wheel artifacts. Animation
imports and subclasses private `napari._qt.containers.QtListModel`/`QtListView`;
ViewerState dynamically writes serialized Layer properties and uses model
`dict`/`model_dump` protocols. The new
[Animation property-domain source review](../.artifacts/m0-animation-property-domain-source-review.json)
resolves the pinned seven-layer key sets, read-only/plain attribute exceptions,
nested model receivers and mutation/error order against actual wheel members and
26 pinned files. It proves no runtime parity or complete API inventory.
The [entrypoint/alias source review](../.artifacts/m0-plugin-entrypoint-alias-source-review.json)
resolves the four frozen plugin entrypoints, selected callback aliases and
application handoffs. It distinguishes the source-inferred CZI legacy adapter
from unexecuted registration. Arbitrary plugin/provider domains, complete API
inventory and actual Qt/evented-list callback ownership still require evidence.
Acceptance API fields remain unfilled; this source work proves no runtime parity.
Metadata eligibility is not runtime compatibility. Exact locks, valid fixtures/oracles,
API/private-API inventories, applicability cells and original/candidate real
plugin workflows remain required. The acceptance draft is unfrozen; all M0-M9
milestones and final conversion gates remain OPEN.

### Start here on every continuation

Read the goal contract and compact resume state, then only the architecture,
milestone or historical evidence needed for the current dependency. Current sources, installed
binaries and actual process status take precedence over historical status words.
Use this single execution loop:

1. **Resume.** Continue the card's design, implementation or validation phase.
   Resume live processes by existing handle and owner; never launch duplicates.
   Preserve terminal results and read linked history only for a named dependency.
2. **Define each owner once.** Record its complete native responsibility or
   workflow, writable files, owned callees, preserved contracts, finite exit
   checks and next dependency. Choose it by progress toward closure of the
   parent workflow, and carry its remaining owned callees to that closure.
   Reuse released decisions unless inputs or required
   contracts change. Resolve blocking interop decisions in parallel. Before a new
   type/storage/compatibility boundary changes, preserve its before-image and
   finite original/current or genuine candidate baseline. New adoption checks
   must pass; diagnosed existing failures may receive a bounded correction
   release with assertions unchanged. Smaller stages require a concrete interop
   reason and a named closure step. Baselines are not recreated after review.
3. **Implement and review in parallel.** Assign meaningful independent files or
   decisions across available workers; never duplicate their exploration. Each
   assignment names dependencies, prohibited shared edits and a concrete done
   condition. One coordinator owns integration and serial builds/GUI jobs.
   Preserve others' edits and source holds while jobs read them. All aliases
   share one authoritative store through mutation, callbacks, cache, copy and
   restoration. Review actual C++ and facade/dependency boundaries. Once the
   current patch is ready, prioritize its build and affected verification;
   independent required-owner work may continue outside its held inputs.
4. **Verify affected behavior.** Freeze only new or changed affected inputs and
   verify the installed binary using unchanged pinned assertions/fixtures,
   applicable independent C++ checks and actual Viewer/plugin/rendering paths.
   Add checks only for an unproved required contract or diagnostic need; rerun
   only affected checks when source, binary or conditions change. Reuse valid
   unchanged evidence and preserve nanobind diagnostics. Source review, headless
   checks and host prototypes do not certify installed application/release parity.
5. **Diagnose bounded failures.** Use the smallest real path, a falsifiable
   hypothesis and a distinguishing check. Preserve failed reports and assertions.
   Correct fixtures only for demonstrated setup defects; never add migration
   skips or exclusions. After two attempts without new diagnostics, change the
   approach or advance independent required work. Carry the attempt count and
   unresolved requirement across resumptions; renaming a probe does not reset it.
6. **Accept once and advance.** After all named installed jobs pass with terminal
   results, record one scoped checkpoint and update the card with native progress,
   commands, source/binary/report identity, gaps and one executable next action.
   Advance the next required owner while finishing changed review/manifest
   accounting; accounting is a prerequisite only when it affects that owner's
   authorization, inputs or acceptance. Complete it before milestone/release
   closure. Keep source review, installed checks and full acceptance distinct.
   Additional hashes, inventories or receipts require a named integrity gap or
   changed acceptance input. All final-source gates still apply at M9.

## Execution rules and resumption

The current task card and execution loop above are the continuation instructions.
Use the M0-M9 table and ordered packets below to choose the next dependency owner.
Do not repeat review-hash registration or runtime matrices for documentation-only
maintenance. During implementation, choose work that changes required native
ownership/control, resolves a named design decision or produces missing behavior
evidence. A new receipt or native-call count alone is not subsystem progress.

A blocked report must identify the exhausted authorized paths, concrete evidence
and smallest input or environment change needed. One unavailable configuration
or two unproductive attempts on a requirement is not by itself a blocked full
goal. Follow the active goal's lifecycle rules; never mark it complete for time,
token usage, a passing finite packet or documentation maintenance.

Resumption entry: use the sole
[current task card](#current-task-card-actual-viewer-async-slice-delivery).
Reuse accepted acceleration, cache, map and model receipts within their recorded
scope. Their historical next actions are references, not continuation commands.

### Final evidence gates

M0 must finish and freeze the draft `docs/conversion-acceptance.json` from the
pinned project's support contracts and CI, including exact
Python/Qt/OS/architecture combinations, native
Qt SDK/runtime constraints, provider and plugin versions, real user workflows,
and workload inputs, measurement methods and numerical/image/performance
tolerances. Record which combinations are applicable and why. This acceptance
record is not yet frozen. Keep CPython 3.11–3.14, all supported Qt bindings and
Windows/Linux/macOS in scope; reconcile the fork's metadata and CI with the
pinned support matrix. Missing infrastructure leaves required configurations
open. A failed check cannot be made passing by removing it from the record.

Completion requires all of the following evidence for the same final sources:

1. Every M0–M9 exit is satisfied. Trace every owned runtime responsibility and
   retained dependency call; all Python application policy and algorithms have
   handwritten C++ implementations. Reviewed facades and dependency calls retain
   their documented limits.
2. `tools/audit_conversion.py --check-complete` exits zero with current source
   hashes, complete responsibility/dependency reviews and no unresolved review
   issues. This audit is a source attestation and cannot prove behavior alone.
3. The full pinned upstream suite uses its original fixtures and passes alongside
   the frozen plugin/provider and actual GUI/rendering workflows. Record skips,
   xfails and deselections against the pinned configuration; migration-induced
   exclusions or missing required provider coverage keep the gate open.
4. Fresh wheels install and pass from outside the checkout in every required
   matrix configuration. Record Python, Qt binding/SDK/runtime, OS, architecture
   and GPU configuration with commands, exit codes, stdout/stderr, report hashes,
   source, dependency-lock, wheel and installed-binary hashes. Required native Qt
   bridges must be built and exercised; skipped bridge tests cannot certify the
   actual application. Include reproducible sdist-to-wheel builds.
5. Lifetime, GC, thread, cancellation, repeated close and GPU cleanup checks pass
   with diagnostics enabled; measured workloads meet the frozen acceptance
   tolerances. There is no assumed universal speedup requirement.

Store the final evidence index in `docs/conversion-release-evidence.json`, linking
each acceptance item to its report and hashes. Neither that index nor test totals
replace the underlying evidence. Set the completion flag and mark the full goal
complete only after reviewing all five gates. Until then keep all unsatisfied
milestones and explicit gaps open.

## Target architecture and compatibility contract

Use C++17 for napari-owned state, algorithms, controllers, task scheduling and
application decisions. Nanobind exposes that implementation through the existing
Python package. Public Python classes may remain as thin facades to preserve
signatures, class/schema identity, decorators and plugin subclass hooks. Array,
unit and plugin payloads may remain Python objects at a declared boundary;
their ownership and GC edges must be explicit.

Each state object needs one authoritative owner. Python properties and
native methods must observe the same state; independent mutable snapshots are
not a completed conversion. Complete native control does not establish native
state authority: C++ assignments to Python-owned instance attributes remain
partial until a native owner retains the canonical live application storage
seen by properties, dictionary aliases, callbacks, copies and restoration.
C++ must own numerical loops and task control.
Use native buffers and avoid per-element Python calls where the defined contract
permits them. Source-required dynamic array, container or callback operations
may call declared Python primitives; those calls must not delegate napari-owned
algorithms or policy. Retain NumPy/SciPy/Pint and supported lazy-array providers
as external dependencies where justified. Move napari-owned policy inside a
dependency adapter into C++ as well.

Keep Qt binding interoperability explicit for Python plugin widgets and
console/notebook use. A native controller must use the real viewer, event loop
and canvas, with tested wrapper identity and destruction. Review retained VisPy
calls as a rendering-backend boundary and move napari scene/visual orchestration
to C++. Replacing the backend itself is a separate scope decision.

M0 must freeze the supported API and provider matrix, including private APIs
used by upstream or representative plugins. Native code changes Python
bytecode/frame introspection; record those differences explicitly rather than
promising identical executable Python bodies. Validate public behavior,
callbacks, event/error ordering, mutable views, serialization and plugin hooks.
Test defined contracts and representative implementations; the static inventory
does not prove compatibility with arbitrary Python objects.

## Implementation order and acceptance gates

| Stage | Concrete work | Required exit evidence |
| --- | --- | --- |
| M0: Contracts and interop | Review the upcoming subsystem and its actual native/dependency call targets. Freeze API/events/provider/platform, plugin and workload acceptance in `docs/conversion-acceptance.json`. Prove real Viewer, Python plugin-widget and rendering-canvas interop. | Current source-hashed responsibility reviews; dependency-call allowlist; frozen configurations/workflows/tolerances; real widget/canvas identity, event-loop and repeated-close tests. |
| M1: Foundations | Finish model/event/container/transform authority and lifetime. Preserve class identity, validation, dumps/copies, cache behavior and live mutable views. | Mutation/alias/restore/provider/error/reentry comparisons; applicable independent C++ tests; clean affected shutdown probes and repeated viewer open/close. |
| M2: Layer/Viewer base | Port Layer and LayerList state/mutation, selection/linking, features/text/colors, extents and scene/cursor/overlay models. | Insert/remove/reorder/select/property workflows match baseline public objects, events, serialization and cache behavior. |
| M3: Image/Labels workflow | Complete request/response paths, multiscale/lazy caches, native tasks and GUI delivery, painting/filling and undo/redo. | Real NumPy/Dask/Zarr IO, supported optional providers, edit/history round-trips, stale/cancelled/error tasks and close-while-loading checks. |
| M4: Remaining layers | Complete Points/Vectors/Surface and port Shapes and Tracks geometry, graph/time/tail and interaction. | All seven layer types pass applicable slicing, transforms, selection, editing and 2D/3D comparisons. |
| M5: Services | Port actions, settings, IO, notifications, startup and plugin coordination; retain declared Python plugin callbacks. | File round-trips, settings migrations and real reader/writer/command/dock-widget plugins match baseline. |
| M6: Actual Qt application | Build native main-window/viewer, controls, menus/dialogs/preferences, docks/console hosts and input/event routing. | Actual mouse/keyboard/menu/dock flows, Python-created widgets, screenshots and repeated-close checks; no unintended model/widget/native roots after close and release of external references. Caller-retained objects and legitimate type/module metadata preserve their declared lifetimes. |
| M7: Rendering orchestration | Port napari visual/scene controllers, transforms, textures/colormaps, clipping, volume options, picking, overlays and GPU lifecycle. | 2D/3D/volume image comparisons, picking and screenshots, supported drivers and GPU cleanup; backend/plugin visual interop. |
| M8: Remaining Python implementation | Sweep event/collection/input/geometry/color/text helpers, builtin plugins, entry points and the five remaining listed numerical fallbacks. | Every runtime responsibility reviewed; only declared facades/adapters/metadata remain; no original application algorithm fallback. |
| M9: Release parity | Run full original upstream fixtures, real plugins, interactive workflows and fresh wheel installs across the M0 acceptance matrix; measure its workloads. | All final evidence gates pass, linked in `docs/conversion-release-evidence.json`; no migration-induced exclusions; required configurations/workflows covered and frozen tolerances met. |

Track a finite responsibility and workflow coverage set for each milestone,
derived from the pinned application and supported contracts. Link every exit
to its implementation, dependency decisions and acceptance evidence. Packet
counts and exported native calls do not close a milestone. Use independent
C++ tests for standalone native logic and installed actual-object/Qt workflows
for Python and GUI protocol control; select checks for the named contract.

M8 includes the original Python branches still retained for
`interpolate_coordinates`, `first_nonzero_coordinate`, `segment_normal`,
`project_points_onto_plane` and `points_in_box`. Cover lazy arrays, dtype and
subclass/provider behavior before removing those branches. `calc_data_range`
and its range adapter now dispatch to native implementations; retain their
bounded evidence and wider unresolved provider contracts. Review remaining
dispatch/conversion decisions in `naparixx/_backend.py` and `_adapters.py` too;
keeping a Python API adapter does not exempt application policy inside it.
The end state must implement generic as well as optimized behavior in C++.

Assign finite M0 decisions alongside implementation: identify an actual
upstream/plugin/provider contract, its supporting sources, the owner and the
evidence needed to decide it. For each required matrix row record applicability,
exact versions, workflow, owner and exit artifact. Derive combinations from
supported configurations and workflow requirements, with sourced applicability;
retain every required configuration without inventing a Cartesian product of
all dependency combinations. Decide private/provider/reflection contracts from
actual upstream or plugin use, rather than expanding arbitrary-object probes. Resolve the decisions that block the upcoming
subsystem before expanding compatibility tests. Independent decisions and
runtime owners may proceed in parallel. Unresolved requirements stay open;
the full M0 record must still be frozen before release acceptance.

Work through complete public workflows alongside the shared foundation; finish
the specific foundation contracts that block each workflow. Keep responsibility
review scoped to changed runtime owners and dependency operations throughout
M1–M8. Build wheels and validate interoperability incrementally, then run the
full release checks against the final sources at M9.
Dates should be estimated per accepted subsystem; the selected test totals do
not support a credible whole-port completion date.

## Next reviewable packets

Use the following dependency order. Review and wheel preparation continue
alongside every packet. Define the next packet by a state owner or complete
user workflow; adding forwards alone does not close a subsystem.

| Priority | Implementation scope | Acceptance before moving on |
| --- | --- | --- |
| Accepted prerequisite: Grid dictionary publication | The finite selected Grid23 publication repair is accepted; original 12-pass/5-fail evidence is preserved. | Reuse its canonical receipt. Wider Grid/M1 contracts remain open; reopen only for new failure or a required unresolved contract. |
| 1. Actual Viewer async slice delivery | Port `QtViewer._on_slice_ready` and required scalar-field/Image/Labels response owners in the actual Viewer. Resolve its specific blocking foundation contracts alongside delivery. | Actual canvas/plugin widget identity, async image/multiscale/Labels rendering, response/loaded-ID/event order, stale/error/cancelled tasks, reentry and queued close match the baseline. A forwarded callback alone is incomplete. |
| 2. Remaining model authority, collections and transforms | Finish EventedModel/event ownership and authoritative typed/evented/nested/selectable collections, selection/LayerList stores and transform arrays/caches. Reconcile live dictionaries, schemas, copies, restoration and callbacks with one owner. Continue independent owners alongside Viewer delivery; do not defer all GUI integration. | Mutation/alias/copy/restore, live views, provider/subclass contracts, errors/event order, GC/reentry, Python 3.11/watcher exhaustion and array stride/dtype/cache contracts match pinned behavior without independent mutable snapshots. |
| 3. Complete Layer/Viewer transitions | Port remaining Layer/ViewerModel shared transitions, selection/linking, features/text/colors and overlay/cursor state through the actual Viewer, Python dock widgets and rendering canvas. | Actual Qt wrapper identity/destruction, public API/events, plugin-created widgets, layer insert/remove/reorder/select and rendered images work across the supported bindings. |
| 4. Image/Labels workflow | Finish request construction/response application, worker ownership, cancellation/stale completion/GUI delivery, multiscale/lazy data and paint/fill/polygon/history. | Open → add/reorder/select → change dimensions → render → edit labels → undo/redo → save → close works through the GUI and Python API, including close while loading. |
| 5. Remaining layers and services | Complete Points, Vectors, Surface, Shapes and Tracks; actions/contexts, shortcuts, settings/migrations, notifications, IO/session persistence, CLI/startup and plugin coordination. | All seven layers pass editing and 2D/3D workflows; real reader/writer/command/dock-widget plugins and file/settings round-trips match the baseline. |
| 6. Finish GUI/rendering and runtime sweep | Port remaining Qt controllers/widgets and napari VisPy orchestration, GPU lifecycle, builtin plugins, helpers and five remaining listed numerical fallbacks. Review policy in backend/adapters. | Keyboard/mouse/menu/dock flows, image/picking comparisons and GPU cleanup pass; each owned algorithm has handwritten C++ implementation and every retained dependency call has a reviewed boundary. |
| 7. Release gate | Complete original upstream fixtures and representative plugin tests; build/install wheels and measure real workloads on the supported Python/Qt/OS/architecture matrix. | No migration-induced exclusions, no ownership failures, current source-hashed reviews, no application fallback, and behavioral/release parity. Only then close M9 and set the completion flag. |

Keep the first Image/Labels workflow as the integration target. Reuse accepted
acceleration/cache/map, Base/controller and installed visual-provider evidence.
Advance the current native Layer/slicing-state, thumbnail, contrast and loaded
publication packet. Wider event, collection and
transform contracts and M1 remain open. Independent required foundation owners
may advance in parallel. Exercise native state through real Qt/rendering/plugin
interop throughout implementation.

Dates should be estimated after each subsystem contract and test inventory
are accepted. Existing selected test totals do not support a reliable
whole-port completion percentage or date.

Each packet must include handwritten native source, explicit facade/dependency
boundaries, unchanged baseline comparisons, applicable independent C++ checks,
fresh installed-extension tests, updated manifest/reviews and a source-hashed
checkpoint. Known contract or lifetime failures stay open even when the selected
tests pass.

Completion requires both semantic/release parity and a passing responsibility
completion audit. `complete_application_port` remains false until both are
proved. See [the milestone details](migration.md#ordered-milestones) and
[validation limits](validation.md).

## Historical checkpoint references

The following narratives describe their original source snapshots and scoped
evidence. Their historical next actions and status words do not update the
current card above. Read one only for a named implementation dependency or
required unresolved contract. Preserve failed reports and immutable receipts.

### Earlier accepted checkpoint: context lifetime

The preceding accepted implementation evidence is `.artifacts/context-lifetime-checkpoint.json`.
The helper verified its source, report, installed-binary, lint, boundary-review
and audit fingerprints before updating this handoff. The previous iterator
checkpoint and `.artifacts/sol-ultra-goal-review-register.json` describe
earlier working trees.
Their counts do not replace the current evidence. All M0–M9 milestones
remain open and `complete_application_port=false`.

Three native context helpers now own the new PyType_GetDict reference and
retain the selected descriptor during binding. The exact focus callback
control runs in C++; a GC-visible native connection record retains the
exact callback and connection token through read-only views without a
direct application reference. The original window atexit worker loop now
forwards to native shutdown control. Shutdown disconnects napari's own
focus callback, queues napari's shared menu actions for deletion and drains
deferred callbacks twice. Other connected handlers retain their identities.

| Selected check | Result |
| --- | --- |
| New context/focus/connection/shutdown cases | 83 passed (18/23/20/22) |
| Main CPython 3.12/PyQt6 selector | 2,398 passed |
| Selected unchanged upstream cases | 1,119 passed; 3 tensorstore skips; 2 mouse cases deselected |
| CPython 3.11 selector | 2,250 passed |
| CPython 3.11 supplemental Qt shutdown checks | 22 passed; PyQt6 Python binding |
| CPython 3.12/PyQt5 selector | 2,283 passed |
| CPython 3.12/PySide6 selector | 2,283 passed |
| Existing Python-disabled core/Qt CTests | 4 passed |

The Python 3.11 selector and supplemental Qt shutdown report are distinct
checks; their counts are not combined into a larger selector. PyQt6 is
installed for the actual Python/Qt GUI checks, while the Python 3.11
native extension was built with NAPARIXX_BUILD_QT and NAPARIXX_BUILD_QT5
both OFF. This does not establish Python 3.11 native Qt bridge or full
release-matrix parity.

At that checkpoint, the source inventory contained 574 runtime/development modules
and 6,155 definitions. The manifest declared 34 partial
modules; 39 contained native evidence. Native-call evidence
does not establish module completion. 11 bounded records were valid; 6,718
responsibilities were unreviewed, with no stale
records or review issues. Six existing records were refreshed only after
reconstructing the prior stub, registrations and three bounded context
helper edits and verifying the exact fresh named cases. New Qt focus,
connection and shutdown responsibilities remain unreviewed. The review
gate remains incomplete and does not establish behavioral parity.

The checkpoint register is `.artifacts/context-lifetime-responsibility-register.json`.
The review backlog is not a count of functions that all require rewriting.
Qt, VisPy and builtin-plugin responsibilities still need individual
ownership and dependency decisions.

The concrete type-namespace leak and Qt callback root are repaired in the
verified selected processes, including the exact original file-menu and
repeated Viewer reproductions. This evidence covers selected Windows
configurations. Broader model/collection/event state authority, Qt
controller ownership, provider/private contracts and full upstream,
plugin, wheel, OS, architecture and performance parity remain open.
All M0–M9 milestones remain open; M1 is incomplete.

See [validation](validation.md#context-namespace-and-qt-lifetime-checkpoint). That packet proves selected Windows
lifetime/compatibility repairs and does not establish full release parity.

### Selected transaction ownership checkpoint

The subsequent native private-map ownership packet is verified within its
bounded selected scope. Its receipt is
`.artifacts/transaction-owner-checkpoint.json`. A GC-visible C++ owner retains
the exact Pydantic private map through a real model slot and thin descriptor,
without an independent queue cache. Native drain control derives membership
from the current queue, preserves live mutation and the last compared payload
through callbacks, and preserves tested shallow/deep memo aliases. Its original
next packet was primary/depth authority, accepted within the bounded scope of
the later checkpoint below. Wider model contracts remain open.

| Selected check for the ownership patch | Result |
| --- | --- |
| Transaction ownership, alias, mutation, restoration and GC cases | 53 passed; all five original reproductions pass |
| Focused model/lifecycle/metadata/context selector, including those 53 | 303 passed |
| Main CPython 3.12/PyQt6 selector | 2,451 passed |
| CPython 3.11 selector, native Qt bridge disabled | 2,303 passed |
| CPython 3.11 supplemental Python/Qt shutdown suite | Initial 21 passed/1 timed out; isolated full retry 22 passed |
| CPython 3.12/PyQt5 selector | Initial 2,334 passed/2 failed; isolated failed-case retry 2 passed and full shutdown retry 22 passed |
| CPython 3.12/PySide6 selector | 2,336 passed |
| Selected unchanged upstream cases | 1,119 passed; 3 tensorstore skips; 2 mouse fixture cases deselected |
| Python-disabled core/Qt CTests | 4 passed |
| Cold installed-extension lifetime probes | 8 passed across four environments, normal/exhausted watcher modes |
| Current responsibility audit | 11 valid bounded records; 6,721 unreviewed; no stale records or review issues; completion exit 1 |

These selectors overlap and must not be added. Initial GUI failures occurred
with other GUI suites active: two repeated-Viewer subprocess timeouts and one
Qt5 callback subprocess exit `0xC000070A`. Their isolated retries passed without
source, timeout or exclusion changes, with faulthandler enabled. The original
reports remain in the receipt. Their cause is unresolved; the retries establish
isolated selected behavior and cannot establish concurrency or release parity.
A separate Hypothesis terminal-summary WMI exception is recorded without
attributing the GUI failures to it.

Six old bounded responsibility records were refreshed only after checking their
actual unchanged native implementations, registrations, retained calls and
named passing cases. New transaction ownership responsibilities remain
unreviewed. The current inventory has 574 runtime/development modules and
6,158 definitions, with 34 partial modules and 39 containing native evidence.
Neither those counts nor the 11 valid reviews establish module completion.

The checkpoint includes current source, report and installed-binary hashes.
It uses editable Windows installs and selected fixtures; original full-suite,
plugin, outside-checkout wheel, Python/Qt/OS/architecture, GPU and workload gates
remain open. Earlier context/iterator/review receipts retain their original
scope and must not be rewritten to match this tree. All M0–M9 remain open.

### Selected primary and delay-state checkpoint

The primary/delay packet is accepted within its bounded selected scope. The
receipt is `.artifacts/primary-state-checkpoint.json`. The exact private-map
`_primary_changes` and `_delay_check_semaphore` objects are now the sole
application authorities. Handwritten C++ controls capture, emission and delay
numeric/context protocols; restored Pydantic PrivateAttr declarations preserve
the schema and provider/subclass contract. No independent native name/depth
cache overrides those objects. The auxiliary journal retains only its standalone
Python-free core view and a weak diagnostic owner, without another application
primary/map payload edge.

| Selected check | Result |
| --- | --- |
| Primary/delay directed cases | 33 passed: 32 pinned comparisons and one native owner-edge case |
| Final model/ownership/context focus | 345 passed, including nine diagnostic and all 53 transaction-owner cases |
| Sequential main CPython 3.12/PyQt6 selector | 2,484 passed |
| CPython 3.11 selector and separate Qt shutdown suite | 2,336 passed; separate 22 passed; native Qt bridges OFF |
| CPython 3.12/PyQt5 and PySide6 selectors | 2,369 passed each |
| Selected unchanged upstream fixtures | 1,119 passed; three existing tensorstore skips; two existing mouse deselections |
| Cold installed-extension probes | Eight passed across four environments and both watcher modes |
| Python-disabled core/Qt checks | Four unchanged-target receipts reused; no claim of new binding coverage |
| Source responsibility audit | 11 valid bounded records; 6,718 unreviewed; zero stale/issues; completion exit 1 |

These overlapping selectors must not be added. Preserve the original 29-failure
pre-port receipt. An older mixin assertion was corrected against the actual
pinned body, which invokes assignment hooks for both counter changes as well as
the field assignment. Earlier 335-pass/one-failure and later 336-pass focused
reports remain historical; the final GC/copy fixes use the fresh 345-pass report.
The first broad main report had 2,481 passes and three GUI subprocess failures
with Windows status `0xC000070A`. Their unchanged isolated retry passed all three,
then the fresh sequential main selector passed all 2,484 cases. Preserve every
original report. Cause remains unresolved; this does not prove concurrency or
full release parity.

The pinned-body oracle shares installed event/Pydantic services. Six existing
bounded reviews were refreshed only after exact prior-text reconstruction,
unchanged native-target inspection and named passing cases. New primary/delay
ownership responsibilities remain unreviewed. A deliberately shared legacy
journal has first-live-owner diagnostics; its standalone enter/leave helpers are
not application counter authority. Wider provider, reflection, subclass, worker,
EventedModel and release contracts remain open. The receipt records current
sources, binaries, reports and limits. All M0–M9 remain open.

### Earlier accepted checkpoint: EventEmitter namespace authority

The bounded EventEmitter packet puts the exact instance dictionary in a
handwritten C++ heap layout exported through nanobind. `_callbacks`,
`_callback_refs` and `_callback_pass_event` remain the sole subscription
representations, shared by generic attributes and the existing C++
subscription/delivery/group controls. There is no independent subscription
cache. Native traversal visits the dictionary and actual type once, excludes
nonowning weak references, and detaches obsolete dictionaries before releasing
their payloads. Default allocation/restoration and tested subtype slots remain
supported. Preserve the original 28-pass/one-failure pre-port report.

The receipt is `.artifacts/emitter-namespace-checkpoint.json`, with detailed
matrix, cold-process and boundary evidence in the corresponding
`emitter-namespace-matrix-receipt.json`, `emitter-namespace-cold-receipt.json`
and `emitter-namespace-boundary-review.json` files under `.artifacts`.

| Selected check | Result |
| --- | --- |
| Namespace and existing event-engine focus | 128 passed, including 37 namespace cases: 32 pinned comparisons and five native-owner cases |
| Main CPython 3.12/PyQt6 selector | 2,530 passed |
| CPython 3.11 selector and separate Python/Qt shutdown suite | 2,382 passed; separate 22 passed; native Qt bridges OFF |
| CPython 3.12/PyQt5 and PySide6 selectors | 2,415 passed each |
| Selected unchanged upstream cases | 1,119 passed; three existing tensorstore skips; two existing mouse deselections |
| Cold installed-extension probes | Eight passed across four environments and both watcher modes |
| Source audit | 13 valid bounded reviews; 6,716 unreviewed; zero invalid/issues; completion exit 1 |

All recorded jobs are terminal. These selectors overlap and must not be added.
The CPython 3.11 initial build lacked CMake on PATH; its preserved failure and
successful tool-path retry are distinct evidence. The four earlier Python-free
core/Qt CTests are reused only for unchanged core inputs and the inspected
CMake extension-list edit; they do not cover the new Python owner.

Two class-shell reviews cover the EventEmitter owner and inherited EmitterGroup
shell. Nested methods and the module still require individual review. The audit
does not parse the CPython heap-type registration: direct source/export review
and installed runtime checks supply that bounded evidence. MRO/layout changes
and reassignment to an unrelated plain Python class remain OPEN compatibility
questions. The latter succeeds on the pinned emitter and raises a layout error
on the native emitter. No plugin/private compatibility waiver is implied.
Full original-fixture, plugin, wheel, support-matrix and workload parity remain
open; all M0–M9 are open and `complete_application_port=false`.

### Latest accepted checkpoint: Dims namespace publication

The bounded packet gives Dims native implementation-metadata ownership and
immediate generic/tested Pydantic dictionary publication, coordinated with the
existing field/flag and private-map records. It preserves the exact physical
BaseModel dictionary, tested explicit caller aliases and trusted provider
lifecycle calls/errors/identities/memo relationships. Broader model authority
and compatibility remain open.

The historical `tests/test_dims_namespace_owner.py` pre-port probe had **22 cases:
two passed, 20 failed, zero errors**. Its receipt is
`.artifacts/dims-namespace-pre-port-receipt.json`; preserve its original XML/log.
Nineteen independent pinned-only contract assertions passed using unchanged
pinned Dims and EventedModel bodies; three cases inspect native ownership only.
Shared installed event/Pydantic services are an explicit oracle limit. These
failing cases belonged to the subsequent Dims packet and were excluded from the accepted
EventEmitter selectors.

Those failures identified delayed obsolete field/private-map payload release,
internal record/cache retention after callers drop their aliases, extra sequence
reads or swallowed errors during trusted lifecycle operations, and a mutable
Python `_native_storage` authority. The handwritten `DimsNamespaceOwner` now
stores implementation metadata in native fields, with a read-only model
descriptor. Native dictionary publication coordinates existing field/flag and
private-map records while preserving BaseModel's physical field dictionary.
The generic/Pydantic publication paths release obsolete internal references;
trusted lifecycle control preserves external provider objects without eager
sequence conversion. This is a bounded change, not whole-model conversion.

Current phase: **verified within bounded selected scope**. The directed file
has 27 cases, including five added GC/subtype/finalizer cases: 23 unchanged
pinned-body comparisons and four native-owner checks. Fresh installed main
reports pass 27 directed, 530 focused and 2,557 selected cases. CPython 3.11
passes 2,409 plus a separate 22-pass Python/Qt shutdown report, with native Qt
bridges OFF. PyQt5/PySide6 pass 2,442 each. The selected upstream report passes
1,119 with three existing tensorstore skips and two mouse deselections. All
eight normal/exhausted cold probes pass; Side requires module-only QtWidgets DLL
preload, with the original failed helper/report preserved and no QApplication.
These counts overlap and must not be added. All jobs are terminal. The canonical
receipt is `.artifacts/dims-namespace-checkpoint.json`, linking matrix, cold,
bounded-review, lint and incomplete source-audit evidence. Reuse unchanged
terminal evidence. No Dims class/method/module or M1 completion is claimed.

Keep the explicit saved `BaseModel.__dict__` descriptor-setter bypass **OPEN**:
the physical dictionary remains canonical, but that path bypasses the Dims
getset and synchronous native record/projection publication is not proved.
The `_native_state` diagnostic's provider conversion and broader private,
provider, reflection and Python 3.13/3.14 contracts also remain open. These are
required follow-up questions, not compatibility waivers.

Integration files: `cpp/dims_model_bindings.cpp`,
`cpp/dims_dictionary.cpp`, `cpp/dims_flags_bindings.cpp`, their headers,
`cpp/dims_namespace_owner.cpp` and its header, `CMakeLists.txt`,
`src/napari/components/dims.py` and `src/naparixx/_native.pyi`, with the private-map
publication hook in `cpp/model_transaction.cpp`. Directed evidence is
`tests/test_dims_namespace_owner.py`; preserve all existing owner/state tests.

Preserve the original descriptor-installation and None-argument binding failures
and their successful corrections. Eight old review objects were refreshed only
for inspected additive stub/CMake fingerprints; no new whole-Dims review was
added. The source audit has 13 valid records, 6,716 unreviewed and zero invalid
records/issues, with expected completion exit 1. The following is the packet's
completed acceptance inventory, preserved as historical evidence:

1. All 27 directed cases pass against the fresh installed main extension, with
   unchanged pinned bodies and preserved explicit map/record/view/memo aliases,
   finalizer timing, reentry and provider errors. New GC checks establish the
   actual native owner's field/private/type edges and clearing order.
2. Existing Dims owner/state, event/model/metadata, all 53 transaction-owner and
   nine diagnostic cases pass; the 37 accepted emitter namespace cases remain
   passing. Inspect observer availability, CPython 3.11 fallback and watcher
   exhaustion rather than treating one mode as proof of another.
3. Fresh affected selectors pass on main, CPython 3.11, PyQt5 and PySide6;
   cold probes and separate CPython 3.11 shutdown checks exit cleanly.
   Serialize shared build directories and actual GUI suites. Preserve genuine
   environmental gaps and original failures without new migration exclusions.
4. Changed native ownership, facades, dependency calls and registrations were
   reviewed within the stated scope; lint, source audit and the bounded
   source/binary/report checkpoint were recorded. Explicit Dims compatibility
   questions, M1 and release gates remain open. Continue the Camera card below.

### Latest accepted checkpoint: Grid dictionary publication

The bounded Grid physical-field-map publication/alias repair now passes its
finite selected inventory. The canonical evidence is
`.artifacts/grid-namespace-checkpoint.json`, linking the matrix, original failed
comparisons, raw before-images, scoped review and final checks. Native Grid
`__dict__` replacement/deletion shares staged publication with Camera, preserves
the same GridState/core, and releases obsolete internal map ownership before
returning. Explicit caller-held maps/views/records remain passive aliases.
Native backlinks bypass internal subtype hooks while ordinary caller writes
preserve them. Grid wide-integer/provider decoding and numerical bodies are
unchanged; provider pins, successor pause cleanup and retired-result checks
retain the accepted Camera safeguards.

| Finite check | Actual result |
| --- | --- |
| Grid directed cases | 23 passed: 16 unchanged pinned comparisons and seven native cases |
| Focused Grid/Camera/Dims/component/model/event/transaction regressions | 851 passed |
| Main CPython 3.12/PyQt6 Viewer selector | 2,600 passed |
| CPython 3.11 selector and separate shutdown | 2,452 passed; separate 22 passed |
| CPython 3.12/PyQt5 and PySide6 selectors | 2,485 passed each |
| Unchanged selected upstream cases | 1,119 passed; original three tensorstore skips and two mouse-coordinate deselections |
| Installed normal/exhausted cold probes | Eight terminal exit-0 processes, each with 13 Grid and retained 13 Camera checks plus inherited checks |
| Lint and bounded review | Passed; eight inspected additive stub sites refreshed, no new Grid/class record |
| Full responsibility completion audit | Expected incomplete exit 1: 13 reviewed, 6,716 unreviewed, zero invalid reviews/issues |

Counts overlap. All 710 runtime/test/helper inputs, 34 staged upstream inputs,
61 prior reports and seven installed binaries are verified through the matrix.
Main/Side Qt SDK/runtime versions are 6.11.2; Qt5 is 5.15.2. CPython 3.11 builds
both native Qt bridges OFF; its cold modes use fallback without watcher slots.
CPython 3.12 normal modes use watchers and exhausted modes reserve all eight.
Shared builds and actual GUI jobs were serial. No new pure core execution was
needed: four historical C++ passes cover 16 unchanged core inputs, excluding the
new CPython getset. The cold helper exactly extends Camera checks and preserves
watcher cleanup and Side's QtWidgets-only preload without a QApplication.

Original Grid17 remains an exact 12,319-byte prefix. Its immutable pre-port
report has 12 passes/five failures; all 16 pinned assertions passed before fork
comparison. The five actual failures are fixed by the installed implementation.
Original discovery still preserves its Windows WMI diagnostic; exit 0 was not
clean-startup evidence. Original failed reports and Camera/Dims receipts remain
historical. No runtime build/test was restarted or excluded to pass. Offline
metadata/schema corrections are recorded separately in the readiness, root
runtime and matrix receipts.

Only generic Grid dictionary publication, aliases and tested native reentry are
accepted. Saved BaseModel descriptor-setter bypass, Grid model_copy provider
consumption, foreign colliding keys, malformed private metadata and wider
provider/subclass/reflection/copy/interpreter/worker contracts remain OPEN.
The pinned-body oracle shares installed Pydantic/events. All M0–M9/full-fixture,
plugin/provider/wheel/platform/GPU/workload and release gates remain open.
Advance actual Viewer async delivery; do not repeat this accepted packet's
matrix or create another bookkeeping checkpoint without new evidence.

### Earlier accepted checkpoint: Camera dictionary publication

Outcome: native control publishes Camera field-map replacement/deletion to its
record and detaches obsolete internal references before returning, while saved
caller maps/views/records preserve their explicit aliases and finalizers see the
published physical dictionary. Keep schemas, fields, events, copy/restore,
subclass hooks and private-map controls consistent with the pinned behavior.

The six-case `.artifacts/camera-namespace-pre-port-discovery.json` has three
field-map lifetime mismatches: generic replacement, deletion and release after
the caller drops its saved old-map alias. Three private-map controls match.
`tests/test_camera_namespace_owner.py` adds 14 finite cases: 13 comparisons with
unchanged pinned Camera/EventedModel bodies and one direct saved-record ownership
case. Preserve its original pre-port report/receipt; these next-packet failures
are excluded from the accepted Dims selectors. The oracle shares installed
Pydantic/event services and does not establish independent whole-application
parity. Capture immediate release before any facade read, GC or native refresh.
The pre-port result is nine passed, five failed, zero errors/skips. All 13 pinned
contracts pass before installed comparison; the failures are four field-map
publication/alias cases and the native saved-record retention case. Evidence is
`.artifacts/camera-namespace-pre-port-receipt.json` with its original XML/log.

Accepted phase: **integrated, built and verified within the selected publication
scope**. All recorded build/test/cold processes are terminal; full Camera/M1 and
release parity remain open. The canonical receipt is
`.artifacts/camera-namespace-checkpoint.json`.
`/root/camera_native_publication` owns `cpp/component_state.cpp` and
`cpp/include/component_state.hpp`. Its implementation exports
`install_camera_namespace(type)` and rotates the active record while preserving
saved old-record/map aliases. The previously identified exception path is repaired:
successors, watcher rows and native backlink storage are staged before physical
map publication, and predecessor observers remain available until commit.
Native backlink stores bypass user subtype assignment hooks; caller assignments
still honor those hooks. Initial linking pins both incoming objects before
allocation. Bounded independent review found no actionable defect within normal
exact-Unicode metadata/publication/provider paths. Static review does not prove
all allocation failures, foreign colliding keys or private metadata mutations.
`/root/primary_matrix_validation` owns
`tests/test_camera_namespace_owner.py` and the Camera branch of
`test_dictionary_finalizer_reentry_sees_new_owner_view` in
`tests/test_component_owner.py`. Root owns the Camera facade, native stub,
integration, builds and documentation. Preserve their edits; consult actual
worker status before changing these files or duplicating their work.

Preserve the original 14 directed cases and pre-port evidence. The stable test
file now has 20 cases: the original 14 byte-prefix unchanged plus four native-only
cases for provider conversion, guarded sync, publication finalizer reentry and
passive old-map authority, plus two subtype assignment-hook cases. The fork-only
Camera diagnostic now tests separate passive-old and active-current records.
Its original source is preserved in `.artifacts/camera-namespace-before`, and
the Grid branch stays unchanged. This diagnostic change does not weaken pinned
comparisons. Root integrated `_native.install_camera_namespace(Camera)` after
class construction and its native stub export.

Completed evidence uses fresh installed development binaries:

| Check | Actual terminal result | Report |
| --- | --- | --- |
| Main editable Qt6 build/install | Exit 0; session 50434, chunk `367680` | `.artifacts/camera-namespace-main-build.txt` |
| Complete directed Camera file | 20 passed; exit 0; session 42895, chunk `235936` | `.artifacts/camera-namespace-post-port-tests.xml` |
| Focused component/model/transaction/Dims/emitter regressions | 626 passed; exit 0; session 86916, chunk `73e31a` | `.artifacts/camera-namespace-focused-tests.xml` |
| Main affected Viewer selector | 2,577 passed; exit 0; session 58851, chunk `b3322b` | `.artifacts/camera-namespace-local-tests.xml` |
| CPython 3.11 selector | 2,429 passed; exit 0; session 71101, chunk `5ee5d5` | `.artifacts/camera-namespace-cp311-tests.xml` |
| Separate CPython 3.11 Python/Qt shutdown | 22 passed; exit 0; session 67193, chunk `90a1f8` | `.artifacts/camera-namespace-cp311-shutdown-tests.xml` |
| CPython 3.12/PyQt5 selector | 2,462 passed; exit 0; session 75824, chunk `6a0bed` | `.artifacts/camera-namespace-pyqt5-tests.xml` |
| CPython 3.12/PySide6 selector | 2,462 passed; exit 0; session 64808, chunk `27b063` | `.artifacts/camera-namespace-pyside6-tests.xml` |
| Unchanged pinned upstream selection | 1,119 passed; three original skips/two original deselections; exit 0; session 1100, chunk `933f1b` | `.artifacts/camera-namespace-upstream-tests.xml` |
| Cold installed-extension checks | All eight processes exit 0; 13 Camera checks plus inherited checks per process | `.artifacts/camera-namespace-matrix-receipt.json` |

All eight XML reports have zero failures/errors; only the upstream selection has
the three original tensorstore skips. These selections overlap; their totals
are not additive and do not establish full upstream parity.
The installed main native binary SHA-256 is
`3b424dc6709d3c7e9e992c856999f7b4171c21fbc9eb5433d0f541bd191ed552`.
The 707 stable runtime/test/helper inputs are recorded in
`.artifacts/camera-namespace-main-readiness.json`. Exact planned commands and new
report names are in `.artifacts/camera-namespace-acceptance-inventory.json`;
that inventory is a plan, not passing evidence. Actual commands, environments,
exits, seven binary fingerprints, 34 unchanged staged upstream inputs and 30
preserved reports are recorded in the terminal matrix receipt. Qt SDK/runtime
versions match main/Side 6.11.2 and Qt5 5.15.2. CPython 3.11 native Qt bridges
remain OFF. Normal/exhausted 3.12 cold modes exercise watchers/fallback; both
3.11 modes use fallback. No live build/test handle is recorded at this handoff.
Consult actual worker and process status before launching anything. Do not
rebuild or rerun these passes without changed relevant inputs or a new failure.

Bounded changed-owner review refreshed only eight inspected old stub fingerprint
sites using fresh named-case evidence; no new Camera class/method/module review
was added. Full lint passes and the source completion gate remains incomplete.
Independent core evidence reuses four historical passes for 16 unchanged inputs;
no new CTest execution or CPython-descriptor coverage is inferred. Preserve the
original Dims/Camera failures and passing reports. A saved BaseModel descriptor
setter bypass, arbitrary
foreign colliding keys, malformed private metadata and broader provider/subclass
contracts remain explicit compatibility questions; ordinary or object attribute
assignment does not prove those routes.

This completed finite acceptance inventory defines the accepted scope; it is
historical evidence, not another startup checklist:

1. All original 14 directed cases and six added native reentry/authority cases
   passed against the fresh main installed binary. Preserve pinned bodies, aliases
   and immediate finalizer timing; do not add migration skips or xfails.
2. Component owner/state, event/model/lifecycle/metadata, Dims owner/state,
   transaction/primary/emitter and actual Viewer regressions passed. The affected
   `.artifacts/run_camera_namespace_selector.py` preserves the Dims selector,
   adds all 20 Camera cases and uses new report names in every affected
   environment. No Camera case was excluded; preserve
   existing unrelated selectors and explicitly recorded baseline exclusions.
   Keep the historical Dims selector and reports unchanged. Serialize GUI suites
   and shared build directories.
3. That affected selector passed in the installed CPython 3.11 fallback environment
   and CPython 3.12 PyQt6/PyQt5/PySide6 environments. Normal/exhausted
   watcher modes passed cold probes in all four environments; the separate
   CPython 3.11 shutdown suite passed. Preserve original reports and actual terminal
   outcomes. Reuse independent C++ checks only when their complete inputs remain
   unchanged; bridges disabled in a development cell leave release parity open.
4. Changed C++ ownership, guard/provider/callback lifetimes, facades,
   dependency calls and registrations were reviewed within the stated scope;
   lint/source audit and one source/binary/report checkpoint record the actual
   evidence. Old fingerprints were refreshed only for inspected changes with
   valid named evidence; all wider milestone/release gaps remain open.

Keep full Camera/M1 and release gates open.


## Recorded iterator checkpoint and remaining gaps

The following status describes the accepted iterator checkpoint and its open
gaps. The accepted context packet and current review state are listed above; this
historical evidence does not certify the current working tree.

Native work includes dimension/camera/grid calculations, transform control and
math, slice request control, loading and scheduler decisions, event and model
helper control, and control bodies in all eight container implementation
modules. Recent batches move 15 lifecycle/transition/tuple helper bodies into
C++, then add Camera/Grid primitive field observation, native update publication
and copy/restore attachment. These are implemented responsibilities rather than
completed subsystems. The Grid packet adds application control for eight
original methods and the visibility helper, native iterator control and native
wide-integer field storage; its six reproduced gaps now match the baseline.
The Camera/Grid ownership packet moves record references into native
fields with explicit GC traversal and clearing. Their owner, dictionary and
weak-model references are read-only, so replacing a Python adapter attribute
cannot release an owner while a native observer still holds its address.
The earlier Dims packet applies native dictionary ownership, explicit GC traversal
and clearing, and read-only reference views to both Dims records. Native field
adoption also refreshes the cached current view while preserving saved views.
LayerList now also forwards 37 original control bodies to 36 handwritten native
entries, with native lazy iterator and batching scope implementations. This
moves insertion/removal, selection/linking, units/extents, event wiring and save
dispatch control; its layer/container/context/linking/IO services and authoritative
state remain partial. It does not complete LayerList or M2.

The iterator ownership packet moves suspended references for five collection/
Grid iterator types into native fields. Each has explicit type/frame GC edges,
clearing before payload finalizer reentry and a read-only frame reference.
The frame dictionary remains a live mutable diagnostic view. This closes one
bounded reference-ownership gap; collection storage authority and wider
private reflection/provider contracts remain open.

At the iterator checkpoint, the inventory contained 574 runtime/development modules and
6,155 definitions. Thirty-two modules were declared partial and 37 contained
native evidence. Source reviews cover only a small part of this inventory;
the full responsibility and dependency review must continue alongside the port.
That checkpoint had eleven valid bounded records, 6,718 responsibilities remain
unreviewed, and it had no stale records/issues. The completion check still
fails as required for an incomplete port. The recorded register was
`.artifacts/remaining-gaps-responsibility-register.json`; the unreviewed count is a
review backlog, not a count of functions that all require rewriting. The inventory
includes 197 Qt modules, 55 VisPy modules and ten builtin-plugin modules;
each still needs a responsibility-level decision rather than an automatic
whole-module conversion label.

Recorded iterator ownership verification included 40 new ownership/protocol/GC
checks, 2,315 cases in the broad main selector, 1,119 selected unchanged
upstream cases, 2,189 Python 3.11 cases and 2,200 cases per PyQt5/PySide6
selector. Three tensorstore-provider cases still skip and two mouse-event
cases remain deselected. Four existing Python-disabled CTests and eight
cold installed-extension probes also pass. These are selected Windows checks;
full upstream, plugin, platform and release parity remained open. Historical
evidence is `.artifacts/iterator-owner-checkpoint.json`; see
[validation.md](validation.md#native-iterator-reference-ownership-checkpoint).
The earlier LayerList packet is preserved in
`.artifacts/layerlist-control-checkpoint.json`.

At the iterator checkpoint, GUI-loaded broad selectors reported 48 retained
native journals, five types and 96 functions. The root trace mapped all
20 printed native addresses to public models without identifying the final
strong owners. That investigation remains historical evidence in
`.artifacts/native-lifetime-trace-assessment.json`. The current context
packet identifies and repairs the type-namespace reference and exact Qt
callback root in its verified selected processes and original menu paths.
Wider lifetime contracts and M1 remain open.

The earlier Dims evidence is preserved in `.artifacts/dims-owner-checkpoint.json`.
Its 25 ownership/cache checks and eight cold watcher checks are historical
evidence for that packet, rather than proof that subsequent application changes
have passed every release configuration.

The earlier planning assessment verified all 785 prior source fingerprints, eight test
report fingerprints and four installed native-extension fingerprints before
editing this document. It reran the responsibility audit and a focused Grid gap
probe; it did not rerun the full test matrix or change application code. See
`.artifacts/full-conversion-planning-assessment.json` for the assessment hashes
and the documentation-only difference from the implementation checkpoint.

| Remaining gap | Required result |
| --- | --- |
| Grid authority and wider compatibility | The six control differences are repaired and 202 Grid checks pass. Broader private/reflection/provider/alias/worker contracts, state authority and full dependency/release review remain open. The standalone GridLayout shape is Index-sized; public Grid calculations preserve wider integer behavior through C++ control. |
| Shared model/event/collection/transform state | Complete native authority across mutation, aliases, copies and restoration. Camera/Grid primitive views observe native/public updates in tested paths. Their records and both Dims records now own immutable GC-visible references; Dims adoption refreshes the current field-view cache. Broader Dims/Camera/Grid provider/private/subclass/fallback/worker contracts, callback/container storage and array/cache ownership remain open. |
| Layer and Viewer infrastructure | LayerList control now runs in C++; finish its native storage/selection/cache authority and dependent services. Port remaining Layer fields/transitions, feature/text/color state, scene/canvas/cursor/overlays and ViewerModel layer management. |
| Loading and editing | Port request construction and response application, native workers/cancellation/completion delivery, labels paint/fill/polygons/history, and remaining geometry/editing. |
| All layer implementations | Complete Image, Labels, Points, Vectors, Surface, Shapes and Tracks state, slicing, interaction and 2D/3D behavior. |
| Application services | Port actions/contexts, shortcuts, settings/migrations, persistence, file/session IO, notifications, startup/CLI and plugin coordination. |
| Qt and rendering | Replace the actual window/viewer/controllers and napari rendering orchestration. The existing Qt host prototype does not replace the application. |
| Utilities and release parity | Port remaining helpers and builtin-plugin implementation; remove six numerical Python fallbacks; complete responsibility reviews, full upstream fixtures, real plugin workflows and the release matrix. |
| Lifetime | The type-namespace and exact Qt callback roots are repaired in the verified selected processes and original menu reproductions. Wider worker, provider, controller and supported-matrix ownership remains open; M1 is incomplete. |

Python remains for public signatures/classes/schema metadata, plugin callbacks
and reviewed external dependency/protocol adapters. Python plugins require the
interpreter. NumPy/SciPy/Pint, array providers, Qt binding interop and any retained
VisPy backend calls need declared boundaries. Application algorithms must not
delegate back to the original napari implementations. Generated C++ and an
embedded original Python implementation do not meet this target.

## Earlier Grid compatibility gap repairs

The pre-port probe `.artifacts/grid-control-plan-probe.py` loads the unchanged
pinned Grid source and compares its public behavior with the installed fork.
Its results are saved in `.artifacts/grid-control-plan-probe.json`. The models
share installed EventedModel/imported services, so this isolates Grid bodies
rather than establishing independent event-engine or whole-application parity.
All six comparisons differed before the port. The fresh
`.artifacts/grid-control-post-port-probe.json` now records six matches:

| Reproduction | Pinned behavior | Fork behavior before this packet |
| --- | --- | --- |
| Disabled grid with a visibility property that raises | Returns `(1, 1)` without reading visibility | Reads visibility and raises |
| Validated constructor with `stride=2**80` | Accepts the stride and computes `(1, 1)` for three visible layers | Native synchronization raises `TypeError` |
| `model_construct(enabled=False, stride=0)` | `actual_shape` returns `(1, 1)` | Eager stride validation raises `ValueError` |
| `model_construct(enabled=True, stride=0)` with only hidden layers | `actual_shape` returns `(1, 1)` | Eager stride validation raises `ValueError` |
| Visibility changes between `iter_viewboxes` yields | Next contents reflect the change | Next contents use the earlier visibility snapshot |
| Subclass overrides `position` | `contents_at` calls the override | Fused native layout bypasses the override |

`cpp/grid_controller.cpp` now implements the eight original bodies and the
visibility helper, with Python signatures/schema/generator forwarding retained.
NumPy and provider primitives remain at an explicit boundary requiring full
review. Native field storage preserves wide integers in construction,
synchronization, copies/restoration and public views. The 202 new checks cover
the repaired observations, protocols, metadata and ownership; existing Grid,
state, viewer and selected upstream cases pass against installed native builds.
The independent C++ core also checks wide and minimum strides and empty input.
The standalone GridLayout's cell shape is still Index-sized and explicitly
rejects occupied wider shapes; public calculation control preserves the wider
Python API. These checks do not close full native authority or release parity.

