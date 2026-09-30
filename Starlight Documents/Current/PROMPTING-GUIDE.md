# How to write clear project prompts

A practical guide for ChaosFox, using Starlight as the example.

You can speak naturally, change your mind, and add details as you learn. You do not need technical vocabulary or a perfect first prompt. A useful prompt makes the intended outcome and the important limits clear.

**The platform and performance examples below are practice prompts, not authorization to implement them.** Cleanup remains the current task.

## 1. Start with three things

1. **Outcome:** What should be different when the work is finished?
2. **Preservation:** What must remain working or untouched?
3. **Proof:** What checks or results will show that the outcome was achieved?

For a larger task, add the relevant folder, priorities, and circumstances that require a question. This matches OpenAI's guidance to define an outcome, supporting evidence and boundaries. [Official OpenAI guidance](https://developers.openai.com/cookbook/examples/codex/using_goals_in_codex)

## 2. A short reusable template

> I want [specific result] in [project or folder]. Preserve [features, files or behavior]. Leave [excluded work] untouched. Work in this order: [priorities]. Consider it complete when [observable checks pass]. Make routine decisions yourself; ask me when [a consequential detail cannot be determined]. Keep updates [preferred length and frequency], and report blockers promptly.

Use only the parts that matter. A small request can be one sentence.

## 3. Your Starlight example

Your original example named Windows, Linux and iOS, then set targets of 500 FPS, a 400 FPS one-percent low and a 350 FPS point-one-percent low. That clearly establishes direction and numerical targets.

The missing details are:

1. Does platform support cover the editor, exported games, or both?
2. Which devices and operating-system versions must be supported?
3. What features must work for you to consider the engine finished?
4. Which resolution, graphics settings and test scenes apply?
5. Are those FPS targets for your desktop, every supported device, or separate hardware tiers?
6. How long should tests run, how many times, and how will startup frames and background activity be handled?
7. How will the benchmark calculate its low-FPS statistics? Use one documented method consistently.

You do not have to know every answer. You can ask for proposed options before committing to implementation.

## 4. A stronger version of your example

> This is a planning request only. Do not modify the engine yet.
>
> Help me define a completion plan for Starlight on Windows, Linux and iOS. First clarify whether each platform needs the editor, exported games, or both. List the required features and identify what can be verified on the hardware available to us.
>
> After functional support is verified, I want performance targets of at least 500 average FPS, 400 FPS for the one-percent low, and 350 FPS for the point-one-percent low on [specified reference hardware]. Confirm whether “500 FPS” means an average target or a minimum on every frame.
>
> Use [resolution], [graphics settings] and [named scenes]. Propose a repeatable benchmark with a stated duration, repeat count, low-FPS calculation and startup policy. Do not silently apply desktop targets to mobile devices.
>
> Preserve existing working features. Report unsupported requirements and unmet targets honestly. Give me the missing decisions and a staged plan before implementation.

The brackets are details to fill in, not assumptions the assistant should invent. These numbers are desired acceptance targets, not a guarantee that every device can achieve them.

## 5. A cleanup prompt based on this session

> Consolidate the current Starlight workspace so another developer can identify the code, tools, documentation and launch instructions immediately. Preserve current features, required fixtures and useful history. Leave GameProject and Starbus untouched.
>
> Check dependencies, move suspected obsolete material into a reversible holding area, update affected paths, and verify functionality. Run the full Analysis Tool before final acceptance. Delete only material confirmed unnecessary and authorized for removal. If deletion is blocked, report the exact item; do not claim it was deleted.
>
> Prioritize correctness over FPS during this cleanup. Provide a folder map and a record of checks, remaining failures and retained files.

## 6. Change direction clearly

You can say:

1. “Keep the original task. Add this requirement.”
2. “Replace the previous requirement with this one.”
3. “Pause after the current run. Do not start another.”
4. “This is a hypothetical example. Critique it; do not execute it.”
5. “Before doing anything, turn what I just said into a concise prompt for me to review.”

## 7. A quick check before sending

Can the reader tell what to do, what to preserve, and how success will be checked? If so, the prompt is useful. More words are only helpful when they remove a real ambiguity.

Clear prompting helps coordination. It does not make you responsible for the assistant ignoring an instruction, inventing a result, or failing to report a blocker.
