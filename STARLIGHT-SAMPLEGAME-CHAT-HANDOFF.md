# Starlight Engine and SampleGame — high-level chat handoff

**23 September tool consolidation:** canonical engine tools are grouped under `tools/`; see `tools/README.md`, `TOOLS-MAP-2026-09-23.md`, and `tools/tool-relocations-2026-09-23.json`. Historical evidence paths remain unchanged. Old script entry points forward to canonical files. Audit integration and per-tool code-alignment review remain unfinished.

## Session stopping point — 18 September 2026

### Future organization task — Chaos's closing note

### File origins and possible project mixing — Chaos's additional closing context

Chaos reports that earlier sorting across successive projects involved moving, retaining and deleting files, and she is concerned some projects may have been mixed accidentally. This is a concern to investigate, not a confirmed finding. Folder placement and filenames alone must not be treated as proof of project ownership.

SampleServer needs particular care. Chaos described prior work involving an existing game and Nexus Forever code, a GPL/AGPL-versus-MIT licensing decision (the exact license was uncertain in her recollection), and an intention to remove the Nexus Forever code and replace it with her own. Neither the completeness of that removal nor the origins or licensing of the remaining files have been verified here. Do not infer that a rename, relocation or stated rewrite settles those questions.

When this deferred organization task is explicitly resumed, begin with a read-only inventory and provenance map: repository history/remotes, file hashes and backup versions, license/copyright headers and notices, dependency/build references, and relevant saved records. Distinguish project-specific code, shared components, third-party material, historical copies and unresolved origins. Mark uncertain files for review in the inventory rather than guessing, moving or deleting them. Preserve evidence and backups before proposing any reorganization. Any licensing conclusions need separate source-grounded review of the actual material; this note makes no legal determination.

This remains future work. No broad scan, cleanup, deletion, license change or project relocation was requested for this session close.



Project history spans multiple chats, breakdowns, documents, folders and backed-up copies of Starlight Engine. Do not assume the current repository or this conversation contains the entire history, or that every discovered engine copy is active.

Chaos wants a future consolidation into one distinct home for **Starlight Engine notes** and another for **SampleGame notes and project material**. Record this as deferred organization work, not an instruction to start moving files during the session close.

Before consolidation, inventory the relevant locations and existing indexes; distinguish current authoritative material, historical/superseded notes, duplicate copies, source trees and intentional backups. Preserve provenance and rollback copies. Use a manifest/index and preserve or repair references so the handoffs, tools and project paths continue to work. Do not merge engine and game scope, delete backups as mere duplicates, or relocate working code/assets just because documentation is being organized. Choose destination folders and migration scope when the task is resumed.



Chaos asked to save context and is ending this session to conserve usage. She plans to return on **22 September 2026**, when she expects her limits to refresh; this is her stated plan, not a verified account reset time. No reminder, scheduled task or automatic resumption was requested.

**Resume by reading the latest handoff and asking/following what Chaos wants to do next.** Do not restart audits, optimization, engine implementation or asset generation automatically. Current engine readiness still comes from dated saved evidence; documentation does not resolve prior runtime failures.

Preserve the central direction: **Starlight is the broader general-purpose engine; SampleGame is a separate game powered by it; the opening slice showcases the engine without defining its limits.**

Saved results:
- Engine knowledge and research entry point: D:/projects/Starlight-Engine/STARLIGHT-KNOWLEDGE.md
- Portable project context: D:/projects/Starlight-Engine/STARLIGHT-SAMPLEGAME-CHAT-HANDOFF.md
- Full SampleGame opening plan, original clarifications and Codex connections: D:/SampleGame/Design/first-scene-brainstorm-notes.md
- Shareable second-person opening: D:/SampleGame/Design/SampleGame-The-Shattered-World-Opening-Readthrough.txt

The latest narrative ends with the reality-fracturing clash, **SampleGame, the Sample World**, then a fade to black and **Powered by the Starlight Engine**. Chaos approved how the read-through expressed her vision. The intended first environment is selected; detailed gameplay, branching, assets, physics and VR presentation still contain explicitly marked open decisions. The light-faction name heard as “LightOrder” is not resolved against the Codex's LightFaction. No production implementation was performed during this scene-planning portion.



Prepared 18 September 2026 from Chaos's explicit direction. This document is intended to be copied or attached to future chats. It is self-contained; local reference paths at the end are optional supporting material and may not be accessible in another chat.

This is the portable companion to the established D:/projects/_recovery/Chaos-HANDOFF.md and D:/projects/Starlight-Engine/STARLIGHT-CONTINUATION-2026-09-18.md. Both existing handoffs also contain the scope clarification; this file does not replace their detailed technical or other-project history.

## The distinction that must survive every handoff

**Starlight Engine is a general-purpose game engine. SampleGame is a separate game project built on and powered by Starlight Engine. The SampleGame opening slice showcases Starlight; it does not define the engine's entire purpose, scope, roadmap or completion criteria.**

Chaos's direction, faithfully condensed:

> We can target the SampleGame scene with Starlight Engine and build engine features that help make that scene. But the scene is not the whole engine. Starlight is much broader; the slice shows it off.

Do not reinterpret Starlight as a one-scene engine, an SampleGame-only engine, or a technology demo whose development ends when the nightclub sequence works. Equally, do not assume every proposed engine feature must be complete before work on an SampleGame milestone can proceed. Preserve the broader purpose while planning concrete increments.

## What each project is

**Starlight Engine:** The reusable engine and creation tools. Its proposed direction includes Beginner, Advanced and Interactive creation modes. Interactive mode is intended to let people build within a playable experience and carry that work into an editable project. Proposed sculpting, terrain, asset, simulation, AI, VR and collaboration capabilities extend beyond any one SampleGame scene. These proposals are not a claim of current implementation or a fixed release commitment.

**SampleGame:** A separate persistent-world MMORPG project with its own world, lore, characters, combat, quests and assets. Starlight powers it. SampleGame may expose concrete needs that guide engine priorities, but those needs do not replace Starlight's wider roadmap. The game itself also extends far beyond its first environment.

**The opening slice:** Chaos's selected first SampleGame environment and an intended showcase of Starlight's capabilities. It is a bounded game milestone, not evidence that the whole MMORPG or general-purpose engine is complete.

## How development should relate

- Work on Starlight capabilities that help deliver SampleGame while preserving their intended reusable role and the existing game/engine boundary.
- Keep SampleGame's characters, encounter choreography, dialogue, quests, content and scene-specific choices in the game project.
- Keep engine roadmap, engine acceptance criteria and game-slice acceptance criteria distinct. Track what a milestone actually demonstrates.
- A successful scene demonstrates its tested workload. It does not prove arbitrary scale, other platforms, full game export, all proposed features or general engine completeness.
- A temporary scene technique or authored sequence must not be presented as a finished general engine subsystem.
- Do not silently remove broader engine requirements to fit a short showcase estimate. Discuss scope and tradeoffs explicitly.
- Do not promise that the engine must serve every imaginable use case. Its intended breadth still needs concrete requirements, milestones and validation.

## The selected SampleGame opening, in brief

A social evening in a neon nightclub becomes an attack by the six-purple-eyed ShadowEnemy. The protagonist's unusual Sample Bow draws attention before the attack. As the fight begins, friends join her and the combat music responds to bow actions.

The lesser enemies retreat in fear. Music stops; enormous footsteps approach. A giant ShadowEnemy commander tears the building open, leaving the protagonist overwhelmed and injured. People are still inside. Her objective is to defend them until all are out of harm's way, despite being far below the commander's power.

The player is offered a flee/stand choice. Chaos's proposed fleeing path eventually leads toward darkness after rejection by SampleGame; intermediate events and mechanics are undecided. Standing means choosing to protect people and a better future. On that narrated path, her resistance buys time for the forces of light to arrive as she nears death.

The light and dark commanders clash. Their swords fracture reality, echoing the world's origin. The title appears: **SampleGame, the Sample World.** The presentation then fades to black with **Powered by the Starlight Engine**.

The Codex's creation account includes Chaos binding light, dark and matter into SampleGame after reality tears. Preserve that role; the world is more nuanced than an instruction to eradicate an entire playable people. Scene dialogue, branching mechanics, asset details and presentation timing still have open decisions.

## VR direction and scope

Chaos expressed interest in both making VR games and authoring from within VR, eventually targeting both PC-powered and standalone headsets. That is product direction, not evidence of current VR support or a promise that every target ships with the first slice. The opening's cinematic camera movement, rolls and knockdowns need a separately designed headset presentation; the narration is not an approved forced-head-motion implementation.

## Current evidence and its limits

The saved Starlight knowledge pack dated 18 September 2026 describes a native editor, scene authoring/persistence, rendering, basic sculpting and terrain, physics, bounded liquids and a project host. It identifies Empty and SampleGame as project modules and separately describes specialized SampleGame runtime code. This corroborates a game/engine boundary; it does not establish entirely separate repositories, elimination of every dependency, or completed standalone packaging.

The same snapshot does not establish the full proposed sculpting suite, VR, generative AI, collaboration or the finished SampleGame opening. Its saved test evidence includes unresolved lifecycle/rendering failures. Consult current evidence before making readiness, performance or delivery claims; this handoff is not a fresh audit.

Historical SampleGame documents mention browser and Godot prototypes. Those references do not override Chaos's current statement that Starlight powers SampleGame. Older files under Starlight Documents are explicitly historical/outdated unless Chaos says otherwise.

## Instructions for the next conversation

1. Preserve the distinction in the first section when discussing either project.
2. Treat this document as user-provided project context, not proof that its dated implementation snapshot is still current. Later explicit user corrections take precedence.
3. Separate confirmed direction, proposed features, implemented foundations and verified behavior in your answers.
4. Do not infer missing lore, mechanics or implementation details merely to make the plan sound complete.
5. The session that produced this handoff was focused on documentation and planning. Attaching it does not itself request implementation, asset generation, deployment or a new audit; follow the user's actual task in the new chat.
6. If local references are unavailable, use this self-contained context and state what cannot be checked rather than pretending to have read the files.

## Optional local references

- Starlight knowledge entry point: D:/projects/Starlight-Engine/STARLIGHT-KNOWLEDGE.md
- Broader engine direction: D:/projects/Starlight-Engine/STARLIGHT-PRODUCT-DIRECTION.md
- Engine proposal assessment: D:/projects/Starlight-Engine/STARLIGHT-SCULPTING-FEASIBILITY-2026-09-18.md
- Dated engine knowledge pack: D:/projects/Starlight-analysis/knowledge-20260918-v3/
- Competitor/research reality check: D:/projects/Starlight-analysis/research-20260918/REALITY-CHECK.md
- Consolidated SampleGame opening plan: D:/SampleGame/Design/first-scene-brainstorm-notes.md
- Shareable narrative: D:/SampleGame/Design/SampleGame-The-Shattered-World-Opening-Readthrough.txt
- Lore: D:/SampleGame/Codex/SampleGame-Codex.pdf
- Combat and appearance: D:/SampleGame/Design/combat.md and D:/SampleGame/Design/characters.md

These paths are references, not attachments embedded in this file. Carry this handoff into a new chat explicitly; do not assume another conversation automatically has access to it.
