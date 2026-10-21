# Remaining conversion plan and checklist

This is the execution checklist for the rest of the handwritten C++17/nanobind
port. It complements the [master plan](full-conversion-plan.md), its current task
card, the [acceptance contract](conversion-acceptance.json), and the
[responsibility-review rules](conversion-review.md). It does not close a
milestone or authorize a compatibility exemption.

Keep napari **0.9.2**, commit `595639295a153c84bcb127a9e07d801355440a35`, as the
baseline until parity. Preserve the Python API, real plugins and Qt/rendering
workflows. C++ must own application algorithms, policy, canonical state and
scheduling. Python compatibility declarations and external primitives need
explicit reviewed boundaries. Generated application C++, original Python body
fallbacks and independent mutable state mirrors remain excluded.

**Execution recommendation:** keep Sol as the primary model. Use High for most
implementation, Extra High for canonical state, schema, lifetime, threading and
Qt/GPU boundaries, and Medium for settled mechanical work. Use Sol Ultra when a
workflow can give independent implementation, contracts and review owners
useful work. These settings are recommendations; this plan does not select them.

C.1-C.7, C.8.A-C.8.F, S.1-S.8 and finite Labels history/edit/publication
have installed acceptance. The [current checkpoint](../.artifacts/labels-native-edit-publication-owner-production-checkpoint.json)
passed606 distinct cases across31 processes, including9 real Viewer/canvas cases.
Accounting692/63 and the fresh audit have zero invalid records. Public before51
on the history pair and prior258 on the schema pair remain separate scopes.
Next: frontend stroke/fill/polygon and actual mouse control. Full editing/save,
cancellation/close, lifetime and all M0-M9 requirements remain open.

## Roadmap for execution

Use the rows below to choose the next complete workflow. The detailed M0-M9
checklists later in this document retain every remaining requirement. A checked
preparation step or finite packet does not close a whole milestone.

| Order | Remaining work | Completion checklist | Suggested model/effort |
| --- | --- | --- | --- |
| Alongside each phase | Acceptance decisions and release checker | Resolve the phase's blocking M0 choices; review/integrate the staged checker, freeze evidence normalization and retain actual reports from required environments | Selected Sol High for decisions; Extra High for compatibility/validator design; Medium for fixed runners |
| 1 | Labels edit history | L.1-L.10 below: source/contracts, before comparisons, native controls, actual backend/mouse acceptance and accounting | Selected Sol High; Extra High for constructor/generator/state semantics; Ultra for separate implementation/contracts/review |
| 2 | Foundations needed by the real Viewer workflow | M1-M2 and C.8.2-C.8.7: model/schema/event/container/task authority, aliases, lifecycle and actual queued delivery | Selected Sol Extra High; Ultra for owners with exclusive files |
| 3 | Complete Image/Labels workflow | M3 and necessary M5-M7 children: open, view, slice/render, edit, undo/redo, real save/reload and repeated close | Selected Sol High; Extra High for threading, cancellation, Qt and lifecycle |
| 4 | Remaining layers and services | M4-M5: Points, Vectors, Surface, Shapes, Tracks; actions, settings, IO, startup and real plugins | Selected Sol High; Ultra for independent layer/service owners |
| 5 | Complete Qt and rendering | M6-M7: application widgets/controllers/input, scene/visual policy, plugin widget interop and GPU resources | Selected Sol High; Extra High for Qt identity/destruction and GPU ownership |
| 6 | Finish all application ownership | M8: complete remaining Python policies, generic paths, module/class/state authority and reviewed dependency boundaries | Selected Sol High for implementation; Medium for a settled inventory/metadata sweep |
| 7 | Qualify the final release | M9: frozen M0 contract, full fixtures/workflows, required platform/provider/plugin matrix, fresh wheel/sdist installs, workloads, lifetime and complete audit | Selected Sol High/Extra High coordination and independent review; Medium for fixed runners |

Resolve each owner's blocking M0 decisions before editing. Complete the full M0
freeze before final release qualification. Qt/rendering, lifetime, cleanup and
real plugin checks accompany the earlier workflows; they are not deferred to
rows 5 or 7. Finish the first complete Image/Labels workflow before widening
implementation across the other layers. Required unrelated Color/Theme/schema
parents keep their own owners and exits alongside this sequence.

For each row, assign exact writable files and required owned callees before
execution. Reuse accepted work instead of reopening unchanged preparation.
The [current task card](full-conversion-plan.md#current-task-card-actual-viewer-async-slice-delivery)
selects the next tool action; the broader [execution order](#execution-order)
explains dependencies. Cleanup is part of every packet's P.9 exit.

## Model and effort guide

Keep the selected Sol model. These are project recommendations; they do not
change the active model or settings. For this migration, use High as the
implementation default and increase effort for a named unresolved decision.

| Work | Suggested model/effort | Concrete use |
| --- | --- | --- |
| Ordinary handwritten C++/nanobind implementation and integration | Selected Sol, High | A coherent owner with established interfaces and contracts |
| Canonical state, schema/class identity, GC/GIL, cancellation, Qt/GPU ownership | Selected Sol, Extra High | Resolve actual alias, error/event-order and lifecycle requirements |
| Independent implementation, contracts and correctness review | Selected Sol, Ultra where supported | One coordinator and up to three workers with exclusive files and meaningful independent tasks |
| Fixed build/test commands, inventory, formatting and settled metadata edits | Selected Sol, Medium; Luna High for narrow repeatable work where available | Run established commands and report actual outcomes; escalate semantic failures to the implementation owner |
| Independent review of an ownership-sensitive change | Selected Sol, High or Extra High | Trace source, contracts and actual evidence independently of the author |
| One demonstrated difficult blocker | Selected Sol, Max where supported | Name the unresolved contract; after two attempts without new diagnostics, change the experiment or obtain independent analysis |

[Official OpenAI model guidance](https://learn.chatgpt.com/docs/models) describes
Ultra as parallel subagent work and Max as deeper reasoning on one task. The
phase-specific choices above are our recommendation. Availability and supported
controls depend on the model, client and account. Do not substitute a different
Sol generation or assume an API effort value from a GUI label; use the chosen
model's advertised controls.

Ultra fits the source implementation/contracts/review phases. Shared integration,
builds, accounting and GUI runs remain serialized under the coordinator.
Treat Ultra as a parallel execution mode: its independent roles still need
concrete exits and appropriate reasoning effort. Use Max for a difficult single
decision, rather than as the default for the whole conversion.
Increase effort for unresolved ownership or compatibility rather than fixed tool
calls. Compare accepted workflow outcomes, rework and elapsed time before
changing the default; selecting a setting does not replace validation.

## Starting point

Snapshot: 2026-10-21. Use the latest accepted checkpoint when execution resumes;
these figures describe this snapshot rather than a conversion percentage.

| Item | Verified state |
| --- | --- |
| Current checkpoint | [Labels edit/publication finite acceptance](../.artifacts/labels-native-edit-publication-owner-production-checkpoint.json) |
| Installed behavior | 606 distinct passes/31 processes:37 new and569 retained including9 actual Viewer/canvas; zero failures/errors/skips/duplicates/drift |
| Responsibility audit | [692 valid reviews, 6193 unreviewed, zero invalid](../.artifacts/labels-native-edit-publication-owner-accepted-responsibility-register.json); inventory scope, not a conversion percentage |
| Remaining reviewed gaps | 667 partial implementations and287 tracked Python application delegations; required parents remain open |
| Review classifications | 667 partial,20 facade and5 adapter;63 narrowly reviewed dependency operations |
| Platform evidence | Installed acceptance on CPython 3.12/Windows/PyQt6; C++ syntax checks on 3.11/3.12/3.13 do not establish other installed configurations |
| Current candidate | [Tracked standalone draft](../experiments/labels_frontend/README.md): all50 bodies/seven lazy expressions, read-only binding snapshot and96 focused checks passed with archive access denied onCP312 Release. This comprises72 new snapshot cases and24 retained expression cases; earlier77 regressions and19-case3.11/3.13 harness remain separate scopes. Defining-execution authentication/publication/frame/provider gates remain open. [Current local evidence](../.artifacts/labels-native-frontend-edit-owner-canonical-snapshot-stage/progress.json). |
| Completion gates | All M0–M9 OPEN; acceptance contract is still draft and unfrozen |
| Cleanup | [No new disposable output](../.artifacts/labels-native-edit-publication-owner-production-cleanup-inventory.json);75 actual app/build/before/failure receipts;5038 unchanged genuine caches preserved |
| Accepted finite work | Shared Labels edit/write/cache13+11 and history controls have finite acceptance, retaining prior Color/Theme/schema work. Whole class/module/canonical/provider/private/lifetime/release parents remain open |

Accepted finite behavior is reusable within its source and contract limits. A
subsystem remains open until its required owned callees, state and lifecycle
have been accepted together. Moving function control into C++ while its
authoritative storage or application policy stays elsewhere is partial work.

The largest unreviewed source areas below help assign owners. These are inventory
entries, including modules/classes/nested definitions, rather than equally sized
functions or a measure of effort. The [planning gap summary](../.artifacts/remaining-conversion-plan-gap-summary.json)
contains the full subsystem grouping and acceptance decision references.

That gap summary is a historical planning snapshot. The accepted audit linked
above is the authority for the current responsibility totals.

| Source area | Unreviewed entries | Primary work |
| --- | --- | --- |
| `napari/_qt` | 1,667 | M6 application controllers, widgets and interop |
| `napari/_vispy` | 596 | M7 napari rendering orchestration |
| `napari/components` | 378 | M1/M2 shared models and viewer state |
| `napari/layers/shapes` | 318 | M4 Shapes workflow and geometry |
| `napari/utils/colormaps` | 296 | M1/M2/M7/M8 color state and policy |
| `napari/utils/events` | 293 | M1 events, collections and lifetime |
| `napari/layers/utils` | 223 | M2–M4/M8 shared layer policy |
| `napari/_vendor` | 162 | Explicit runtime ownership/dependency decisions |
| `napari/benchmarks` | 361 | Explicit tooling reviews and M9 measurement |

## Immediate execution handoff

The [current finite checkpoint](../.artifacts/labels-native-edit-publication-owner-production-checkpoint.json) accepts606 distinct installed
cases across31 processes. Reuse the accepted history/write/cache and earlier
Color/Theme/schema scopes within their contracts. The next complete owner is
frontend stroke/fill/polygon and actual mouse control, including required
helper default dispatch, property and exception policies. Complete save/reload
and lifecycle remain required workflow children.

| Next work | Suggested execution | Required result |
| --- | --- | --- |
| Review and admit frontend paint/fill/polygon/mouse closure | Sol High; Extra High generator/alias/error/lifetime review | Reuse the reviewed50-body/21-site selection,43 qualified before cases and handwritten drafts. Review canonical producer provenance, atomic handoff, full locals/tracing/release ABI and required provider policies; integrate only after effective source/ABI approval, then qualify actual Viewer behavior while retaining accepted606/31. |
| Complete Color/RGBA class, protocols, schema and module lifecycle | Sol Extra High; Max for one demonstrated hard blocker | Native authority preserves genuine aliases/subclasses, public protocols, validation, serialization and reload |
| Complete Theme/EventedModel canonical state through M1 | Sol Extra High; Ultra for disjoint owners | One authoritative storage path through construction/mutation/copy/dump/restore and observers |
| Resolve lifetime and required frame/platform contracts | Sol Extra High; Medium for fixed runners | Controlled reproductions and applicable installed evidence resolve the remaining requirements |
| Finish real Viewer delivery and Image/Labels workflow | Sol High; Extra High for GIL/cancellation/close | Real API/GUI/plugin path from open through editing/save to repeated close |

- [x] C.1 Select the Color/HSL closure and review its bounded [admission architecture](../.artifacts/theme-native-color-policy-owner-architecture-source-review.json). This source decision does not approve the full class or dependency boundary.
- [x] C.2 Qualify [34 real-provider public cases on the original](../.artifacts/theme-native-color-owner-thirty-four-original-before-terminal.json), freeze the 37-case fixture and accept its [independent review](../.artifacts/theme-native-color-owner-contracts-source-review.json). The three native-only cases have no fabricated original baseline.
- [x] C.3 Correct all four initial source findings in the [reviewed corrected stage](../.artifacts/theme-native-color-policy-owner-corrected-source-review.json); add reviewed regressions and qualify their [eight public cases on the original](../.artifacts/theme-native-color-owner-review-regressions-eight-original-before-terminal.json). Preserve the initial rejected stage/findings. Correct the later path=None binding defect under its [independent review](../.artifacts/theme-native-color-policy-owner-runtime-binding-source-review.json).
- [x] C.4 Obtain independent effective native/metadata/admission and [regression-contract approval](../.artifacts/theme-native-color-owner-review-regressions-contracts-source-review.json), preserving real finders/loaders, aliases, builtins, RGBA sharing and tuple snapshots.
- [x] C.5 Integrate the four reviewed targets, accept the [nine-target compatibility guards](../.artifacts/theme-native-color-owner-runtime-fixed-compatibility-source-review.json) with held assertions/selectors, and complete the [final installed build](../.artifacts/theme-native-color-owner-path-none-fixed-native-build-terminal.json). Preserve the pre-fix build, failed installed37 attempt and binary snapshots.
- [x] C.6 Run 37 Color, 11 focused regression and 164 affected prior cases on the same final pair. The [independent actual evidence review](../.artifacts/theme-native-color-owner-runtime-fixed-installed-evidence-review.json) confirms 212 distinct passes across 18 processes, zero required failures/errors/skips/duplicates/drift. Original34 and Original8 are reused, not replayed.
- [x] C.7 Review per-process lifetime diagnostics, inventory disposable output after all jobs settle, apply independently reviewed accounting and produce the [604-review/37-operation audit](../.artifacts/theme-native-color-owner-runtime-fixed-accepted-responsibility-register.json). Update checkpoint/card once; clean lifetime and complete parent ownership remain unproven.
- [ ] C.8 Finish the remaining same-parent class, canonical-state, schema, private/module and lifecycle requirements through M1. Do not declare Color, Theme or a milestone complete from C.1-C.7 alone.

Execute the following C.8 substeps in dependency order. Give each implementation
packet one concrete exit; carry required unresolved children on this same parent.

- [x] C.8.1 Assign the [remaining closure and exact staged files](../.artifacts/theme-native-color-class-owner-selection.json), with separate Color/RGBA and Theme/EventedModel owners, [reviewed architecture boundaries](../.artifacts/theme-native-color-class-owner-architecture-decision.json) and frozen focused contracts. The actual before-class comparisons passed: [40 main cases](../.artifacts/theme-native-color-class-owner-forty-before-class-current-accepted-terminal.json), [three PEP479 cases](../.artifacts/theme-native-color-class-owner-pep479-three-before-class-current-accepted-terminal.json) and [five correction cases](../.artifacts/theme-native-color-class-owner-review-regressions-five-before-class-current-accepted-terminal.json). These 48 cases compare against the current accepted implementation; five native-only witnesses have no before result. This closes assignment/baseline preparation only. Suggested effort: Sol Extra High; Ultra for independent contracts/review.

The immediate finite packet covers 21 named policies, seven nested expression
policies, two named generators and exact-class allocation. Run these gates first;
its acceptance closes only the evidenced subset of C.8.2-C.8.4.

- [x] C.8.A Accept the [corrected effective source review](../.artifacts/theme-native-color-class-owner-corrected-source-review.json), three focused fixture reviews and [ten-target compatibility review](../.artifacts/theme-native-color-class-owner-compatibility-source-review.json), all PASS_SOURCE_ONLY with no findings. Preserve the initial rejected stage and CC1-CC3 findings as history. Reuse the actual 48 public comparisons.
- [x] C.8.B Root integrates only the four frozen corrected targets: `cpp/theme_color_publication.cpp`, `src/naparixx/_native.pyi`, `src/naparixx/_color_policy.py` and new `src/naparixx/_color_class.py`. Check exact patches/inverses and held Theme/model sources; bind reviewed compatibility copies without changing assertions or selectors. Suggested effort: Sol High.
- [x] C.8.C Preserve the accepted binary pair and build the installed native/Qt extensions once with the constrained CP312/Windows/PyQt6 toolchain. Bind installed fixture guards to the actual new source/inventory/binary pair. Suggested effort: Sol Medium for fixed commands; High for build failures.
- [x] C.8.D Run the 43 main, four PEP479 and six correction cases first, then the necessary 212 prior cases on that same final pair. The forecast is 265 distinct installed cases across 21 processes; only actual terminal/XML evidence can establish acceptance. Preserve any failure and correct its cause before downstream acceptance. Suggested effort: Sol Medium runners; High/Extra High for failures.
- [x] C.8.E Obtain independent review of the actual case union, unchanged contracts and separate per-process lifetime diagnostics. After jobs settle, inventory disposable assets while preserving required build/provider/cache/evidence files. Suggested effort: Sol High review; Medium inventory.
- [x] C.8.F Apply independently reviewed responsibility/dependency accounting, refresh the source audit and publish one finite checkpoint/card update. Keep full class/subclass publication, schema factories, module authority, Theme canonical storage and lifetime/platform exits open until separately proved. Suggested effort: Sol High coordination; Medium bookkeeping.

[The current finite checkpoint](../.artifacts/theme-native-color-class-owner-production-checkpoint.json)
closes C.8.B-C.8.F: [actual265/21 evidence](../.artifacts/theme-native-color-class-owner-installed-evidence-review.json),
[accounting peer](../.artifacts/theme-native-color-class-owner-accounting-source-review.json),
[current628/42 audit](../.artifacts/theme-native-color-class-owner-accepted-responsibility-register.json)
and [cleanup27](../.artifacts/theme-native-color-class-owner-production-cleanup-inventory.json).
The two guard-only setup failures and rejected source stage remain preserved.

### Accepted schema factory control

This finite packet within C.8.4 is accepted. Its frozen stage covers actual
`to_string_ser_schema`, `with_info_plain_validator_function`, `_dict_not_none`
and its dictionary-comprehension policy. The 74 other factory bodies, full
schema-generation/model authority and module lifecycle remain required.

| Role | Ownership | Suggested effort |
| --- | --- | --- |
| C++ implementation | `theme_validate_assignment_native`: frozen stage only; CPP, stub, `_color_policy.py`, new `_color_schema.py` | Sol High; Extra High for warning/frame/alias/cleanup semantics |
| Independent source reviewer | `colormap_map_tests`: effective native and metadata review; no production edits | Sol Extra High |
| Contracts and compatibility preparation | `owned_policy_fixture_review`: fixture/guard copies and proposed accounting, preserving behavioral assertions | Sol High |
| Coordinator | Root: shared integration, build, installed runs, actual evidence and accounting/docs publication | Sol High decisions; Medium fixed runners |

Execute in this order. A checked preparation step closes only that preparation.

- [x] S.1 Freeze the bounded architecture and contracts. The [architecture review](../.artifacts/theme-native-schema-factory-owner-architecture-source-review.json) and [independent contract review](../.artifacts/theme-native-schema-factory-owner-contracts-source-review.json) passed for their source-only scopes.
- [x] S.2 Qualify the [30 actual public baseline cases](../.artifacts/theme-native-schema-factory-owner-thirty-before-schema-current-class-terminal.json) on the accepted class pair and preserve its [binary snapshot](../.artifacts/theme-native-schema-factory-owner-before-binary-snapshot/index.json). The two native-only witnesses have no fabricated before result.
- [x] S.3 Freeze the [four staged targets and exact patches/inverses](../.artifacts/theme-native-schema-factory-owner-stage/ready.json). Three-version C++ syntax, metadata compile and scoped lint checks passed; production and installed runtime are unchanged.
- [x] S.4 Complete independent effective native-source review and compatibility review for all 11 source roles. Resolve findings before integration. Preserve existing 265-case assertions/selectors and the three permitted compatibility filename mappings; carry held-source custody separately from the four changed native targets. Suggested effort: Sol Extra High review; High compatibility preparation.
- [x] S.5 Integrate only the four reviewed targets; verify exact forward/inverse patches and held sources. Reuse the constrained build runner, build the installed native/Qt pair once, and bind all guards to actual source/inventory/binary identities. Suggested effort: Sol High integration; Medium established build commands.
- [x] S.6 Run the full 32-case schema fixture first, then the 265 affected prior cases on that same final pair. The forecast is 297 distinct cases across 22 processes; actual terminal/XML results must establish the union. Preserve failures and fix their cause before acceptance. Suggested effort: Sol Medium runners; High/Extra High for semantic failures.
- [x] S.7 Independently verify the actual case union and separate per-process lifetime diagnostics. After jobs settle, inventory and remove verified disposable output while preserving required assets, genuine caches, sources, stages and evidence. Suggested effort: Sol High review; Medium cleanup.
- [x] S.8 Apply independently reviewed responsibility/dependency accounting, refresh the audit and publish one finite checkpoint/card update. Preserve existing operation scopes; review schema-specific warning emission and declaration/literal metadata transport separately. Keep full class/schema/module/canonical-state/lifetime/platform and all M0-M9 gates open until proved. Suggested effort: Sol High coordination/review; Medium settled bookkeeping.

[The schema checkpoint](../.artifacts/theme-native-schema-factory-owner-production-checkpoint.json)
closes S.4-S.8 within their finite scope: [actual297/22 evidence](../.artifacts/theme-native-schema-factory-owner-installed-evidence-review.json),
[accounting review](../.artifacts/theme-native-schema-factory-owner-accounting-source-review.json),
[fresh633/44 audit](../.artifacts/theme-native-schema-factory-owner-accepted-responsibility-register.json)
and [cleanup24](../.artifacts/theme-native-schema-factory-owner-production-cleanup-inventory.json).
Both evidence-checker and producer setup failures remain preserved. Broader
schema74/class/module/canonical/private/lifetime/platform requirements stay open.

The next real workflow owner is [Labels edit-history13](../.artifacts/labels-edit-history-owner-stage/ready.json).
Its [architecture review](../.artifacts/labels-edit-history-owner-architecture-source-review.json)
passed for source-only scope. Preserve the genuine NamedTuple constructor,
context-manager function and generator aliases, with C++ owning the admitted
bodies and the existing Layer dictionary as the sole state authority. Source
architecture approval does not establish implementation or runtime acceptance.

### Accepted finite packet: Labels edit history

The 13 selected methods cover reset/staging/strokes, save/load history, masked
atom creation/replay and undo/redo. Full paint/fill/polygon operations, Labels
class/module ownership, serialization and the complete M3 workflow remain open.
Required NamedTuple-generation and contextlib provider-policy ownership also
remain open; finite constructor/generator control does not approve those helpers.
The initial rejected stages remain preserved. The
[effective native source review](../.artifacts/labels-edit-history-owner-runtime-stdlib-effective-source-review.json)
and [compatibility review](../.artifacts/labels-edit-history-owner-compatibility-source-review.json)
passed for the corrected scope. The
[four-file integration](../.artifacts/labels-edit-history-owner-production-integration.json)
and [installed build](../.artifacts/labels-edit-history-owner-production-native-build-terminal.json)
completed. The current binaries are the Labels candidate; the accepted schema
pair remains in its saved snapshot. Installed acceptance is still pending.

| Role | Exclusive responsibility | Suggested effort |
| --- | --- | --- |
| Implementation | Stage only: `cpp/labels.cpp`, `_native.pyi`, new `_labels_history.py` and Labels bootstrap proposals | Sol High; Extra High for source-order, constructor/generator and canonical-state decisions |
| Contracts | Focused fixture and contract interface, including real backends and mouse/generator/error cases | Sol High; Extra High for ownership/error-order cases |
| Independent reviewer | Architecture, effective staged sources and contracts; no author edits | Sol Extra High |
| Coordinator | Provider locks, baselines, production integration, build, installed runs, evidence/accounting and cleanup | Sol High decisions; Medium established commands |

The preparation steps below are complete and reusable. The five admission and
seven factory-scope public comparisons have actual independent evidence reviews
([admission](../.artifacts/labels-edit-history-owner-admission-path-before-evidence-review.json),
[factory scope](../.artifacts/labels-edit-history-owner-factory-scope-before-evidence-review.json)).
Earlier failed setup attempts remain preserved. This six-step table records
completed finite history acceptance; the next-action authority below selects
shared edit/write/cache publication. Broader L.8 requirements remain open.

| Step | Concrete work and dependency | Exit | Suggested effort |
| --- | --- | --- | --- |
| 1, complete | Qualify corrected admission and factory-scope public baselines |258 actual before passes; reuse the saved schema pair and preserved failures | Sol Medium runners; High for setup failures |
| 2, complete | Implement genuine function/constructor provenance, generator transport and finite scope controls | Source review passed; runtime aliases/reentry/thread/GC behavior still requires installed evidence | Sol Extra High; Max only for a demonstrated single blocker |
| 3, complete | Compose/check four corrected targets and obtain native source review | Exact patches/inverses, static checks and clean effective source review | Sol High composition; Extra High review |
| 4, complete | Review15-source compatibility, then integrate/build and bind actual runtime guards | Independent compatibility/runner review;297 prior assertions/selectors held; actual build exit0 and installed pair bound | Sol High integration; Medium settled build/metadata commands |
| 5, complete | Run56 focused,4 admission,2 constructor,9 factory-scope,192 upstream,297 prior and9 real Viewer/canvas cases on the final pair | Actual569 distinct cases/28 processes independently verified; lifetime diagnostics remain separate | Sol Medium runners; High evidence review; Extra High semantic/lifetime failures |
| 6, complete | Complete independent evidence/accounting review, cleanup and checkpoint | Finite Labels exit accepted; broader Labels/class/module/save/lifetime and M0-M9 requirements still tracked | Sol High review/coordination; Medium bookkeeping |

Application/build jobs remain serialized. Extra regression cases for new restoration/reentry/thread behavior
must be defined before candidate execution when that behavior changes. A case
forecast is a runner plan, not a passed result.

- [x] L.1 Freeze the [13-method selection/design](../.artifacts/labels-edit-history-owner-stage/ready.json) and obtain the [independent architecture review](../.artifacts/labels-edit-history-owner-architecture-source-review.json). All selected method ASTs match the pinned source; source-only approval preserves the single Layer dictionary and genuine constructor/generator aliases.
- [x] L.2 Lock and qualify the [real provider profile](../.artifacts/labels-edit-history-provider-profile.json) on CP312. [TensorStore 0.1.85](../.artifacts/labels-edit-history-tensorstore-install-terminal.json) and [ml_dtypes 0.5.0](../.artifacts/labels-edit-history-ml-dtypes-install-terminal.json) are isolated; actual NumPy/Dask/Zarr/xarray/TensorStore imports and a TensorStore read round-trip passed. Accepted inputs/binaries and the 17 prior TensorStore files remained unchanged.
- [x] L.3 Freeze the [focused contracts](../.artifacts/labels-edit-history-owner-contracts-ready.json), obtain [independent source review](../.artifacts/labels-edit-history-owner-contracts-source-review.json) and qualify the before comparisons on the accepted schema pair. Actual [192 unchanged upstream edit/history/backend/mouse cases](../.artifacts/labels-edit-history-owner-upstream-before-current-schema-terminal.json) and [54 focused public cases](../.artifacts/labels-edit-history-owner-focused-public-before-current-schema-terminal.json) passed, zero failures/errors/skips/drift; their case IDs are disjoint. The [saved pair](../.artifacts/labels-edit-history-owner-before-binary-snapshot/index.json) preserves this before state. The two installed-only witnesses were explicitly deselected and have no fabricated before result. This is before-Labels comparison against the current accepted application, since the whole fork is already modified; it closes preparation only.
- [x] L.4 Freeze the [corrected13 controls, constructor admission and generator transport](../.artifacts/labels-edit-history-owner-runtime-stdlib-stage/implementation-ready.json), exact four-target patches/inverses, source profiles and static checks. The existing Layer dictionary/queues/staged list/block flag remain sole state authority. This closes source preparation only. Suggested effort: Sol High; Extra High for semantic boundaries.
- [x] L.5 Obtain [effective native/metadata review](../.artifacts/labels-edit-history-owner-runtime-stdlib-effective-source-review.json), [15-source compatibility review](../.artifacts/labels-edit-history-owner-compatibility-source-review.json) and [runtime-binding source review](../.artifacts/labels-edit-history-owner-runtime-binding-source-review.json). All are source-only passes; assertions/selectors and rejected stages remain preserved. Suggested effort: Sol Extra High review; High compatibility preparation.
- [x] L.6 Preserve the accepted [before pair](../.artifacts/labels-edit-history-owner-before-binary-snapshot/index.json), [integrate four reviewed targets](../.artifacts/labels-edit-history-owner-production-integration.json) and complete the [actual installed build](../.artifacts/labels-edit-history-owner-production-native-build-terminal.json) with no source drift. Runtime acceptance remains pending. Suggested effort: Sol High integration; Medium established build commands.
- [x] L.7 Bind actual final guards and pass56 focused,4 admission,2 constructor,9 factory-scope and192 unchanged upstream backend/mouse cases on the installed history pair. The [independent effective review](../.artifacts/labels-edit-history-owner-installed-effective-evidence-review.json) also includes9 actual Viewer/canvas cases. Native-only witnesses have no fabricated before result.
- [ ] L.8 The297 affected prior cases and9 actual Viewer/canvas cases passed in the [569-case/28-process review](../.artifacts/labels-edit-history-owner-installed-effective-evidence-review.json); per-process diagnostics are separate. The [bounded persistent-root/one-reload observation](../.artifacts/labels-edit-history-owner-lifetime-followup-review/bounded-runtime-observation-review.json) passed its specified GC edges and retired-generation release. Complete remaining actual Viewer cancellation/close, all-field/error/reentry and GUI/interpreter lifetime exits through the same workflow parent. Functional passes do not close these requirements. Suggested effort: Sol Extra High for unresolved lifecycle/threading.
- [x] L.9 [Effective cleanup](../.artifacts/labels-edit-history-owner-viewer-qualified-production-cleanup-inventory.json) found no new disposable output after39 settled terminals;5038 genuine caches remain unchanged. Apply [independently reviewed accounting](../.artifacts/labels-edit-history-owner-accounting-source-review.json) and the [fresh audit](../.artifacts/labels-edit-history-owner-accepted-responsibility-register.json):664 valid records/49 operations,0invalid; all31 added records remain PARTIAL.
- [x] L.10 Publish the [finite Labels-history checkpoint](../.artifacts/labels-edit-history-owner-production-checkpoint.json) and select shared edit/write/cache publication as the next owner. Full Labels/class/module/state/save/provider/private/lifetime and M0-M9 parents remain OPEN, including L.8.

The remaining parent requirements follow after this finite acceptance.

- [ ] C.8.2 Complete Color/RGBA class/metaclass, slot/descriptor/allocation and native authoritative state, preserving sharing and source-defined tuple snapshots through mutation, copy/pickle/reflection and reload generations. Suggested effort: Sol Extra High.
- [ ] C.8.3 Complete the unresolved same-parent requirements for Color/Representation protocols and required owned helpers. Reuse the accepted native bodies for as_named/as_hex/as_hsl/as_hsl_tuple, __str__/__repr_args__/__eq__/__hash__ and rgb_to_hls within their evidenced scope; preserve direct aliases, subclasses, live tables/callbacks and exact error/order behavior. Do not report these accepted bodies as unported or treat them as full parent acceptance. Suggested effort: Sol High; Extra High for protocol conflicts.
- [ ] C.8.4 Complete Color _validate/core-schema/JSON-schema and serialization callbacks plus regex/name/reverse tables, repeat_colors/rads and module initialization/publication/reload/reentry ownership. Retain only independently reviewed external primitives and Python metadata. Suggested effort: Sol Extra High.
- [ ] C.8.5 Complete separate Theme/EventedModel canonical __dict__/fields-set/extra/private authority through allocation, validation, set/delete, copy/dump/restore and genuine plugin subclasses. Preserve aliases and publication before observers. Suggested effort: Sol Extra High; Ultra for disjoint implementation/contracts/review.
- [ ] C.8.6 Resolve required private/frame/closure, GC/reference ownership, interpreter/shutdown and applicable platform contracts using controlled actual-object/Qt reproductions. Preserve separate lifetime diagnostics until their causes are demonstrated and fixed. Suggested effort: Sol Extra High; Max for one hardest unresolved blocker.
- [ ] C.8.7 Accept the complete parent only after its required owned callees, authoritative state and lifecycle pass together on the applicable installed profile; update accounting and same-parent exits without closing unproven M0-M9 gates. Suggested effort: Sol High coordination/review; Medium for fixed build/test commands.

The earlier Theme template packet is accepted at its [historical checkpoint](../.artifacts/theme-native-theme-template-owner-production-checkpoint.json).
Its 164 affected cases also passed on the current pair. The following checked
items record that finite acceptance; wider parent requirements remain open.

- [x] N.1 Select the complete sixteen-body Theme packet and record its [blocking M0 source decisions](../.artifacts/theme-native-theme-template-owner-m0-decisions.json). Four narrow primitive operations now have finite installed evidence and independent accounting review; wider compatibility and Color-provider decisions remain open.
- [x] N.2 Qualify [23 public contracts](../.artifacts/theme-native-theme-template-owner-twenty-three-original-before-terminal.json) and [seven additional unchanged upstream Theme cases](../.artifacts/theme-native-theme-template-owner-seven-upstream-before-terminal.json) on the original implementation. Both passed; the two installed-only native witnesses have no fabricated original baseline.
- [x] N.3 Stage the handwritten implementation and pass CPython 3.11/3.12/3.13 C++ syntax checks. Obtain the [independent fixture review](../.artifacts/theme-native-theme-template-owner-contracts-source-review.json); it passed for its finite source-only scope.
- [x] N.4 Accept the [independent effective native-source review](../.artifacts/theme-native-theme-template-owner-source-review.json), including the corrected exception/cleanup order. It passed with no findings, followed by the installed acceptance below.
- [x] N.5 Integrate the three reviewed production targets, verify their exact forward/inverse patches, bind fresh source/fixture/compatibility guards, and build the installed Qt6/nanobind extension with the existing constrained toolchain.
- [x] N.6 Run all 25 new installed cases and necessary affected prior coverage against that same installed binary pair. Include the seven newly covered upstream cases in the expanded upstream selection. The actual union is 164 distinct cases across 16 processes, verified by the [installed evidence review](../.artifacts/theme-native-theme-template-owner-installed-evidence-review.json).
- [x] N.7 Review actual runtime results, separate per-process lifetime diagnostics and source/dependency ownership. Once every process has settled, inventory cleanup against the accepted cleanup cutoff above. Update accounting and the task card once the finite packet is accepted, preserving all failures and remaining parent requirements.
- [ ] N.8 Reuse accepted Color constructor/parser/storage/rounding/formatting control (C.1-C.7); finish remaining class/protocol/schema/module/canonical/lifecycle ownership via C.8 and shared M1 foundations. Keep the Theme parent OPEN until all required exits pass.

Use Sol Extra High for remaining N.8/C.8 canonical ownership and lifecycle work;
use High for settled method/protocol implementation and integration. Ultra suits disjoint
implementation/contracts/review work. Fixed build/test commands can use Medium;
interpreting a semantic failure belongs with the relevant owner at High or
Extra High. Serialize shared builds and application/GUI runs.

If a review, guard, build or behavior check fails, preserve its evidence, correct
the affected scope and resume at that gate. Do not proceed to acceptance with an
unresolved failure or restart passed preparation for unchanged inputs.

## Current packet checklist: shared Labels edit/write/cache publication

This completes the common write/publication path used by paint, fill and polygon
editing. Its13 named controls and11 expressions preserve the actual Layer
namespace, backend data and raw/view cache arrays. History-before-write,
backend-write-before-cache, event error/reentry, contour halos, batching and
literal `refresh is True` behavior are required. Frontend geometry/mouse policy
and real reader/writer save/reload remain explicit required workflow children.

- [x] E.1 Freeze the [13-control/11-expression selection and contracts](../.artifacts/labels-native-edit-publication-owner-stage/ready.json), with the [independent architecture/contracts peer](../.artifacts/labels-native-edit-publication-owner-contracts-source-review.json). Source-only approval covers the two defining namespaces and required aliases/authority; no native ABI or runtime outcome is assumed. Suggested effort: Sol High; Extra High ownership review.
- [x] E.2 The [independent before reviews](../.artifacts/labels-native-edit-publication-owner-installed-evidence-index.json) qualify51 distinct current-history cases:12 focused,20 new upstream,4 LEP1,6 reused backend and9 reused Viewer/canvas. Installed-only1 has no fabricated before. Prior history258 remains a separate scope.
- [x] E.3 Handwritten C++ owns all13 selected policies and11 expressions with two-module genuine-function admission, minimal escaping captures, GC-visible locals and actual G/B/alias transport. Broader private/frame/lifetime parents remain OPEN.
- [x] E.4 [Corrected native review](../.artifacts/labels-native-edit-publication-owner-native-review/corrected-source-review.json), [compatibility review](../.artifacts/labels-native-edit-publication-owner-compatibility-review/source-review.json) and [runtime-binding review](../.artifacts/labels-native-edit-publication-owner-runtime-binding-review/source-review.json) passed. Original LEP1 HOLD and its exact correction remain preserved; old assertions/selectors held.
- [x] E.5 [Five reviewed targets integrated](../.artifacts/labels-native-edit-publication-owner-production-integration.json), constrained build passed and actual17-role guards bind the new installed pair. The previous pair is preserved.
- [x] E.6 The [actual installed review](../.artifacts/labels-native-edit-publication-owner-installed-review/installed-evidence-review.json) verifies606 distinct cases/31 processes:37 new and569 retained, including9 actual Viewer/canvas cases; zero failures/errors/skips/duplicates/drift. Diagnostics remain per process.
- [x] E.7 [Cleanup](../.artifacts/labels-native-edit-publication-owner-production-cleanup-inventory.json) covers75 settled receipts with no new junk/deletions; independently reviewed accounting and [fresh audit](../.artifacts/labels-native-edit-publication-owner-accepted-responsibility-register.json) give692 valid reviews/63 operations,0invalid. Publish the [finite checkpoint](../.artifacts/labels-native-edit-publication-owner-production-checkpoint.json); broader parents remain OPEN.
- [ ] E.8 Continue the same real editing/save/reload/close workflow through frontend geometry/mouse, plugin IO and lifetime/cancellation exits. Keep all M0-M9/full parent gates open until proved. Suggested effort: Sol High; Extra High lifecycle/interop.

## Next-action authority

The master card is authoritative. Reuse the [606-case finite checkpoint](../.artifacts/labels-native-edit-publication-owner-production-checkpoint.json)
and its accepted history/write/cache/helper dependencies. Next freeze frontend
stroke/fill/polygon and actual mouse ownership/contracts, then implement its
handwritten native policies. E.8 and L.8 retain the complete save/reload/close,
cancellation, canonical/class/module/provider/private/lifetime/platform exits.
Do not repeat accepted preparation or claim whole Labels/M0-M9 completion.

## Execution order

The sequence below follows the existing M0–M9 gates. Resolve the M0 decisions
that block an upcoming owner before implementing it; finish the complete M0
freeze before release qualification. Run the actual Image/Labels viewer workflow
throughout the foundation and layer work, so GUI, plugin and rendering problems
surface while their owners are being implemented.

| Priority | Work | Dependencies and concrete exit | Suggested execution |
| --- | --- | --- | --- |
| 0 | M0: acceptance and boundaries | Name the required contracts, real baseline oracles, provider/plugin profiles, matrix rows and tolerances. Resolve decisions blocking the current owner; retain other required rows until verified. | Sol High; Extra High for architecture or compatibility conflicts |
| 1 | Labels frontend editing and concrete workflow blockers | Reuse accepted history/write/cache/schema/Theme/Color dependencies. Complete frontend/helper/property/exception/mouse control and actual Viewer/Image/Labels blockers. Keep required class/schema/Theme parents assigned. | Sol High; Extra High for ownership; Ultra for independent roles |
| 2 | M1: canonical foundations and lifetime | Finish authoritative model, event, collection, transform and cache ownership. Preserve live aliases, schema/subclasses, validation, copies/restoration, reentry and destruction. | Sol Extra High; Ultra for disjoint implementation/test/review roles |
| 3 | M2: Layer/Viewer base and actual delivery | Complete shared state transitions and actual queued slice delivery through the viewer, canvas and plugin widgets. | Sol High; Extra High for ownership or event-order failures |
| 4 | M3: Image/Labels end-to-end | Open → add/select → slice/render → paint/fill → undo/redo → save → close, including lazy/multiscale data, stale results and close while loading. | Sol High; Extra High for task/cancellation/lifetime design |
| 5 | M4 remaining layers and M5 services | Complete Points, Vectors, Surface, Shapes and Tracks; implement IO, settings/actions/startup and plugin coordination. Separate independent owners after shared foundations are stable. | Sol High; Ultra for independent layer or service owners |
| 6 | M6 Qt application and M7 rendering | Finish native application controllers/widgets/input and napari rendering orchestration, with real Python widget/backend interop and GPU lifecycle evidence. Integrate these incrementally during earlier steps. | Sol High; Extra High for Qt identity/destruction and GPU boundaries |
| 7 | M8 remaining application Python | Sweep the entire audited inventory, remaining generic numerical branches, builtin plugins, helpers and policy inside adapters. | Sol Medium for inventory/mechanical work; High for implementation |
| 8 | M9 final release parity | Freeze M0, close M1–M8, run full original fixtures/real workflows and fresh wheel/sdist installations on the required matrix, and verify lifetime and workloads. | Sol High coordination/review; Medium for fixed-command runners |

Within M1, assign distinct Color/RGBA slot and class ownership and
Theme/EventedModel storage ownership. Color derives from the provider's
Representation class; RGBA has its own slots. Shared foundations must preserve
those actual representations rather than merging them into EventedModel.
Prioritize the state, event, transform/cache and task contracts that actually
block Viewer delivery and Image/Labels. Integrate lifetime/GC and real Viewer
checks with each owner; unrelated required schema/private/platform work may
proceed alongside the workflow with exclusive ownership. Record the concrete
dependency before delaying delivery for a prerequisite. Native Qt interop and
rendering checks accompany Image/Labels throughout. Complete that first whole
workflow before spreading implementation across the remaining layers/services.

Parallel work must have exclusive file/state ownership. Use at most the current
four available agent slots: coordinator, C++ implementation, contracts/tests and
independent review. The coordinator serializes shared integration, builds and
application/GUI runs. Independent platform jobs require separate environments;
do not assume those environments already exist.

## M0 — Contracts and interoperability

Owner: coordinator with an independent compatibility reviewer. Exit: frozen,
validated acceptance contract and demonstrated real widget/canvas interop.

- [ ] M0.1 Assign every audited runtime responsibility to a subsystem owner and milestone, including module initialization, fields/state, callbacks, decorators and retained dependency operations.
- [ ] M0.2 Freeze required public APIs, events, signatures, exceptions, mutable views, serialization, subclass/plugin hooks and documented fork differences.
- [ ] M0.3 Resolve required private/reflection/frame/closure behavior from actual upstream fixtures and representative plugins; record explicit compatibility differences and keep unresolved required contracts open.
- [ ] M0.4 Reconcile required CPython 3.11-3.14 and the 3.15+ admission policy, Windows/Linux/macOS, architectures and applicable PyQt5/PyQt6/PySide6 combinations against the pinned contracts. Preserve the provisional 34-row required floor retaining 32 configured CI modes; this is not an exhaustive Cartesian matrix. Freeze exact patch/interpreter/ABI per cell; source-backed applicability must not silently remove requirements.
- [ ] M0.5 Freeze compiler, nanobind, Qt SDK/runtime, dependency/provider versions, artifact hashes and minimum/latest applicable profiles; retain configured full/headless/example scopes and native bridge requirements.
- [ ] M0.6 Freeze real reader, writer, command and dock-widget plugin distributions, versions and workflows, including required NPE2 and legacy-engine paths.
- [ ] M0.7 Freeze required NumPy/Dask/Zarr/TensorStore/xarray and applicable Torch/Numba/geometry provider contracts: dtype/layout/strides, laziness, multiscale, alias/copy, serialization and error behavior.
- [ ] M0.8 Define deterministic workflow datasets, baseline case IDs/fixture hashes, ordered event traces and numerical/image output oracles before candidate measurements.
- [ ] M0.9 Freeze GPU/driver/OpenGL/display/software-renderer applicability and image tolerances; freeze performance/time/memory measurement methods and justified thresholds. Do not assume a universal speedup.
- [ ] M0.10 Demonstrate actual viewer/event-loop/canvas identities, Python-created plugin widgets and repeated close under the applicable Qt bindings.
- [ ] M0.11 Resolve all twelve named unresolved_choices using the owner/exit/dependency rows below, and implement the composite release evidence validator; a structurally valid draft is insufficient for freezing.
- [ ] M0.12 Pass `python -B tools/check_conversion_acceptance.py --check-frozen` only after all required prerequisites are actually resolved.

### Release checker preparation

The corrected checker and portable tests have been integrated as
[tools/check_conversion_release.py](../tools/check_conversion_release.py) and
[tests/test_conversion_release.py](../tests/test_conversion_release.py).
[All38 production tooling tests passed](../.artifacts/release-evidence-validator-production-tooling-tests-terminal.json);
the actual CLI correctly rejects the missing final index. This establishes
finite tooling behavior. M0 normalization/transport decisions and actual final
release reports remain open.

- [x] R.1 Obtain [independent corrected checker review](../.artifacts/release-evidence-validator-final-review/review.json) and [portable setup review](../.artifacts/release-evidence-validator-portable-review.json), with38 pure tooling tests. Rejection/custody/coverage/type checks passed for the proposed schema; production integration, unresolved M0 normalization and actual release reports remain required. Suggested effort: Sol Extra High.
- [ ] R.2 Resolve and freeze exact case/cell/ABI/lock/oracle and workload-result normalization. Define retained payload and origin verification for evidence produced on another platform; an unavailable required environment remains an open gate. Suggested effort: Sol Extra High; High for source-backed profiles.
- [ ] R.3 Reviewed tooling integration and38 production tests passed. Finish binding the final `python -B tools/check_conversion_release.py` invocation and evidence schema to resolved M0 normalization/transport decisions; these remain open. Suggested effort: Sol High integration/review; Medium fixed commands.
- [ ] R.4 Collect and independently validate actual final-source reports from every required environment, full fixture/workflow, distribution, lifetime/GPU and workload gate. Publish docs/conversion-release-evidence.json only from qualified results; finish M9 before changing completion flags. Suggested effort: Sol High coordination/review; Extra High unresolved release failures.

R.1-R.3 can proceed alongside application conversion with separate file ownership.
R.4 depends on the complete final application and frozen acceptance decisions.

## M0 decision owners and exits

All twelve choices below remain OPEN in the acceptance contract. These are
responsibility roles; assign the available agent and exact files when the
dependent packet starts. Record each resolution in its matching acceptance
choice, with source references and an actual decision/workflow/matrix report.
Resolve the current packet's blocking choices first. Freeze datasets,
tolerances and measurement methods before candidate measurements.

Use Sol High for source-backed locks and compatibility decisions; use Extra
High for interpreter/ABI, Qt/GPU, provider/plugin or validator conflicts.
Use Medium for settled collection/lock extraction and fixed runners.

| Acceptance choice | Owner role | Concrete resolution artifact or evidence | Blocks/dependencies |
| --- | --- | --- | --- |
| `python-support-boundary` | platform/release owner | Exact CPython patch/ABI locks per required 3.11-3.14 cell; explicit 3.15+ admission decision | M0.4, M9.3 |
| `os-and-architecture-applicability` | platform/Qt reviewer | Immutable OS/architecture/toolchain profiles and justified Qt/provider applicability for each required cell | M0.4-M0.5, M6, M9.3 |
| `qt-version-and-sdk-locks` | native Qt owner | Binding/SIP/Shiboken/runtime/SDK locks and actual qVersion/native-build/Viewer/plugin/canvas evidence per GUI cell | M0.5, M0.10, M6, M9.3 |
| `minimum-and-latest-dependencies` | provider/release owner | Applicable minimum/latest transitive locks, platform wheel hashes and real marker/availability checks | M0.5, M0.7, M9.2-M9.3 |
| `full-fixture-coverage` | contracts/QA owner | Pinned original case IDs/fixture hashes and baseline collection/skip/xfail reasons per applicable cell | M0.8, M9.2 |
| `provider-protocol-profiles` | array/provider owner | Locked real-provider artifacts and dtype/layout/stride/lazy/multiscale/alias/copy/serialization/error contracts | M0.7, M3, M4 |
| `representative-real-plugins` | plugin/Qt owner | Exact real reader/writer/command/dock-plugin artifacts, hooks, NPE2/legacy workflows and required cells | M0.6, M5, M6, M9.2 |
| `gpu-image-environment` | rendering/QA owner | Exact GPU/driver/OpenGL/display/software-renderer profiles and predeclared image oracles/tolerances | M0.9, M7, M9.5 |
| `workflow-inputs-and-oracles` | contracts/workflow owner | Dataset hashes, ordered API/GUI/plugin steps, event traces and expected numerical/image outputs | M0.8, M2-M7, M9.2 |
| `performance-measurement-policy` | benchmark/QA owner | Exact ASV/end-to-end cases, inputs/hardware, warmup/repetitions/statistics/synchronization and justified time/memory thresholds | M0.9, M9.5 |
| `native-distribution-policy` | build/release owner | Wheel/sdist tags, compiler/SDK/flags/locks plus reproducible sdist-to-wheel and fresh outside-checkout installed native Qt checks | M0.5, M9.3 |
| `acceptance-validator-and-evidence-index` | coordinator/independent release reviewer | Composite checker and docs/conversion-release-evidence.json reject draft/unresolved/partial/missing-cell evidence for the final source/locks/artifacts | M0.11-M0.12, M9.1-M9.8 |

## Current Theme owner — finish its workflow

Owner: Theme C++ implementation plus independent contracts and review. Exit:
accepted stylesheet/template behavior with its required ownership boundaries;
the sixteen-body packet alone cannot close the complete Theme parent.

- [ ] T.1 Finish the required provider/reflection/callback boundaries beyond the accepted finite packet. Its four narrow matching/literal/declaration operations are approved with actual installed evidence; wider Color/private compatibility remains open. Reuse accepted evidence without replaying unchanged tests.
- [x] T.2 Integrate and accept the staged sixteen selected Theme template/font/color/gradient bodies, both comprehension policies and all six application regex callbacks in handwritten C++. The finite CP312 installed exit passed at the current checkpoint; wider parent and platform exits remain open.
- [ ] T.3 Preserve `template(css, **theme)`, actual keyword iteration, live namespaces/retained builtins, fresh matcher/callback lookup, exact arithmetic/formatting and source-required error/publication order.
- [ ] T.4 Reuse accepted sixteen Color bodies and five generators; complete their remaining required methods, class/allocation/schema/module/private/canonical/lifecycle ownership via C.8. The accepted constructor/as_rgb/as_rgb_tuple controls do not approve the full provider or state boundary.
- [ ] T.5 Declare matching-engine primitive boundaries narrowly. A retained matcher must not retain Python application callback bodies; escaping native callbacks need explicit GC/lifetime ownership.
- [ ] T.6 Verify real stylesheet consumers, dynamic callback/provider overrides, errors and relevant frame contracts against the pinned source; run actual Qt/theme consumers on the installed extension.
- [ ] T.7 Finish Theme construction, schema/class/private initialization, canonical storage, deletion, copies/dumps/restoration and other policy callers as M1 requirements; keep the parent partial until these exits pass.

## M1 — Native foundations

Owner: model/state, event, container, transform and task/cache owners. Exit:
one authoritative storage path with accepted public protocols and lifecycle.

- [ ] M1.1 Complete EventedModel/native model authority, including actual `__dict__`, fields-set/extra/private storage, class/schema identity and plugin subclasses; preserve aliases through construction, validation, set/delete, copy, dump and restoration.
- [ ] M1.2 Complete event emission/blocking, callback tokens, weak references, changes/transactions and reentry with baseline event/error order.
- [ ] M1.3 Finish typed/evented/nested/selectable collections, selection and LayerList stores, iterators and mutable views without independent snapshots.
- [ ] M1.4 Finish transform objects/chains, authoritative arrays, inverse/composition/cache behavior and supported dtype/stride/provider contracts.
- [ ] M1.5 Complete native task/cache ownership, request IDs, dispatch, cancellation/stale completion, errors, GIL/thread boundaries and actual GUI delivery. Release task/request owners after success, error, cancellation and stale completion, during close and interpreter shutdown; verify stop-to-wait and saved-error/iterator/callback roots.
- [ ] M1.6 Finish nanobind GC traversal/clear/reference ownership, deallocation and exception translation for real application objects and callbacks.
- [ ] M1.7 Resolve known lifetime diagnostics through controlled reproductions; verify observer/plugin disconnect, reentry, interpreter exit and repeated viewer close after external references are released.
- [ ] M1.8 Run meaningful standalone native tests where logic is independent, and installed actual-object/Qt tests for protocols and ownership. Close the whole foundation exit only with applicable evidence.

## M2 — Layer and Viewer base

Owner: Layer/LayerList/ViewerModel/controllers. Exit: accepted common layer
transitions through the actual application and preserved plugin/widget identity.

- [ ] M2.1 Complete shared Layer and Viewer canonical state, extents, dimensions/grid/camera transitions and source-required caches.
- [ ] M2.2 Finish insert/remove/reorder/select/link, active-layer transitions and live selection/LayerList aliases.
- [ ] M2.3 Port remaining features, text, color state, cursor and overlay models/controllers with their event and serialization contracts.
- [ ] M2.4 Complete `QtViewer._on_slice_ready` and required scalar-field/Image/Labels response application through the real viewer/canvas; a forwarded callback is insufficient.
- [ ] M2.5 Verify loaded/request-ID and response/event order, stale/error/cancelled completions, reentry and queued close, with actual Python plugin widgets present.

## M3 — Image and Labels

Owner: scalar-field/Image/Labels/slicing/edit/history owners. Exit: complete
real-data viewing and editing round-trip with correct task and lifecycle behavior.

- [ ] M3.1 Finish Image request construction, native slicing/responses, multiscale selection, contrast/thumbnail publication and lazy/cache behavior.
- [ ] M3.2 Finish Labels state, native paint/fill/polygon operations, selection and edit/history storage, undo/redo and serialization.
- [ ] M3.3 Exercise required NumPy, Dask, Zarr and other frozen provider profiles, including dtype/stride/lazy/multiscale and supported array subclass behavior.
- [ ] M3.4 Verify open → add/reorder/select → change dimensions → render → edit → undo/redo → save → close through GUI and Python API.
- [ ] M3.5 Verify close while loading, cancellation races, stale responses, worker/provider exceptions and event-order/reentry contracts.

## M4 — Remaining five layers

Owner: one complete layer workflow per implementation owner. Exit: all seven
layer types accepted for applicable data, editing, slicing and rendering modes.

- [ ] M4.1 Points: finish state, features/colors/text, slicing, selection/add/remove/move and picking/edit events.
- [ ] M4.2 Vectors: finish geometry/state, features/colors, slicing/transforms, display and selection contracts.
- [ ] M4.3 Surface: finish mesh/value/state handling, geometry/transforms, display and supported picking contracts.
- [ ] M4.4 Shapes: complete all geometry, hit testing, selection/interactions, creation/editing, triangulation-provider policy, features/colors/text and reconstruction/state authority.
- [ ] M4.5 Tracks: port graph/time/tail/state, features/colors, slicing and interactive display/picking policy.
- [ ] M4.6 Run applicable 2D/3D, transforms, slicing, selection/editing and serialization workflows for all seven layers; retain real optional provider requirements.

## M5 — Services and plugins

Owner: actions, settings, IO/startup and plugin coordination owners. Exit:
real plugin and persistence workflows with reviewed Python callback boundaries.

- [ ] M5.1 Port actions/contexts, command routing, key/mouse bindings and shortcut policy.
- [ ] M5.2 Finish settings state/validation/migrations, persistence and preference changes with real model/UI consumers.
- [ ] M5.3 Port IO dispatch, reader/writer selection, file round-trips, supported session/state persistence and error/notification routing.
- [ ] M5.4 Complete CLI/startup/shutdown, resource/theme/icon coordination and application notification policy.
- [ ] M5.5 Finish NPE2 and required legacy-engine discovery/registration/command coordination; preserve declared third-party Python callbacks and real plugin-created widgets.
- [ ] M5.6 Exercise frozen real reader/writer/command/dock-widget plugins, settings migrations and save/reload/open/close workflows.

## M6 — Actual Qt application

Owner: native Qt host, window/viewer/controllers/widgets and binding bridge.
Exit: real interactive application behavior and wrapper/destruction parity.

- [ ] M6.1 Complete the native main window/viewer, layer controls/lists, menus, toolbars, dialogs/preferences and docks.
- [ ] M6.2 Complete actual mouse/keyboard/event routing, shortcuts, drag/drop and observer/signal connections with baseline order and reentry.
- [ ] M6.3 Finish Python console/notebook hosts and supported Python-created plugin-widget/native QObject interoperability.
- [ ] M6.4 Preserve actual widget/canvas/model wrapper identity, parent ownership, callback lifetimes and QObject destruction for each required binding.
- [ ] M6.5 Run representative input/menu/dialog/dock flows and screenshot checks; repeatedly open/close/release viewers and verify unintended roots are gone.

## M7 — Rendering orchestration

Owner: native napari scene/visual controllers and rendering lifecycle. Exit:
frozen numerical/image/picking results and GPU cleanup across required environments.

- [ ] M7.1 Port napari visual/scene orchestration, transforms, textures, colormaps and clipping.
- [ ] M7.2 Complete image/volume rendering options, overlays, text, picking and supported visual/backend/plugin interoperability.
- [ ] M7.3 Define and review the retained rendering-backend boundary; move napari application decisions into C++ even when VisPy/OpenGL primitives remain external.
- [ ] M7.4 Verify applicable 2D/3D/volume reference images, screenshots, picking and update ordering against frozen tolerances.
- [ ] M7.5 Verify context/thread/GPU resource ownership, updates during cancellation/close and supported driver/display cleanup.

## M8 — Remove all remaining application Python

Owner: coordinator-led complete responsibility sweep, with subsystem owners
handling implementation gaps. Exit: every owned runtime responsibility complete.

- [ ] M8.1 Assign and complete every remaining application runtime module/definition in the responsibility register; finish the667 partial records and287 tracked delegations and classify/resolve the 5,832 unreviewed runtime entries. The 361 genuine benchmark/tooling entries require explicit current-source/call/dependency/evidence reviews; tooling classification cannot hide runtime implementation. Refresh these snapshot counts as work is accepted.
- [ ] M8.2 Finish event/collection/input helpers, geometry/color/text policies, builtin-plugin implementation, vendored application policy and entry points not closed by earlier workflows.
- [ ] M8.3 Replace remaining Python numerical branches for `interpolate_coordinates`, `first_nonzero_coordinate`, `segment_normal`, `project_points_onto_plane` and `points_in_box`, preserving generic/provider behavior as well as optimized paths.
- [ ] M8.4 Review application policy inside `naparixx/_backend.py`, `_adapters.py` and other compatibility wrappers; finish native generic paths and remove original-body fallback.
- [ ] M8.5 Verify every retained external operation has a narrow reviewed responsibility boundary; preserve required owned policy/callees rather than hiding them in dependency exemptions.
- [ ] M8.6 Audit fields/allocation, inheritance, exports, callbacks/decorators, module initialization and dispatch in addition to function bodies. Confirm canonical state has one owner.
- [ ] M8.7 Leave only reviewed Python facades/adapters/metadata and declared plugin/dependency calls. No generated application C++, undecided fallback or unreviewed application algorithm may remain.

## M9 — Final release parity

Owner: release coordinator with independent acceptance review. Exit: all final
gates pass for the same final source, dependencies and installed artifacts.

- [ ] M9.1 Close M0–M8 with linked required evidence; freeze acceptance and validate its support/applicability decisions.
- [ ] M9.2 Run the full pinned upstream fixtures, frozen provider/plugin corpus and real GUI/rendering/API workflows. Preserve original fixtures and compare collection, skips/xfails/deselections to the applicable baseline; migration-induced exclusions remain failures of the gate.
- [ ] M9.3 Build wheels and reproducible sdist-to-wheel artifacts with frozen toolchains/locks; install outside the checkout in every required configuration and exercise the actual native Qt bridge.
- [ ] M9.4 Run lifetime/GC/interpreter/thread/GIL/cancellation/reentry/repeated-close/GPU checks with diagnostics. Resolve applicable failures rather than borrowing selected-pass totals as proof.
- [ ] M9.5 Measure frozen workloads against the pinned baseline under the same input/hardware/warmup/statistical protocol; meet justified numerical/image/performance/memory tolerances.
- [ ] M9.6 Index every required matrix/workflow/milestone outcome, full collection/skip/xfail comparison, command/exit/log/report, locks, distribution and installed binary identity in docs/conversion-release-evidence.json. The composite validator must reject draft/unresolved/partial records or missing required cells and bind full fixtures, fresh wheel/sdist/native Qt installs, workloads and lifetime reports to one final source/lock/artifact set.
- [ ] M9.7 Pass `python -B tools/audit_conversion.py --check-complete` with zero unresolved responsibility/dependency issues. This source audit complements runtime evidence.
- [ ] M9.8 Independently verify all final gates before setting `migration.json.complete_application_port=true` and marking the full goal complete.

## Checklist for every execution packet

Use this same short checklist for each cohesive state owner or user workflow.
Retain unresolved required children on its parent backlog. A packet can accept
finite behavior while its parent milestone remains open.

- [ ] P.1 Name the public workflow, canonical state owner, required owned callees, blocking M0 decisions, edited files and concrete acceptance exit.
- [ ] P.2 Reuse the latest qualified checkpoint/runners. Establish missing baseline behavior with original fixtures or focused contracts derived from a demonstrated gap; add installed-only witnesses only where needed.
- [ ] P.3 Implement handwritten native control, policy and authoritative storage, with explicit nanobind/GC/thread/error and facade/dependency boundaries.
- [ ] P.4 Verify all changed contracts, generic/provider paths and required owned children; preserve failures and correct fixtures only for demonstrated setup defects without weakening behavioral assertions.
- [ ] P.5 Obtain one coherent independent review. Recheck affected contracts after material changes or unresolved findings; do not repeat settled reviews/probes for unchanged inputs.
- [ ] P.6 Integrate, build and test the actual installed extension and relevant GUI/plugin consumers on one consistent source/binary set. Run necessary downstream checks, then broaden only for a remaining concern.
- [ ] P.7 Check applicable lifetime/alias/thread/close behavior. Record unresolved diagnostics explicitly; passing functional cases do not establish clean lifetime.
- [ ] P.8 Update the responsibility/dependency reviews, manifest and compact task card once after acceptance. Link evidence and keep every unproven parent or release gate open.
- [ ] P.9 After relevant jobs settle, compare disposable output with the latest accepted cleanup cutoff and remove only verified junk inside the workspace. If nothing disposable changed, record preservation and skip deletion. Use Python `-B`/`PYTHONDONTWRITEBYTECODE=1`, pytest `-p no:cacheprovider` and Ruff `--no-cache`; preserve required SDK/venv/providers/upstream/build/PDB/OBJ, genuine caches, sources and evidence. Do not retry or bypass rejected cleanup targets.

Every packet handoff needs only its owner/files, changed contracts, dependency,
commands, result and next action. Reuse runners and evidence indexes. Create a
new artifact only when it records a real reproducibility or acceptance gap;
avoid duplicate receipts and repeated unchanged probes. Inventory cleanup as
work proceeds, and delete only verified disposable files within the workspace
after their owning process has finished.

## Tracking and estimates

Maintain one compact row for each active owner:

| Owner/workflow | Milestone | Dependencies | Files | Model/effort | State | Required exit and evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Theme template control | M1/M5 | Source decisions, originals and final installed coverage passed | `cpp/theme_color_publication.cpp`, `src/napari/utils/theme.py`, `src/naparixx/_native.pyi` | Sol High; Ultra for independent roles | Accepted finite CP312 scope | [Checkpoint: 164 distinct passes](../.artifacts/theme-native-theme-template-owner-production-checkpoint.json); Color/provider/state/lifetime parent remains open |
| Color policy and admission | M1/M5 | Source/fixture/compatibility/accounting peers and final installed acceptance passed | `cpp/theme_color_publication.cpp`, `src/napari/utils/theme.py`, `src/naparixx/_native.pyi`, `src/naparixx/_color_policy.py` | Sol High; Extra High for admission/identity; Ultra for independent roles | Accepted finite CP312 scope | [Checkpoint: 212 distinct passes](../.artifacts/theme-native-color-policy-owner-production-checkpoint.json); complete class/provider/state/lifecycle parent remains OPEN |
| Accepted Color/class packet | M1/M5 | Corrected source/contracts/compatibility/accounting peers, actual48 before comparisons and same-pair265 installed cases passed | `cpp/theme_color_publication.cpp`, `src/naparixx/_native.pyi`, `_color_policy.py`, `_color_class.py` | Sol High implementation; Medium runners; Extra High semantic failures | Accepted finite CP312 scope | [Checkpoint265/21](../.artifacts/theme-native-color-class-owner-production-checkpoint.json); broad class/schema/module/canonical/lifetime parents remain OPEN |
| Schema factory control | M1/M5, C.8.4 | S.1-S.8 source/compatibility/public30/build/actual297/cleanup/accounting passed | `cpp/theme_color_publication.cpp`, `src/naparixx/_native.pyi`, `_color_policy.py`, `_color_schema.py` | Sol High integration; Extra High semantics/review; Medium runners | Accepted finite CP312 scope | [Checkpoint297/22](../.artifacts/theme-native-schema-factory-owner-production-checkpoint.json); full74factory/schema/class/module/canonical/lifetime parents remain OPEN |
| Labels edit-history13 | M3.2 | Source/compatibility/before258/build/actual569/accounting/cleanup passed | `cpp/labels.cpp`, stub, `_labels_history.py`, Labels bootstrap | Sol High; Extra High semantics; Medium runners | Accepted finite CP312 scope | [Checkpoint569/28](../.artifacts/labels-edit-history-owner-production-checkpoint.json); L.8/whole workflow/lifetime remains OPEN |
| Composite release checker | M0.11, M9.6 | Reviewed checker/portable setup integrated;38 production tooling tests passed | `tools/check_conversion_release.py`, `tests/test_conversion_release.py` | Sol High review; Extra High normalization; Medium runners | Tooling preparation accepted; final release index absent | Freeze normalization/transport and qualify actual required final-source reports; no release gate closed |
| Color/RGBA and Theme canonical class/state owners | M1/M5 | Finite packet acceptance; distinct Color/RGBA slot classes and Theme/EventedModel schema boundary | Assign remaining exact files after S.8 | Sol Extra High; Ultra for independent roles | Required parent work, open | One native authority for each actual representation preserves public aliases, validation, schema/subclasses, copies/dumps/restoration and lifecycle |

Use `planned → implementing → staged → reviewed → installed-validated → accepted` for
packet state. Mark a milestone checkbox only when its required scope and evidence
pass, not when a planning document exists. A checked item should link its exit
evidence in the owner row or acceptance record. Track complete workflows and
unresolved owners instead of counting exports or forwarding functions.

Estimate schedule and effort after two or three representative complete owners
have produced measured implementation, review, build, test and rework times.
The current selected test totals and review counts cannot support a reliable
whole-port completion percentage or date. Update this checklist as actual
dependency decisions and accepted workflows change; do not restart settled
history during each continuation.
