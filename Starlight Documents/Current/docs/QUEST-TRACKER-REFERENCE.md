# Quest tracker implementation requirements

Historical design checkpoint; consult the current task report for implementation status.

## Starlight design

Compact HUD tracker: original quest icon, quest title, indented active objective lines, real current/required counts where applicable, and explicit completion marks. A short action sentence such as 'Collect power cells: 2/3' answers what the player should do next. Rich story text, prerequisites and authored rewards belong in a separate journal/detail view. Completed objectives remain understandable; do not replace all progress with a fleeting notification.

Creator data: stable quest and objective IDs separate from display text; title, description, icon category, ordered or parallel objectives, required count, optional target reference, prerequisites and completion policy. No fabricated rewards, target locations or progress. Return-to-NPC is an optional authored stage, distinct from automatic completion. Pin/unpin and collapse affect display, not quest acceptance or completion. Objective links may show details; map links require an implemented map and valid target.

Runtime: gameplay events update authoritative progress; the tracker only displays it. Distinguish unavailable, available, active, ready-to-turn-in when applicable, and completed. Duplicate events must not double-count unique collection targets or award completion twice. Track scene authoring separately from Play state. Author definitions save with the scene and support Undo. Editor Play and exported games share the same progress evaluator and tracker data. Player save/resume is a separate requirement from saving authored quest definitions and must not be claimed without implementation and tests.

Presentation: readable wrapped text, dark translucent backing, meaningful original icons plus words (not colour alone), restrained progress feedback, bounded panel height and scrolling, no viewport/input blocking. Support the application's scale, small windows and reduced-motion preferences. At 25% the whole interface is intentionally very small; verify clipping separately from readable default size.

## Verified current gap

Source inspected: editor/cpp/gameplay_rules.hpp and scene.hpp. Rules already accept collection, interaction and area events and have a complete-objective action. Runtime stores completed objective strings and a transient message. This is not yet a full quest model with authored objective text, counts, journal, tracked selection, or a quest-icon HUD. These remain work to implement and validate; research is not completion of Task 3.

## Acceptance for implementation

Use an authored demonstration with collect-three, enter-area, and optional return stages. Show 0/3 -> 1/3 -> 3/3 from real events; verify replay does not inflate counts; show completion and the next step correctly. Confirm authoring Undo/save/reload, independent Play reset, identical export behavior, wrapped long text and visible icon at supported sizes. Close owned test windows and preserve the user's scene. Do not expand into navigation, reward economy or invented grind as part of the basic tracker.
