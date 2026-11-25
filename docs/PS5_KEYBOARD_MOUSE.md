# Play PS5 games with a keyboard and mouse

Open **Keyboard & Mouse** in the PS5 Remote Play or capture-card dialog. Both use the same control profile and Remote Play connection. With a capture card, HDMI supplies the video. You can set up your controls before connecting a PS5.

## Start and stop control

| Default shortcut | Action |
| --- | --- |
| **F3** | Give the keyboard and mouse control of the PS5. Press it again to return to Windows. |
| **F1** | Show or hide the bindings for the current situation. You can keep steering or holding a button while the overlay is open. |
| **Ctrl+Alt+Shift+Z** | Release the keyboard and mouse immediately. This shortcut is fixed. |

Switching to another app or opening **Edit controls** releases held inputs. When you return to the player, press **F3** or select **Control PS5** to resume. A view-only connection sends no controls.

Under **Devices & shortcuts**, you can rebind **F1** and **F3** for all profiles on this PC. If a key is already in use, Veyra shows both assignments so you can choose which to change. Select **Try controls** to practice locally. If you skip it on first use, you can open it here later.

To hide the shortcut hints during play, click the keyboard button beside the eye in the player's top menu, or turn off **Show shortcut hints during play** in **Devices & shortcuts**. **F1** still shows the current situation, its bindings, and the keys for switching situations.

## Edit a profile

- In **Profiles & game setup**, select the GTA VI preset and match the required console settings. Save changes in a personal copy. You can import or export a profile as JSON.
- In **Bindings**, select a situation, then find an action in the list or select its key, mouse input, or controller button on the map. For example, **F** on foot shows **Enter vehicle** and **Triangle**. Select **Change key** to reassign it. An edit applies to the selected situation unless you choose the base binding.
- In **Mouse & aim**, set your mouse DPI and adjust sensitivity. To edit the camera response for **On foot** or **Aiming**, drag a point on the graph or enter its stick position and turn speed.
- In **Situations**, inspect how the profile switches between controls for walking and driving. Previewing a game situation in the editor leaves the live situation unchanged. During play, the **F1** overlay shows the profile's situation shortcuts. In the GTA VI preset, press **F6** to select **On foot** or **F7** to select **Driving** after a missed switch.

Profiles can also define a separate **Console menu**. Its bindings appear in the live overlay when you select it. The offline editor lists the profile's game situations.

## Build and test

Enable `VEYRA_BUILD_QML_UI` and `VEYRA_ENABLE_REMOTEPLAY` for a Windows player build. Install the x64 MSVC Rust toolchain specified in Stickmouse Mapper's `rust-toolchain.toml` and make Cargo available on the build path. CMake builds the pinned Mapper revision and stages its DLL, the GTA VI profile, and its license with the player.

For an offline build, set `FETCHCONTENT_SOURCE_DIR_STICKMOUSE` to the pinned checkout or the extracted `stickmouse-mapper` directory from the corresponding-source archive. Cargo also needs the locked crates in its local cache.

To run the UI and controller checks, use an x64 Visual Studio developer shell with Qt on the CMake prefix path. Set `VEYRA_FFMPEG_ROOT` to the patched FFmpeg development directory to include both simulated console routes.

```powershell
cmake -S tests/remoteplay/keyboard-mouse -B out/ps5-keyboard-checks -G Ninja -DCMAKE_BUILD_TYPE=Release "-DVEYRA_FFMPEG_ROOT=$env:VEYRA_FFMPEG_ROOT"
cmake --build out/ps5-keyboard-checks --parallel 2
ctest --test-dir out/ps5-keyboard-checks --output-on-failure
```
