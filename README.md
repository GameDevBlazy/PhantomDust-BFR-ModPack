# BFR mod pack 1.42.9

A Phantom Dust (Microsoft Store, PC) mod pack for online play with friends. Every player in a match needs this same pack
and the same skill choice; the pack gives the game its own skill update version (1.42 with the Battle for Reality skills,
1.43 with the original skills), so Xbox Live only puts BFR players together.

## What it adds
- **Battle for Reality skill pack**: revised skills 1-374 and new skills 375-465. Switch it in the Lab: "Enable Battle for
  Reality skill pack (1-466)" or "Revert back to OG Phantom Dust skills (0-374)".
- **Xbox Live / system link lobby**: Edit Arsenal and arsenal lists 1-3; for the host, Change Rule (the game's rules plus
  Most KOs, First to 5 KOs and Survival with 3 revives a fighter) and Change Game Type; BFR Options for everyone:
  Teammate Lock-on, Tiebreaker Vote, and Next Map for the host; this session's wins and losses.
- **Game rules**: First to 5 KOs scores the last fighter who did damage (knocking yourself out scores nothing); the KO
  tally shows beside each name; a downed fighter's name plate keeps flashing until revived; Most KOs is decided when the
  match clock runs out.
- **Tie-breaker**: after a 1-1 session, SESSION INFO says so and players vote with the Tiebreaker Vote row (3 yes, or a
  tie of yes and no, means a tie-breaker).
- **3 vs 3** and six fighters, the 3 VS 3 LOBBY in split screen, HUD for fighters 5 and 6.
- **Split screen**: Edit Arsenal, Change Rule with the Create a Mode modes, game type, the stage shown behind the level
  list.
- **Arsenal lists**: the list title shows its number; Y > ARSENAL OPTIONS adds Add Arsenal (a full list of 16 sends it
  to the next list), Erase Arsenal, Reset All Arsenals and Acquire All Skills.
- **Arsenal Data**: Y switches the Xbox Live results between All and each stage (Palace, Highway, Panorama, City,
  Refinery, Lane, Ruins).
- **Edgar's Dream: Summit** (split screen STAGE LIST, after the stock stages): the top of Edgar's Dream's stairs - the
  arena of the Demented Freia fight - as a stage of its own, 4 players (built from your own Edgar's Dream files).
- **Bigger skill table and stage memory**, Download Battle choices (stage, characters, arsenal).
- **15 outfits** for every playable and enemy fighter: the 5th is the OG outfit, then colour themes (built from your
  own game files the first time one is picked).
- **Edgar's Dream** (offline): Create a Stage, Create a Mode, Spectator, Create an Outfit (skin tone, head accessory,
  jacket and pants colours, with a turning preview).

## Requirements
- Windows 10 or 11 and Phantom Dust from the Microsoft Store (game version 1.40, not modified).
- Nothing of the game is in this pack. The menus are patches: on the first start the loader reads your own game files,
  checks them and builds the changed menus in the pack's data folder. If your game files are not the expected ones,
  the loader refuses that file and the log says so.

## Install
1. Close Phantom Dust.
2. Run `INSTALL BFR.bat`. It checks every pack file and puts a "Phantom Dust BFR" shortcut on your desktop.
3. Start the game with that shortcut.

Keep the pack folder where it is; the shortcut starts the launcher from it. If you move the folder, run the install again.

## Uninstall
Close the game and run `UNINSTALL BFR.bat`. The shortcut is removed and the pack's data folder is renamed (kept) under
`%LOCALAPPDATA%\Packages\Microsoft.MSEsper_8wekyb3d8bbwe\AC\Temp\`. The game and your saves are never changed.

## Troubleshooting
The loader's log is `%LOCALAPPDATA%\Packages\Microsoft.MSEsper_8wekyb3d8bbwe\AC\Temp\CwoLoaderBFR\cwo_loader.log`.

## Online
Play online only with players who run this same pack. Offline-only parts (Create a Stage, Spectator, Create a Mode's
own modes) stay offline.

## Credits
- Battle for Reality skill data: the pack's author.
- Loader, launcher, menu patches and tools: written for this project by Claude (Anthropic), working with the pack owner.
- Phantom Dust belongs to Microsoft. This pack contains none of the game's files; you need your own copy of the game.
