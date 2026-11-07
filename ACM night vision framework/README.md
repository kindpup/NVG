# ACM Night Vision and Equipment Systems

This add-on contains the ACM rework of the owner-provided night-vision and equipment package. The imported systems include night vision, thermal palettes, equipment and weapon lights, lasers, radial/system menus, remote feeds, target detection, squad/role support, IVAS displays, and their required prefabs, materials, textures, and configuration resources.

## Existing PVS-15 controls

- `ACM_ToggleNVG` toggles the ACM PVS-15 view; it is currently bound to N.
- `ACM_ChangeSightMode` switches between green night vision and white-hot thermal; it is currently bound to B.

The imported action categories have their own ACM-prefixed entries in `Configs/System/keyBindingMenu.conf`; assign optional controls through the game's input settings.

## Rework notes

- Source script, action, and resource identifiers have ACM-prefixed names. Parallel palette variants use an ACM V2 prefix to avoid filename collisions.
- ACM's original project identity was kept. The imported generated resource database was omitted so Workbench can rebuild it.
- The imported visual assets remain unchanged. The separate ACM menu layouts from the earlier two-mode version are still visual assets; the imported system menus are included separately.
- The full import has not yet been freshly compiled or verified in-game.