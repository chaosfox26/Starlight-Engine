"""Read-only repository census and evidence-backed current-engine documentation.

Run through build_deep_analysis.py / Starlight.DeepAnalysis.exe for a detached GUI.
This performs static analysis, not a renderer benchmark or a correctness proof.
"""
from __future__ import annotations
import argparse, collections, concurrent.futures, ctypes, datetime, hashlib, json
import os, re, struct, subprocess, sys, time, traceback
from pathlib import Path

VERSION = '2.1'
SOURCE_EXT = {'.cpp', '.c', '.h', '.hpp', '.inl', '.hlsl', '.py', '.cmake', '.bat', '.cmd', '.ps1'}
LIVE_PREFIX = ('tools/checks/', 'tools/diagnostics/', 'tools/build/', 'tools/render/', 'tools/profiling/', 'editor/cpp/', 'editor/native/', 'deep/engine/', 'deep/Physics/', 'deep/src/', 'deep/projects/')
TOKEN_RULES = {
    'types': r'\b(?:class|struct|enum\s+class|enum)\s+(\w+)',
    'includes': r'^\s*#\s*include\s*[<"]([^>"\n]+)',
    'native_exports': r'\b(?:API\s+|__declspec\s*\(dllexport\)\s+)[^;{}(\n]*?\b(\w+)\s*\(',
    'function_candidates': r'^\s*(?:[\w:<>*&]+\s+)+([\w:~]+)\s*\([^;]*\)\s*(?:const\s*)?\{',
    'allocation': r'\b(?:new|make_unique|make_shared|malloc|realloc|HeapAlloc)\b|\.(?:resize|reserve|push_back|emplace_back)\s*\(',
    'synchronization': r'\b(?:WaitForSingleObject|WaitForMultipleObjects|flush|notify_all|condition_variable|mutex|atomic|join)\b',
    'threads_jobs': r'\b(?:jthread|thread|async|PersistentWorkers|JobSystemThreadPool|parallelFor|parallel_for)\b',
    'graphics': r'\b(?:Create\w*(?:Resource|PipelineState|CommandList|CommandAllocator)|ResourceBarrier|ExecuteCommandLists|Dispatch|DrawIndexedInstanced|Present|TraceRayInline|BuildRaytracingAccelerationStructure)\b',
    'file_process_network': r'\b(?:CreateFileW|CreateProcessW|LoadLibraryExW|WinHttp\w+|socket|ifstream|ofstream|ShellExecuteW)\b',
    'ui': r'\b(?:CreateWindowExW|CreateWindowW|DestroyWindow|SetTimer|KillTimer|SetWindowPos|MoveWindow|SendMessageW)\b',
    'validation': r'\b(?:throw|validate|isfinite|assert|static_assert)\b',
    'todo': r'\b(?:TODO|FIXME|HACK|XXX)\b',
}

# Interpretations are deliberately separate from automatically collected evidence.
# Each is emitted as supported only when EVERY required source marker exists.
CAPABILITIES = [
 ('Dynamic world physics closeout', 'Movement, physical props, constraints, fracture, traveling projectiles, lightning, particles and bounded fluids share gameplay and presentation. The closeout report records core, visible editor and mixed exported-world checks.', [('editor/cpp/adventure.hpp','advance_projectiles'),('editor/cpp/body_constraints.hpp','JointSegments'),('tools/checks/physics_boundary_checks.inl','Unsupported gameplay fracture'),('docs/PHYSICS-CLOSEOUT-CHECKS-20260927.md','772')], 'Bounded acceptance only. Unsupported fracture uses generic debris; conservative collision, heightfield liquids and fixed fog domains remain. Cloth, soft bodies and ragdolls are absent.'),

 ('Character movement and grappling', 'Shared player controller provides sprint resources, double jump, directional dodge, lunge, crouch, prone, slide and grapple traversal with collision.', [('editor/cpp/adventure_controller.hpp','class AdventureController'),('editor/cpp/grapple_motion.hpp','class GrappleMotion'),('editor/cpp/adventure.hpp',r'grapple_\.step')], 'Source presence is not complete visible or clean-machine validation. Consult the current requirement sign-off for acceptance and remaining gates.'),
 ('Telegraph combat and combat actors', 'Runtime attacks, healing, health, target selection, pursuit and caster-attached cone presentation share the adventure runtime.', [('editor/cpp/adventure.hpp','class AdventurePlayer'),('editor/cpp/adventure.hpp','surface_telegraph_contact'),('editor/cpp/combat_control.hpp','struct CombatActor')], 'Damage and health values are provisional. Finished character assets and complete milestone sign-off remain deferred or open; see current evidence.'),
 ('Crowd control and boss break bars', 'Fourteen control effects, player anti-chain immunity and boss break/stagger/recovery states have explicit runtime representations.', [('editor/cpp/combat_control.hpp','struct CombatControl'),('editor/cpp/combat_control.hpp','struct BossBreakBar'),('editor/cpp/combat_control.hpp','ControlEffect')], 'Boss behavior is distinct from PvP immunity. Per-effect source and tests must be inspected; this catalogue does not certify balance or full gameplay completion.'),
 ('Discoverable editor help', 'Optional searchable lessons share one visible entry point across editing modes.', [('editor/cpp/tutorials.inl','void show_help'),('editor/cpp/tutorial_progress.hpp','decode_tutorial_progress'),('editor/cpp/editor_main.cpp','HelpOpen')], 'Text guidance with saved reading positions; live appearance and input verification pending. Not interactive task tracking.'),
 ('Walk and build preview', 'Exploration and the existing editing tools operate on the same authored scene; gameplay tests use a separate temporary copy.', [('editor/cpp/editor_main.cpp','InteractiveBuild'),('editor/cpp/adventure.hpp','rebuild_for_authoring'),('editor/cpp/classic_completion.inl','explorer->rebuild_for_authoring')], 'Experimental preview. Physical input, complete visual acceptance, objectives, XP and unlocks are not established.'),
 ('Adventure gameplay foundation', 'Saved bounded events, conditions and actions share one runtime across editor Play and Windows export; authoring supports Undo and independent Play state.', [('editor/cpp/adventure.hpp','class AdventurePlayer'),('editor/cpp/gameplay_rules.hpp','class GameplayRulesRuntime'),('editor/cpp/scene.hpp','GameplayRule'),('editor/cpp/editor_main.cpp','gameplay_rule_command')], 'Collection, interaction and spherical area events with item/objective conditions and objective/door/finish actions. Shared character movement and combat now exist; final Step 4/4A acceptance remains open. No arbitrary scripting. Check current validation evidence separately.'),
 ('Windows adventure export', 'Packages the standalone player, engine and C++ runtime libraries with an integrity-checked portable scene.', [('editor/cpp/game_export.cpp','export_playable_game'),('editor/cpp/player_main.cpp','wWinMain'),('CMakeLists.txt','Starlight.NativePlayer')], 'Package validation does not prove visible play, clean-machine compatibility, code signing, or non-Windows support.'),
 ('Native application and build', 'The active build assembles a native Win32 editor, core library, session library, renderer and physics DLLs, native project host, project modules and checks. Python orchestrates compilation; it is not the editor runtime.', [('CMakeLists.txt','add_executable\\(Starlight.NativeEditor'),('editor/cpp/editor_main.cpp','wWinMain'),('Build-Native.py','CMake/Ninja')], 'Windows/D3D12 is the current build contract. Compiling successfully does not validate runtime behavior or portable packaging.'),
 ('Classic creator interface', 'The native editor keeps floating controls over the canvas, beginner/advanced settings, terrain and shape tools, named colors and a full feature catalogue. The catalogue describes both experimental tools and unimplemented ambitions.', [('editor/cpp/editor_main.cpp','advanced'),('tools/checks/classic_ui_checks.inl','beginner hides'),('editor/cpp/classic_completion.inl','catalog|Catalogue|catalogue|feature_catalog'),('editor/cpp/named_colors.hpp','Pink')], 'A catalogue entry is not proof of a working subsystem. Latest saved UI evidence is described separately.'),
 ('Independent game preview', 'Current Play uses the main editor renderer/session in both window modes. Retained GamePreview code can own a secondary session but is not the current Play command path.', [('editor/cpp/classic_completion.inl','void playback_command'),('editor/cpp/classic_completion.inl',r'renderer->playback\(next'),('editor/cpp/game_preview.hpp','NativeSession')], 'Independent views consume additional resources; this is not a demonstrated shared GPU resource pool or a packaged game exporter.'),
 ('Scene data and compatibility', 'SceneData models authored objects, terrain, liquid sources, environment and ray-tracing preferences. Character records also represent props. JSON serialization and validation are native C++.', [('editor/cpp/scene.hpp','SchemaVersion = 7'),('editor/cpp/scene_json.cpp','from_json'),('editor/cpp/scene.cpp','validate')], 'This is a record/vector model, not evidence of a general archetype ECS. Saved authored state differs from a simulation checkpoint.'),
 ('Scene packet ABI', 'The writer emits NativeScene V7 and the viewport accepts versions V1–V7. The binary packet is distinct from JSON scene schema and project/preset schemas.', [('editor/cpp/render_scene.cpp','0x37564c53'),('editor/native/viewport.cpp','0x31564c53.*0x37564c53')], 'Native exports remain active ABI boundaries even though the managed editor was retired.'),
 ('Local automation', 'The native command layer provides describe/inspect and transactional add, move, route, rename, remove, configure, shape, transform and terrain operations with expected scene hashes.', [('editor/cpp/commands.cpp','starlight-editor-local'),('editor/cpp/commands.cpp','expectedSha256'),('editor/cpp/command_runner.cpp','--describe')], 'This is local command automation. It is not an embedded AI model, cloud service, or complete GUI command parity.'),
 ('Persistence and editing safety', 'Platform and command code implement native file handling, hashed scene revisions and save protection; editor history supports undo/redo and canceled edits.', [('editor/cpp/platform.cpp','ReplaceFile|MoveFile'),('editor/cpp/mesh.hpp','SceneHistory'),('editor/cpp/commands.cpp','expectedSha256')], 'Whole-scene history and asset portability need separate memory and distribution policies. No unlimited-history safety claim is made.'),
 ('Terrain authoring', 'Editable terrain has 97×97 height samples at 0.25 m spacing over a 24 m span. It supports brushes, surface painting and denser preview geometry.', [('editor/cpp/scene.hpp','N = 97'),('editor/cpp/scene.hpp','Step = .25, Half = 12'),('editor/cpp/geometry.cpp','193|subdiv|N - 1')], 'A heightfield cannot encode caves or stacked surfaces. Rendering currently rebuilds geometry; this is not world streaming.'),
 ('Object geometry and sculpting', 'Native mesh code provides transforms, bounds/picking and sculpt operations on authored mesh copies, with scene history integration.', [('editor/cpp/mesh.cpp','sculpt|Sculpt'),('editor/cpp/interactions.cpp','snap|Snap'),('editor/cpp/scene.hpp','SculptMesh')], 'Do not infer topology repair, adaptive remeshing, UV authoring or a production sculpting package.'),
 ('Props and effects', 'The scene shape list includes basic solids, rocks, pebbles, grass, fire/light/lava, Chaos Lightning, Chaos Rock, Light-Bound Shard and Craxis Smoke.', [('editor/cpp/scene.hpp','chaos-lightning'),('editor/cpp/geometry.cpp','chaos|lightning'),('deep/engine/chaos_lightning.h','subdivide')], 'Procedural props are not a world scatter, foliage simulation, fracture or particle system.'),
 ('Static model import', 'The native asset path parses GLB geometry and validates accessors, buffers and graph structure for preview meshes.', [('editor/cpp/assets.cpp','load_glb'),('editor/cpp/assets.cpp','accessor|Accessor')], 'Creator static import must not inherit the separate game project crowd runtime’s skinning/animation capability. Full imported material/animation authoring is not established.'),
 ('Reusable assets and scene composition', 'Search asset folders by name and type; insert models, native objects, groups and scene objects. Save native templates/groups, copy selections between scenes, repair missing models, and export portable scene or selection folders with integrity manifests.', [('editor/cpp/asset_browser.inl','asset_browser_command'),('editor/cpp/selection_clipboard.inl','selection_clipboard_command'),('editor/cpp/object_template.cpp','encode_object_group'),('editor/cpp/portable_scene.cpp','export_portable_scene')], 'Text browser has no thumbnails. Native templates exclude imported models; portable scene folders support embedded GLB dependencies. This is editor-content reuse, not standalone game export. GUI visual acceptance and clean-machine validation remain separate.'),
 ('Render ownership and updates', 'NativeSession owns a worker thread with coalesced scene/camera/control input. It exposes status, capture, playback and suspend/resume. Viewport and physics work are owned by the session path.', [('editor/cpp/native_session.hpp','std::jthread'),('editor/cpp/native_session.cpp','NativeSession::run'),('editor/cpp/native_session.cpp','condition_')], 'Resize now preserves the viewport/device and replaces size-dependent images; scene changes can still rebuild geometry. Worker ownership is not a proof of race freedom.'),
 ('Native rendering contract', 'The viewport explicitly selects native output and disables the upscaler request. HDR rendering, spatial edge AA and presentation use the shared D3D12 renderer.', [('editor/native/viewport.cpp','modeRequest="native"'),('editor/native/viewport.cpp','upsRequest="off"'),('editor/native/edge_aa.h','Dispatch|dispatch|numthreads')], 'No super resolution or frame generation is allowed for current acceptance. Native resolution does not guarantee alias-free output.'),
 ('Inline hardware ray tracing', 'DxrPass builds acceleration structures and executes compute inline RayQuery, with selectable tiers and raster fallback. The shader build targets cs_6_5.', [('deep/engine/dxr.hlsl','TraceRayInline'),('deep/engine/dxr.h','BuildRaytracingAccelerationStructure'),('tools/build/cmake_shaders.py','cs_6_5')], 'This is not an RT state-object/shader-table pipeline. Historical device-loss failures remain dated evidence; current throughput requires a successful run with explicit workload, native dimensions and presentation conditions.'),
 ('Native temporal denoiser', 'The native wavelet path implements prefilter, temporal reconstruction, atrous filtering and composition, with history/guide resources.', [('deep/engine/svgf.hlsl','Prefilter'),('deep/engine/svgf.hlsl','Temporal'),('deep/engine/svgf.hlsl','Atrous'),('deep/engine/svgf.hlsl','Composite')], 'ABI mode 0 is native wavelet. Existence of the implementation is not fresh runtime validation or proof of zero ghosting.'),
 ('Optional native Ray Reconstruction', 'Direct NGX integration supplies optional Ray Reconstruction and capability/failure handling. The denoiser API maps mode 1 to RR and mode 0 to wavelet.', [('deep/engine/ray_reconstruction.h','DLAA'),('deep/engine/ray_reconstruction.h','NGX_D3D12'),('editor/native/viewport.cpp','mode==1\\?1:0')], 'RR must remain 1:1. Vendor capability, local SDK/DLL paths and fallback behavior require explicit validation; no super-resolution authorization follows from RR.'),
 ('Atmosphere and lighting', 'Atmosphere compute shaders provide injection, integration and composition; scene settings include time of day, day cycle and fog. Local lights and smoke are packet inputs.', [('deep/engine/atmosphere.hlsl','Inject'),('editor/cpp/scene.hpp','DayCycle'),('editor/native/viewport.cpp','Too many smoke volumes')], 'Bounded froxel fog and procedural skies are not a full weather or fluid-smoke solver. Raster and ray visibility differ.'),
 ('Rigid-body simulation', 'The native physics DLL integrates Jolt with fixed stepping, collision shape preparation and a job system. The editor wrapper consumes body poses and liquid data.', [('deep/Physics/PhysicsWorld.cpp','JobSystemThreadPool'),('deep/Physics/PhysicsWorld.cpp','async'),('editor/cpp/physics.cpp','liquid|Liquid')], 'Async collision cooks and four-worker Jolt scheduling are not a global concurrency budget. High-density coupled performance remains unverified.'),
 ('Water and lava flow', 'TerrainFlow maintains separate water/lava arrays on a 48×48 grid, with 0.5 m cells, obstacle coverage and geometry extraction.', [('editor/cpp/flow.hpp','N = 48'),('editor/cpp/flow.hpp','Cell = .5'),('editor/cpp/flow.cpp','transport'),('editor/cpp/flow.hpp','FlowObstacleMask')], 'This is terrain-bound surface transport, not a general 3D incompressible fluid solver or guaranteed two-way pressure coupling.'),
 ('Routes and simulation controls', 'Objects carry X/Z route points and speed; NativeSession implements repeat/follow and playback/reset behavior.', [('editor/cpp/scene.hpp','WalkPoint'),('editor/cpp/native_session.cpp','repeat_'),('editor/cpp/native_session.cpp','follow_')], 'Route playback is distinct from navigation, skeletal locomotion, collision-aware character controllers and animation graphs.'),
 ('Local music', 'Native music controls use Windows MCI with playlist/playback/volume behavior.', [('editor/cpp/music.hpp','mciSendString|mciSendCommand'),('editor/cpp/classic_completion.inl','music')], 'Editor music is not a spatial audio engine or exported game soundtrack system.'),
 ('Texture foundation', 'The shared texture loader supports DDS block-compressed textures, mip selection and format-aware GPU uploads, alongside existing image loading.', [('deep/engine/model/texture.cpp','loadTextureDDS'),('deep/engine/model/texture.cpp','BC5'),('deep/engine/model/texture.cpp','BC7')], 'A BC5/BC7 loader is not a complete editor PBR material library, offline cooker or asynchronous residency manager.'),
 ('Project host boundary', 'The native host loads project modules through StarlightRunProject; editor project handling validates schema 3 manifests and launches/reconciles presets.', [('deep/src/runtime.cpp','StarlightRunProject'),('editor/cpp/projects.cpp','schemaVersion'),('CMakeLists.txt','foreach\\(project_name IN ITEMS Empty GameProject')], 'Creator scene Play and project-DLL execution are separate workflows. See current publication and closeout evidence for tested export cases; project-host and editor-player workflows remain distinct.'),
 ('Crowd runtime', 'The separate game project contains crowd gathering, pose evaluation and instance-bucket emission using renderer parallel preparation.', [('deep/projects/GameProject/project.cpp','poolThreads'),('deep/projects/GameProject/project.cpp','pose|Pose'),('deep/projects/GameProject/project.cpp','bucket|Bucket')], 'Do not attribute large-crowd figures to the editor or to independent gameplay entities; inherited results are workload-specific.'),
 ('Persistent CPU workers', 'PersistentWorkers uses contiguous ranges, caller participation, condition variables and a shared completion count. Threads are joined on destruction.', [('deep/engine/persistent_workers.h','const int per'),('deep/engine/persistent_workers.h','done_.wait'),('deep/engine/persistent_workers.h','thread.join')], 'This class does not implement a general task DAG, fibers or work stealing. Wider thread counts require measured gains, not utilization alone.'),
 ('Command recording and GPU lifetime', 'The renderer owns command lists/allocators, barriers, submission and frame retirement. CPU parallel preparation does not automatically imply concurrent D3D12 recording.', [('deep/engine/render_device.h','CreateCommandList'),('deep/engine/render_device.h','CreateCommandAllocator'),('deep/engine/render_device.h','ExecuteCommandLists')], 'Future parallel recording needs per-worker/per-slot ownership, reserved bindings and ordered resource transitions.'),
 ('Diagnostics and existing audits', 'Native checks cover core data, controlled diagnostics, textures, UI, sessions and render harnesses. The audit GUI coordinates checks and preserves result files.', [('CMakeLists.txt','native-diagnostics'),('CMakeLists.txt','native-classic-ui'),('tools/audit/audit.cpp','wWinMain')], 'Checks are evidence for their exact run, not blanket certification. This analyzer does not run them or change failed assertions.'),
 ('Diagnostic benchmark path', 'The current source contains auditOffscreen and optional benchmark exports. These are committed diagnostic facilities, not an accepted gameplay rendering mode.', [('deep/engine/render_device.h','auditOffscreen'),('editor/native/viewport.cpp','sl_benchmark')], 'Historical renderer failures are preserved; newer Gate 0 evidence and the reliability checkpoint record an isolated output-target correction. A source scan alone does not certify matched performance or lifecycle reliability.'),
]

def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def category(rel):
    parts = rel.split('/')
    if parts[0] == '.git': return 'git metadata'
    if parts[0] == 'Starlight Documents': return 'outdated historical documents (user-designated)'
    if 'third_party' in parts or rel == 'deep/src/json.hpp': return 'third-party dependency'
    if any(x in parts for x in ('backups', '_backups', 'results', 'source-snapshot')) or '.before-' in rel or rel.lower().endswith(('.bak','.backup')) or parts[0] == 'deep-incomplete-first': return 'historical snapshot/experiment'
    if parts[0] == 'audit-runs': return 'saved audit evidence'
    if rel.startswith('tools/build/') and Path(rel).suffix.lower() in SOURCE_EXT:
        return 'current source candidate'
    if any(x in parts for x in ('out','build','bin','build-native','generated','__pycache__')): return 'build/generated/binary artifacts'
    if rel.startswith(LIVE_PREFIX) and (Path(rel).suffix.lower() in SOURCE_EXT or Path(rel).name == 'CMakeLists.txt'):
        return 'current source candidate'
    if parts[0] in ('tools','migration') and Path(rel).suffix.lower() in SOURCE_EXT: return 'analysis/migration tooling'
    if Path(rel).suffix.lower() in ('.md','.json','.txt','.log'): return 'documentation/configuration/evidence'
    return 'other asset or repository file'

def enumerate_files(root, issues):
    found = []
    def error(e): issues.append({'phase':'enumeration','path':str(e.filename),'error':str(e)})
    for base, dirs, files in os.walk(root, followlinks=False, onerror=error):
        keep=[]
        for name in sorted(dirs):
            p=Path(base)/name
            try:
                if p.lstat().st_file_attributes & 0x400:
                    issues.append({'phase':'enumeration','path':str(p),'error':'Reparse point not followed; external content outside coverage'})
                else: keep.append(name)
            except OSError as e: error(e)
        dirs[:]=keep
        for name in sorted(files):
            p=Path(base)/name
            try:
                st=p.lstat()
                if st.st_file_attributes & 0x400:
                    issues.append({'phase':'enumeration','path':str(p),'error':'File reparse point not followed'})
                else: found.append((p,st.st_size,st.st_mtime_ns))
            except OSError as e: error(e)
    return sorted(found, key=lambda x:x[0].relative_to(root).as_posix())

def streams(path):
    """Enumerate named NTFS data streams; default content is hashed separately."""
    from ctypes import wintypes as w
    class DATA(ctypes.Structure):
        _fields_=[('size',ctypes.c_longlong),('name',w.WCHAR*296)]
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    first=k.FindFirstStreamW; first.argtypes=[w.LPCWSTR,ctypes.c_int,ctypes.POINTER(DATA),w.DWORD]; first.restype=w.HANDLE
    nxt=k.FindNextStreamW; nxt.argtypes=[w.HANDLE,ctypes.POINTER(DATA)]; nxt.restype=w.BOOL
    close=k.FindClose; close.argtypes=[w.HANDLE]
    d=DATA(); h=first(str(path),0,ctypes.byref(d),0)
    if h==ctypes.c_void_p(-1).value:
        err=ctypes.get_last_error()
        if err in (38,): return []
        raise OSError(err,'Cannot enumerate NTFS streams',str(path))
    result=[]
    try:
        while True:
            if d.name != '::$DATA': result.append((d.name,d.size))
            if not nxt(h,ctypes.byref(d)):
                if ctypes.get_last_error()!=38: raise ctypes.WinError(ctypes.get_last_error())
                break
    finally: close(h)
    return result

def digest_file(path):
    h=hashlib.sha256(); n=0
    with open(path,'rb') as f:
        while block:=f.read(4*1024*1024): h.update(block); n+=len(block)
    return h.hexdigest(),n

def hash_entry(root, item):
    p,size,mtime=item
    result={'path':p.relative_to(root).as_posix(),'bytes_at_enumeration':size,'mtime_ns':mtime}
    result['category']=category(result['path'])
    try:
        before=p.stat(); digest,n=digest_file(p); after=p.stat()
        result.update(sha256=digest,bytes_read=n,stable=(size==before.st_size==after.st_size==n and mtime==before.st_mtime_ns==after.st_mtime_ns))
        result['streams']=[]
        try:
            original=streams(p)
            for name,length in original:
                sha,count=digest_file(str(p)+name)
                result['streams'].append({'name':name,'bytes_expected':length,'bytes_read':count,'sha256':sha,'size_matches':count==length})
            result['streams_enumerated']=True
            result['streams_stable']=original==streams(p)
        except OSError as e: result['stream_error']=str(e)
    except OSError as e: result['error']=str(e)
    return result

def pe_summary(path):
    """Bounded PE header/import/export inspection; never loads or executes DLLs."""
    result={'path':str(path)}
    try:
        with path.open('rb') as f:
            if f.read(2)!=b'MZ': return None
            f.seek(0x3c); off=struct.unpack('<I',f.read(4))[0]
            if off>path.stat().st_size-24: raise ValueError('Invalid PE offset')
            f.seek(off)
            if f.read(4)!=b'PE\0\0': return None
            machine,sections,stamp,_,_,optsize,flags=struct.unpack('<HHIIIHH',f.read(20))
            opt=f.read(optsize); magic=struct.unpack_from('<H',opt)[0]
            dstart=112 if magic==0x20b else 96 if magic==0x10b else 0
            if not dstart: raise ValueError('Unknown PE optional header')
            raw=f.read(sections*40); mapping=[]
            for i in range(sections):
                _,vs,va,rs,rp=struct.unpack_from('<8sIIII',raw,i*40); mapping.append((va,max(vs,rs),rp,rs))
            def rva(address):
                for va,length,pos,rawsize in mapping:
                    if va<=address<va+length and address-va<rawsize: return pos+address-va
                raise ValueError('Unmapped RVA')
            def at(address,n): f.seek(rva(address)); data=f.read(n); return data
            def z(address): return at(address,4096).split(b'\0',1)[0].decode('ascii','replace')
            def directory(i): return struct.unpack_from('<II',opt,dstart+i*8) if len(opt)>=dstart+(i+1)*8 else (0,0)
            result.update(machine=hex(machine),sections=sections,coff_timestamp=stamp,pe32plus=magic==0x20b,clr_directory_present=bool(directory(14)[0]),imports=[],exports=[])
            address,length=directory(1)
            if address:
                for i in range(min(length//20,4096)):
                    entry=struct.unpack('<IIIII',at(address+i*20,20))
                    if not any(entry): break
                    result['imports'].append(z(entry[3]))
            address,length=directory(0)
            if address:
                e=struct.unpack('<IIHHIIIIIII',at(address,40)); count=e[7]; names=e[9]
                for i in range(min(count,20000)): result['exports'].append(z(struct.unpack('<I',at(names+4*i,4))[0]))
    except (OSError,ValueError,struct.error) as e: result['error']=str(e)
    return result

def git(root,*args):
    p=subprocess.run(['git','-C',str(root),*args],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30,env={**os.environ,'GIT_OPTIONAL_LOCKS':'0'})
    return {'args':list(args),'exitCode':p.returncode,'stdout':p.stdout,'stderr':p.stderr}

def source_analysis(root, files, out):
    summaries=[]; sources={}; regex={k:re.compile(v,re.M) for k,v in TOKEN_RULES.items()}
    with (out/'source-evidence.jsonl').open('w',encoding='utf-8') as evidence:
        for f in files:
            rel=f['path']
            if f['category'] not in ('current source candidate','analysis/migration tooling') and rel not in ('CMakeLists.txt','Build-Native.py'): continue
            if Path(rel).suffix.lower() not in SOURCE_EXT and Path(rel).name!='CMakeLists.txt': continue
            # Generated byte arrays are fully hashed; avoid treating DXIL bytes as source statements.
            if Path(rel).name in ('dxr_shader.h','atmosphere_shader.h','render_device_editor.h'):
                summaries.append({'path':rel,'classification':'generated source, byte inventory only','sha256':f.get('sha256')}); continue
            try:
                raw=(root/rel).read_bytes(); text=raw.decode('utf-8',errors='replace')
                sources[rel]=text; counts=collections.Counter(); symbols=[]; includes=[]
                for number,line in enumerate(text.splitlines(),1):
                    for kind,pattern in regex.items():
                        for match in pattern.finditer(line):
                            counts[kind]+=1
                            record={'file':rel,'line':number,'kind':kind,'text':line.strip()}
                            evidence.write(json.dumps(record,ensure_ascii=False)+'\n')
                            if kind in ('types','native_exports','function_candidates'): symbols.append({'kind':kind,'name':match.group(1),'line':number})
                            if kind=='includes': includes.append(match.group(1))
                summaries.append({'path':rel,'lines':len(text.splitlines()),'sha256':hashlib.sha256(raw).hexdigest(),'matches_inventory':hashlib.sha256(raw).hexdigest()==f.get('sha256'),'lexical_counts':dict(counts),'symbols':symbols,'includes':includes})
            except OSError as e: summaries.append({'path':rel,'error':str(e)})
    dump(out/'source-index.json',summaries)
    return sources,summaries

def capability_analysis(sources):
    result=[]
    for title,description,requirements,limit in CAPABILITIES:
        evidence=[]; missing=[]
        for rel,pattern in requirements:
            match=re.search(pattern,sources.get(rel,''),re.I)
            if match:
                line=sources[rel].count('\n',0,match.start())+1
                evidence.append({'file':rel,'line':line,'matched':match.group(0),'excerpt':sources[rel].splitlines()[line-1]})
            else: missing.append({'file':rel,'pattern':pattern})
        result.append({'title':title,'interpretation':description,'status':'source markers found; interpretation requires review' if not missing else 'incomplete source support—do not assume implemented','evidence':evidence,'missing':missing,'limits':limit})
    return result

def build_analysis(root,sources):
    declarations=[]
    for rel in ('CMakeLists.txt','deep/Physics/CMakeLists.txt'):
        text=sources.get(rel,'')
        for m in re.finditer(r'\b(add_library|add_executable|add_test|target_link_libraries|target_sources|add_subdirectory|option|set)\s*\(([^)]*)\)',text,re.S):
            declarations.append({'file':rel,'line':text.count('\n',0,m.start())+1,'command':m.group(1),'arguments':m.group(2).strip()})
    # Existing generated build files provide corroboration, not a freshly configured build.
    compiled=[]
    for p in (root/'out/classic').glob('CMakeFiles/impl-*.ninja'):
        for n,line in enumerate(p.read_text(encoding='utf-8',errors='replace').splitlines(),1):
            if line.startswith('build ') and 'CXX_COMPILER' in line: compiled.append({'buildFile':p.relative_to(root).as_posix(),'line':n,'rule':line})
    includes=[]
    for rel,text in sources.items():
        for m in re.finditer(r'^\s*#\s*include\s*"([^"]+)"',text,re.M):
            name=m.group(1); candidates=[(root/rel).parent/name,root/'editor/cpp'/name,root/'deep/engine'/name,root/'out/classic/generated'/name]
            resolved=next((p.resolve() for p in candidates if p.is_file()),None)
            includes.append({'from':rel,'include':name,'resolved':str(resolved) if resolved else None,'method':'heuristic quoted-include resolution; not compiler reachability'})
    return {'cmake_declarations':declarations,'existing_ninja_compile_rules':compiled,'include_edges':includes,'limits':'CMake parsing is lexical; conditions/variables and generated rules require review. No linker reachability or dead-code proof.'}

def saved_evidence(root):
    result=[]
    patterns=['audit-runs/run-*/native-session/session-results.json','audit-runs/run-*/native-session/lifecycle-memory.json','audit-runs/run-*/native-classic-ui/result.json','audit-runs/run-*/tier-*/render.json','out/classic/classic-ui-*.json','migration/classic-completion-validation.json','out/optimization-audit/layouts.json','audit-runs/editor-menu-comparison-*/*checks.json','audit-runs/gate0-*/audit-verdict.json','audit-runs/gate0-*/visible-editor/result.json','audit-runs/gate0-*/visible-presentation/render.json']
    paths={path for pattern in patterns for path in root.glob(pattern)}
    # Discover new evidence families without silently assigning them a verdict.
    paths.update((root/'audit-runs').rglob('*.json'))
    for path in sorted(paths):
        rel=path.relative_to(root).as_posix()
        try:
            if path.stat().st_size > 2*1024*1024:
                result.append({'path':rel,'error':'Evidence exceeds 2 MiB interpretation limit; inventoried separately; review required'})
                continue
            raw=path.read_bytes(); data=json.loads(raw)
            result.append({'path':rel,'sha256':hashlib.sha256(raw).hexdigest(),'data':data,'meaning':'saved evidence only, not rerun or proof of source/binary equivalence'})
        except (OSError,ValueError) as e: result.append({'path':rel,'error':str(e)})
    return result

def contracts(sources):
    """Concrete source schemas/defaults/bindings, separate from product descriptions."""
    result={'records':[], 'feature_catalog':None, 'limits':'Lexical source extracts, not a type-checked ABI/layout model. Packed/wire types and authoring types may differ.'}
    patterns={
        'serialization_field':r'\bj\["[^"]+"\]|\bj\.at\("[^"]+"\)',
        'environment_switch':r'(?:getenv|GetEnvironmentVariable|_wgetenv)|STARLIGHT_[A-Z_]+',
        'shader_binding_or_threads':r'register\s*\(|numthreads\s*\(|\b(?:cbuffer|RWTexture\w*|Texture\w*|StructuredBuffer|RWStructuredBuffer)\b',
        'validation_error':r'throw\s+std::|throw\s+runtime_error|throw\s+Error',
        'native_api':r'\bAPI\b.*\(|__declspec\s*\(dllexport\)',
        'resource_format_or_creation':r'DXGI_FORMAT_|CreateCommittedResource|CreateDescriptorHeap',
        'compile_or_path_dependency':r'nvsdk|\.lib["\s]|[A-Z]:[/\\]|find_package|find_program',
    }
    models={'editor/cpp/scene.hpp','editor/cpp/flow.hpp','editor/cpp/mesh.hpp','editor/cpp/native_session.hpp','editor/cpp/render_scene.hpp','editor/cpp/physics.hpp'}
    for rel,text in sources.items():
        for n,line in enumerate(text.splitlines(),1):
            kinds=[kind for kind,pat in patterns.items() if re.search(pat,line)]
            if rel in models and re.search(r'\b(?:double|float|int|bool|uint\w*|std::|static constexpr|struct|class)\b',line): kinds.append('data_model_declaration')
            for kind in kinds: result['records'].append({'file':rel,'line':n,'kind':kind,'text':line.strip()})
    match=re.search(r'R"FEATURES\((.*?)\)FEATURES"',sources.get('editor/cpp/features.hpp',''),re.S)
    if match:
        try:result['feature_catalog']=json.loads(match.group(1))
        except ValueError as e:result['catalog_error']=str(e)
    return result

def contract_report(root,out,data):
    lines=['# Data, interfaces, resources and operational contracts','',data['limits'],'',
           'The feature catalogue below is product metadata, including future ideas. Its Proof strings describe intended proof, not newly executed verification.','',
           '## Feature catalogue as stored','', '```json',json.dumps(data['feature_catalog'],indent=2,ensure_ascii=False),'```','']
    groups=collections.defaultdict(list)
    for r in data['records']:groups[r['kind']].append(r)
    for kind,records in sorted(groups.items()):
        lines += ['## '+kind.replace('_',' ').title(),'']
        for r in records:
            lines += [f'[{r["file"]}:{r["line"]}]({(root/r["file"]).as_posix()}:{r["line"]})','', '```cpp',r['text'],'```','']
    (out/'contracts-and-resources.md').write_text('\n'.join(lines),encoding='utf-8')

def report(root,out,state,files,index,capabilities,build,evidence,binaries):
    link=lambda p,n=None:f'[{p}{":"+str(n) if n else ""}]({(root/p).as_posix()}{":"+str(n) if n else ""})'
    counts=collections.Counter(f['category'] for f in files); sizes=collections.Counter()
    for f in files: sizes[f['category']]+=f.get('bytes_read',0)
    lines=['# Starlight Engine — current implementation deep dive','',f'Generated {state["finishedUtc"]} by repository analyzer {VERSION}. Repository: `{root}`.','',
      '## What this snapshot represents','',
      'Starlight is a native Windows scene creator and D3D12 rendering/simulation toolkit, plus a separate native project-module runtime. Its current source covers classic world-building controls, authored scenes, static model import, terrain/sculpting, routes/playback, Jolt integration, surface liquids, atmosphere, inline ray tracing and denoising. It is not established here as a complete general-purpose game exporter, streaming world engine, or production-certified runtime. Each subsystem below carries source evidence and limits.','',
      'This describes the **dirty working tree**, not only the last commit. Historical documents—including everything in `Starlight Documents`—are inventoried but never promoted to current implementation truth. Static source markers establish the presence of code; they do not establish successful runtime execution. Existing validation is dated evidence, not new test results.','',
      '## Exact coverage and snapshot limitations','',
      f'- Files inventoried: **{len(files):,}**; default-stream bytes actually read: **{state["bytesRead"]:,}**.',
      f'- Named NTFS data streams hashed: **{state["streams"]:,}**; bytes: **{state["streamBytes"]:,}**.',
      f'- Stable successful file hashes: **{state["stableFiles"]:,}**. Coverage issues: **{len(state["issues"]):,}**.',
      f'- Files added/removed/metadata-changed during scan: **{len(state["treeChanges"]):,}**.',
      f'- Git HEAD: `{state["gitStart"]["head"]["stdout"].strip()}`.',
      f'- Manifest SHA-256: `{state["manifestSha256"]}`.',
      '', 'Every accessible regular file is read to EOF and SHA-256 hashed, including ignored files, vendored code, Git metadata, binaries, experiments and saved evidence. Named NTFS streams are enumerated and hashed separately. Reparse points are recorded and not followed. Directory entries are re-enumerated at the end. This is a live filesystem census, **not an atomic volume snapshot**: concurrent changes may escape metadata checks. Hashes identify content; they do not explain machine-code semantics or prove correctness. ACLs, deleted/unallocated bytes, filesystem metadata streams and external dependencies are outside the content-byte scope. Hard links are counted per path, not as physical disk usage.', '',
      '| File family | Files | Read bytes |','|---|---:|---:|']
    for name,count in sorted(counts.items()): lines.append(f'| {name} | {count:,} | {sizes[name]:,} |')
    lines+=['','Full per-file identities, exceptions and duplicate groups are in `byte-manifest.jsonl`, `run-state.json` and `duplicates.json`. Nothing is deleted or pruned.','',
      '## Source control and build provenance','', '```text',state['gitStart']['status']['stdout'].rstrip(),'```','',
      'The root CMake build defines the active target family. Third-party Jolt, generated shader headers, historical snapshots and sample assets remain separately classified. Current-source classification is a path-based candidate set, not whole-program reachability. Existing Ninja compile rules are included as saved build metadata, not proof the current source was just compiled.','',
      '| Declaration | Arguments | Evidence |','|---|---|---|']
    for d in build['cmake_declarations']:
        if d['command'] in ('add_library','add_executable','add_test','add_subdirectory','option'):
            arg=' '.join(d['arguments'].split()).replace('|','\\|')
            lines.append(f'| {d["command"]} | `{arg}` | {link(d["file"],d["line"])} |')
    lines+=['','## Ownership and execution paths','',
      '```text\nNative Win32 UI / commands\n  -> authored SceneData + undo/save transactions\n  -> NativeSession worker (camera, simulation, scene updates)\n  -> render packet -> viewport C ABI -> D3D12 resources\n  -> raster / inline DXR -> wavelet or native RR -> atmosphere/composite -> presentation\n\nActive Play -> existing editor canvas/session; retained secondary-preview class -> separate session\nNative physics wrapper -> Jolt jobs/collision cooking -> body poses\nTerrainFlow + obstacle coverage -> water/lava surface geometry\n\nProject/preset UI -> native Starlight host -> Empty / game project DLL\nGameProject crowd preparation -> persistent range workers -> serial GPU submission\n```','',
      'This diagram is a source-backed interpretation of the inspected module relationships. It is not a captured runtime trace; consult include/build evidence and subsystem citations.','', '## Implemented subsystems, source support and boundaries','']
    for c in capabilities:
        lines += [f'### {c["title"]}','',f'**Evidence status:** {c["status"]}.','',c['interpretation'] if not c['missing'] else 'Expected interpretation (not fully established by this scan): '+c['interpretation'],'',c['limits'],'']
        for e in c['evidence']: lines.append(f'- {link(e["file"],e["line"])} — `{e["matched"].replace("`", "")}`')
        for m in c['missing']: lines.append(f'- Missing required marker: `{m["file"]}` / `{m["pattern"]}`. Review before asserting this capability.')
        lines.append('')
    lines+=['## Saved validation: successes and failures remain separate','',
      'No renderer, UI, physics or stress test was launched by this analyzer. The following data is read from retained results. Different scene conditions, presentation modes and binaries must not be combined into speedup claims. No valid maximum-FPS result is inferred from a failed or background-throttled run.','',
      '| Evidence | Recorded outcome |','|---|---|']
    for e in evidence:
        p=e['path']; d=e.get('data',{})
        if '/tier-' in p: outcome=json.dumps({k:d.get(k) for k in ('success','error','exitCode','fps','denoiserRequested','rrActive','headless') if k in d})
        elif 'lifecycle-memory' in p: outcome=json.dumps({k:d.get(k) for k in ('cycles','swaps','resourcesRetired','baseline','final','initialHandles','finalHandles','initialPrivateBytes','finalPrivateBytes') if k in d})
        elif 'classic-ui' in p: outcome=json.dumps({'pass':d.get('pass'),'checks':len(d.get('checks',[])),'failed':[c.get('name') for c in d.get('checks',[]) if isinstance(c,dict) and not c.get('pass')],'window_creation_trace':d.get('window_creation_trace')})
        elif 'session-results' in p: outcome=json.dumps({'success':d.get('success'),'checksReached':len(d.get('checks',[])),'error':d.get('error')})
        else: outcome='Structured snapshot preserved in saved-evidence.json; not rerun.'
        lines.append(f'| {link(p)} | {outcome.replace("|", " ")} |')
    lines+=['', 'The retained September 17 run 192850 was unsuccessful: new offscreen rendering lost the graphics device; legacy focused runs failed foreground acquisition; scene lifecycle returned mesh/BLAS/upload counts to baseline but failed its handle threshold. UI passed 52 checks without capturing the earlier helper-window creation. Consult the extracted records above for any later runs; no newer success is assumed or overridden by this historical note.','',
      '## Memory, concurrency and repeated work','',
      'The scanner records allocation/growth, synchronization, thread/job, graphics, UI and validation sites for every current-source candidate. These are lexical review sites—not confirmed leaks, deadlocks, cache misses or performance bottlenecks. Source names and commented/dead code can match. Runtime proof requires compiler/debugger/profiler evidence.', '',
      'Known review areas from the current architecture include complete scene packet rebuilds during simulation, viewport recreation on resize, repeated DXR topology/transform work, physics lookup/scratch churn, full terrain reconstruction, scene-history footprint, concurrent terrain cooking, and denoiser working-set residency. The existing optimization report supplies a staged proposal; it is not evidence those refactors have been implemented.', '',
      '| Source | Lines | Allocation/growth sites | Sync sites | Thread/job sites | Graphics sites |','|---|---:|---:|---:|---:|---:|']
    for s in index:
        if 'lexical_counts' not in s: continue
        c=s['lexical_counts']; lines.append(f'| {link(s["path"])} | {s["lines"]} | {c.get("allocation",0)} | {c.get("synchronization",0)} | {c.get("threads_jobs",0)} | {c.get("graphics",0)} |')
    lines+=['','## Binary and dependency boundary','',
      'PE files are inspected as data, never loaded. Imports, exports, architecture and CLR directory presence are captured in binary-index.json. A CLR directory identifies a managed image; its absence does not prove that no dynamically loaded dependency can use a managed runtime. PE timestamps are metadata, not reliable build provenance. Source-to-binary equivalence requires a reproducible build and is not claimed.','',
      f'PE images inspected: **{len(binaries)}**. External SDK, DLL and original-workspace references are retained in source/build evidence; their content lies outside this repository census.', '',
      'The companion `contracts-and-resources.md` and `contracts-index.json` contain source-linked authoring types/defaults, serialized field names, native exports, shader registers/thread dimensions, resource formats/creation, environment switches, validation errors and the complete feature catalogue. These concrete contracts complement the architectural summary; the stored catalogue mixes roadmap and implemented functionality.', '',
      '## What is not established as a delivered product','',
      'No evidence here establishes complete game export/cooking, portable distribution, general world/texture streaming, a full editor PBR asset library, animated character import/retargeting/navigation, general 3D fluid simulation, arbitrary docking, cross-platform/VR shipping support, a universal ECS/task graph, or safe multithreaded command recording. Absence from these inspected paths is a coverage boundary, not a compiler proof that no related code exists anywhere in the archive. Historical plans and feature catalogue labels do not close these gaps.', '',
      '## Reproducibility and companion artifacts','',
      '- `byte-manifest.jsonl`: one record per enumerated file, exact hashes/read counts, stability and named streams.\n- `run-state.json`: start/end identity, Git state, coverage issues and changes during the scan.\n- `source-index.json`: all current-source candidates, line counts, lexical types/exports/includes and source re-read consistency.\n- `source-evidence.jsonl`: complete matching lines for all analysis categories.\n- `build-graph.json`: CMake declarations, saved Ninja compile rules and heuristic include edges.\n- `capabilities.json`: all report interpretations, required markers and missing evidence.\n- `saved-evidence.json`: preserved test result content and content hashes.\n- `binary-index.json`: PE metadata/imports/exports without executing binaries.\n- `duplicates.json`: identical content groups; no removal recommendation implied.\n- `source-atlas.md`: file-by-file symbol/interface map.\n- `analysis-rules.json`: versioned classification, detection and report interpretation rules.','',
      'The tool is deterministic in its rules but captures a live tree. Re-run into a new output directory after changes; do not overwrite evidence. A complete byte census is not exhaustive semantic verification. Report any read failures or missing source markers before describing this as complete.']
    (out/'Starlight-Current-Engine-Summary.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    atlas=['# Current source atlas','', 'Lexical declarations and includes; not compiler-resolved call graphs. All source bytes are identified in the manifest.','']
    for s in index:
        atlas += [f'## {s["path"]}','',f'SHA-256: `{s.get("sha256","unavailable")}`. Lines: {s.get("lines","generated/error")}.','']
        for symbol in s.get('symbols',[]): atlas.append(f'- {symbol["kind"]}: `{symbol["name"]}` — {link(s["path"],symbol["line"])}')
        atlas+=['','Includes: '+', '.join('`'+x+'`' for x in s.get('includes',[])),'']
    (out/'source-atlas.md').write_text('\n'.join(atlas),encoding='utf-8')

def write_progress(out, percent, message):
    # Readers, antivirus and filesystem filters can briefly hold replacement
    # access on Windows. Bound retries; never turn a persistent error into pass.
    temp=out/f'progress-{os.getpid()}.tmp'
    temp.write_text(f'{percent}\n{message}',encoding='utf-8')
    deadline=time.monotonic()+2
    while True:
        try:
            os.replace(temp,out/'progress.txt')
            return
        except PermissionError:
            if time.monotonic()>=deadline: raise
            time.sleep(.025)


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--root',required=True,type=Path); parser.add_argument('--output',required=True,type=Path); parser.add_argument('--workers',type=int,default=4); parser.add_argument('--extra-evidence',type=Path); args=parser.parse_args()
    root=args.root.resolve(); out=args.output.resolve()
    if out==root or root in out.parents: raise SystemExit('Output must be outside repository for a non-self-referential byte census.')
    out.mkdir(parents=True,exist_ok=True)
    if (out/'byte-manifest.jsonl').exists(): raise SystemExit('Refusing to overwrite an existing analysis run.')
    def progress(percent,message):
        write_progress(out,percent,message)
    state={'analyzerVersion':VERSION,'root':str(root),'startedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'issues':[]}
    try:
        progress(1,'Recording source identity and enumerating every repository file...')
        state['gitStart']={'head':git(root,'rev-parse','HEAD'),'status':git(root,'status','--porcelain=v1','--untracked-files=all'),'submodules':git(root,'submodule','status'),'diff':git(root,'diff','--stat')}
        work=enumerate_files(root,state['issues']); total=sum(s for _,s,_ in work); files=[]; read=0; last=0
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.workers,16))) as pool, (out/'byte-manifest.jsonl').open('w',encoding='utf-8',newline='\n') as manifest:
            # Executor.map retains deterministic manifest order and bounded I/O workers.
            for entry in pool.map(lambda item:hash_entry(root,item),work):
                files.append(entry); read+=entry.get('bytes_read',0); manifest.write(json.dumps(entry,ensure_ascii=False,sort_keys=True)+'\n')
                if time.monotonic()-last>.3:
                    progress(5+int( sixty_ratio(read,total)),f'Hashing {len(files):,}/{len(work):,} files — {read/1e9:.2f}/{total/1e9:.2f} GB read'); last=time.monotonic()
        for f in files:
            if f.get('error') or not f.get('stable') or f.get('stream_error') or not f.get('streams_stable',False) or any(not x['size_matches'] for x in f.get('streams',[])):
                state['issues'].append({'phase':'hash','path':f['path'],'error':f.get('error') or f.get('stream_error') or 'File or named stream changed during read'})
        progress(67,'Analyzing source, interfaces, build relationships and subsystem evidence...')
        sources,index=source_analysis(root,files,out); capabilities=capability_analysis(sources); build=build_analysis(root,sources)
        dump(out/'capabilities.json',capabilities); dump(out/'build-graph.json',build)
        data=contracts(sources); dump(out/'contracts-index.json',data); contract_report(root,out,data)
        dump(out/'analysis-rules.json',{'version':VERSION,'lexicalRules':TOKEN_RULES,'capabilities':CAPABILITIES,'currentSourcePrefixes':LIVE_PREFIX,'historicalRule':'Starlight Documents is outdated unless user says otherwise'})
        for s in index:
            if s.get('error') or s.get('matches_inventory') is False: state['issues'].append({'phase':'source','path':s['path'],'error':s.get('error','Source changed since hashing')})
        progress(76,'Inspecting PE binaries without executing them and reading saved test evidence...')
        binaries=[]
        for f in files:
            if Path(f['path']).suffix.lower() in ('.exe','.dll'):
                b=pe_summary(root/f['path'])
                if b: b['sha256']=f.get('sha256'); binaries.append(b)
        dump(out/'binary-index.json',binaries); evidence=saved_evidence(root)
        if args.extra_evidence:
            extra=json.loads(args.extra_evidence.read_text(encoding='utf-8'))
            if not isinstance(extra,list):raise ValueError('Extra evidence must be a list')
            evidence.extend(dict(item, reportEvidenceRole='declared runtime input',
                                 reportEvidenceOrigin=str(args.extra_evidence.resolve())) for item in extra)
        dump(out/'saved-evidence.json',evidence)
        groups=collections.defaultdict(list)
        for f in files:
            if f.get('sha256'): groups[f['sha256']].append(f['path'])
        dump(out/'duplicates.json',[{'sha256':h,'paths':p} for h,p in sorted(groups.items()) if len(p)>1])
        progress(90,'Checking snapshot consistency and generating the engine summary and source atlas...')
        ending=enumerate_files(root,state['issues']); before={str(p):(s,t) for p,s,t in work}; after={str(p):(s,t) for p,s,t in ending}
        state['treeChanges']=[{'path':p,'before':before.get(p),'after':after.get(p)} for p in sorted(before.keys()|after.keys()) if before.get(p)!=after.get(p)]
        state.update(bytesRead=read,streams=sum(len(f.get('streams',[])) for f in files),streamBytes=sum(x['bytes_read'] for f in files for x in f.get('streams',[])),stableFiles=sum(bool(f.get('stable')) for f in files),manifestSha256=digest_file(out/'byte-manifest.jsonl')[0],finishedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        state['gitEnd']={'head':git(root,'rev-parse','HEAD'),'status':git(root,'status','--porcelain=v1','--untracked-files=all')}
        state['missingCapabilityMarkers']=[c['title'] for c in capabilities if c['missing']]
        state['result']='completed with limitations' if state['issues'] or state['treeChanges'] or state['missingCapabilityMarkers'] else 'completed static analysis (runtime untested)'
        dump(out/'run-state.json',state); report(root,out,state,files,index,capabilities,build,evidence,binaries)
        from knowledge_report import generate
        generate(root,out,state,index,capabilities,data,evidence,inventory=files)
        # Seal generated deliverables independently of the repository manifest.
        seal={p.name:digest_file(p)[0] for p in sorted(out.iterdir()) if p.is_file() and p.name not in ('progress.txt','progress.tmp','worker.log','artifact-hashes.json')}
        dump(out/'artifact-hashes.json',seal)
        progress(100,'Completed — engine knowledge reference is ready. Start with STARLIGHT-START-HERE.md.')
    except Exception:
        (out/'failure.txt').write_text(traceback.format_exc(),encoding='utf-8'); progress(-1,'Analysis failed. Evidence retained; see failure.txt.'); raise

def sixty_ratio(read,total): return min(60,60*read/max(total,1))

if __name__=='__main__': main()
