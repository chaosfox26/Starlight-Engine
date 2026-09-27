# Latest addition: flowing water and lava (experimental)

Use the desktop Starlight Editor shortcut (terrain-flow build). Under Build > Terrain, choose **Water pour** or **Lava pour**, then click uphill on your terrain. Press **Play**. Each marker pours a fixed amount for three seconds; the liquid moves downhill and collects. **Pause** holds it; **Reset** removes simulated liquid and keeps your pour markers so you can try again. Terrain settings offers **Remove all pours**. Save stores the terrain and pour setup. Lava moves more slowly.

This first approximation follows terrain only. Placed objects do not block it; water/lava do not interact. No splashes, caves, rigid-body physics or flowing waterfalls. Shores are currently grid-shaped. Still-water controls remain separate. Build menu is the only Terrain/Shapes/Sculpting section switcher; sculpting is Coming soon. Latest authoritative status: D:/projects/_recovery/STARLIGHT-EDITOR-CONTEXT.md. Older guide text below may describe earlier UI.

# Starlight terrain builder — quick start

Open **Launch-Starlight-Editor.cmd** in D:/projects/Starlight-Engine, or use the desktop **Starlight Editor** shortcut. This opens the checked terrain-builder build.

## Build a landscape
- Open **▲ Terrain** in the top toolbar. Choose Raise / Lower; hold left mouse to build hills, right mouse to dig. The tool stays selected until you choose Select objects or another editing tool.
- Smooth softens rough ground. Flatten levels toward the height where the stroke began. Hill, Plateau and Crater stamps apply once per click.
- Brush size controls the area; strength controls the change. Paint ground uses grass green, earth brown, sand, rock gray or snow white. These are solid colors, not textures or actual vegetation.
- Water: dig a basin, choose a level, and Apply water level. Enable the lava checkbox for a glowing lava surface instead. This fills all terrain below that level; separate lake levels are not supported yet.
- Stamp selected shape copies its top surface into the ground. The original remains: move it aside or delete it to see and smooth the terrain underneath.
- Hamburger → Save writes terrain, colors, water/lava, shapes, effects and routes into the .starscene file. Open restores them. Ctrl+Z reverses a stroke. Existing saves receive backups.

## Place and adjust objects
Open **▣ Shapes**. Add a cube, block, trapezoid or octagon. Use Move arrows, Turn rings or Stretch handles. The Properties overlay has exact height, size and rotation controls.

Optional snapping is in Shapes: half-metre grid, ground, and nearby object faces. Snapping applies when you release a drag or place a shape. Object snapping uses bounding boxes within 0.25 m, not exact mesh sockets. Ground snap and Keep above floor use terrain height under the object center; very wide objects can intersect slopes. Switch Keep above floor off if you want to bury objects. Ground snap overrides vertical object snapping when both are on.

Scroll to zoom; hold middle mouse and drag to tilt/turn. Hover the canvas edge to travel. Menus overlay the canvas. Right-drag box selection remains planned; right mouse lowers terrain while Raise/Lower is active.

## Fire, lava and lights
Open **☀ Effects** to place a stylized animated flame, lava disc or light. Select objects and adjust them with shape handles. Select an effect, choose its color, brightness and reach, and Apply. Three combined fire/light sources maximum in this basic renderer. Lava discs glow but do not illuminate nearby objects themselves. Fire animation is lightweight geometry motion, not a particle or combustion simulation. Lights illuminate nearby surfaces without shadows or occlusion; they can shine through walls.

## Music
Open **♫ Music**, Choose songs, and select local MP3/WAV files (other offered formats depend on installed Windows codecs). Play/Pause, Next, Stop and Volume are separate from scene playback. The playlist wraps after the last song. Music files stay local, are not copied into scenes, and the playlist currently resets when the editor closes.

## Current boundaries
This is an experimental Windows native DirectX 12 builder. Terrain is 24 × 24 metres with a 0.25 m grid, heights limited to ±20 m. No caves, overhangs, flowing liquids, collisions or physics simulation. Characters still use static geometry and their routes do not automatically follow terrain height or avoid obstacles. Sculpting uploads updated geometry; the 500 FPS cap is a ceiling, not a guarantee. Large worlds, terrain textures, skeletal walking and complete game export remain planned.

Native scene playback: windowed Play uses a separate native window, full-screen Play uses the canvas. Play/Pause, play-once completion, Repeat and Reset are available. Repeat restarts the scene's routes together after the longest route completes.

Checked: terrain strokes and ray picking; raise/lower/smooth/flatten/stamps/paint; water; shape conversion; all three snapping options; undo; save/reopen; native images and lighting changes; animated fire/lava; local silent WAV decoding/play/pause/stop. Real music listening quality and extensive hands-on usability still need your feedback.
