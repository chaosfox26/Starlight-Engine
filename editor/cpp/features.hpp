#pragma once
namespace starlight::editor {
inline constexpr char feature_catalog[] = R"FEATURES([
  {
    "Id": "native-play",
    "Name": "Native Play",
    "Description": "Run the scene you made in the real game engine. Windowed: a game window. Full screen: inside the canvas.",
    "DependsOn": "Saved scenes and renderer connection",
    "Proof": "Saved characters and routes run correctly; stopping returns to editing.",
    "Status": "experimental",
    "Available": false,
    "Label": "Experimental — available in editor"
  },
  {
    "Id": "playback",
    "Name": "Playback choices",
    "Description": "Play once and finish automatically, or repeat your routes. Keep Play/Pause and Reset.",
    "DependsOn": "Route completion and repeat rules",
    "Proof": "Different route lengths, pause, replay and reset all work without losing edits.",
    "Status": "experimental",
    "Available": false,
    "Label": "Experimental — available in editor"
  },
  {
    "Id": "assets",
    "Name": "Assets & reusable objects",
    "Description": "Organize models, pictures and files. Reuse groups of objects in different scenes.",
    "DependsOn": "Portable asset references and import pipeline",
    "Proof": "A copied project opens with its assets on another computer.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "animation",
    "Name": "Animation",
    "Description": "Make characters walk, turn and change between movements.",
    "DependsOn": "Animation import and native scene playback",
    "Proof": "Walking follows the route and transitions without broken poses.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "gameplay",
    "Name": "Gameplay rules",
    "Description": "Build actions such as opening a door or collecting an item without writing code.",
    "DependsOn": "Scene objects, events and saved behaviors",
    "Proof": "A small game can be built, saved and played with working rules.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "physics",
    "Name": "Physics",
    "Description": "Give objects weight and stop them passing through floors and walls.",
    "DependsOn": "Transforms and collision shapes",
    "Proof": "Objects collide reliably at different frame rates.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "navigation",
    "Name": "Navigation",
    "Description": "Help characters find their way around obstacles.",
    "DependsOn": "Walkable surfaces and collision data",
    "Proof": "Characters reach a destination without walking through walls.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "materials",
    "Name": "Materials & lighting",
    "Description": "Choose how surfaces look and light your scene. Advanced lighting stays optional.",
    "DependsOn": "Texture import and renderer materials",
    "Proof": "Saved appearances match native playback with measured performance cost.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "worlds",
    "Name": "Terrain & large scenes",
    "Description": "Experimental 24 × 24 metre terrain: sculpt and stamp terrain, blend detailed procedural surfaces, place rocks and grass, and pour water or lava. Object sculpt brushes are available. Large-world streaming and caves remain planned.",
    "DependsOn": "Assets, scene storage and streaming",
    "Proof": "A large scene loads and moves smoothly within a recorded memory budget.",
    "Status": "experimental",
    "Available": false,
    "Label": "Experimental — available in editor"
  },
  {
    "Id": "2d",
    "Name": "2D games",
    "Description": "Build games with pictures, layers, 2D cameras and tile maps.",
    "DependsOn": "2D rendering, animation and collision",
    "Proof": "Create and export a complete small 2D game.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "audio",
    "Name": "Audio",
    "Description": "Add music, sound effects and sounds that come from objects in the world.",
    "DependsOn": "Audio import, playback and scene events",
    "Proof": "Saved sound settings and spatial audio work in an exported game.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "ui-input",
    "Name": "Game screens & controls",
    "Description": "Build game menus and connect keyboard, mouse or controller actions.",
    "DependsOn": "Input mapping and runtime UI",
    "Proof": "A playable game works with declared input devices and readable UI.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "profiling",
    "Name": "Performance & debugging",
    "Description": "See what slows a scene down and find problems in game rules.",
    "DependsOn": "Runtime timing and diagnostic events",
    "Proof": "CPU, GPU, memory and errors are reported without misleading totals.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "teamwork",
    "Name": "Teamwork & history",
    "Description": "Review changes and safely work on a project with other people.",
    "DependsOn": "Stable asset IDs and conflict handling",
    "Proof": "Conflicting edits can be inspected and recovered without losing work.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "export",
    "Name": "Build & export",
    "Description": "Turn your project into a game other people can install and play.",
    "DependsOn": "Portable assets and runtime packaging",
    "Proof": "A packaged game runs on a clean computer without the editor.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "platforms",
    "Name": "Windows, Linux & macOS",
    "Description": "Create and play 2D and 3D games across the three desktop systems.",
    "DependsOn": "Portable editor, renderer and platform services",
    "Proof": "Editor workflows and exported games pass checks on each system.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "vr",
    "Name": "VR",
    "Description": "Create games for supported headsets and hand controllers.",
    "DependsOn": "Headset runtime, stereo rendering and input",
    "Proof": "An exported VR sample passes tracking and frame-time checks on named devices.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "technology",
    "Name": "Graphics technology updates",
    "Description": "Choose supported upscaling, frame generation and latency versions for each project.",
    "DependsOn": "Vendor integrations, compatibility checks and rollback",
    "Proof": "An update and rollback preserve project settings; native rendering remains available.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "steam",
    "Name": "Steam & cloud saves",
    "Description": "Use your Steam account and keep supported project files in Steam Cloud.",
    "DependsOn": "Own Steam application ID, portable projects and sync conflict handling",
    "Proof": "Private tests restore files on another machine and recover offline conflicts.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  },
  {
    "Id": "ai",
    "Name": "AI editor access",
    "Description": "Let compatible AI assistants use the same editing tools you use.",
    "DependsOn": "Shared commands and full editor coverage",
    "Proof": "Each supported action works through both the GUI and documented AI interface.",
    "Status": "scaffolded",
    "Available": false,
    "Label": "Coming soon — not implemented"
  }
])FEATURES";
}
