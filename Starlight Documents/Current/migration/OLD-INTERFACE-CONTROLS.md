# Old interface: source and control inventory

Generated from the preserved editor source on 2026-09-17. This is a static inventory, not a claim that every action was executed. Read OLD-INTERFACE-SPECIFICATION.md for the live walkthrough, runtime placement overrides, quirks, and native parity requirements.

Indexed 49 authored files, 130 XAML control declarations (including hidden/template controls and combo items), 56 event bindings, and 20 catalogue entries.

## Source manifest

| File | Role | Lines | SHA-256 |
|---|---|---:|---|
| [App.xaml:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/App.xaml:1) | interface markup/style | 19 | `843da7eb01ad4bb58fda1ac9df4cebf4953119a272aa62195807458dadccd1c5` |
| [App.xaml.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/App.xaml.cs:1) | implementation | 13 | `8d33972f33679883be13d12c830b831d46f0e885512006195a5ef080c12fa3cc` |
| [AuditChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/AuditChecks.cs:1) | validation | 39 | `37ef7a137ffa3954b9a8336a5c8b36a9e19f6f5652ea8075d447df9072b3cc47` |
| [BuildDock.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:1) | implementation | 83 | `8def3be6d2af1ca8cc96b42944b624552fac1f7dbd395a3c0145c962607631a4` |
| [BuildingTools.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildingTools.cs:1) | implementation | 81 | `59cc0619e14350c6abc705599425227ca540e1600864a0af0081bfc18fd206ab` |
| [ChaosChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ChaosChecks.cs:1) | validation | 41 | `262177b3d0c18d3754f2af7376346a5c543d24f83d601a176790d5c7c97c3717` |
| [ChaosTools.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ChaosTools.cs:1) | implementation | 30 | `ac4fca99461224c55faa9eb5223f4cb152a8198385e8745a31075b8bb42666be` |
| [CharacterSettings.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/CharacterSettings.cs:1) | implementation | 9 | `3f0f8a81b3f2d2a6d591729a3fb2724ad2065dbdd65078df6a3f55e6e9831db9` |
| [CornerControlsChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/CornerControlsChecks.cs:1) | validation | 31 | `47958a183b32fcf62abf48b60b454466796a761901e416ffb9ed9eca063c18ee` |
| [DenoiseChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/DenoiseChecks.cs:1) | validation | 33 | `5cda73dd6aa5ff17dd3a4d6ede3f8c737573e7ac435c3bdef13272b409a2a5f2` |
| [Diagnostics.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/Diagnostics.cs:1) | implementation | 29 | `e44419d59f3fe64d07f99bbd7be9bc14156de7a168dceaf774306760cb1ebd4e` |
| [DialogHistory.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/DialogHistory.cs:1) | implementation | 33 | `5ad0b7d2719fc44e38a21b573cc95821653f54639d4ce52e267ed782106ab1a3` |
| [DxrChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/DxrChecks.cs:1) | validation | 31 | `0352a9f05786fe5d8a42808e9a93745dda09c443c2658cd7071c808110ee922d` |
| [EdgeDimming.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EdgeDimming.cs:1) | implementation | 32 | `487a806346dfc63a3208df7a7f923c5d29ddc62f6b58331d73418cc7051a2395` |
| [EditorChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EditorChecks.cs:1) | validation | 18 | `fa8d4786ecb4712bf96199863ba57de35bd6bc5545a3166f3af2350f4b2b6314` |
| [EditorCommands.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EditorCommands.cs:1) | implementation | 54 | `b422269e92355281030651da5cae7231057b5fcdcde68457b226210cc250575f` |
| [EditorHistory.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EditorHistory.cs:1) | implementation | 15 | `e7f60598f1876b2855f18b4d76f3be6649bce1fe38f0f0de8a49b0554d943a27` |
| [EffectGeometry.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EffectGeometry.cs:1) | implementation | 13 | `b2cd1fb2769df9c06224ed72c391f9ab626bfcc2e26be000cd30f017cc12c273` |
| [EffectsTools.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EffectsTools.cs:1) | implementation | 12 | `2bcacd05d13e67a0c77a05a005aa91eb5b59adc9ab9443bb3ef98313a979d514` |
| [FeatureCatalog.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:1) | implementation | 40 | `fc27661d59e8be90c06036d2d4c8e5c743ad8edc6d345fc608b02a5521d3b5c9` |
| [FeaturePanels.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeaturePanels.cs:1) | implementation | 21 | `ef2177ff4e99d8c8bdbe88e0713a3879970b097080dfa4eb89c623d0d61d57e1` |
| [FlowObstacles.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FlowObstacles.cs:1) | implementation | 17 | `e224b866922a9aa4d013c9a34c03839b1c8bec89b4469edd48b732cc369dfeb2` |
| [FlowTools.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FlowTools.cs:1) | implementation | 33 | `66159e973d3362a4b1d891a682842cbaa8dc442db0988721dd612c6a893f5d4d` |
| [ForegroundFocus.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ForegroundFocus.cs:1) | implementation | 38 | `9456b79e66dc4b8a040142132983aaa2b5bedc26d978f63ae013dae468814031` |
| [FullScreenCanvas.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FullScreenCanvas.cs:1) | implementation | 44 | `8a46e9ec01732e282a8280c1e63460ef6be38e822fc91bad89e12edb22a4714e` |
| [GlbModel.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/GlbModel.cs:1) | implementation | 48 | `a69adeec5a8ee1875bd835c54c1f14a88c34d3b165c2c30f0a87c94982317710` |
| [InteractionChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/InteractionChecks.cs:1) | validation | 34 | `10aa6574633ec067afb3a8fac5580c478bcbdd796b634fb0bd35ad7119d43a55` |
| [LocalMusic.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/LocalMusic.cs:1) | implementation | 21 | `cb8bc564c599f7690d1a09e46ac9b02114476521e44da12b4fc0a4da9f7e55e4` |
| [MainWindow.xaml:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:1) | interface markup/style | 290 | `03454ca6e3fcfc7dd41e3dbd946f8da2cf10bc1847313007bd46c8f770483354` |
| [MainWindow.xaml.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:1) | implementation | 80 | `b2fdef9c983f60baa272a019db283642c195c838e882d8ef344c7b25f7de3ab2` |
| [NativeEditor.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/NativeEditor.cs:1) | implementation | 234 | `df7633c0b3672ce8595da56bffb98ad572fe35d39e72edd60d074793ff2b4b72` |
| [NativeScene.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/NativeScene.cs:1) | implementation | 47 | `8ea01ef7ffbc678f6a22182d40b8c41510101d5a082440e90299128acd4d2424` |
| [NativeViewport.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/NativeViewport.cs:1) | implementation | 96 | `d1a4b2a544eb9f75f57c4a9d325e913dcc81f8e8a823b1bf7d294eb2bcc7dde5` |
| [NatureGeometry.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/NatureGeometry.cs:1) | implementation | 35 | `2542a04d248f9e3e6cb592899083ca2b9a7ca81e13ecf878af08ee67c31e9e00` |
| [ObjectGeometry.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ObjectGeometry.cs:1) | implementation | 40 | `a1ac0a2e9c4b42753c6847ba1f0c3cc3d64e662f89c33ebafb1203a12258f278` |
| [ObjectSnapping.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ObjectSnapping.cs:1) | implementation | 17 | `deebae8fb9af19b998cb34d7b1133f884c68812f781e2da55c4be9ff11714f00` |
| [PhysicsWorld.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/PhysicsWorld.cs:1) | implementation | 64 | `3bbda44ecd49302383c91c07e9b4e4097d82e26827bb8fcf15f2cffb4c700591` |
| [PointerInteraction.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/PointerInteraction.cs:1) | implementation | 38 | `12b81aaf48ce192bcb6acc537cb0001ce828e17f82d578631980cf0738b7425f` |
| [ProjectWindow.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ProjectWindow.cs:1) | implementation | 53 | `d84dba36fc9f97fce9603b3e78fc0ab18701da5c397a8e20b2bd990ae4ad2df1` |
| [SceneData.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/SceneData.cs:1) | implementation | 68 | `16fbc6f76e0979e0ff20fa895a54c85b9c6d1ce3e8daf75230cb0ace29a574b2` |
| [SculptTools.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/SculptTools.cs:1) | implementation | 54 | `accb13cd9dad87e5877bab4b4ee51f4ff3e02b789fe8d75df01f76345ee30962` |
| [SurfaceDetail.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/SurfaceDetail.cs:1) | implementation | 21 | `f128d6e316cf5a1f5166865036fd135b5cba60d398fa0cff0087113540be6ca5` |
| [TerrainChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainChecks.cs:1) | validation | 38 | `d15ece51b8930b3c7da33129b6fc21d90758aa37f1a4220ae94863c7eb7b4ca8` |
| [TerrainData.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainData.cs:1) | implementation | 61 | `6614f9d4b88597a896406c6d2824f68fce5fafe94d6cc8e325d48eead0bc8c90` |
| [TerrainFlow.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainFlow.cs:1) | implementation | 48 | `0e7d89b212d9b66be36e478ffc1f33de5214db853408a3259e66e7af9f9ca648` |
| [TerrainTools.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:1) | implementation | 49 | `7a31369ff2b4e681a4f2ca191cdd0bcb47e525a7a99d4f86090675de2aa4d328` |
| [WindowTheme.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/WindowTheme.cs:1) | implementation | 23 | `de5dc04faf41520a6eb3766c2e975f3e52287f07dfb528a743dbc437842cabd0` |
| [WorldNavigation.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/WorldNavigation.cs:1) | implementation | 7 | `7d26b3d771e83a0f8f2a90998548ea6fc102739ccac87f134e14e6f263a47bc5` |
| [WorldQualityChecks.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/WorldQualityChecks.cs:1) | validation | 32 | `881772de29249e784961ba3c8025a64d9e3c15441539f0d1bb108e6c98fdfc81` |

## Every declared XAML control

Ancestor paths identify the original owning popup. Runtime code changes header order, hides legacy header controls, and reanchors Builder settings; declaration alone is not visible-state proof.

### Window: Window

Source: [MainWindow.xaml:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:1). Parent path: ``.

| Attribute | Value |
|---|---|
| xmlns | http://schemas.microsoft.com/winfx/2006/xaml/presentation |
| xmlns:x | http://schemas.microsoft.com/winfx/2006/xaml |
| x:Class | Starlight.Editor.MainWindow |
| Title | Starlight Engine Editor |
| Width | 1380 |
| Height | 900 |
| MinWidth | 1050 |
| MinHeight | 720 |
| Foreground | #EFF2FA |
| FontFamily | Segoe UI |
| Icon | Assets/starlight.png |
| Closing | OnClosing |

### ToggleButton: TerrainMenuToggle

Source: [MainWindow.xaml:53](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:53). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | TerrainMenuToggle |
| Content | ▲ |
| ToolTip | Choose Terrain, Shapes, or Sculpting. You can also hide the building bar. |
| AutomationProperties.Name | Builder |
| Style | {StaticResource MenuToggle} |

### Popup: BuilderMenuPopup

Source: [MainWindow.xaml:54](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:54). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | BuilderMenuPopup |
| IsOpen | {Binding IsChecked, ElementName=TerrainMenuToggle} |
| PlacementTarget | {Binding ElementName=TerrainMenuToggle} |
| Placement | Bottom |
| StaysOpen | False |
| AllowsTransparency | True |

### Button: Terrain

Source: [MainWindow.xaml:57](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:57). Parent path: `Window / EditorRoot / TopToolbar / RightTools / BuilderMenuPopup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Terrain |
| Tag | terrain |
| Click | ChooseBuilderSection |
| ToolTip | Raise, lower, smooth, and paint the ground. Select again to hide its bar. |

### Button: Shapes

Source: [MainWindow.xaml:58](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:58). Parent path: `Window / EditorRoot / TopToolbar / RightTools / BuilderMenuPopup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Shapes |
| Tag | shapes |
| Click | ChooseBuilderSection |
| ToolTip | Place blocks, then move, turn, and resize them. Select again to hide its bar. |

### Button: Nature

Source: [MainWindow.xaml:59](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:59). Parent path: `Window / EditorRoot / TopToolbar / RightTools / BuilderMenuPopup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Nature |
| ToolTip | Rocks, grass, lightning and glowing props. |
| Tag | nature |
| Click | ChooseBuilderSection |

### Button: Sculpting

Source: [MainWindow.xaml:60](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:60). Parent path: `Window / EditorRoot / TopToolbar / RightTools / BuilderMenuPopup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Sculpting |
| Tag | sculpt |
| Click | ChooseBuilderSection |
| ToolTip | Select an object, then brush to push, pull, smooth, or flatten its surface. |

### Button: Hide building bar

Source: [MainWindow.xaml:61](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:61). Parent path: `Window / EditorRoot / TopToolbar / RightTools / BuilderMenuPopup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Hide building bar |
| Click | HideBuilderBar |

### Popup: TerrainSettingsPopup

Source: [MainWindow.xaml:64](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:64). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | TerrainSettingsPopup |
| Placement | Top |
| StaysOpen | False |
| AllowsTransparency | True |

### Slider: BrushSize

Source: [MainWindow.xaml:69](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:69). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | BrushSize |
| Minimum | 0.4 |
| Maximum | 5 |
| Value | 2 |
| ToolTip | Make the brush cover a smaller or larger area. |

### Slider: BrushStrength

Source: [MainWindow.xaml:70](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:70). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | BrushStrength |
| Minimum | 0.2 |
| Maximum | 4 |
| Value | 1 |
| ToolTip | Choose how gently or quickly the ground changes. |

### ComboBox: TerrainPaint

Source: [MainWindow.xaml:71](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:71). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | TerrainPaint |
| SelectedIndex | 0 |
| ToolTip | Choose the ground color to paint. |

### ComboBoxItem: Detailed grass

Source: [MainWindow.xaml:71](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:71). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel / TerrainPaint`.

| Attribute | Value |
|---|---|
| Content | Detailed grass |

### ComboBoxItem: Rich earth

Source: [MainWindow.xaml:71](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:71). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel / TerrainPaint`.

| Attribute | Value |
|---|---|
| Content | Rich earth |

### ComboBoxItem: Sand

Source: [MainWindow.xaml:71](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:71). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel / TerrainPaint`.

| Attribute | Value |
|---|---|
| Content | Sand |

### ComboBoxItem: Weathered stone

Source: [MainWindow.xaml:71](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:71). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel / TerrainPaint`.

| Attribute | Value |
|---|---|
| Content | Weathered stone |

### ComboBoxItem: Snow white

Source: [MainWindow.xaml:71](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:71). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel / TerrainPaint`.

| Attribute | Value |
|---|---|
| Content | Snow white |

### Button: Stamp selected shape

Source: [MainWindow.xaml:72](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:72). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Stamp selected shape |
| Click | StampSelectedShape |
| ToolTip | Copy a selected shape into terrain. Its original stays in the scene; move it aside to see and smooth the ground. |

### Button: Place water pour

Source: [MainWindow.xaml:74](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:74). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Place water pour |
| Tag | water-source |
| Click | ChooseDockTerrain |

### Button: Place lava pour

Source: [MainWindow.xaml:75](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:75). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Place lava pour |
| Tag | lava-source |
| Click | ChooseDockTerrain |

### Button: Remove all pours

Source: [MainWindow.xaml:76](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:76). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Remove all pours |
| Click | ClearFlowSources |

### CheckBox: LavaOn

Source: [MainWindow.xaml:79](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:79). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | LavaOn |
| Content | Use lava instead of water |
| ToolTip | Turn the still water surface into glowing lava. |

### CheckBox: WaterOn

Source: [MainWindow.xaml:79](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:79). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | WaterOn |
| Content | Show water |
| IsChecked | True |

### TextBox: WaterHeight

Source: [MainWindow.xaml:80](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:80). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | WaterHeight |
| Text | -0.5 |
| ToolTip | Water fills ground below this height. Zero is the original floor. |

### Button: Apply water level

Source: [MainWindow.xaml:81](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:81). Parent path: `Window / EditorRoot / TopToolbar / RightTools / TerrainSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Apply water level |
| Click | ApplyWater |
| ToolTip | Fill low areas up to this height, or hide water if unchecked. |

### ToggleButton: EffectsMenuToggle

Source: [MainWindow.xaml:85](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:85). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | EffectsMenuToggle |
| Content | ☀ |
| ToolTip | Place fire, lava pools, and lights. |
| AutomationProperties.Name | Environment |
| Style | {StaticResource MenuToggle} |

### Popup: Popup

Source: [MainWindow.xaml:86](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:86). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| IsOpen | {Binding IsChecked, ElementName=EffectsMenuToggle} |
| PlacementTarget | {Binding ElementName=EffectsMenuToggle} |
| Placement | Bottom |
| StaysOpen | False |
| AllowsTransparency | True |

### Button: Add fire

Source: [MainWindow.xaml:89](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:89). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Add fire |
| Tag | fire |
| Click | AddEffect |
| ToolTip | Place an animated, stylized flame with a local light. |

### Button: Add lava pool

Source: [MainWindow.xaml:90](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:90). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Add lava pool |
| Tag | lava |
| Click | AddEffect |
| ToolTip | Place a glowing lava disc. Move or stretch it with the shape handles. |

### Button: Add light

Source: [MainWindow.xaml:91](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:91). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Add light |
| Tag | light |
| Click | AddEffect |
| ToolTip | Place a light you can move and recolor. Up to three light/fire sources in this first pass. |

### TextBox: EffectColor

Source: [MainWindow.xaml:92](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:92). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | EffectColor |
| Text | #FF651C |

### TextBox: EffectBrightness

Source: [MainWindow.xaml:93](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:93). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | EffectBrightness |
| Text | 25 |

### TextBox: EffectRange

Source: [MainWindow.xaml:94](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:94). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | EffectRange |
| Text | 6 |

### TextBox: EffectCone

Source: [MainWindow.xaml:94](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:94). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | EffectCone |
| Text | 180 |

### Button: Apply to selected effect

Source: [MainWindow.xaml:95](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:95). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Apply to selected effect |
| Click | ApplyEffect |

### ToggleButton: MusicMenuToggle

Source: [MainWindow.xaml:99](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:99). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | MusicMenuToggle |
| Visibility | Collapsed |
| Content | ♫ |
| ToolTip | Play music from your computer while you build. |
| AutomationProperties.Name | Music player |
| Style | {StaticResource MenuToggle} |

### Popup: Popup

Source: [MainWindow.xaml:100](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:100). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| IsOpen | {Binding IsChecked, ElementName=MusicMenuToggle} |
| PlacementTarget | {Binding ElementName=MusicMenuToggle} |
| Placement | Bottom |
| StaysOpen | False |
| AllowsTransparency | True |

### Button: Choose songs…

Source: [MainWindow.xaml:104](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:104). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Choose songs… |
| Click | PickMusic |
| ToolTip | Pick one or more local MP3 or WAV files. |

### Button: Play / Pause

Source: [MainWindow.xaml:105](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:105). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| Content | Play / Pause |
| Click | ToggleMusic |

### Button: Next

Source: [MainWindow.xaml:105](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:105). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| Content | Next |
| Click | NextMusic |

### Button: Stop

Source: [MainWindow.xaml:105](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:105). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| Content | Stop |
| Click | StopMusic |

### Slider: MusicVolume

Source: [MainWindow.xaml:106](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:106). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | MusicVolume |
| Minimum | 0 |
| Maximum | 1 |
| Value | 0.5 |
| ValueChanged | MusicVolumeChanged |
| ToolTip | Adjust music volume. |

### ToggleButton: ShapesMenuToggle

Source: [MainWindow.xaml:110](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:110). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | ShapesMenuToggle |
| Visibility | Collapsed |
| Content | ▣ |
| ToolTip | Build with shapes. Add a block, then move, turn or stretch it. |
| AutomationProperties.Name | Build with shapes |
| Click | OpenShapesDock |
| Style | {StaticResource MenuToggle} |

### Popup: ShapeSettingsPopup

Source: [MainWindow.xaml:111](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:111). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | ShapeSettingsPopup |
| Placement | Top |
| StaysOpen | False |
| AllowsTransparency | True |

### CheckBox: GridSnap

Source: [MainWindow.xaml:114](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:114). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | GridSnap |
| Content | Snap to grid (0.5 m) |
| ToolTip | Line up moved objects on half-metre steps. |

### CheckBox: GroundSnap

Source: [MainWindow.xaml:114](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:114). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | GroundSnap |
| Content | Snap to ground |
| ToolTip | Set the bottom of moved objects onto the ground beneath their center. |

### CheckBox: ObjectSnap

Source: [MainWindow.xaml:114](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:114). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | ObjectSnap |
| Content | Snap to nearby objects |
| ToolTip | Snap bounding-box faces together when they are within 0.25 metres. |

### ComboBox: GroundColor

Source: [MainWindow.xaml:115](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:115). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | GroundColor |
| SelectedIndex | 0 |
| SelectionChanged | GroundColorChanged |
| ToolTip | Choose a color for the floor. |

### ComboBoxItem: Dark blue

Source: [MainWindow.xaml:116](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:116). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / GroundColor`.

| Attribute | Value |
|---|---|
| Content | Dark blue |
| Tag | #182033 |

### ComboBoxItem: Black

Source: [MainWindow.xaml:116](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:116). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / GroundColor`.

| Attribute | Value |
|---|---|
| Content | Black |
| Tag | #050505 |

### ComboBoxItem: Purple

Source: [MainWindow.xaml:116](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:116). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / GroundColor`.

| Attribute | Value |
|---|---|
| Content | Purple |
| Tag | #592D80 |

### ComboBoxItem: Pink

Source: [MainWindow.xaml:116](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:116). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / GroundColor`.

| Attribute | Value |
|---|---|
| Content | Pink |
| Tag | #C65394 |

### ComboBoxItem: Green

Source: [MainWindow.xaml:116](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:116). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / GroundColor`.

| Attribute | Value |
|---|---|
| Content | Green |
| Tag | #356F45 |

### ComboBoxItem: Sand

Source: [MainWindow.xaml:116](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:116). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / GroundColor`.

| Attribute | Value |
|---|---|
| Content | Sand |
| Tag | #B29B6D |

### Button: Side

Source: [MainWindow.xaml:119](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:119). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| Content | Side |
| Tag | side |
| Click | CameraView |
| Padding | 3,8 |
| ToolTip | Look at your scene from the side. |

### Button: Above

Source: [MainWindow.xaml:119](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:119). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| Content | Above |
| Tag | above |
| Click | CameraView |
| Padding | 3,8 |
| ToolTip | Look down from above. |

### Button: Low

Source: [MainWindow.xaml:119](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:119). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| Content | Low |
| Tag | low |
| Click | CameraView |
| Padding | 3,8 |
| ToolTip | Look up at objects from near the ground. |

### Button: Focus selected object

Source: [MainWindow.xaml:120](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:120). Parent path: `Window / EditorRoot / TopToolbar / RightTools / ShapeSettingsPopup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Focus selected object |
| Tag | focus |
| Click | CameraView |
| ToolTip | Bring the selected object into view. Middle-drag looks around it. |

### ToggleButton: FeatureMenuToggle

Source: [MainWindow.xaml:124](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:124). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | FeatureMenuToggle |
| Content | ◇ |
| ToolTip | Music and future tools. |
| AutomationProperties.Name | More |
| Style | {StaticResource MenuToggle} |

### Popup: Popup

Source: [MainWindow.xaml:125](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:125). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| IsOpen | {Binding IsChecked, ElementName=FeatureMenuToggle} |
| PlacementTarget | {Binding ElementName=FeatureMenuToggle} |
| Placement | Bottom |
| StaysOpen | False |
| AllowsTransparency | True |

### Button: Music player

Source: [MainWindow.xaml:128](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:128). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / FeatureSurface / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Music player |
| Click | OpenMusicFromMore |
| HorizontalAlignment | Left |

### ListBox: FeatureList

Source: [MainWindow.xaml:133](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:133). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / FeatureSurface / StackPanel / Grid`.

| Attribute | Value |
|---|---|
| x:Name | FeatureList |
| SelectionChanged | SelectFeature |
| ScrollViewer.HorizontalScrollBarVisibility | Disabled |

### Button: Check availability

Source: [MainWindow.xaml:145](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:145). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / FeatureSurface / StackPanel / Grid / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Check availability |
| Click | CheckFeature |
| ToolTip | Check this tool's status. This does not change your scene. |

### ToggleButton: AssetMenuToggle

Source: [MainWindow.xaml:154](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:154). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| x:Name | AssetMenuToggle |
| Content | ♟ |
| ToolTip | Add characters, draw their routes, and change their settings. |
| AutomationProperties.Name | Characters |
| Style | {StaticResource MenuToggle} |

### Popup: Popup

Source: [MainWindow.xaml:155](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:155). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| IsOpen | {Binding IsChecked, ElementName=AssetMenuToggle} |
| PlacementTarget | {Binding ElementName=AssetMenuToggle} |
| Placement | Bottom |
| StaysOpen | False |
| AllowsTransparency | True |

### Button: ＋ Place character

Source: [MainWindow.xaml:166](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:166). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| Content | ＋ Place character |
| Click | AddCharacter |
| Margin | 0,12,0,4 |
| ToolTip | Add the chosen character to the middle of your scene. |

### Button: Import character…

Source: [MainWindow.xaml:167](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:167). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Import character… |
| Click | ImportCharacter |
| Margin | 0,4,0,14 |
| ToolTip | Add a character from a .glb file on your computer. |

### ToggleButton: PropertiesMenuToggle

Source: [MainWindow.xaml:168](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:168). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | PropertiesMenuToggle |
| Checked | OpenCharacterSettings |
| Content | Character settings |
| ToolTip | Change the selected character's name, size, or movement speed. |
| AutomationProperties.Name | Character settings |

### ToggleButton: AdvancedToggle

Source: [MainWindow.xaml:169](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:169). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | AdvancedToggle |
| Content | Advanced controls |
| ToolTip | Show or hide number controls for precise changes. |
| AutomationProperties.Name | Advanced mode |
| Checked | ChangeMode |
| Unchecked | ChangeMode |

### Button: MoveButton

Source: [MainWindow.xaml:170](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:170). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | MoveButton |
| Content | Move character |
| ToolTip | Move a character by dragging it across the floor. Its path moves too. |
| AutomationProperties.Name | Move character |
| Click | SetMove |

### Button: RouteButton

Source: [MainWindow.xaml:171](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:171). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | RouteButton |
| Content | Draw character route |
| ToolTip | Choose a character, then drag across the floor to draw where it should go. |
| AutomationProperties.Name | Draw route |
| Click | SetRoute |
| BorderBrush | #E776E9 |

### ListBox: CharacterList

Source: [MainWindow.xaml:173](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:173). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | CharacterList |
| DisplayMemberPath | Name |
| SelectionChanged | SelectCharacter |
| MaxHeight | 220 |
| Margin | 0,8,0,0 |
| ToolTip | Choose which character you want to change. |

### Popup: Popup

Source: [MainWindow.xaml:177](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:177). Parent path: `Window / EditorRoot / TopToolbar / RightTools`.

| Attribute | Value |
|---|---|
| IsOpen | {Binding IsChecked, ElementName=PropertiesMenuToggle} |
| PlacementTarget | {Binding ElementName=AssetMenuToggle} |
| Placement | Bottom |
| StaysOpen | False |
| AllowsTransparency | True |

### TextBox: NameBox

Source: [MainWindow.xaml:182](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:182). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | NameBox |
| LostFocus | UpdateName |
| ToolTip | Name this character so you can recognize it in your scene. |

### Slider: SpeedSlider

Source: [MainWindow.xaml:184](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:184). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | SpeedSlider |
| Minimum | 0.2 |
| Maximum | 5 |
| Value | 1.5 |
| ValueChanged | ChangeSpeed |
| Margin | 0,12 |
| ToolTip | Choose how fast the character moves along its path. |

### Slider: SizeSlider

Source: [MainWindow.xaml:187](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:187). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | SizeSlider |
| Minimum | 0.3 |
| Maximum | 3 |
| Value | 1 |
| ValueChanged | ChangeSize |
| Margin | 0,12 |
| ToolTip | Make the character smaller or larger. |

### TextBox: PositionXBox

Source: [MainWindow.xaml:193](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:193). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / AdvancedFields / UniformGrid / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | PositionXBox |
| ToolTip | Exact left-to-right position: -12 to 12 metres. |

### TextBox: PositionZBox

Source: [MainWindow.xaml:197](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:197). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / AdvancedFields / UniformGrid / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | PositionZBox |
| ToolTip | Exact position along the floor: -12 to 12 metres. |

### TextBox: ExactSpeedBox

Source: [MainWindow.xaml:201](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:201). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / AdvancedFields / UniformGrid / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | ExactSpeedBox |
| ToolTip | Exact route speed: 0.2 to 5 metres per second. |

### TextBox: ExactSizeBox

Source: [MainWindow.xaml:205](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:205). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / AdvancedFields / UniformGrid / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | ExactSizeBox |
| ToolTip | Choose the character's size. 1 is normal; 2 is twice as big. Use 0.3 to 3. |

### Button: Apply exact values

Source: [MainWindow.xaml:208](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:208). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / AdvancedFields`.

| Attribute | Value |
|---|---|
| Content | Apply exact values |
| Click | ApplyExactValues |
| ToolTip | Use these numbers to update the character. Any number that needs fixing will be explained. |

### Expander: Build: height, size and floor

Source: [MainWindow.xaml:211](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:211). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Header | Build: height, size and floor |
| Margin | 0,10 |
| Foreground | #EFF2FA |

### TextBox: ObjectHeight

Source: [MainWindow.xaml:212](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:212). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | ObjectHeight |

### TextBox: ObjectWidth

Source: [MainWindow.xaml:213](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:213). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| x:Name | ObjectWidth |

### TextBox: ObjectTall

Source: [MainWindow.xaml:213](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:213). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| x:Name | ObjectTall |

### TextBox: ObjectDepth

Source: [MainWindow.xaml:213](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:213). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel / UniformGrid`.

| Attribute | Value |
|---|---|
| x:Name | ObjectDepth |

### TextBox: ObjectAngle

Source: [MainWindow.xaml:214](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:214). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | ObjectAngle |

### CheckBox: FloorLock

Source: [MainWindow.xaml:215](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:215). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | FloorLock |
| Content | Keep above floor |
| Foreground | #EFF2FA |
| ToolTip | Prevent the object going below the floor. Turn off to partly bury it. |

### Button: Apply changes

Source: [MainWindow.xaml:216](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:216). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Apply changes |
| Click | ApplyBuildingFields |

### Expander: Rigid body

Source: [MainWindow.xaml:218](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:218). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Header | Rigid body |
| Foreground | White |

### CheckBox: BodyDynamic

Source: [MainWindow.xaml:218](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:218). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | BodyDynamic |
| Content | Simulate during Play |
| Foreground | White |

### TextBox: BodyDensity

Source: [MainWindow.xaml:218](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:218). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | BodyDensity |

### Button: Apply body settings

Source: [MainWindow.xaml:218](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:218). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / Expander / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Apply body settings |
| Click | ApplyBody |

### TextBox: TargetX

Source: [MainWindow.xaml:219](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:219). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / ChaosFields / UniformGrid`.

| Attribute | Value |
|---|---|
| x:Name | TargetX |

### TextBox: TargetY

Source: [MainWindow.xaml:219](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:219). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / ChaosFields / UniformGrid`.

| Attribute | Value |
|---|---|
| x:Name | TargetY |

### TextBox: TargetZ

Source: [MainWindow.xaml:219](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:219). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / ChaosFields / UniformGrid`.

| Attribute | Value |
|---|---|
| x:Name | TargetZ |

### TextBox: BranchSpread

Source: [MainWindow.xaml:219](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:219). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / ChaosFields`.

| Attribute | Value |
|---|---|
| x:Name | BranchSpread |

### Button: Apply lightning

Source: [MainWindow.xaml:219](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:219). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel / ChaosFields`.

| Attribute | Value |
|---|---|
| Content | Apply lightning |
| Click | ApplyChaos |

### Button: Clear route

Source: [MainWindow.xaml:221](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:221). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Clear route |
| Click | ClearRoute |
| ToolTip | Erase this character's path. The character stays in the scene. |

### Button: Remove character

Source: [MainWindow.xaml:222](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:222). Parent path: `Window / EditorRoot / TopToolbar / RightTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Remove character |
| Click | RemoveCharacter |
| ToolTip | Remove this character from the scene. Your original file stays safe. Press Ctrl+Z to undo. |

### ToggleButton: FileMenuToggle

Source: [MainWindow.xaml:228](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:228). Parent path: `Window / EditorRoot / TopToolbar / LeftTools`.

| Attribute | Value |
|---|---|
| x:Name | FileMenuToggle |
| Content | ☰ |
| ToolTip | Start a scene, open or save your work, or choose a project and saved settings. |
| AutomationProperties.Name | Main menu |
| Style | {StaticResource MenuToggle} |
| VerticalAlignment | Center |

### Popup: Popup

Source: [MainWindow.xaml:229](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:229). Parent path: `Window / EditorRoot / TopToolbar / LeftTools`.

| Attribute | Value |
|---|---|
| IsOpen | {Binding IsChecked, ElementName=FileMenuToggle} |
| PlacementTarget | {Binding ElementName=FileMenuToggle} |
| Placement | Bottom |
| StaysOpen | False |
| AllowsTransparency | True |

### Button: FullScreenButton

Source: [MainWindow.xaml:233](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:233). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | FullScreenButton |
| Content | Enter full screen |
| ToolTip | Make your scene fill the screen. Move your mouse to an edge to see the menus. |
| Click | ToggleFullScreen |

### Button: New scene

Source: [MainWindow.xaml:234](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:234). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | New scene |
| ToolTip | Start an empty scene. You will be asked to save any changes. |
| Click | NewScene |

### Button: Open scene…

Source: [MainWindow.xaml:235](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:235). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Open scene… |
| ToolTip | Open a scene you saved earlier. |
| Click | OpenScene |

### Button: Save scene

Source: [MainWindow.xaml:236](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:236). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Save scene |
| ToolTip | Save your scene. Ctrl+S also saves. Previous versions are backed up. |
| Click | SaveScene |

### Button: Projects & presets…

Source: [MainWindow.xaml:237](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:237). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Projects & presets… |
| ToolTip | Choose a project and saved settings, then check them or run the project. |
| Click | OpenProject |

### Button: Frame world

Source: [MainWindow.xaml:238](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:238). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Frame world |
| Click | FrameWorld |
| ToolTip | Return the camera to the center and show the landscape. |

### Slider: DayHour

Source: [MainWindow.xaml:239](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:239). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | DayHour |
| Minimum | 0 |
| Maximum | 24 |
| Value | 15 |

### CheckBox: CycleDay

Source: [MainWindow.xaml:239](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:239). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | CycleDay |
| Content | Run day / night cycle |
| Foreground | White |

### TextBox: DayDuration

Source: [MainWindow.xaml:239](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:239). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | DayDuration |
| Text | 180 |

### Slider: FogAmount

Source: [MainWindow.xaml:239](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:239). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | FogAmount |
| Minimum | 0 |
| Maximum | 0.3 |
| Value | 0.018 |

### Button: Apply atmosphere

Source: [MainWindow.xaml:239](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:239). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Apply atmosphere |
| Click | ApplyAtmosphere |

### CheckBox: RaytracingEnabled

Source: [MainWindow.xaml:241](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:241). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | RaytracingEnabled |
| Content | Enable DXR 1.1 |
| Foreground | White |

### ComboBox: RaytracingQuality

Source: [MainWindow.xaml:242](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:242). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | RaytracingQuality |
| SelectedIndex | 1 |
| Foreground | #182033 |
| Background | #EDF1FF |

### ComboBoxItem: ComboBoxItem

Source: [MainWindow.xaml:243](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:243). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel / RaytracingQuality`.

| Attribute | Value |
|---|---|

### ComboBoxItem: ComboBoxItem

Source: [MainWindow.xaml:243](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:243). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel / RaytracingQuality`.

| Attribute | Value |
|---|---|

### ComboBoxItem: ComboBoxItem

Source: [MainWindow.xaml:243](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:243). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel / RaytracingQuality`.

| Attribute | Value |
|---|---|

### ComboBoxItem: ComboBoxItem

Source: [MainWindow.xaml:243](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:243). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel / RaytracingQuality`.

| Attribute | Value |
|---|---|

### ComboBoxItem: ComboBoxItem

Source: [MainWindow.xaml:243](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:243). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel / RaytracingQuality`.

| Attribute | Value |
|---|---|

### Slider: PathBounces

Source: [MainWindow.xaml:245](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:245). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | PathBounces |
| Minimum | 2 |
| Maximum | 4 |
| Value | 3 |
| TickFrequency | 1 |
| IsSnapToTickEnabled | True |

### CheckBox: FreezeRaytracingAnimation

Source: [MainWindow.xaml:246](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:246). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | FreezeRaytracingAnimation |
| Content | Freeze reference animation |
| ToolTip | Hold visual animation for accumulation. Use Pause to stop physics. |
| Foreground | White |

### CheckBox: RayReconstructionEnabled

Source: [MainWindow.xaml:247](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:247). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | RayReconstructionEnabled |
| Content | Ray Reconstruction |
| ToolTip | Use NVIDIA RR when available. Turn off for the native temporal denoiser. No upscaling or frame generation. |
| Foreground | White |
| IsChecked | True |

### Button: Apply raytracing

Source: [MainWindow.xaml:248](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:248). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| Content | Apply raytracing |
| Click | ApplyRaytracing |

### Slider: PanSpeed

Source: [MainWindow.xaml:251](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:251). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | PanSpeed |
| Minimum | 0.25 |
| Maximum | 6 |
| Value | 1 |
| TickFrequency | 0.25 |
| ToolTip | Speed of edge and arrow-key travel. Default is about four times the old speed. |

### Slider: TiltSpeed

Source: [MainWindow.xaml:252](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:252). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | TiltSpeed |
| Minimum | 0.2 |
| Maximum | 4 |
| Value | 1 |
| TickFrequency | 0.1 |

### CheckBox: ArrowPanEnabled

Source: [MainWindow.xaml:253](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:253). Parent path: `Window / EditorRoot / TopToolbar / LeftTools / Popup / Border / ScrollViewer / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | ArrowPanEnabled |
| Content | Arrow keys move camera |
| Margin | 4,10,0,4 |
| ToolTip | Optional: hold the arrow keys to travel around your scene. This stays off while you type. |

### CheckBox: RepeatPreview

Source: [MainWindow.xaml:277](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:277). Parent path: `Window / EditorRoot / SceneSurface / CanvasRoot / PlaybackControls / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | RepeatPreview |
| Content | Repeat |
| VerticalAlignment | Center |
| Foreground | #EFF2FA |
| ToolTip | Start all routes again after the longest one finishes. Characters return to their starting positions. |

### Button: ↺

Source: [MainWindow.xaml:279](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:279). Parent path: `Window / EditorRoot / SceneSurface / CanvasRoot / PlaybackControls / StackPanel`.

| Attribute | Value |
|---|---|
| Content | ↺ |
| ToolTip | Stop the preview and return characters to their starting positions. |
| AutomationProperties.Name | Reset preview |
| Click | ResetPreview |
| FontSize | 20 |
| Padding | 12,5 |

### Button: PlayButton

Source: [MainWindow.xaml:280](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml:280). Parent path: `Window / EditorRoot / SceneSurface / CanvasRoot / PlaybackControls / StackPanel`.

| Attribute | Value |
|---|---|
| x:Name | PlayButton |
| Content | ▶ |
| ToolTip | Preview your characters moving along the paths you drew. |
| AutomationProperties.Name | Play or pause preview |
| Background | #7946AD |
| BorderBrush | #DB78ED |
| Padding | 14,7 |
| Click | Play |

## Event-handler resolution

| Control | Event | Handler | Definition |
|---|---|---|---|
| Window | Closing | `OnClosing` | [MainWindow.xaml.cs:71](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:71) |
| Terrain | Click | `ChooseBuilderSection` | [BuildDock.cs:12](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:12) |
| Shapes | Click | `ChooseBuilderSection` | [BuildDock.cs:12](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:12) |
| Nature | Click | `ChooseBuilderSection` | [BuildDock.cs:12](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:12) |
| Sculpting | Click | `ChooseBuilderSection` | [BuildDock.cs:12](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:12) |
| Hide building bar | Click | `HideBuilderBar` | [BuildDock.cs:13](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:13) |
| Stamp selected shape | Click | `StampSelectedShape` | [TerrainTools.cs:41](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:41) |
| Place water pour | Click | `ChooseDockTerrain` | [BuildDock.cs:63](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:63) |
| Place lava pour | Click | `ChooseDockTerrain` | [BuildDock.cs:63](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:63) |
| Remove all pours | Click | `ClearFlowSources` | [FlowTools.cs:17](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FlowTools.cs:17) |
| Apply water level | Click | `ApplyWater` | [TerrainTools.cs:40](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:40) |
| Add fire | Click | `AddEffect` | [EffectsTools.cs:7](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EffectsTools.cs:7) |
| Add lava pool | Click | `AddEffect` | [EffectsTools.cs:7](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EffectsTools.cs:7) |
| Add light | Click | `AddEffect` | [EffectsTools.cs:7](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EffectsTools.cs:7) |
| Apply to selected effect | Click | `ApplyEffect` | [EffectsTools.cs:10](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EffectsTools.cs:10) |
| Choose songs… | Click | `PickMusic` | [TerrainTools.cs:43](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:43) |
| Play / Pause | Click | `ToggleMusic` | [TerrainTools.cs:45](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:45) |
| Next | Click | `NextMusic` | [TerrainTools.cs:46](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:46) |
| Stop | Click | `StopMusic` | [TerrainTools.cs:47](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:47) |
| MusicVolume | ValueChanged | `MusicVolumeChanged` | [TerrainTools.cs:48](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:48) |
| ShapesMenuToggle | Click | `OpenShapesDock` | [BuildDock.cs:15](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:15) |
| GroundColor | SelectionChanged | `GroundColorChanged` | [BuildingTools.cs:58](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildingTools.cs:58) |
| Side | Click | `CameraView` | [EditorHistory.cs:8](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EditorHistory.cs:8) |
| Above | Click | `CameraView` | [EditorHistory.cs:8](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EditorHistory.cs:8) |
| Low | Click | `CameraView` | [EditorHistory.cs:8](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EditorHistory.cs:8) |
| Focus selected object | Click | `CameraView` | [EditorHistory.cs:8](D:/projects/_recovery/starlight-managed-retired-20260917/editor/EditorHistory.cs:8) |
| Music player | Click | `OpenMusicFromMore` | [TerrainTools.cs:42](D:/projects/_recovery/starlight-managed-retired-20260917/editor/TerrainTools.cs:42) |
| FeatureList | SelectionChanged | `SelectFeature` | [FeaturePanels.cs:12](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeaturePanels.cs:12) |
| Check availability | Click | `CheckFeature` | [FeaturePanels.cs:18](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeaturePanels.cs:18) |
| ＋ Place character | Click | `AddCharacter` | [MainWindow.xaml.cs:41](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:41) |
| Import character… | Click | `ImportCharacter` | [MainWindow.xaml.cs:45](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:45) |
| PropertiesMenuToggle | Checked | `OpenCharacterSettings` | [MainWindow.xaml.cs:46](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:46) |
| AdvancedToggle | Checked | `ChangeMode` | [MainWindow.xaml.cs:74](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:74) |
| AdvancedToggle | Unchecked | `ChangeMode` | [MainWindow.xaml.cs:74](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:74) |
| MoveButton | Click | `SetMove` | [MainWindow.xaml.cs:47](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:47) |
| RouteButton | Click | `SetRoute` | [MainWindow.xaml.cs:48](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:48) |
| CharacterList | SelectionChanged | `SelectCharacter` | [MainWindow.xaml.cs:57](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:57) |
| NameBox | LostFocus | `UpdateName` | [MainWindow.xaml.cs:58](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:58) |
| SpeedSlider | ValueChanged | `ChangeSpeed` | [MainWindow.xaml.cs:59](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:59) |
| SizeSlider | ValueChanged | `ChangeSize` | [MainWindow.xaml.cs:60](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:60) |
| Apply exact values | Click | `ApplyExactValues` | [MainWindow.xaml.cs:76](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:76) |
| Apply changes | Click | `ApplyBuildingFields` | [BuildingTools.cs:57](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildingTools.cs:57) |
| Apply body settings | Click | `ApplyBody` | [PhysicsWorld.cs:63](D:/projects/_recovery/starlight-managed-retired-20260917/editor/PhysicsWorld.cs:63) |
| Apply lightning | Click | `ApplyChaos` | [ChaosTools.cs:29](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ChaosTools.cs:29) |
| Clear route | Click | `ClearRoute` | [MainWindow.xaml.cs:61](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:61) |
| Remove character | Click | `RemoveCharacter` | [MainWindow.xaml.cs:62](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:62) |
| FullScreenButton | Click | `ToggleFullScreen` | [FullScreenCanvas.cs:14](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FullScreenCanvas.cs:14) |
| New scene | Click | `NewScene` | [MainWindow.xaml.cs:66](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:66) |
| Open scene… | Click | `OpenScene` | [MainWindow.xaml.cs:67](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:67) |
| Save scene | Click | `SaveScene` | [MainWindow.xaml.cs:69](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:69) |
| Projects & presets… | Click | `OpenProject` | [MainWindow.xaml.cs:70](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:70) |
| Frame world | Click | `FrameWorld` | [WorldNavigation.cs:5](D:/projects/_recovery/starlight-managed-retired-20260917/editor/WorldNavigation.cs:5) |
| Apply atmosphere | Click | `ApplyAtmosphere` | [ChaosTools.cs:24](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ChaosTools.cs:24) |
| Apply raytracing | Click | `ApplyRaytracing` | [ChaosTools.cs:25](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ChaosTools.cs:25) |
| ↺ | Click | `ResetPreview` | [MainWindow.xaml.cs:64](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:64) |
| PlayButton | Click | `Play` | [LocalMusic.cs:17](D:/projects/_recovery/starlight-managed-retired-20260917/editor/LocalMusic.cs:17); [MainWindow.xaml.cs:63](D:/projects/_recovery/starlight-managed-retired-20260917/editor/MainWindow.xaml.cs:63) |

## Dynamically constructed interface

The generated XAML inventory is complemented by these runtime constructors; their complete controls are enumerated in the main specification.

| Source | Runtime interface |
|---|---|
| [BuildDock.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/BuildDock.cs:1) | Builder branches; Terrain and Shapes vector buttons; Undo/Redo/settings/close; pill layout; selected states. Unattached Terrain/Shapes tabs are not live UI. |
| [NatureGeometry.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/NatureGeometry.cs:1) | Eight Nature prop buttons. |
| [SculptTools.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/SculptTools.cs:1) | Four sculpt buttons, radius and strength sliders. |
| [ProjectWindow.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/ProjectWindow.cs:1) | Project/preset modal: selectors, eight value fields, four checkboxes and five action buttons. |
| [NativeEditor.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/NativeEditor.cs:1) | Native viewport, popup hosts, separate Play window and its Play/Pause and Reset buttons. |
| [FullScreenCanvas.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FullScreenCanvas.cs:1) | Full-screen corner hosts, layout restoration and retained fade helpers. |
| [FeaturePanels.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeaturePanels.cs:1) | Catalogue selection detail and availability response. |
| [DialogHistory.cs:1](D:/projects/_recovery/starlight-managed-retired-20260917/editor/DialogHistory.cs:1) | Shared native file dialog ownership, canonical paths and directory persistence. |

## All twenty More catalogue detail pages

These strings describe the old discovery UI. They are not verified engine capabilities. The native-only resolution mandate supersedes historical vendor-upscaling/FG text.

### Native Play

`native-play` · experimental · [FeatureCatalog.cs:13](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:13)

Run the scene you made in the real game engine. Windowed: a game window. Full screen: inside the canvas.

**What needs to come first:** Saved scenes and renderer connection

**How we will know it works:** Saved characters and routes run correctly; stopping returns to editing.

### Playback choices

`playback` · experimental · [FeatureCatalog.cs:14](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:14)

Play once and finish automatically, or repeat your routes. Keep Play/Pause and Reset.

**What needs to come first:** Route completion and repeat rules

**How we will know it works:** Different route lengths, pause, replay and reset all work without losing edits.

### Assets & reusable objects

`assets` · scaffolded · [FeatureCatalog.cs:15](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:15)

Organize models, pictures and files. Reuse groups of objects in different scenes.

**What needs to come first:** Portable asset references and import pipeline

**How we will know it works:** A copied project opens with its assets on another computer.

### Animation

`animation` · scaffolded · [FeatureCatalog.cs:16](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:16)

Make characters walk, turn and change between movements.

**What needs to come first:** Animation import and native scene playback

**How we will know it works:** Walking follows the route and transitions without broken poses.

### Gameplay rules

`gameplay` · scaffolded · [FeatureCatalog.cs:17](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:17)

Build actions such as opening a door or collecting an item without writing code.

**What needs to come first:** Scene objects, events and saved behaviors

**How we will know it works:** A small game can be built, saved and played with working rules.

### Physics

`physics` · scaffolded · [FeatureCatalog.cs:18](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:18)

Give objects weight and stop them passing through floors and walls.

**What needs to come first:** Transforms and collision shapes

**How we will know it works:** Objects collide reliably at different frame rates.

### Navigation

`navigation` · scaffolded · [FeatureCatalog.cs:19](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:19)

Help characters find their way around obstacles.

**What needs to come first:** Walkable surfaces and collision data

**How we will know it works:** Characters reach a destination without walking through walls.

### Materials & lighting

`materials` · scaffolded · [FeatureCatalog.cs:20](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:20)

Choose how surfaces look and light your scene. Advanced lighting stays optional.

**What needs to come first:** Texture import and renderer materials

**How we will know it works:** Saved appearances match native playback with measured performance cost.

### Terrain & large scenes

`worlds` · experimental · [FeatureCatalog.cs:21](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:21)

Experimental 24 × 24 metre terrain: sculpt and stamp terrain, blend detailed procedural surfaces, place rocks and grass, and pour water or lava. Object sculpt brushes are available. Large-world streaming and caves remain planned.

**What needs to come first:** Assets, scene storage and streaming

**How we will know it works:** A large scene loads and moves smoothly within a recorded memory budget.

### 2D games

`2d` · scaffolded · [FeatureCatalog.cs:22](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:22)

Build games with pictures, layers, 2D cameras and tile maps.

**What needs to come first:** 2D rendering, animation and collision

**How we will know it works:** Create and export a complete small 2D game.

### Audio

`audio` · scaffolded · [FeatureCatalog.cs:23](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:23)

Add music, sound effects and sounds that come from objects in the world.

**What needs to come first:** Audio import, playback and scene events

**How we will know it works:** Saved sound settings and spatial audio work in an exported game.

### Game screens & controls

`ui-input` · scaffolded · [FeatureCatalog.cs:24](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:24)

Build game menus and connect keyboard, mouse or controller actions.

**What needs to come first:** Input mapping and runtime UI

**How we will know it works:** A playable game works with declared input devices and readable UI.

### Performance & debugging

`profiling` · scaffolded · [FeatureCatalog.cs:25](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:25)

See what slows a scene down and find problems in game rules.

**What needs to come first:** Runtime timing and diagnostic events

**How we will know it works:** CPU, GPU, memory and errors are reported without misleading totals.

### Teamwork & history

`teamwork` · scaffolded · [FeatureCatalog.cs:26](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:26)

Review changes and safely work on a project with other people.

**What needs to come first:** Stable asset IDs and conflict handling

**How we will know it works:** Conflicting edits can be inspected and recovered without losing work.

### Build & export

`export` · scaffolded · [FeatureCatalog.cs:27](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:27)

Turn your project into a game other people can install and play.

**What needs to come first:** Portable assets and runtime packaging

**How we will know it works:** A packaged game runs on a clean computer without the editor.

### Windows, Linux & macOS

`platforms` · scaffolded · [FeatureCatalog.cs:28](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:28)

Create and play 2D and 3D games across the three desktop systems.

**What needs to come first:** Portable editor, renderer and platform services

**How we will know it works:** Editor workflows and exported games pass checks on each system.

### VR

`vr` · scaffolded · [FeatureCatalog.cs:29](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:29)

Create games for supported headsets and hand controllers.

**What needs to come first:** Headset runtime, stereo rendering and input

**How we will know it works:** An exported VR sample passes tracking and frame-time checks on named devices.

### Graphics technology updates

`technology` · scaffolded · [FeatureCatalog.cs:30](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:30)

Choose supported upscaling, frame generation and latency versions for each project.

**What needs to come first:** Vendor integrations, compatibility checks and rollback

**How we will know it works:** An update and rollback preserve project settings; native rendering remains available.

### Steam & cloud saves

`steam` · scaffolded · [FeatureCatalog.cs:31](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:31)

Use your Steam account and keep supported project files in Steam Cloud.

**What needs to come first:** Own Steam application ID, portable projects and sync conflict handling

**How we will know it works:** Private tests restore files on another machine and recover offline conflicts.

### AI editor access

`ai` · scaffolded · [FeatureCatalog.cs:32](D:/projects/_recovery/starlight-managed-retired-20260917/editor/FeatureCatalog.cs:32)

Let compatible AI assistants use the same editing tools you use.

**What needs to come first:** Shared commands and full editor coverage

**How we will know it works:** Each supported action works through both the GUI and documented AI interface.

## Archived validation code (not rerun in this documentation pass)

The named Checks files in the manifest are prior implementation tests. Their source assertions are evidence of intended behavior, not fresh pass results. The audit-release executable was exercised interactively; no managed rebuild or complete engine regression run was performed for this reference document.
