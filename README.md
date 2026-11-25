# MISSION RECALL

MISSION RECALL is a Windows 3D lunar exploration game built with Unreal Engine 5.8 and C++. The player lands a crew capsule, repairs it when needed, and explores a lunar research site on foot or by rover. The project was originally built with raylib before moving to Unreal Engine.

## Gameplay

- **Land the capsule.** Control the descent and steer the capsule. Touchdown speed, lateral motion, and tilt determine the landing grade and whether zero, one, or two service panels need repairs.
- **Get suited for EVA.** Walk, jump, crouch, change camera views, and use the jetpack. Equip the suit before performing repairs or research.
- **Explore and collect data.** Complete regolith sampling, seismic monitoring, and the science lander investigation. Drive the open-wheel rover for a 100 m survey to unlock the capsule lab.
- **Run lab experiments.** Enter through the airlock to use the plant growth chamber and life-support console.
- **Play in local split-screen.** The second player uses a gamepad. Both players contribute to the same repairs and research objectives.
- **Track student attempts.** The game records landing grade, points, timing, and repair results in a CSV scorecard. Save and resume an after-landing mission locally. Continue can also import compatible SLEEPY4 data from a locally available legacy build.

## Run on Windows

1. Install Unreal Engine 5.8 and the Windows C++ toolchain supported by your Unreal installation, including the Windows SDK.
2. Open `MissionRecall.uproject`, build the project module if prompted, then select **Play** in the Unreal Editor. Start a mission from the in-game menu to generate the landing site.
3. To build and launch from VS Code, open the repository folder and run the **Run Mission Recall** task. The task calls `BuildEditor.bat` and `RunGame.bat`. Set the `UE_ROOT` environment variable if Unreal is installed outside the default Epic Games launcher directory.

The project selects Direct3D 11 on Windows. Local saves and scorecards are written under `Saved/MissionRecall`.

## Controls

| Activity | Controls |
| --- | --- |
| Landing | Move the mouse to orbit. Hold right mouse and move to steer, or use W, Q and D. Hold S or left mouse for a faster descent. Release the controls before touchdown. |
| EVA | W, Q, S and D to move, with A also available to strafe left. Space jumps, C crouches, V changes view, and I opens suit options. F toggles the jetpack and Space applies thrust. |
| Unsuited flight | W, Q and D to move, Space to rise, and S or Left Ctrl to descend. |
| Interactions | E enters or exits the rover or lab. Hold E for three seconds at a service panel or 30 seconds at a research station. Stay still and in range. |
| Rover | W accelerates, Q and D steer, S brakes, mouse orbits, and V recenters the camera. |
| Game | Escape pauses, F2 toggles split-screen, F5 saves a landed mission, and F9 reloads it. |
| Second player | Left stick moves, right stick looks, A jumps, X interacts, Y toggles the jetpack, and right trigger applies thrust. |

## Implementation

- `RecallFlight.cpp` handles landing, EVA, rover controls, and research interactions.
- `RecallWorld.cpp` generates the terrain, capsule, rover, instruments, and lab at runtime.
- `RecallHUD.cpp` provides the Slate menus, mission display, and scorecard screens.
- `RecallProgress.cpp` handles save snapshots, CSV scorecards, and legacy imports.
- `RecallAvatar.cpp` handles character visuals and animation. `RecallAudio.cpp` synthesizes mission audio in C++.

## Assets and verification

This source snapshot includes the procedural hardware material. The original menu artwork, imported astronaut rig and animation clips, Moon globe, and high-resolution terrain texture are omitted because their redistribution terms are unconfirmed. A text-based menu and a procedural player model are available in the code. Procedural terrain uses the engine default material without the original lunar texture, and the imported background globe is absent.

The original local Unreal project launched in Play-In-Editor using Direct3D 11. A recorded test reached a perfect landing and wrote the scorecard and save. The public snapshot was checked for project files and asset fallbacks. A clean Unreal build of this snapshot and a full surface, lab, and split-screen playthrough remain unverified.
