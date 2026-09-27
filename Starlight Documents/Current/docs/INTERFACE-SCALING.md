# Interface size

Open the hamburger menu and choose **Interface size**:

- **Auto-fit window checked** reduces the interface as the window shrinks and restores
  it as the window grows. It uses stable 5% bands between 25% and 100%, relative
  to a 1280 × 800 logical-pixel workspace, with a small tolerance to prevent jitter.
- **Auto-fit window unchecked** enables the manual 25–200% slider. Choose any whole percentage. Arrow
  keys adjust by 1%; Page Up/Down adjust by 5%. The percentage updates while
  dragging; the interface resizes on release so the slider stays under the pointer.
  Window resizing does not change a manual setting. Switching back from Auto
  restores the last manual value.

Auto is the initial choice for new users; existing preferences are preserved.
The choice is saved for this editor user in `interface-size.json`; it does not
change Windows display settings or the scene. The editor combines this choice
with the window's DPI when Windows moves it between displays.

Text, buttons, vector icons, menus, settings, Help, character controls, the asset
browser and project controls use the application scale. The rendered scene stays
at the viewport's native resolution. Scene values, hidden advanced settings and
Undo history are not rewritten when the interface size changes.

Small windows expose oversized panel contents through scrollbars. Scroll normally
to move vertically, or hold Shift while scrolling to move horizontally in Help,
More, Music and the asset browser. Tab brings focused controls into view. Long
Help lessons and asset lists also have their own scrolling areas.

Automated interface checks use isolated scenes and do not overwrite the user's
saved interface-size preference. Direct DPI-message tests exercise the editor's
response without changing Windows settings; they do not substitute for a physical
multi-monitor transition test.
