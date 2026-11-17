# BFR mod pack - source code

This folder holds the source of the BFR mod pack's own programs, written for this project. The pack's `launcher\CwoLoader.dll` and `launcher\CwoLaunch.exe` are built from exactly this `loader` folder.

Anyone interested in Phantom Dust modding is welcome to read it and use any of it in their own projects.

Nothing of the game is in here. There are no game files and none of the game's own code. The comments name places in the game's program (addresses such as `0x1404bdc50` and function names such as `FUN_140252510`). That is where the loader reads or changes the running game.

## loader/ - the loader, the launcher and the signing tool (C++, Windows)
- `src/cwo_loader.cpp` - the loader (`CwoLoader.dll`). It is one translation unit, and every `cwo_*.inl` file is part of it.
- `src/cwo_launch.cpp` - the launcher (`CwoLaunch.exe`). It starts Phantom Dust with the loader and checks for pack updates.
- `src/cwo_sign.cpp` - the tool that signs a pack's MANIFEST.
  - The private key is never in this repository.
  - The launcher only accepts updates signed with it.
- `tests/` - offline tests, for example the patch applier against your own game files.
- `build.bat` - builds everything into `build\`.
  - It needs Visual Studio's C++ tools (x64).
  - Run it from an "x64 Native Tools Command Prompt", or set `CWO_VCVARS` to your `vcvars64.bat`.
  - Windows' own libraries only; no other downloads.

## tools/ - the Python tools that build the pack's data
- **Python 3:** almost all of them use only Python's standard library.
- **capstone:** one tool needs the free capstone library (`pip install capstone`) to read the game's machine code.
- **Your game files:** the tools read your own copy of Phantom Dust. Set `PD_GAME` to the game's folder (the one holding `Assets\Data`); see `pd_paths.py`.
- **Shared helpers:** `pd_common.py` holds the helpers (PNG and DXT textures, stage collision, the stage list).
- **Main tools:**
  - `build_bfr_pack.py` - builds the pack.
  - `build_lobby3.py`, `build_modlist_menu.py`, `build_hud_option.py`, `build_option_more.py`, `build_lab_vault.py`, `build_stage_rows.py` - patch the game's menu scripts.
  - `build_lobby_rows.py`, `build_stage_portraits.py`, `build_select_outfits.py`, `build_select_atlas_only.py` - make the menu pictures.
  - `edgar_outfits.py`, `outfit_generator.py` - make the outfits.
  - `build_pieces_canvas.py` - makes the Edgar's Lab piece stages.
  - `add_community_skill.py` - adds an approved community skill.

Changed game files are shipped only as patches (`.cwopatch`). Each player's loader applies them to that player's own game files.

Phantom Dust belongs to Microsoft.
