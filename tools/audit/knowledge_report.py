"""Generate the reusable engine knowledge pack from collected evidence.

Narrative interpretations are curated and versioned; source markers and dated test
results corroborate them, never silently promote a roadmap label to completion.
Can regenerate documents from a sealed run without repeating the byte census.
"""
import feature_evidence
import engine_comparison
import argparse
import collections
import datetime
import hashlib
import json
import re
from pathlib import Path

VERSION = '2.2'

# Product-oriented explanations, distinct from the scanner's implementation markers.
# title -> user purpose, interaction/workflow, connections, completeness
FEATURES = {
 'Dynamic world physics closeout': ('Make supported worlds move and react.', 'Author physical objects, constraints, projectiles, destruction, lightning, grass, fire, fog and liquids; Play and exported games share runtime behavior.', 'See PHYSICS-CLOSEOUT-CHECKS-20260927.md for 772 core checks and the visible/editor/export evidence. Test counts are not universal certification.', 'Bounded closeout passes; mesh/textures next, performance last. Fixed fluid domains, conservative collision and unsupported-fracture fallback remain.'),

 'Character movement and grappling': ('Move continuously through an authored world.', 'WASD and live facing drive movement; Shift sprints, Space jumps, V dodges or lunges, Control changes posture or slides, and G grapples. Resources and traversal states are shared between Play and the standalone player.', 'Controller, collision, terrain, moving anchors, camera and runtime presentation are connected; source and dated checks are separate evidence.', 'Step 4 accepted within documented scope; current physics closeout fixtures pass'),
 'Telegraph combat and combat actors': ('Fight and heal while moving using visible aimed areas.', 'Player attack uses a repeating 2.5-second cycle; release completes the current cycle. Enemy Swipe uses 1.5 seconds. Cones follow live caster position/facing and fill with channel progress. Friendly damage/healing are blue/green; hostile damage/healing red/orange.', 'Runtime health, target selection, pursuit, collision, line of sight, breakable objects and healing share the adventure runtime without changing saved scene definitions.', 'Step 4A accepted with agreed placeholder damage/health; current exported combat checks pass'),
 'Crowd control and boss break bars': ('Apply explicit control restrictions and coordinated boss vulnerability windows.', 'Stun, knockback, pull, launch, knockdown, root, slow, silence, fear, taunt, sleep, blind, disorient and tether have separate rules. Player immunity follows control; boss CC instead damages an intact 1,000-point bar, with up to eight seconds stagger, 25 percent extra damage and 90-second refill.', 'CombatControl, CombatActor and BossBreakBar connect runtime actions and displays. Friendly/self CC protection, player anti-chain rules and boss contribution rules are separate.', 'Implemented runtime components and integrations; exact current test outcomes and remaining acceptance are reported separately'),
 'Discoverable editor help': ('Find a way forward without leaving the project.', 'Help / Tutorials and F1 open optional searchable lessons, with Back, Next, Restart and saved reading positions. Close or Escape returns to creating.', 'Shared controls remain available across current editing experiences. Stable lesson IDs preserve progress through catalog changes; malformed preferences fall back safely.', 'Implemented text guidance; live UI acceptance pending. Not animated demonstrations, guided task detection or the full three-mode startup'),
 'Walk and build preview': ('Make building part of exploring the world.', 'Walk at player height, right-drag to look, and use existing editing tools. Play starts a separate test; Pause restores authored objects at the tested viewpoint.', 'Shares scene data, Undo, asset insertion and snapping. Returning to ordinary editing preserves authored changes. Core checks cover viewpoint/data separation, not full GUI acceptance.', 'Experimental code foundation; live input and visual review, objectives, experience and unlocks remain pending'),
 'Adventure gameplay foundation': ('Make a small playable adventure without writing code.', 'New starter adventure creates a key, door and exit level with save protection. Existing scenes can opt into walking controls; Beginner and Advanced expose different settings on the same data. Play uses WASD, right-drag and E.', 'Static-box collision and key links use an independent play copy; Reset preserves authored objects. The starter path has code-driven walking and interaction checks, distinct from human usability review.', 'Bounded authored rules plus the movement and combat foundation described below; not arbitrary scripting or the complete Interactive building experience'),
 'Windows adventure export': ('Share an authored adventure as a standalone Windows game.', 'Export playable game writes a new folder with a player, engine/runtime libraries and portable scene content. Existing folders are refused; missing runtime files abort the export.', 'The player shares movement and interaction code with the editor. Package hashes and content validation are diagnostic integrity checks, not signatures. Hidden startup rendering is distinct from visible input verification.', 'Windows x64 / Direct3D 12 foundation; clean-machine, signing, other platforms and visible player acceptance remain unverified'),
 'Reusable assets and scene composition': ('Reuse authored content across scenes without rebuilding it.', 'Browse a remembered folder with text/type filters. Insert models, native templates/groups or scene objects; copy/paste selections during an editor session; repair missing models; export portable scene/selection folders.', 'Insertions receive independent identities and one Undo. Groups retain spacing and translated routes. Portable folders carry embedded GLBs and hash manifests; editor-session copied models retain original file references.', 'Implemented bounded content workflow; thumbnails and imported-model templates remain outside scope; Windows adventure packaging is tracked separately'),
 'Native application and build': ('The desktop application used to create and preview scenes.', 'Open the native editor through its launcher. The active product is native C++; Python is build/analysis tooling.', 'The editor uses the scene core and NativeSession; the separate project host loads native project modules.', 'Implemented foundation'),
 'Classic creator interface': ('Keep world creation approachable and familiar.', 'Use floating Builder, Characters, Environment and More menus over the canvas. Simple settings hide advanced transforms/body controls. Terrain and Shapes have dedicated gear panels; named colors and quality choices replace numeric labels.', 'Controls edit authored scene data. Advanced disclosure preserves hidden properties; the feature catalogue also contains future ideas.', 'Implemented workflow; broader polish remains bounded by tested cases'),
 'Independent game preview': ('Preview the authored scene inside the editor.', 'Play uses the existing editor canvas and session in both windowed and fullscreen modes. Pause holds simulation; Reset returns to the authored start.', 'Retained GamePreview support is not the active Play command path. Inline preview is not a packaged executable export.', 'Implemented inline workflow; exhaustive parity remains open'),
 'Scene data and compatibility': ('Represent the world the user has authored.', 'A scene contains objects, terrain, pours, environment and rendering preferences. Character records also represent shapes/effects; a record is not necessarily an animated person.', 'Scene JSON is separate from renderer packets and project manifests. Authored state is separate from transient simulation.', 'Implemented foundation'),
 'Scene packet ABI': ('Carry prepared scene geometry and settings into the native viewport.', 'The editor writes the V7 binary scene packet; the parser retains V1–V7 support. The scene JSON schema and project schema use independent version numbers.', 'The packet connects editor geometry to GPU resource creation and viewport rendering.', 'Implemented compatibility contract'),
 'Local automation': ('Allow local tools or an assistant to inspect and edit scenes consistently.', 'Describe capabilities, inspect a scene, or apply a validated command batch: add, move, route, rename, remove, configure, shape, transform, terrain brush, water and stamp.', 'Commands use expected scene hashes and persistence rules; they do not embed an AI provider or duplicate every GUI action.', 'Partial product coverage: implemented local commands'),
 'Persistence and editing safety': ('Save work and reverse or cancel edits.', 'Use save/open and undo/redo. Brushes are transactions; canceled work can return to the previous scene. Save/Discard/Cancel protects replacement/closing; stale scene writes are rejected.', 'Scene history stores scene state; validation and atomic file replacement protect persistence. It is not a bounded delta-history system.', 'Implemented workflows; long-session memory remains a concern'),
 'Terrain authoring': ('Build and paint the ground of a scene.', 'Raise/lower, smooth, flatten and paint terrain; place hill/crater/plateau stamps or stamp a selected shape. Terrain brush settings are independent from object sculpt settings.', 'The 97×97 authored heightfield becomes a 193×193 display mesh. Physics uses collision tiles; liquid surfaces use the terrain as their bed.', 'Partial terrain system: finite heightfield, no streamed world/caves'),
 'Object geometry and sculpting': ('Place solids precisely and reshape their actual mesh.', 'Translate, rotate, scale and snap objects. Pull, Push, Smooth and Flatten edit a mesh copy; undo/cancel preserves the source asset. Starter placement is bounded and supports adjustable grid spacing. Rotation handles can snap their edited axis on release; grouped placement preserves relative spacing.', 'Transforms affect rendering, bounds and collision preparation. Sculpting is separate from terrain height brushes.', 'Partial sculpting system: core brushes and placement'),
 'Props and effects': ('Populate scenes with basic geometry, nature and visual effects.', 'Place basic solids, rock/boulder/pebble/grass objects, fire/light/lava helpers, Chaos Lightning, Chaos Rock, Light-Bound Shard and ShadowEnemy Smoke.', 'Procedural geometry, materials, lights and volumes create these appearances. Supported fracture and wind/contact grass motion now have explicit physics checks; asset appearance upgrades remain Task 2.', 'Implemented prop set; broader nature systems planned'),
 'Static model import': ('Bring a model into the scene as preview geometry.', 'Choose a GLB model, validate it, then place and transform instances. The importer handles static mesh geometry and validates its buffers/accessors/hierarchy.', 'Imported objects feed scene geometry and optional sculpt/collision preparation. Creator import is distinct from the skinned crowd runtime.', 'Partial asset pipeline: static GLB preview'),
 'Render ownership and updates': ('Keep rendering and simulation working while the UI processes interaction.', 'A session receives coalesced scene/camera/playback changes, renders frames and exposes status/capture. Hidden preview sessions can suspend and resume.', 'The session coordinates physics, flow and viewport submission. Scene refresh and resize still contain expensive reconstruction paths.', 'Implemented ownership; optimization and failure recovery incomplete'),
 'Native rendering contract': ('Render actual frames at the chosen display resolution.', 'Current acceptance requires render/output dimensions to match, no super resolution and no frame generation. HDR scene shading and spatial edge smoothing precede presentation.', 'Native-resolution RR is an optional denoiser, not permission to enable spatial upscaling. Existing runtime vendor source is not proof it is active in the editor.', 'Implemented configuration; dated visible benchmark evidence is workload-specific'),
 'Inline hardware ray tracing': ('Offer a selectable quality ladder for ray-based lighting.', 'Choose Off, Low, Medium, High or Path tracing. The implementation ranges from direct visibility to AO/reflections, diffuse GI and sampled multi-bounce opaque transport; freeze/bounce settings support reference views.', 'Inline compute RayQuery uses BLAS/TLAS geometry, then passes noisy lighting and guides to denoising. Unsupported capability can fall back to raster.', 'Partial rendering system: implemented tiers, bounded materials'),
 'Native temporal denoiser': ('Stabilize noisy ray-traced lighting without vendor reconstruction.', 'Mode 0 selects the native temporal wavelet path: prefilter, history reprojection/rejection, atrous filtering and composition.', 'Depth, normal, motion and material guides support temporal decisions. Emission/history handling is part of the rendering pipeline.', 'Implemented algorithm; dated native-wavelet diagnostic runs passed, broader quality and stability remain unverified'),
 'Optional native Ray Reconstruction': ('Offer an alternative vendor denoiser at native resolution.', 'Mode 1 requests NVIDIA RR through NGX at 1:1 input/output dimensions. Missing or failed support can fall back to the native filter with status reporting.', 'RR depends on local SDK/runtime availability and guide buffers. Preserve ABI 0=wavelet, 1=RR.', 'Implemented optional integration; portability and current runtime validation bounded'),
 'Atmosphere and lighting': ('Set scene mood and illuminate the world.', 'Configure time of day/day cycle, fog, local lights and smoke. Water/lava/chaos appearances combine procedural shading, emission and explicit lighting.', 'Atmosphere injection/integration/composition follows the surface rendering path. Raster proxy shadows differ from triangle visibility in RT.', 'Partial atmosphere: bounded froxels/lights, no full weather/smoke simulation'),
 'Rigid-body simulation': ('Let configured props fall, tumble and interact with collision geometry.', 'Body settings and density feed Jolt fixed-step simulation. Terrain/prop collision preparation runs asynchronously; playback/reset separates simulation from authored placement.', 'Body poses update scene transforms and liquid obstacle/coupling inputs. Collision geometry can approximate visual meshes.', 'Bounded physics and lifecycle closeout fixtures pass; arbitrary-scale performance remains unverified'),
 'Water and lava flow': ('Pour finite water/lava and observe terrain-bound movement.', 'Arm a pour, click the ground, then Play/Pause/Reset. Separate water/lava fields evolve on a 48×48 grid, with obstacle coverage and liquid geometry.', 'Terrain elevation and sampled solid occupancy guide transport; the physics wrapper uses liquid data for approximate body coupling.', 'Partial simulation: surface flow, not general 3D fluids'),
 'Routes and simulation controls': ('Make placed objects traverse authored paths during preview.', 'Draw X/Z routes, set speed, Play/Pause/Reset, optionally repeat or follow a character. Escape cancels active operations before leaving fullscreen.', 'Routes drive transforms separately from skeletal animation and navmesh/character collision. Route completion and fluids have distinct playback concerns.', 'Partial movement system: authored routes and controls'),
 'Local music': ('Play local music while creating.', 'The dedicated player provides track selection, playlist next, play/pause, stop and volume.', 'Windows MCI supplies local playback; this is editor media, not a spatial sound or exported soundtrack engine.', 'Implemented editor convenience'),
 'Texture foundation': ('Load compressed texture data correctly in supported rendering paths.', 'DDS BC5/BC7 handling preserves authored mips and uses format-aware uploads/views; image-loading fallback remains.', 'The separate runtime texture path is broader than editor procedural materials. A loader does not supply a full terrain PBR library or streaming manager.', 'Partial asset/material pipeline'),
 'Project host boundary': ('Launch native projects with selected scenes and presets.', 'Choose a project manifest, choose a scene, reconcile/check/save preset settings and launch the native host. Empty and SampleGame are project modules.', 'Project schema 3 and .startest settings belong to the project workflow. A creator .starscene is not automatically exported into a standalone game.', 'Implemented host; general export/distribution incomplete'),
 'Crowd runtime': ('Exercise large numbers of skinned instances in the separate SampleGame runtime.', 'Gather/cull instances, evaluate poses and build instance buckets using specialized project code.', 'It shares renderer lineage and worker machinery, but is not the creator static-model importer or 100,000 independent gameplay entities.', 'Implemented specialized runtime; scope-specific results'),
 'Persistent CPU workers': ('Reuse CPU threads for parallel preparation.', 'Persistent range workers divide contiguous work, let the caller participate and join at completion. Actual participation depends on requested workers and grain sizes.', 'Jolt and asynchronous collision cooking have separate concurrency budgets. This is not a universal task graph or work-stealing scheduler.', 'Implemented pool; general scheduling roadmap pending'),
 'Command recording and GPU lifetime': ('Submit GPU work while preserving resource ownership.', 'Frame slots, command allocators/lists, barriers and fences order GPU work. CPU preparation may be parallel while recording/submission remains serial.', 'Future parallel recording needs reserved bindings and per-worker/per-slot ownership, not simply more CPU threads.', 'Implemented serial submission foundation'),
 'Diagnostics and existing audits': ('Explain failures and retain reproducible evidence.', 'Native suites test core data, diagnostics, textures, sessions and classic UI; the audit harness records isolated runs, process measurements and renderer comparisons.', 'Saved tests identify exactly what was reached/passed/failed. This knowledge tool reads them; it does not rerun them.', 'Implemented tooling; validation gaps explicit'),
 'Diagnostic benchmark path': ('Attempt unattended measurement without background presentation throttling.', 'The diagnostic path exposes benchmark controls and auditOffscreen; historical failures and subsequent visible-run results must be distinguished by date and workload.', 'The 22 September reliability checkpoint documents debug-confirmed invalid backbuffer use and an isolated output-target correction; that historical finding does not establish the outcome of later lifecycle checks. Consult the dated runtime evidence for current bounded results.', 'Implemented diagnostic path; broader reliability validation incomplete'),
}

def read_json(path): return json.loads(path.read_text(encoding='utf-8'))
def write_json(path,data): path.write_text(json.dumps(data,indent=2,ensure_ascii=False),encoding='utf-8')
def file_hash(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def escaped(s): return str(s).replace('|','\\|').replace('\n',' ')

def latest_validation(evidence):
    # Normalize only for matching; preserve original evidence paths for provenance.
    evidence=[dict(e, path=e['path'].replace('\\','/'), originalPath=e['path']) for e in evidence
              if not e['path'].endswith(('.progress.json','.status.json'))]
    runs=collections.defaultdict(list)
    for e in evidence:
        match=re.search(r'audit-runs/(run-[^/]+)/',e['path'])
        if match:runs[match.group(1)].append(e)
    name=max(runs) if runs else None; entries=runs[name] if name else []; summary=[]; ui=[]
    render=collections.Counter()
    for e in entries:
        d=e.get('data',{}); path=e['path']
        if not isinstance(d,dict): continue
        if e.get('error'):summary.append(f'Unreadable evidence: {path}.');continue
        if '/tier-' in path:
            arm='new' if '-new/' in path else 'legacy' if '-old/' in path else 'unspecified'
            render[(arm,'recorded success' if d.get('success') is True else d.get('error','unknown result'))]+=1
        elif path.endswith('native-session/session-results.json'):
            summary.append(f'Session checks: {"passed" if d.get("success") is True else "failed/unknown"}; {len(d.get("checks",[]))} checks reached. '+str(d.get('error','')))
        elif path.endswith('native-session/lifecycle-memory.json'):
            before=d.get('baseline',{}); after=d.get('final',{})
            summary.append(f'Scene lifecycle: {d.get("cycles","unknown")} cycles; resource retirement={d.get("resourcesRetired","not recorded")}; handles {before.get("handles",d.get("initialHandles","?"))} → {after.get("handles",d.get("finalHandles","?"))}.')
        elif path.endswith('native-classic-ui/result.json'):
            raw_checks=d.get('checks',[])
            ui=[c for c in raw_checks if isinstance(c,dict)] if isinstance(raw_checks,list) else []
            passed=sum(c.get('pass') is True for c in ui)
            summary.append(f'Classic UI: {passed}/{len(ui)} named checks passed in this saved run.')
            trace=d.get('window_creation_trace')
            if trace is not None:summary.append(f'Helper-window trace: {len(trace.get("events",[]))} creation events; {trace.get("hookedThreads","unknown")} hooked threads. No events does not establish ownership.')
    for (arm,outcome),count in sorted(render.items()):summary.append(f'Renderer {arm}: {count} result(s) — {outcome}. Success alone does not prove comparable, unthrottled throughput.')
    summary.insert(0, f'Historical lifecycle/renderer cohort: {name}. Later UI and visible-render records below are separate workloads, not replacements for lifecycle evidence.')
    supplemental=[]
    for e in evidence:
        path=e['path']; d=e.get('data', {})
        if not isinstance(d,dict): continue
        if 'editor-menu-comparison-' in path or '/visible-editor/result.json' in path:
            raw_checks=d.get('checks',[])
            checks=[c for c in raw_checks if isinstance(c,dict)] if isinstance(raw_checks,list) else []
            if checks or d.get('pass') is False or d.get('error'):
                summary.append(f'Separate saved UI evidence {path}: {sum(c.get("pass") is True for c in checks)}/{len(checks)} checks passed; overall pass={d.get("pass", "not recorded")}. Not proof of current-source equivalence or visual completeness.')
                if d.get('error'): summary.append(f'UI failure: {d["error"]}')
                supplemental.append(path)
        elif '/visible-presentation/render.json' in path:
            summary.append(f'Separate saved visible render evidence {path}: success={d.get("success", "not recorded")}. Inspect dimensions, foreground validity and workload before comparing performance.')
            supplemental.append(path)
        elif 'gate0-' in path and path.endswith('/audit-verdict.json'):
            summary.append(f'Gate 0 verdict preserved at {path}; bounded stage passes do not close unresolved lifecycle/long-soak gaps.')
            supplemental.append(path)
    declared=[]
    for e in evidence:
        if e.get('reportEvidenceRole')!='declared runtime input': continue
        d=e.get('data',{}); path=e['path']
        if not isinstance(d,dict): continue
        outcome=d.get('outcome',d.get('pass',d.get('success','not recorded')))
        # Linked records retain source/build freshness in the evidence graph.
        if 'outcome' in d or 'pass' in d or 'success' in d:
            declared.append(f'Declared runtime evidence {path}: outcome={outcome}. '+str(d.get('error','')))
            supplemental.append(path)
    if declared:
        summary=['Runtime evidence explicitly supplied for this report (may be reused; source/build freshness is assessed separately):']+declared+summary
    used={e['path'] for e in entries}|set(supplemental)
    return {'run':name,'summary':summary,'ui_checks':ui,'paths':[e['originalPath'] for e in evidence if e['path'] in used]}

def knowledge_model(capabilities,contract,evidence,index,provenance):
    features=[]
    for c in capabilities:
        details=FEATURES.get(c['title'])
        if details:
            purpose,workflow,connections,status=details
        else:
            purpose=c['interpretation'];workflow='Not yet described in the curated knowledge rules.';connections='Needs knowledge review.';status='Unmapped feature'
        supported=not c.get('missing') and bool(c.get('evidence'))
        features.append({'name':c['title'],'purpose':purpose,'workflow':workflow,'connections':connections,
            'implementation_status':status if supported else 'Unknown — required source support missing',
            'source_supported':supported,'limits':c['limits'],'evidence':c.get('evidence',[]),'missing':c.get('missing',[]),
            'interpretation_status':'Curated description corroborated by lexical source evidence; not automatic semantic proof'})
    covered={e['file'] for f in features for e in f['evidence']}
    interpreted=set(latest_validation(evidence).get('paths',[]))
    evidence_gaps=[{'path':e['path'],'reason':e.get('error','No summary rule; manual interpretation required')} for e in evidence if e.get('error') or e['path'] not in interpreted]
    return {'evidence_review_required':evidence_gaps,'version':VERSION,'provenance':provenance,'features':features,'validation':latest_validation(evidence),
            'catalogue':contract.get('feature_catalog') or [],
            'source_files_without_direct_feature_citation':[s['path'] for s in index if s['path'] not in covered],
            'unmapped_capabilities':[c['title'] for c in capabilities if c['title'] not in FEATURES]}

def generate(root,out,state,index,capabilities,contract,evidence,provenance=None,inventory=()):
    provenance=provenance or {'mode':'fresh scan','evidenceTimestamp':state.get('finishedUtc'),'sourceCheck':'Source hashes checked during scan; see run-state.json.'}
    model=knowledge_model(capabilities,contract,evidence,index,provenance)
    validation=model['validation']; features=model['features']
    # User direction is a separately dated input, never implementation evidence.
    direction=root/'STARLIGHT-PRODUCT-DIRECTION.md'
    dirty=state.get('gitEnd',state.get('gitStart',{})).get('status',{}).get('stdout','').splitlines()
    model['changed_source_review_required']=[line[3:] for line in dirty if line[3:].startswith(('editor/','deep/'))]
    model['product_direction']=None
    if direction.is_file():
        direction_bytes=direction.read_bytes()
        (out/direction.name).write_bytes(direction_bytes)
        model['product_direction']={'status':'proposed; not implementation evidence',
            'source':direction.as_posix(),'document':direction.name,
            'sha256':hashlib.sha256(direction_bytes).hexdigest(),
            'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'provenance':'Current user-direction document captured at generation, independently of the source/test snapshot.',
            'content':direction_bytes.decode('utf-8')}
    graph=feature_evidence.connect(model,index,evidence,inventory)
    model['feature_status_report']='feature-status.json'
    feature_evidence.write_report(out,graph)
    write_json(out/'engine-knowledge.json',model)
    engine_comparison.generate(out,model,graph)
    policy='Treat `Starlight Documents` as outdated historical context until the user explicitly changes that instruction. The current source snapshot and dated validation take precedence. A roadmap entry, source marker and passing test are three different kinds of evidence.'
    purpose='Starlight is a native Windows world/scene creator and D3D12 rendering/simulation toolkit, accompanied by a separate native project-module runtime. Its purpose is approachable creation: place content, shape a world, adjust its appearance, save it and preview its behavior through a familiar classic interface.'
    supported=sum(f['source_supported'] for f in features)
    start=['# Starlight — start here','',purpose,'', '[Engine comparison](ENGINE-COMPARISON.md): documented capabilities, dated references, and explicitly unmeasured performance.', '',
      'This is the compact context for an assistant joining the project. Read it first, then consult the feature reference by topic. Routine questions should not require rereading source files or the raw audit logs.','',
      f'Knowledge generator {VERSION}; source/evidence snapshot: {provenance.get("evidenceTimestamp","unknown")}. Generation mode: {provenance["mode"]}.', '',
      '## What the creator lets someone do','',
      '- Build a scene using shapes, imported static GLB models, nature props and chaos/light/smoke effects.\n- Shape and paint finite terrain; sculpt object meshes; place, transform and snap objects.\n- Configure environment, day/night, fog, lights and ray-tracing quality.\n- Add finite water/lava pours and configure physical props.\n- Draw routes and preview with Play/Pause/Reset, repeat and follow controls.\n- Save/reopen scenes, undo/redo edits, use local command automation, and play local music.\n- Launch separate native projects through project manifests and presets.', '',
      'These are the intended current-source workflows summarized below. Implementation coverage and runtime validation remain separate; if required source markers disappear, the affected feature is downgraded to unknown in the feature reference.', '',
      '## How the parts fit together','',
      'The Win32 editor owns controls and authored scene edits. NativeSession owns background rendering/simulation work and receives scene/camera/playback changes. Scene geometry is encoded into a native packet for the D3D12 viewport. Jolt supplies body simulation; TerrainFlow supplies terrain-bound liquid transport. Raster or inline DXR produces scene lighting, followed by the selected denoiser where applicable and atmosphere/composition/presentation.', '',
      'The active Play command uses the main canvas and session in both window modes; secondary-preview code remains in the source but does not define the current Play workflow. The project host loads Empty or SampleGame project DLLs. That project runtime—including its skinned crowd workload—is distinct from the creator’s static GLB import and scene Play. Shared renderer lineage does not establish identical capabilities or game-export parity.', '',
      '## Decisions to preserve','',
      '- Pure native C++ product and the simple classic interface; avoid unsolicited complexity or redesign.\n- Native 1:1 render resolution, no super resolution, no frame generation. Optional RR remains native 1:1.\n- Denoiser ABI: **0 = native temporal wavelet; 1 = Ray Reconstruction**.\n- Preserve scene files, undo/save behavior, existing uncommitted work and rollback evidence.\n- Long audits: compile, launch detached with a progress GUI, then yield. Review only after the user says Completed; do not poll or stream.\n- Historical work, paused automation and sibling projects are not an implementation queue.', '',policy,'',
      '## What is partial or still a future direction','',
      'The editor has a finite heightfield and static model preview, not a delivered streamed world or full animated character pipeline. Liquids are a surface grid, not a general 3D pressure solver. Compressed texture loading is not a complete editor PBR library/residency manager. Local music is not spatial game audio. Routes are not navigation; the adventure controller adds basic static-box walking, not a full character locomotion system. The persistent range pool is not a general task graph; parallel CPU preparation is not multithreaded D3D12 recording. General game export/cooking, portable distribution and cross-platform/VR shipping are not established by this snapshot.', '',
      '## Current validation and unresolved state','',f'Latest captured audit evidence: **{validation["run"] or "unavailable"}**. These tests were not rerun by the knowledge generator.','']
    start += ['- '+s for s in validation['summary']]
    start += ['', 'Treat failed rendering measurements as failures, not FPS evidence. The retained optimization report is a proposal for removing repeated work and improving concurrency, not proof those broad refactors were implemented.', '',
      '## Where to look next','',
      '- [Feature reference](STARLIGHT-FEATURE-REFERENCE.md): what each system does, how to use it, relationships, implementation limits and evidence.\n- [Workflow guide](STARLIGHT-WORKFLOWS.md): common creation paths and boundaries.\n- [Knowledge coverage](KNOWLEDGE-COVERAGE.md): freshness, unmapped areas and what this tool cannot infer.\n- [Technical evidence summary](Starlight-Current-Engine-Summary.md): byte coverage, build/source/binary details and saved results.\n- [Contracts and resources](contracts-and-resources.md) and [source atlas](source-atlas.md): implementation lookup only when needed.', '',
      f'{supported}/{len(features)} curated subsystem descriptions have their required source markers. This count measures documentation support, not product completeness.', '',
      'Use source code when implementing a change, resolving a contradiction or investigating an uncovered behavior. Refresh this knowledge pack after changes; do not repeat broad discovery merely to answer a feature question.']
    if model['product_direction']:
        start += ['', '## User product direction — separate from implemented features','',
            '[Three creation modes: Interactive, Beginner and Advanced](STARLIGHT-PRODUCT-DIRECTION.md). Interactive proposes a playable building experience that reconciles construction into the project. This is future direction, not a shipped three-mode workflow. Close precedents exist; the linked comparison does not support a first-ever claim. The direction document was captured when this pack was generated, separately from the older source and test evidence.']
    start += ['', '[Generated feature/evidence briefing](GENERATED-FEATURE-BRIEFING.md)', '', '## Coverage warnings', '', f"{len(model['evidence_review_required'])} evidence files need interpretation; {len(model['changed_source_review_required'])} changed product paths need semantic review. See KNOWLEDGE-COVERAGE.md. These are review flags, not proof of engine failure."]
    (out/'STARLIGHT-START-HERE.md').write_text('\n'.join(start)+'\n',encoding='utf-8')
    ref=['# Starlight feature reference','',purpose,'',policy,'',
         '## Reading the status labels','',
         '**Implemented** means the curated execution path has corroborating source markers. **Partial** identifies a working slice with important missing product scope. **Planned/scaffolded** comes from catalogue metadata and is not delivered functionality. **Unknown** means required support is missing. No feature is marked universally complete merely because its name appears in source. Runtime check outcomes are dated separately.', '',
         '## Feature map','', '| System | What it is for | Implementation coverage |','|---|---|---|']
    for f in features:ref.append(f'| {f["name"]} | {escaped(f["purpose"])} | {escaped(f["implementation_status"])} |')
    for f in features:
        ref += ['', '## '+f['name'],'', '**Purpose:** '+f['purpose'],'', '**Implementation:** '+f['implementation_status']+'.','',
                '**User workflow / behavior:** '+f['workflow'],'', '**Connections:** '+f['connections'],'', '**Limits:** '+f['limits'],'',
                '**Evidence:** Source support is corroboration, not a new runtime test.']
        for e in f['evidence']:ref.append(f'- [{e["file"]}:{e["line"]}]({(root/e["file"]).as_posix()}:{e["line"]})')
        for m in f['missing']:ref.append(f'- Required support missing: `{m["file"]}` / `{m["pattern"]}`. Description above requires review.')
    ref += ['', '## Saved UI behavior checks','', f'These named interactions were exercised in `{validation["run"] or "unknown run"}`. They are a useful detailed behavior index, not fresh verification of future changes.', '', '| Interaction | Recorded result |','|---|---|']
    for c in validation['ui_checks']:ref.append(f'| {escaped(c.get("name","unnamed"))} | {"Pass" if c.get("pass") is True else "Fail/unknown"} |')
    ref += ['', '## Catalogue and planned product scope','',
            'These are the actual stored catalogue entries. Status, Available, descriptions and requested Proof are metadata; they can lag implementation. Read the source-supported system descriptions above before interpreting an unavailable catalogue flag. A Proof field is an acceptance aspiration, not a passed test.', '',
            '| Catalogue feature | Description | Stored status / availability | Dependencies | Requested proof (not a test result) |','|---|---|---|---|---|']
    for c in model['catalogue']:
        ref.append('| '+' | '.join(escaped(x) for x in (c.get('Name'),c.get('Description'),f'{c.get("Status")} / {c.get("Available")}',c.get('DependsOn'),c.get('Proof')))+' |')
    (out/'STARLIGHT-FEATURE-REFERENCE.md').write_text('\n'.join(ref)+'\n',encoding='utf-8')
    workflows=['# Starlight workflows and system relationships','',
      'This guide explains the current-source product model. It is not a new end-to-end UI test. Check the feature reference for current evidence/status before assuming an edge case is supported.', '',
      '## Create and save a world','',
      'Start with an authored scene. Use Builder to select terrain, shapes, nature or sculpt tools. Shape/paint the ground, place props or import a static GLB, then adjust position, rotation, scale and snapping. Beginner controls hide unrelated advanced values; advanced Build/Body controls expose more detail. Edits update SceneData and history, then the session prepares a render packet. Save writes authored state with revision protection; it does not serialize a full live physics/fluid checkpoint.', '',
      '## Shape terrain versus sculpt an object','',
      'Terrain brushes edit the finite ground heightfield and paint categories. A selected-shape stamp samples its upper surface into terrain. Object sculpting edits a copied mesh with Pull/Push/Smooth/Flatten and its own brush settings. Terrain rendering, collision tiles and fluid-grid sampling use different representations; a denser preview mesh does not add independent authored height samples. Neither workflow establishes caves, booleans or general remeshing.', '',
      '## Add motion and simulation','',
      'Draw a route for a placed object and set its speed, or configure dynamic props and liquid sources. Ground-click pours are placement operations and participate in undo. Play drives routes, bodies and flow; Pause holds simulation and Reset returns to the authored start. Route repetition/follow is distinct from skeletal locomotion and obstacle navigation. Visual material/lighting animation has separate timing concerns, so paused simulation is not automatically a frozen render reference.', '',
      '## Preview without losing the editor','',
      'The active Play command uses the primary editor canvas and session in both window modes. Retained secondary-preview code is not evidence of the current button workflow. Escape first cancels active drags/pours or closes popups/settings before leaving fullscreen. Preview resources are independent from the authoring view; general multi-viewport shared-resource pooling is not established.', '',
      '## Choose appearance and rendering quality','',
      'Adjust environment/time/day cycle, fog, local lights and surface categories. Native raster is available; optional inline ray-tracing tiers change visibility, AO/reflection/GI/path transport. Mode 0 chooses native wavelet denoising, mode 1 requests native-resolution RR. Both preserve the no-super-resolution/no-frame-generation mandate. Shader emission, explicit lights, volumetric approximation and physical simulation are distinct; a luminous lava/rock surface does not imply heat, fracture or a full fluid solver.', '',
      '## Launch a project versus play a creator scene','',
      'A creator .starscene stores an authored scene. The project modal selects a .star manifest, scene and .startest settings; the native host loads the project DLL. Preset checking/reconciliation is part of that second workflow. Empty and SampleGame/crowd modules do not mean every creator feature has an equivalent exported game path. A general packaging/export wizard and clean-machine asset portability remain unestablished.', '',
      '## Work through local automation','',
      'Use describe/inspect to learn capabilities and scene state, then submit validated operations with an expected scene hash. Failed/stale edits should not overwrite newer work. GUI-only behavior is not automatically available through commands, and the executable contains no embedded cloud AI service. Music remains a separate local editor convenience.', '',
      '## Diagnose or extend a system','',
      'Read STARLIGHT-START-HERE.md, then the relevant feature section. Consult its source links only for implementation, a contradiction or missing detail. Use the saved audit evidence to identify what failed or was not reached. The optimization plan is a proposal; do not assume a cache/scheduler/offscreen redesign has shipped. After changes, refresh the knowledge pack and keep historical evidence tied to its original source/binary snapshot.']
    (out/'STARLIGHT-WORKFLOWS.md').write_text('\n'.join(workflows)+'\n',encoding='utf-8')
    coverage=['# Knowledge freshness and coverage','',
      'The main deliverable is engine understanding, not file-count statistics. The byte inventory and source atlas support traceability. This tool combines curated explanations, automatic source markers, the actual feature catalogue and dated saved tests. It is not a model that discovers arbitrary new feature semantics on its own.', '',
      '## Provenance','', '```json',json.dumps(provenance,indent=2,ensure_ascii=False),'```','',
      '## What may require code investigation','',
      '- New behavior not represented by the curated feature rules.\n- Source changes after the recorded snapshot.\n- A claim with missing markers, an unknown status, or contradictory test results.\n- Exact algorithms, concurrency/lifetime bugs, compiler reachability or runtime quality/performance.', '',
      'No source count, hash, catalogue label or lexical match establishes that every possible feature or runtime behavior has been understood. The reference reduces repeated discovery; it does not replace targeted engineering review.', '',
      '## Unmapped capability names','']
    coverage += ['- '+x for x in model['unmapped_capabilities']] or ['None in the current curated capability set.']
    coverage += ['', '## Source files without a direct feature citation','',
      'These may be support files, tests, generated code or tooling. They remain indexed in the source atlas; lack of a direct feature citation is not dead-code evidence. New entries should prompt a documentation review.','']
    coverage += ['- `'+p+'`' for p in model['source_files_without_direct_feature_citation']]
    coverage += ['', '## Evidence requiring interpretation', '', 'New or unrecognized evidence is not a pass and is not silently dropped.']
    coverage += ['- '+x['path']+' — '+x['reason'] for x in model['evidence_review_required']] or ['None.']
    coverage += ['', '## Changed product source requiring semantic review', '', 'Source markers do not certify that descriptions cover these changes.']
    coverage += ['- '+x for x in model['changed_source_review_required']] or ['None recorded.']
    (out/'KNOWLEDGE-COVERAGE.md').write_text('\n'.join(coverage)+'\n',encoding='utf-8')
    return model

def regenerate(run,out,root,extra_evidence=None):
    """Recompose saved evidence without a new scan or altering the original seal."""
    seal=read_json(run/'artifact-hashes.json')
    required=['run-state.json','source-index.json','capabilities.json','contracts-index.json','saved-evidence.json']
    if (run/'byte-manifest.jsonl').exists():required.append('byte-manifest.jsonl')
    for name in required:
        if seal.get(name)!=file_hash(run/name):raise ValueError('Saved evidence seal mismatch: '+name)
    state=read_json(run/'run-state.json');index=read_json(run/'source-index.json')
    inventory=[json.loads(line) for line in (run/'byte-manifest.jsonl').read_text(encoding='utf-8').splitlines()] if 'byte-manifest.jsonl' in required else []
    changed=[];checked=0
    verification={s['path']:s for s in inventory if s.get('stable') is True}
    verification.update({s['path']:s for s in index})
    for s in verification.values():
        p=s['path']
        # Tool/report implementation changes are intentional. Product source must match.
        if not (p in ('CMakeLists.txt','CMakePresets.json','Build-Native.py') or
                (p.startswith(('editor/','deep/')) and Path(p).suffix in ('.cpp','.hpp','.h','.inl','.hlsl','.cmake'))):continue
        checked+=1
        if not (root/p).is_file() or file_hash(root/p)!=s.get('sha256'):changed.append(p)
    if changed:raise ValueError('Product source changed; refresh source analysis before presenting current knowledge: '+repr(changed))
    if out.exists():raise ValueError('Use a new knowledge output directory; previous output is preserved.')
    out.mkdir(parents=True)
    provenance={'mode':'knowledge documents regenerated from sealed evidence; no new byte census or runtime tests',
        'evidenceRun':str(run),'evidenceTimestamp':state.get('finishedUtc'),'generatedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'productSourceFilesRehashed':checked,'sourceCheck':'All indexed product source/build inputs matched the evidence snapshot. New/unindexed files are not established by this check.'}
    evidence=read_json(run/'saved-evidence.json')
    if extra_evidence is not None:
        extra=read_json(extra_evidence)
        if not isinstance(extra,list): raise ValueError('Extra evidence must be a list')
        provenance['declaredRuntimeInput']={'path':str(extra_evidence),'sha256':file_hash(extra_evidence),
            'limits':'Saved runtime evidence; no tests rerun and no current-build equivalence implied.'}
        supplied={e['path'] for e in extra}
        evidence=[e for e in evidence if e['path'] not in supplied]
        evidence.extend(dict(e,reportEvidenceRole='declared runtime input',reportEvidenceOrigin=str(extra_evidence)) for e in extra)
    model=generate(root,out,state,index,read_json(run/'capabilities.json'),read_json(run/'contracts-index.json'),evidence,provenance,inventory)
    # Keep links to the original large supporting artifacts rather than duplicate them.
    for name in ('STARLIGHT-START-HERE.md',):
        text=(out/name).read_text(encoding='utf-8')
        for artifact in ('Starlight-Current-Engine-Summary.md','contracts-and-resources.md','source-atlas.md'):
            text=text.replace(']('+artifact+')',']('+ (run/artifact).as_posix()+')')
        (out/name).write_text(text,encoding='utf-8')
    write_json(out/'knowledge-artifact-hashes.json',{p.name:file_hash(p) for p in sorted(out.iterdir()) if p.is_file()})
    return model

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--from-run',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--root',type=Path,required=True);p.add_argument('--extra-evidence',type=Path);a=p.parse_args()
    result=regenerate(a.from_run.resolve(),a.output.resolve(),a.root.resolve(),a.extra_evidence)
    print('Knowledge pack generated:',a.output,'; features:',len(result['features']))
