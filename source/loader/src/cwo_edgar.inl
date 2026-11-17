// ---- v3.4 Edgar's Dream (owner 2026-10-24)
// The chooser (v3.3) opens every creator as a page here, on the menus, before any stage is loaded.
// Game facts used (Ghidra full-pass export batch_065, reviewed 2026-10-24; 07_Combat_and_Skills/EDGARS_DREAM_2026-10-24.md):
//   Mode rule table: GSDATA (*0x1404c5228) + 0x33040, 13 rules x 16 bytes {start life, min, max, minutes}; the match clock
//     is minutes * 0x708 ticks (FUN_140128cc0), the life clamp FUN_14024d970. The match's rule is *0x1404bc5dc.
//   Players: *(0x1404bc9d8 + 8p); +0x20 present, +0x24 standing (0 = down), +0x4C life, +0x50 life cap; the state machine
//     at +0x12F00 (+0x12F04 state, +0x12F08 countdown). State 0x1B = down: FUN_14025c670 counts +0x12F08 to 0, waits for
//     the fall (FUN_14025a050: +0x1396C == -1 and +0x13968 == 0), then revives (rule 10, FUN_140251f80) or removes the
//     player from the match (FUN_140129e70). The revive halves the life cap (except rule 10) and copies it to the life.
//   Starting capsules: FUN_14014ee70, loop limit byte at 0x14014f2f0 (cmp r15d, 3) and the per-player counters set to 3 at
//     0x14014f018 / 0x14014f024 (IGT-046b bytes).
//   Online sessions: *0x1404bc9d4 is 4 or 5 (FUN_140187710 sets 4): nothing here acts then.
//   Aura: manager *0x14052ad00, player slot 0x120 * player, +0xC0 bit 0x10 shown, +0xC8 the colour auraMove writes.
//   Fighter setup entries 0x1404bc420 (4 x 0x48): +0 used, +0xC pad, +0x10 human, +0x20 a record whose +4 is the
//     fighter class (FUN_140128cc0): 0 or no record pdNormalPlayerClass, 1-5 the bosses pdBoss00/01/02/03/03b (v3.5
//     correction: kind 1 is a boss, not "the CPU").
//   v3.5 CPU brain (FUN_14024e790, FUN_14024ec80, FUN_140176c80, FUN_14017b020, FUN_14017b350): a normal fighter with
//     +0x30 == 0 is driven by its brain at +0x142d0 (run each frame by FUN_1401775b0 while *0x1404c0180 != 0); with
//     +0x30 != 0 its pad is read (FUN_140256580, +0x14248). FUN_140176c80(brain, fighter, record) sets the brain up from a
//     record: +0x18 CPU row (table *0x1404c0188, 10 bytes), +0x31 deck plan name ("com\deck", "" = none), +0x46 AI
//     program name ("com\ai", e.g. eneprog006, as in questdata.qdt); it takes 0x8000 from the fighter's own arena
//     (+0x12c48 base, +0x12c50 next, +0x12c58 size; the game hangs when it is full), so it is done once per match.
static const ULONG_PTR RVA_RULE = 0x4bc5dc, RVA_NETMODE = 0x4bc9d4, RVA_REVIVE = 0x251f80, RVA_FIGHTERS = 0x4bc420;
static const ULONG_PTR RVA_AI_INIT = 0x176c80, RVA_AI_ON = 0x4c0180, RVA_AI_POLL = 0x17b4a0;
typedef void(__fastcall *PFN_AI_INIT)(ULONG_PTR, ULONG_PTR, ULONG_PTR);
// v5.9 (owner): FUN_140176c80 -> FUN_14017b020 -> FUN_14017b350 only queues the
// program file (com\ai, descriptor brain + 0x138 + 0x2240) and clears "started" (+0x24e0). FUN_14017b4a0 (brain + 0x138)
// starts it once the file is in (FUN_14017b520, +0x24e0 = 1), but its one caller FUN_14024f6c0 runs only while the fighter
// is not ready yet (+0x28 == 0, before the match). A brain set up in the match is polled here until its program runs.
typedef int(__fastcall *PFN_AI_POLL)(ULONG_PTR);
static const ULONG_PTR AI_SUB = 0x14408, AI_STARTED = 0x24e0;
static const ULONG_PTR RVA_CAPS_LOOP = 0x14f2f0, RVA_CAPS_C1 = 0x14f018, RVA_CAPS_C2 = 0x14f024;
static const int RULE_TABLE = 0x33040, RULE_N = 13;
typedef void(__fastcall *PFN_REVIVE)(ULONG_PTR);
static ULONG_PTR *g_actor_test_tab;                            // self-test: stand-in player table
static const char *f_stage_title(int stage);                   // cwo_forge.inl (drawing section)

// ---------------------------------------------------------------- creator items
enum { EDP_CHOOSER = 0, EDP_SCHOOL, EDP_CHAR, EDP_QUEST, EDP_MODE, EDP_SPECT, EDP_OUTFIT, EDP_N };   // v5.61 Create an Outfit
static const int EDP_LOBBY = 50;                                // v5.2 the 3 VS 3 LOBBY (cwo_lobby.inl), not a creator page
static const int EDP_FUSE = 51;                                 // v5.17 the Skill Fuser (cwo_fusion.inl), not a creator page
static void fu_input(WORD pressed);
template <class PUT, class BOX> static void fu_draw(PUT &put, BOX &box, int x, int y);
static const int EDP_RECORDS = 52;                              // v5.21 Records (cwo_records.inl), not a creator page
static void rc_input(WORD pressed);
template <class PUT, class BOX> static void rc_draw(PUT &put, BOX &box, int x, int y);
static const int EDP_MUSIC = 53;                                // v5.23 Stage Music (cwo_music.inl), not a creator page
static void mu_input(WORD pressed);
template <class PUT, class BOX> static void mu_draw(PUT &put, BOX &box, int x, int y);
static const int EDP_DRAFT = 54;                                // v5.25 Draft Arsenal (cwo_draft.inl), not a creator page
static void dr_input(WORD pressed);
template <class PUT, class BOX> static void dr_draw(PUT &put, BOX &box, int x, int y);
static const int EDP_RAID = 55;                                 // v5.26 Boss Raid (cwo_bossraid.inl), not a creator page
static const int EDP_STAGE = 56;                                // v5.32 CREATE A STAGE (cwo_createstage.inl), not a creator page
static const int EDP_VISIONS = 57;                              // v5.86 Visions (cwo_visions.inl), not a creator page
static const int EDP_VAULT = 58;                                // v5.133 Skill Vault (cwo_vault.inl), opened from View Stock
static void vt_input(WORD pressed);
template <class PUT, class BOX> static void vt_draw(PUT &put, BOX &box, int x, int y);
static void vi_input(WORD pressed);
template <class PUT, class BOX> static void vi_draw(PUT &put, BOX &box, int x, int y);
static void cs_input(WORD pressed);
template <class PUT, class BOX> static void cs_draw(PUT &put, BOX &box, int x, int y);
static void br_input(WORD pressed);
template <class PUT, class BOX> static void br_draw(PUT &put, BOX &box, int x, int y);
static void lob_input(WORD pressed);
static void lob_open(bool boot);
static void lob3_request();
static void lob_rate_step();
static void sixf_diag_step();                                 // v5.5 cwo_sixf2.inl
static void sixf_end_step();                                  // v5.6 cwo_sixf2.inl
static void sixf_lock_trace_step();                           // v5.6 cwo_sixf2.inl
static void sixf_down_glow_step();                            // v5.6 cwo_sixf2.inl
template <class PUT, class BOX> static void lob_draw(PUT &put, BOX &box, int x, int y);
enum { FK_NUM, FK_MIN, FK_SCHOOL, FK_HUE, FK_PCT, FK_SKILL, FK_PART, FK_ONOFF, FK_BODY, FK_ANYSCHOOL, FK_QUEST, FK_STAGE,
       FK_CHARA, FK_CAPS, FK_SHOW, FK_BRAIN, FK_HEAD, FK_SIZE, FK_FROM, FK_SLOT, FK_WEAR, FK_MOVES, FK_VOICE, FK_EVADE, FK_THEME, FK_BPART, FK_DEG, FK_PIECES, FK_PIECE, FK_LAYER, FK_FACE,
       FK_MINNL, FK_RULE, FK_FMT, FK_ENVMODE, FK_ENVSK, FK_SEC, FK_GENDER,
       FK_PIC, FK_QLEVEL, FK_REINF, FK_PARTNER, FK_QRULES, FK_RANK, FK_CHARAN, FK_OUTFIT, FK_KEEP, FK_CREDITS, FK_PLAYAS,
       FK_OCHAR, FK_SKINT, FK_OCOL, FK_OSLOT, FK_OSTOCK, FK_OFX, FK_OFXPL, FK_OFXPCT, FK_OFXSPD, FK_OFXSHOW };   // v5.79 the effect editor   // v5.61 Create an Outfit   // v5.13 Create a Quest v2   // v5.10 the male / female base   // v4.8 Create a Mode
struct EdField { const char *label; int kind, lo, hi, step, def; const char *help; };
struct EdItem { char name[24]; int v[40]; };
struct EdCreator {
    const char *title, *file, *word;            // page title, save file, what one item is called
    int nf;
    const EdField *f;
    const char *const *actions;                 // rows after the fields
    int na;
    const char *const *howto;                   // the help card (Y)
    EdItem item[16];                            // v5.24: 16 (Create a Character keeps 8: the select-screen cells)
    int n, cur;
};
static int ed_cap(int page) { return page == EDP_CHAR ? 8 : 16; }

static const EdField SCHOOL_F[] = {
    {"Based on", FK_SCHOOL, 0, 5, 1, 1, "A stock school (its color when Color is 'game'), or None"},
    {"Aura color", FK_HUE, -10, 350, 10, -10, "L/R: hue in 10-degree steps; 'game' = the school's color"},
    {"Color strength", FK_PCT, 10, 100, 10, 80, "How saturated the aura color is"},
    {"Color brightness", FK_PCT, 20, 100, 10, 70, "How bright the aura glows (added on top of the picture)"},
    {"Aura effect", FK_SKILL, -1, 752, 1, -1, "A skill whose effect loops on your body as the aura"},
    {"Aura effect part", FK_PART, 0, 5, 1, 0, "Which part of the skill's effect (see its name)"},
    {"Aura effect shows", FK_SHOW, 0, 1, 1, 0, "With aura: only while your aura is up; or always"},
    {"School skill 1", FK_SKILL, -1, 752, 1, -1, "Each new card in your hand becomes one of these skills"},
    {"School skill 2", FK_SKILL, -1, 752, 1, -1, "Skills of this school (in turn)"},
    {"School skill 3", FK_SKILL, -1, 752, 1, -1, "Skills of this school (in turn)"},
    {"School skill 4", FK_SKILL, -1, 752, 1, -1, "Skills of this school (in turn)"},
    // v3.9 (owner 2026-10-25): where the effect sits, its size and speed, which pieces, and the aura itself
    {"Effect on", FK_BPART, 0, 8, 1, 0, "Where on the body: the skill's own place, or a part"},
    {"  move up / down", FK_NUM, -40, 40, 1, 0, "Half-step units (the character is about 34 tall)"},
    {"  move left / right", FK_NUM, -40, 40, 1, 0, "Half-step units, from the character's view"},
    {"  move fwd / back", FK_NUM, -40, 40, 1, 0, "Half-step units, in front (+) or behind (-)"},
    {"  tilt", FK_DEG, -180, 180, 15, 0, "Tips it forward / back"},
    {"  turn", FK_DEG, -180, 180, 15, 0, "Turns it around the character"},
    {"  size", FK_PCT, 10, 400, 10, 100, "The whole effect"},
    {"  width", FK_PCT, 10, 400, 10, 100, "Side to side"},
    {"  height", FK_PCT, 10, 400, 10, 100, "Up and down"},
    {"  speed", FK_PCT, 10, 400, 10, 100, "How fast it plays (100 = the skill's own speed)"},
    {"Effect pieces", FK_PIECES, 0, 1, 1, 0, "The part's own pieces, or the pieces set below"},
    {"  piece 1", FK_PIECE, -1, 15, 1, -1, "Only this piece of the effect shows (none: all pieces)"},
    {"  piece 2", FK_PIECE, -1, 15, 1, -1, "Another piece (none = not used)"},
    {"  piece 3", FK_PIECE, -1, 15, 1, -1, "Another piece (none = not used)"},
    {"  piece 4", FK_PIECE, -1, 15, 1, -1, "Another piece (none = not used)"},
    {"Replace the aura", FK_ONOFF, 0, 1, 1, 0, "On: the game's aura is hidden; the effect shows instead"},
    // v5.9 (owner 21:32: "The hue doesn't affect it"): the aura effect's own colour, on a copy only your aura draws from
    {"Effect color", FK_HUE, -10, 350, 10, -10, "The aura effect's color ('game color' = the skill's own)"},
    {"  color strength", FK_PCT, 10, 100, 10, 100, "How much of that color (100 = all of it)"}};
// v3.9 the School page shows the rows in this order (the saved values keep the order of SCHOOL_F)
static const int SCHOOL_ORDER[] = {0, 1, 2, 3, 26, 4, 5, 6, 27, 28, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 7, 8, 9, 10};
static const char *SCHOOL_A[] = {"Use this school now", "Try it now (Skill Test Stage)", "Delete this one (A twice)", "Save", "Back"};
static const char *SCHOOL_H[] = {"CREATE A SCHOOL - how to use", "1  L/R on 'School' picks one; A on it makes a new one",
                                 "2  Aura color: L/R picks a hue (Based on: Aura Particle = the Aura Particle school's color)",
                                 "3  Aura effect: any skill's effect; Effect part = which piece of it",
                                 "   0 hand (during the move)  1 feet / ground  2 extra piece",
                                 "   3 the impact  4 held (on the head)  5 the burst when a hold ends",
                                 "4  Effect on + move / tilt / size / speed: place it on the body",
                                 "5  Effect pieces: custom = only the pieces you pick",
                                 "   Effect color: the effect's own color (textures keep theirs)",
                                 "6  Try it now: straight into the Skill Test Stage (Start: menu)",
                                 "7  In the test, R3 starts the aura effect again", NULL};
// v3.6 (owner 2026-10-24; "dramatically detailed ... the character's stance down to all their animations, what they should look like").
// Built at the install by 13_Independent_Research_Tools/build_character.py from the proven routes: body + moves (IGT-031b/c/d),
// size fit (031f), head (038b), per-slot animations (037b). The values keep the 3.4 order first (body, school, colour).
enum { CH_BODY, CH_SCHOOL, CH_COLOUR, CH_HEAD, CH_SIZE, CH_MOVES, CH_G0, CH_GN = 12, CH_S0 = CH_G0 + CH_GN, CH_WEAR = CH_S0 + 6,
       CH_VOICE, CH_EVADE, CH_THEME, CH_MID, CH_MIDSTYLE, CH_LOW, CH_LOWSTYLE, CH_EXMOT, CH_FACE, CH_HEADX, CH_BASE, CH_NF };   // v4.0 rows appended       // v3.8: the three new rows come last (saved characters keep their values)
static const EdField CHAR_F[] = {
    {"Body", FK_BODY, 0, 19, 1, 0, "LOOK: whose body: 11 players or 9 NPCs (one look)"},
    {"School", FK_ANYSCHOOL, 0, 21, 1, 1, "A stock school or one you made (aura + skills come too)"},
    {"Outfit color", FK_NUM, 0, 3, 1, 0, "LOOK: the body's color set (0 = first of 4)"},
    {"Head", FK_HEAD, -1, 19, 1, -1, "LOOK: another character's head: face, eyes, hair, hat"},
    {"Height", FK_SIZE, 0, 1, 1, 0, "LOOK: the body's own height, or fitted to the moves"},
    {"Moves (base)", FK_MOVES, 0, 21, 1, 0, "MOVES: whose animations (11 players, or the OG Xbox)"},
    {"Stance (standing)", FK_FROM, -1, 10, 1, -1, "WAIT0 .. WAIT_DAM: how you stand, idle and brace"},
    {"Walking", FK_FROM, -1, 10, 1, -1, "WALK, WALK_FAST, WALK_SLOW and walking hurt / guarded"},
    {"Walk turns L/R/back", FK_FROM, -1, 10, 1, -1, "TURN_WALK_R / L / BR / BL: turning left, right, back"},
    {"Running", FK_FROM, -1, 10, 1, -1, "RUN, RUN_FAST, RUN_SLOW and running hurt / guarded"},
    {"Run turns L/R/back", FK_FROM, -1, 10, 1, -1, "TURN_RUN_R / L / BR / BL"},
    {"Hurt turns", FK_FROM, -1, 10, 1, -1, "TURN_WDAM_* / TURN_RDAM_*: turning while hurt"},
    {"Evade (dash)", FK_FROM, -1, 10, 1, -1, "DASH_A / DASHEND_A / DASH_B / DASHEND_B (e.g. cartwheel)"},
    {"Jump", FK_FROM, -1, 10, 1, -1, "JUMP_START, UP, DOWN, LAND0, LAND1"},
    {"Float", FK_FROM, -1, 10, 1, -1, "FLOAT_IN, FLOAT_WAIT, FLOAT_MOVE"},
    {"Knock-down + get up", FK_FROM, -1, 10, 1, -1, "BLOW_*, WALLHIT_*, DOWN_* and standing back up"},
    {"Skill poses", FK_FROM, -1, 10, 1, -1, "ATK_* / DEF_* / SPE_*: how each skill is thrown or cast"},
    {"Hit reactions", FK_FROM, -1, 10, 1, -1, "DMG_*: how you react to each kind of hit"},
    {"One animation 1", FK_SLOT, 0, 129, 1, 33, "Any single animation by its game name (LB/RB: 10)"},
    {"  ... from", FK_FROM, -1, 10, 1, -1, "Whose version of that one animation"},
    {"One animation 2", FK_SLOT, 0, 129, 1, 42, "Any single animation by its game name"},
    {"  ... from", FK_FROM, -1, 10, 1, -1, "Whose version of that one animation"},
    {"One animation 3", FK_SLOT, 0, 129, 1, 0, "Any single animation by its game name"},
    {"  ... from", FK_FROM, -1, 10, 1, -1, "Whose version of that one animation"},
    {"Also as outfit", FK_WEAR, 0, 4, 1, 0, "Own select slot; 1-4: also one of your outfits"},
    // v3.8 (owner 2026-10-25): voice, evade type, outfit theme
    {"Voice", FK_VOICE, -1, 27, 1, -1, "Battle voice: any of the game's 28 (players, enemies)"},
    {"Evade type", FK_EVADE, 0, 2, 1, 0, "Jump or Dash (cartwheel / roll); 'moves' = the base's"},
    {"Outfit theme", FK_THEME, 0, 10, 1, 0, "LOOK: the outfit recolored (fire, ice, gold ...)"},
    // v4.0 (owner 2026-10-25): more body parts and the skill motions' source
    {"Middle (torso, arms)", FK_HEAD, -1, 19, 1, -1, "LOOK: another character's torso, jacket, arms and hands"},
    {"  middle style", FK_LAYER, 0, 2, 1, 0, "Replace yours, layer over, or layer over keeping arms"},
    {"Lower (pants, legs)", FK_HEAD, -1, 19, 1, -1, "LOOK: another character's pants, legs and shoes"},
    {"  lower style", FK_LAYER, 0, 1, 1, 0, "Replace yours, or layer over yours"},
    {"Skill motions from", FK_FROM, -1, 10, 1, -1, "The 6 extra skill motions: base moves, or a player's"},
    // v4.1 (owner: the name and face on the select screen)
    {"Select-screen face", FK_FACE, 0, 1, 1, 0, "Select screen: the dark '?' face, or the body's own"},
    // v4.9 (owner)
    {"Head extras", FK_HEAD, -1, 19, 1, -1, "LOOK: another's hair, hat, glasses ... on top of yours"},
    // v5.10 (owner HARD RULE 2026-10-26: "when you start with a base, you can select the base will be male or female. Once you
    // click female, it would only load all the animations and body parts from female characters")
    {"Base", FK_GENDER, 0, 1, 1, 0, "Male or female: only that gender's bodies, parts, moves"}};
// v4.0 the Character page shows the rows in this order (the saved values keep the order of CHAR_F)
static const int CHAR_ORDER[] = {35, 0, 2, 3, 34, 28, 29, 30, 31, 4, 27, 33, 1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
                                 32, 25, 26, 24};
static const char *CHAR_A[] = {"My characters (catalog)", "Use this character now", "Preview live (Character viewer)",
                               "Try it now (Skill Test Stage)", "Delete this one (A twice)", "Save", "Back"};
static const char *CHAR_H[] = {"CREATE A CHARACTER - how to use", "1  Base: Male or Female (Female = Freia, Tsubutaki,",
                               "   Mikan): every list then offers only that half", "2  LOOK: body, color, head, middle, lower, extras",
                               "3  MOVES: a base set, then any group or single one", "4  The model beside the page follows each change",
                               "5  My characters: every one you made. A: play as it now", "   (top right of the Multiplayer character select)",
                               "   Y: change it   X twice: delete it", NULL};
// v5.13 (owner 2026-10-26: every quest setting editable): the first 8
// values keep their place (older saves); 13_Independent_Research_Tools/quest_writer.py writes them (field facts in its
// header and CAMPAIGN_QUEST_SPEC_BATCH023-025.md section 9)
static const EdField QUEST_F[] = {
    {"Replaces quest", FK_QUEST, 0, 255, 1, 1, "The campaign quest this one takes the place of"},
    {"Stage", FK_STAGE, 1, 34, 1, 2, "Where it is fought"},
    {"Time limit", FK_MIN, 1, 30, 1, 10, "Minutes"},
    {"Rounds", FK_NUM, 1, 3, 1, 1, "Rounds to win"},
    {"Opponents", FK_NUM, 1, 3, 1, 1, "How many fight you at once (3 on the field with an ally)"},
    {"Opponent 1", FK_CHARA, 0, 24, 1, 1, "Who"},
    {"Opponent 2", FK_CHARA, 0, 24, 1, 2, "Who (when 2 or more)"},
    {"Opponent 3", FK_CHARA, 0, 24, 1, 3, "Who (when 3)"},
    {"Stage version", FK_NUM, 0, 3, 1, 0, "The stage's version (as Single Play lists them)"},
    {"Mission picture", FK_PIC, -1, 143, 1, -1, "The mission board picture (MptNNN); keep = the quest's"},
    {"  opponent 1 outfit", FK_OUTFIT, 0, 31, 1, 0, "Its outfit / color"},
    {"  opponent 2 outfit", FK_OUTFIT, 0, 31, 1, 0, "Its outfit / color"},
    {"  opponent 3 outfit", FK_OUTFIT, 0, 31, 1, 0, "Its outfit / color"},
    {"  opponent 1 level", FK_QLEVEL, 0, 4, 1, 0, "How it fights: the quest's own, Tough, Brutal ..."},
    {"  opponent 2 level", FK_QLEVEL, 0, 4, 1, 0, "How it fights"},
    {"  opponent 3 level", FK_QLEVEL, 0, 4, 1, 0, "How it fights"},
    {"Opponents' life", FK_NUM, 5, 99, 5, 20, "Life each opponent starts with (game: 20)"},
    {"Reinforcements", FK_REINF, 0, 2, 1, 0, "When one goes down: the next, a random one, or the same"},
    {"Opponents in all", FK_NUM, 1, 20, 1, 1, "How many come in all; the extra ones are reinforcements"},
    {"Partner", FK_PARTNER, 0, 12, 1, 0, "The quest's, none, one partner, or any you unlocked"},
    {"  partner level", FK_NUM, 1, 3, 1, 1, "The partner's strength (the game's partner levels 1-3)"},
    {"Ally", FK_CHARAN, -1, 24, 1, -1, "Anyone as a CPU on your team (none = no ally)"},
    {"  ally outfit", FK_OUTFIT, 0, 31, 1, 0, "Its outfit / color"},
    {"  ally level", FK_QLEVEL, 0, 4, 1, 4, "Brutal escort stays near you; others chase opponents"},
    {"Win rules from quest", FK_QRULES, -1, 255, 1, -1, "Its own, or another quest's rules, goals and events"},
    {"Rank", FK_RANK, 0, 4, 1, 0, "The letter on the mission board"},
    {"Your start point", FK_KEEP, -1, 3, 1, -1, "Where you start (0-3, first floor); keep = the quest's"},
    {"First-clear credits", FK_CREDITS, 0, 30000, 500, 0, "Credits for the first clear (0 = the quest's rewards)"},
    {"Your life", FK_NUM, 5, 99, 5, 20, "Life you start this quest with (game: 20)"},
    // v5.14.1 (owner)
    {"Play as", FK_PLAYAS, -1, 24, 1, -1, "Yourself, or any character (model, moves, voice)"},
    {"  your outfit", FK_OUTFIT, 0, 31, 1, 0, "That character's outfit"}};
static const int QUEST_ORDER[] = {0, 1, 8, 9, 25, 2, 3, 4, 5, 10, 13, 6, 11, 14, 7, 12, 15, 16, 28, 29, 30, 17, 18, 19, 20, 21, 22, 23, 24,
                                  26, 27};
static const char *QUEST_A[] = {"Save (written into the campaign at the quest install)", "Add the ready-made Training quest", "Back"};
static const char *QUEST_H[] = {"CREATE A QUEST - how to use", "1  Name it: that becomes the quest title", "2  Pick the quest it replaces",
                                "2b An '(unused)' quest is an empty slot: the game never offers it",
                                "3  Stage, picture, rank, time, rounds; opponents: who, outfit, level",
                                "4  Life, reinforcements, partner, an ally who follows you",
                                "5  Win rules: its own, or another quest's (objectives + events)",
                                "6  Save; the quest install writes it into the campaign",
                                "   (backups kept; the install can undo it). Description: CwoQuestTexts.txt", NULL};
static const char *ED_QLEVELS[5] = {"the quest's own", "Tough", "Brutal", "Brutal - weakest first", "Brutal escort"};
static const char *ED_PARTNERS[13] = {"keep the quest's", "none (solo)", "Meister", "Chunky", "Cuff Button", "pH", "Edgar", "Know",
                                      "Tsubutaki", "Sammah", "JD", "Robo", "you choose (any unlocked)"};
static const char *ED_REINF[3] = {"the next one", "a random one", "the same one again"};
static const char *ED_RANKS[5] = {"keep the quest's", "S", "A", "B", "C"};
static bool ed_pic_ok(int v) {                                 // the 118 Menu\Mpt%03d.alr pictures (11-18, 100-111, 131, 133 none)
    return v < 0 || (v <= 10) || (v >= 19 && v <= 99) || (v >= 112 && v <= 130) || v == 132 || v == 134 || (v >= 139 && v <= 143);
}
static int lob_outfits_of_row(int row);                        // cwo_lobby.inl: the charaset's outfit count (0 = not known)
// v4.8 (owner 2026-10-25):
// the first 4 values keep their place (older saves). Facts: rule table GSDATA +0x33040 (16 bytes x 13: start life, life
// min, life max, minutes; FUN_140128cc0 / FUN_14024d970); Special Environment (rule 5, FUN_14012dab0 / FUN_14012dbf0):
// its environment list = GSDATA +0x33114 (up to 32 skill ids, ended by 0), one every GSDATA +0x33110 seconds, shuffled at
// the start; capsule spots *0x140584f80 + 0xD10 + (p * 3 + k) * 0x48, +0x18 the skill (IGT-058e).
static const EdField MODE_F[] = {
    {"Time limit", FK_MINNL, 0, 30, 1, 10, "Minutes of a match (0 = no limit; the game's rules: 10)"},
    {"Starting life", FK_NUM, 5, 99, 5, 20, "Life each fighter starts with (game: 20)"},
    {"Lives", FK_NUM, 1, 9, 1, 1, "KOs before a fighter is out (gets up at full life)"},
    {"Starting capsules", FK_CAPS, 3, 3, 1, 3, "Always 3, as the game (v5.188.3: the 2-capsule start is gone)"},   // owner 2026-11-15
    {"Game rule it plays on", FK_RULE, 0, 11, 1, 0, "The game's rule underneath: its arsenal limits, extras"},
    {"Players", FK_FMT, 0, 13, 1, 0, "Who fights whom: 1 vs 1 ... King of the Hill, Survival"},
    {"Environment", FK_ENVMODE, 0, 2, 1, 0, "None, the game's random ones, or the ones you pick below"},
    {"  environment 1", FK_ENVSK, -1, 752, 1, -1, "An environment skill that takes over the field in turn"},
    {"  environment 2", FK_ENVSK, -1, 752, 1, -1, "Another one (none = not used)"},
    {"  environment 3", FK_ENVSK, -1, 752, 1, -1, "Another one (none = not used)"},
    {"  environment 4", FK_ENVSK, -1, 752, 1, -1, "Another one (none = not used)"},
    {"  a new one every", FK_SEC, 10, 300, 10, 60, "Seconds between environments"},
    {"Banned skill 1", FK_SKILL, -1, 752, 1, -1, "Never comes from a capsule: becomes another of its kind"},
    {"Banned skill 2", FK_SKILL, -1, 752, 1, -1, "Another banned skill (LB / RB held: 10 at a time)"},
    {"Banned skill 3", FK_SKILL, -1, 752, 1, -1, "Another banned skill"},
    {"Banned skill 4", FK_SKILL, -1, 752, 1, -1, "Another banned skill"},
    {"Banned skill 5", FK_SKILL, -1, 752, 1, -1, "Another banned skill"},
    {"Refill: not built yet", FK_ONOFF, 0, 0, 1, 0, "Arsenal refill is not built yet: always off"},   // v5.188.11 (in game the long label ran into its value)   // v5.188.7 hourly pass: the row did nothing
    {"Realistic collision", FK_ONOFF, 0, 1, 1, 0, "On: shots hit anyone in the way, teammates too; offline"}};   // v5.81
static const char *MODE_A[] = {"3 VS 3 LOBBY (split screen)", "Play this mode (on / off)",   // v5.188.7: the backup quick lobby row is gone (one way in)
                               "Add the ready-made modes", "Delete this one (A twice)", "Save", "Back"};
// v5.188.37 (hourly pass 8, in game: the help card dropped every '>' - 'Play it: X  Change Rule' - the game's menu rows do
// not draw it; its own footer window does): help cards say 'then' instead
static const char *MODE_H[] = {"CREATE A MODE - how to use", "1  L/R on 'Mode' picks one; A on 'Name' renames it",
                               "2  Time, life, lives, capsules, the game rule underneath", "3  Players, environments, banned skills",
                               "4  The lines at the bottom say exactly what the mode does", "5  Play it: in a lobby, X then Change Rule picks it", "   (split screen, or MULTIPLAYER then SINGLE PLAY)", "   3 VS 3 LOBBY needs 2 controllers; one controller:", "   MULTIPLAYER then SINGLE PLAY, Game Type 3 vs 3",
                               "6  Online: only Most KOs, First to 5 KOs and Survival revives", "   play; the other modes stay offline", NULL};   // v5.188.7 (was: 'Never online')
static const char *ED_RULES[12] = {"Standard Battle", "Ante", "Quick", "Puny", "Heavy", "Special Environment", "Single School",
                                   "Highlander", "Puny Only", "Heavy Only", "KO Battle", "Annihilate the Odd One"};
static const char *ED_FMTS[14] = {"1 vs 1", "2 vs 2 (teams)", "1 vs 2 (handicap)", "2 vs 1 (handicap)", "free for all, 3",
                                  "free for all, 4", "1 vs 3 (handicap)", "3 vs 3 (6 fighters)", "Survival (endless CPU waves)",
                                  "Shrinking arena (a closing ring)", "King of the Hill (hold the zone)",
                                  "Most KOs (points at time up)", "First to 5 KOs", "Survival revives (a set number a side)"};   // v5.53
// v3.5 (owner 2026-10-24)
static const EdField SPECT_F[] = {
    {"Player 1 starts as a CPU", FK_ONOFF, 0, 1, 1, 0, "On: every offline match starts with the CPU as you"},
    {"CPU program", FK_BRAIN, 0, 13, 1, 6, "Which of the game's CPU programs plays your fighter"}};   // v5.62 all of them
static const char *SPECT_A[] = {"Back"};
static const char *SPECT_H[] = {"SPECTATOR - how to use", "1  In ANY offline match: hold View, press LB",
                                "   = the CPU takes over your fighter; again = you", "2  'Starts as a CPU' on: every match starts that way",
                                "3  'CPU program': opponent's copies a CPU in the match", "   Your fighter keeps your deck; offline only", NULL};
// v5.62 (owner 2026-11-02): every fighting
// program in the game's comi (the first 8 keep their old numbers for saved setups)
static const char *ED_BRAINS[] = {"opponent's", "eneprog000", "eneprog001", "eneprog003", "eneprog004", "eneprog005", "eneprog006", "eneprog_freya",
                                  "eneprog000_nodef", "eneprog000_noNdef", "eneprog001_nodef", "eneprog001_noNdef", "eneprog002_nodef",
                                  "eneprog002_noNdef"};
static const char *CHOOSER_H[] = {"EDGAR'S DREAM - how to use", "D-pad: choose   A: open   B: back to the stage list",
                                  "Y on any page: this help   X on a value: back to its default",
                                  "Every creator saves itself; your place is remembered", "'Try it now' jumps into the Skill Test Stage", NULL};

// v5.61 Create an Outfit (owner 2026-11-02): character, the outfit it replaces, skin tone, head accessory, jacket, pants
static const EdField OUTFIT_F[] = {
    {"Character", FK_OCHAR, 0, 10, 1, 0, "Whose outfit"},
    {"Outfit", FK_NUM, 1, 15, 1, 1, "The outfit slot it starts from (1-15; 5 = the OG outfit)"},
    {"Skin tone", FK_SKINT, 0, 15, 1, 0, "Face and hands"},
    {"Head accessory", FK_HEAD, -1, 10, 1, -1, "Another character's hair, hat or glasses on top"},
    {"Jacket color", FK_OCOL, 0, 41, 1, 0, "Jacket, shirt, sleeves"},
    {"Pants color", FK_OCOL, 0, 41, 1, 0, "Pants, belts, boots"},
    {"Save to slot", FK_OSLOT, 0, 15, 1, 0, "Where Save and apply puts it: its own slot, or 1-15"},   // v5.69
    {"Outfit stock", FK_OSTOCK, 0, 50, 1, 0, "Saved outfits (up to 50); Apply puts one in the slot"},
    {"Outfit effect", FK_OFX, 0, 7, 1, 0, "Any skill's effect (L/R; hold to run), then the presets"},   // v5.75 / v5.101
    // v5.79 the effect editor (owner 2026-11-02); changes show at once in a match
    {"Effect placement", FK_OFXPL, 0, 1, 1, 0, "Whole body: around the outline. One spot: you move it"},
    {"Effect left / right", FK_NUM, -40, 40, 2, 0, "From the middle of the body: - left, + right"},
    {"Effect up / down", FK_NUM, -40, 40, 2, 0, "From the middle of the body: - down, + up"},
    {"Effect front / back", FK_NUM, -40, 40, 2, 0, "From the middle of the body: - behind, + in front"},
    {"Effect size", FK_OFXPCT, 25, 300, 1, 100, "Expand or shrink it: L/R 1 % (hold to run), RT held 25 %"},   // v5.101 1 % steps
    {"Effect speed", FK_OFXSPD, 0, 300, 1, 100, "Slower or faster (L/R 1 %, RT held 25 %); 0 = paused"},
    {"Effect shows", FK_OFXSHOW, 0, 2, 1, 0, "Always, at aura level 10, or growing with the aura"},   // v5.82
    {"Aura color", FK_OCOL, 0, 41, 1, 0, "This outfit's aura in battle: the school's, or a color"}};
// v5.101 (owner 2026-11-03; then "ALSO THE PREVIEW MODE IS MISSING?"): the entry is back, the preview shows the
// model alone (cwo_ccprev.inl cc_prev_draw, v5.101.2)
static const char *OUTFIT_A[] = {"Preview live (turning model)", "Save and apply to the slot", "Save to the outfit stock",
                                "Apply the stock outfit to the slot", "Add the theme outfits to the stock", "Take it off (the slot's original outfit)",
                                "Delete this one (A twice)", "Save", "Back"};
static const char *OUTFIT_H[] = {"CREATE AN OUTFIT - how to use", "1  Character and the outfit it starts from",
                                "2  Skin tone, a head accessory, jacket and pants colors", "3  Preview live: the model turns with its effect (B: back)",
                                "4  Save and apply: it goes into the Save to slot (this machine)",
                                "5  Outfit stock: keep up to 50 per character, apply any of them to a slot",
                                "6  Right stick: turn the model   Take it off: the slot's original outfit again",
                                "7  Effect rows: where it sits, its size and speed - shown at once in a match", NULL};
static EdCreator g_edc[EDP_N] = {
    {},
    {"CREATE A SCHOOL", "CwoSchools.txt", "School", 29, SCHOOL_F, SCHOOL_A, 5, SCHOOL_H},
    {"CREATE A CHARACTER", "CwoCharacters.txt", "Character", CH_NF, CHAR_F, CHAR_A, 7, CHAR_H},
    {"CREATE A QUEST", "CwoQuests.txt", "Quest", 31, QUEST_F, QUEST_A, 3, QUEST_H},
    {"CREATE A MODE", "CwoModes.txt", "Mode", 19, MODE_F, MODE_A, 6, MODE_H},
    {"SPECTATOR", "CwoSpectator.txt", "Setup", 2, SPECT_F, SPECT_A, 1, SPECT_H},
    {"CREATE AN OUTFIT", "CwoOutfits.txt", "Outfit", 17, OUTFIT_F, OUTFIT_A, 9, OUTFIT_H}};   // v5.93 + Aura color; v5.101.2 Preview live back

static volatile LONG g_ed_page, g_ed_row, g_ed_help, g_ed_kb;   // page, row on it, help card shown, keyboard open
// v5.35 (owner 2026-10-28): Create a Character opens on a list of every saved character (g_ed_cat); A plays as it now
static volatile LONG g_ed_cat, g_ed_cat_sel;
// v5.10 Create a Character preview (owner 2026-10-26; "figure out what can
// increase the load time when selecting the parts"): a change is marked with its time, the preview rebuilds 0.3 s after the
// last one and only when the look changed (cwo_ccprev.inl); leaving the preview goes back to the chooser
static volatile ULONGLONG g_cc_change_t;
static volatile LONG g_cc_back;
static void cc_mark_dirty() { g_cc_change_t = GetTickCount64(); InterlockedExchange(&g_cc_dirty, 1); }
// g_co_prev: cwo_loader.cpp (v5.69: the pad hook routes the outfit preview's input)
static void co_mark_dirty();
static void co_prev_request();
static bool co_wear(const int *v, char *msg, size_t n);
static bool co_unwear(const int *v, char *msg, size_t n);
static volatile LONG g_ed_mode_on = -1, g_ed_school_on = -1, g_ed_char_on = -1;   // what is in use (item index, -1 none)
static int g_ed_rows_sel[EDP_N];                               // remembered row per page
static bool g_ed_loaded;
static char *g_ed_test_gs;                                     // self-test: stand-in GSDATA
static unsigned char *g_ed_test_code;                          // self-test: stand-in code bytes (3 capsule bytes)

static const char *ED_SCHOOLS[] = {"Aura Particle", "Psycho", "Optical", "Nature", "Ki", "Faith"};   // v5.171 school 0 = the Aura Particle school (owner rule), never "none"
static const char *ED_ANIM[130] = {   // the game's animation name table 0x1402db400 (ANIMATION_SLOT_NAMES.md)
    "WAIT0", "WAIT1", "WAIT1IN", "WAIT1OUT", "WAIT_ACT0", "WAIT_CON", "WAIT_DAM", "WALK", "WALK_CON", "WALK_DAM",
    "WALK_FAST", "WALK_SLOW", "RUN", "RUN_CON", "RUN_DAM", "RUN_FAST", "RUN_SLOW", "TURN_WALK_R", "TURN_WALK_L",
    "TURN_WALK_BR", "TURN_WALK_BL", "TURN_RUN_R", "TURN_RUN_L", "TURN_RUN_BR", "TURN_RUN_BL", "TURN_WDAM_R",
    "TURN_WDAM_L", "TURN_WDAM_BR", "TURN_WDAM_BL", "TURN_RDAM_R", "TURN_RDAM_L", "TURN_RDAM_BR", "TURN_RDAM_BL",
    "DASH_A", "DASHEND_A", "DASH_B", "DASHEND_B", "JUMP_START", "JUMP_UP", "JUMP_DOWN", "JUMP_LAND0", "JUMP_LAND1",
    "FLOAT_IN", "FLOAT_WAIT", "FLOAT_MOVE", "BLOW0_FRONT", "BLOW1_FRONT", "BLOW2_FRONT", "BLOW0_BACK", "BLOW1_BACK",
    "BLOW_LAND_LF", "BLOW_LAND_MF", "BLOW_LAND_SF", "BLOW_LAND_LB", "BLOW_LAND_MB", "BLOW_LAND_SB", "WALLHIT_FRONT",
    "WALLHIT_FRONT_LOOP", "WALLHIT_BACK", "WALLHIT_BACK_LOOP", "DOWN_FRONT", "DOWN_FRONT_STAND", "DOWN_BACK",
    "DOWN_BACK_STAND", "ATK_SORD", "ATK_SHOT", "DEF_SHIELD", "ATK_WHAND_LASER", "ATK_SKYSHOT_LOW", "ATK_SKYSHOT_HIGH",
    "ATK_KI_SHOT", "ATK_WHAND_SORD", "DEF_BRUSH", "DEF_ABSORB", "ATK_STAMP", "DEF_SHELTER", "ATK_LASER", "SPE_DASH",
    "SPE_WARP", "ATK_SPHERE", "ATK_THROW", "ATK_SPIKE", "ATK_WHAND_SPIKE", "ATK_GROUND", "DEF_WHAND_SHIELD",
    "ATK_DRAIN", "SPE_POWER_UP", "DEF_COURSE", "ATK_SET_BOMB", "SPE_RESET", "ATK_TUKI", "ATK_PUNCH", "ATK_SUPER_SHOT",
    "SPE_HEAL", "ATK_KICK", "ATK_BODY", "ENV_SET", "ATK_FAITH_SHOT", "ATK_PUNCH_2", "ATK_PUNCH_3", "ATK_LASER_2",
    "ATK_LASER_3", "DMG_RAISE_HIGH", "DMG_DRAIN_HIGH", "DMG_SPIKE_HIGH", "DMG_BIND_AIR", "DMG_CONFUSION",
    "DMG_BIND_CROSS", "DMG_BIND_CHAIN", "DMG_BIND_PRESS", "DMG_AURA", "DMG_STAMP_FACEUP", "DMG_ABILITY",
    "DMG_STATUS_UP", "DMG_STATUS_DOWN", "DMG_STATUS_RESET", "DMG_PARARISE", "DMG_SHOT_FRONT", "DMG_SHOT_BACK",
    "DMG_SHOT_LEFT", "DMG_SHOT_RIGHT", "DMG_SPHERE_FRONT", "DMG_SPHERE_BACK", "DMG_SORD_FRONT", "DMG_SORD_BACK",
    "DMG_DRAIN_LOW", "DMG_SPIKE_LOW", "DMG_RAISE_LOW", "DMG_STAMP_FACEDOWN", "DMG_SHORT",};
// v3.6.1: 0-10 the players, 11-19 the Event-pack NPCs (build_character.py NPCS); Leader, Arthur, Reindeer, Ai (owner), Spokesman (same unsolved skeleton class) and Ubiquitous (owner) are not offered
static const char *ED_BODIES[] = {"Player (Nanashi)", "Edgar", "Freia", "Meister", "Chunky", "Cuff Button", "pH", "Know", "Tsubutaki", "JD", "Sammah",
                                  "NPC 03", "NPC Kajikawa", "NPC Tetsuya", "NPC Mac", "NPC Baroness", "NPC Mikan", "NPC Maniac", "NPC Kei",
                                  "NPC Spokesman"};
static const int ED_NBODIES = 20;
// v3.8 move sources: the 11 players' current moves, then their OG files (the original Xbox game's animations)
static const char *ED_MOVES[22] = {"Player (Nanashi)", "Edgar", "Freia", "Meister", "Chunky", "Cuff Button", "pH", "Know", "Tsubutaki", "JD", "Sammah",
                                   "OG Player", "OG Edgar", "OG Freia", "OG Meister", "OG Chunky", "OG Cuff Button", "OG pH", "OG Know",
                                   "OG Tsubutaki", "OG JD", "OG Sammah"};
// v3.8 the 28 battle voices the charaset names (build_slot_charaset.VOICE_ROWS order: rows 0-26, then 42)
static const char *ED_VOICES[28] = {"Nanashi", "Edgar", "Freia", "Meister", "Chunky", "Cuff Button", "pH", "Know", "Tsubutaki", "JD", "Sammah",
                                    "Anthro", "Andro", "Vestio", "Partheno", "Ommato", "Scoto", "Claustro", "Catoptro", "Mechano", "Gyne",
                                    "Euroto", "Hedono", "Guard", "Ceno", "Germano", "Belono", "Freia (Dement)"};
static const char *ED_CHARAS[] = {"Player", "Edgar", "Freia", "Meister", "Chunky", "Cuff Button", "pH", "Know", "Tsubutaki", "JD", "Sammah",
                                  "Anthro", "Andro", "Vestio", "Partheno"};
static const char *ED_PARTS[] = {"0 hand (the move)", "1 feet / ground (flight)", "2 extra piece", "3 the impact", "4 head (held)", "5 burst (release)"};
static const char *ED_BPARTS[] = {"the skill's own place", "ground", "hips", "chest", "head", "right hand", "left hand", "right foot", "left foot"};

static int ed_rows(int page) { const EdCreator &c = g_edc[page]; return 2 + c.nf + c.na; }   // item, name, fields, actions
static int ed_fld(int page, int row) {                          // v3.9 the field a row shows (School: SCHOOL_ORDER)
    int k = row - 2;
    if (page == EDP_SCHOOL && k >= 0 && k < (int)(sizeof(SCHOOL_ORDER) / sizeof(*SCHOOL_ORDER))) return SCHOOL_ORDER[k];
    if (page == EDP_CHAR && k >= 0 && k < (int)(sizeof(CHAR_ORDER) / sizeof(*CHAR_ORDER))) return CHAR_ORDER[k];
    if (page == EDP_QUEST && k >= 0 && k < (int)(sizeof(QUEST_ORDER) / sizeof(*QUEST_ORDER))) return QUEST_ORDER[k];
    return k;
}

static void ed_defaults(int page, EdItem &it, const char *name) {
    const EdCreator &c = g_edc[page];
    memset(&it, 0, sizeof(it));
    strncpy_s(it.name, sizeof(it.name), name, _TRUNCATE);
    for (int i = 0; i < c.nf; i++) it.v[i] = c.f[i].def;
}

static void ed_path(const char *file, wchar_t *out) { swprintf_s(out, MAX_PATH, L"%s%S", g_dir, file); }

static void ed_save(int page) {
    const EdCreator &c = g_edc[page];
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    ed_path(c.file, path);
    swprintf_s(tmp, MAX_PATH, L"%s.tmp", path);
    FILE *f = NULL;
    if (cwo_wfopen(&f, tmp, L"w") != 0 || !f) { logf("Edgar's Dream: cannot write %ls", tmp); set_status("Could not save (see the log)"); return; }
    fprintf(f, "# %s: name then %d values (%s", c.title, c.nf, c.nf ? c.f[0].label : "");
    for (int i = 1; i < c.nf; i++) fprintf(f, ", %s", c.f[i].label);
    fprintf(f, ")\n");
    for (int k = 0; k < c.n; k++) {
        fprintf(f, "%s", c.item[k].name);
        for (int i = 0; i < c.nf; i++) fprintf(f, "\t%d", c.item[k].v[i]);
        fprintf(f, "\n");
    }
    fclose(f);
    MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

static void ed_load(int page) {
    EdCreator &c = g_edc[page];
    c.n = 0; c.cur = 0;
    wchar_t path[MAX_PATH];
    ed_path(c.file, path);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") == 0 && f) {
        char line[2048];
        while (c.n < ed_cap(page) && fgets(line, sizeof(line), f)) {
            bool whole = strchr(line, '\n') != NULL || feof(f);
            if (!whole) { int ch; while ((ch = fgetc(f)) != EOF && ch != '\n') {} }   // v4.4: never a second piece
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
            EdItem it;
            ed_defaults(page, it, "");
            char *ctx = NULL, *tok = strtok_s(line, "\t\r\n", &ctx);
            if (!tok) continue;
            strncpy_s(it.name, sizeof(it.name), tok, _TRUNCATE);
            for (int i = 0; i < c.nf && (tok = strtok_s(NULL, "\t\r\n", &ctx)) != NULL; i++) {
                int v = atoi(tok);
                it.v[i] = v < c.f[i].lo ? c.f[i].lo : v > c.f[i].hi ? c.f[i].hi : v;   // a hand-edited file can't break it
            }
            if (page == EDP_CHAR) it.v[CH_BASE] = cb_female(it.v[CH_BODY]) ? 1 : 0;   // v5.10 the base follows the body
            c.item[c.n++] = it;
        }
        fclose(f);
    }
    if (c.n == 0) {                                            // first use: one ready-made item (Mode: the presets)
        if (page == EDP_MODE) {
            static const struct { const char *n; int t, l, lives, caps, fmt; } P[] = {
                {"Standard", 10, 20, 1, 3, 0}, {"3 vs 3", 15, 20, 3, 3, 7}, {"Long fight", 15, 40, 1, 3, 0}, {"Quick KO", 3, 5, 1, 3, 0}};
            for (int k = 0; k < 4; k++) {
                ed_defaults(page, c.item[k], P[k].n);
                c.item[k].v[0] = P[k].t; c.item[k].v[1] = P[k].l; c.item[k].v[2] = P[k].lives; c.item[k].v[3] = P[k].caps;
                c.item[k].v[5] = P[k].fmt;                         // v5.1 Players: 7 = 3 vs 3 (cwo_sixf2.inl)
            }
            c.n = 4;
        } else {
            char nm[24];
            sprintf_s(nm, "My %s 1", c.word);
            ed_defaults(page, c.item[0], nm);
            c.n = 1;
        }
    }
}

static bool ed_kb_pages() { return g_ed_open && g_forge_session && !g_forge_active && !g_in_match; }   // v5.188.36 (cwo_dreamprobe.inl)
// v3.5.1: the chooser takes no button that was already down when it opened (the main menu's A chose Create a Skill)
static bool ed_release_gate(WORD b) {
    if (!g_ed_wait_release) return false;
    if (b & (BTN_A | BTN_B | BTN_X | BTN_Y | BTN_START | BTN_LB | BTN_RB | BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) return true;
    InterlockedExchange(&g_ed_wait_release, 0);
    return true;                                               // this frame: nothing pressed anyway
}
static void ed_close() {                                       // the chooser hands the controller back to the game's list
    InterlockedExchange(&g_ed_from_opt, 0);                     // v5.188.2
    InterlockedExchange(&g_ed_game_hold, 1);
    InterlockedExchange(&g_ed_open, 0);
}

// remembered place and what is in use (owner-friendliness 2: Edgar's Dream opens where you left it)
static void ed_state_save() {
    wchar_t path[MAX_PATH];
    ed_path("CwoEdgar.txt", path);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"w") != 0 || !f) return;
    fprintf(f, "sel %ld\nmode %ld\nschool %ld\ncharacter %ld\n", g_ed_sel, g_ed_mode_on, g_ed_school_on, g_ed_char_on);
    for (int p = 1; p < EDP_N; p++) fprintf(f, "page%d %d %d\n", p, g_ed_rows_sel[p], g_edc[p].cur);
    fclose(f);
}

static void ed_state_load() {
    wchar_t path[MAX_PATH];
    ed_path("CwoEdgar.txt", path);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return;
    char key[16];
    int a = 0, b = 0;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int got = sscanf_s(line, "%15s %d %d", key, (unsigned)sizeof(key), &a, &b);
        if (got < 2) continue;
        if (!strcmp(key, "sel")) InterlockedExchange(&g_ed_sel, a >= 0 && a < ED_N ? a : 0);
        else if (!strcmp(key, "mode")) InterlockedExchange(&g_ed_mode_on, a >= -1 && a < g_edc[EDP_MODE].n ? a : -1);
        else if (!strcmp(key, "school")) InterlockedExchange(&g_ed_school_on, a >= -1 && a < g_edc[EDP_SCHOOL].n ? a : -1);
        else if (!strcmp(key, "character")) InterlockedExchange(&g_ed_char_on, a >= -1 && a < g_edc[EDP_CHAR].n ? a : -1);
        else if (!strncmp(key, "page", 4) && got == 3) {
            int p = atoi(key + 4);
            if (p >= 1 && p < EDP_N) {
                g_ed_rows_sel[p] = a >= 0 && a < ed_rows(p) ? a : 0;
                g_edc[p].cur = b >= 0 && b < g_edc[p].n ? b : 0;
            }
        }
    }
    fclose(f);
}

static volatile LONG g_ed_loading;
static void ed_load_all() {
    if (g_ed_loaded || InterlockedExchange(&g_ed_loading, 1)) return;
    for (int p = 1; p < EDP_N; p++) ed_load(p);
    ed_state_load();
    g_ed_loaded = true;                                        // a mode left on is written again by ed_step (ed_mode_keep)
}

// ---------------------------------------------------------------- campaign quest titles (read from the install, display only)
static char g_ed_qtitle[256][28];
static bool g_ed_qread;
static void ed_read_quests() {
    if (g_ed_qread) return;
    g_ed_qread = true;
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) slash[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\questdata\\en\\questdata.qdt", exe);   // 256 records x 0x520 (Batch 023)
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"rb") != 0 || !f) return;
    unsigned char rec[0x520];
    for (int q = 0; q < 256 && fread(rec, 1, sizeof(rec), f) == sizeof(rec); q++) {
        int n = 0;
        for (int i = 4; i < 0x36 && rec[i] && n < 27; i++) if (rec[i] >= 0x20 && rec[i] < 0x7F) g_ed_qtitle[q][n++] = (char)rec[i];
        g_ed_qtitle[q][n] = 0;
    }
    fclose(f);
}

// ---------------------------------------------------------------- value text
static const char *CO_SKINS_T[8] = {"the character's own", "Fair", "Light", "Medium", "Olive", "Tan", "Brown", "Deep"};   // v5.61 (cwo_outfit.inl)
static const char *CO_COLOURS_T[16] = {"the outfit's own", "Black", "White", "Gray", "Red", "Crimson", "Orange", "Yellow", "Gold",
                                       "Green", "Teal", "Blue", "Navy", "Purple", "Pink", "Brown"};
static const char *co_stock_label(int ch, int idx);               // v5.69 cwo_outfit.inl
static const char *ofx_name(int i);                                // v5.75 cwo_outfitfx.inl
static int ofx_next(int v, int dir);                               // v5.101 cwo_outfitfx.inl: presets + the deck's skills
static volatile LONG g_ofx_dirty;                                  // v5.79 an effect row changed (cwo_outfitfx.inl ofx_live)
static bool co_stock_save(const EdItem &it, char *msg, size_t n);
static bool co_stock_apply(const int *v, char *msg, size_t n);
static void co_themes_to_stock(const int *v, char *msg, size_t n);
static void co_prev_request();
static void ed_value(int page, int fi, int v, char *out, size_t n) {
    const EdField &fd = g_edc[page].f[fi];
    switch (fd.kind) {
    case FK_MIN: sprintf_s(out, n, "%d min", v); break;
    case FK_MINNL: if (v <= 0) sprintf_s(out, n, fi == 0 ? "no limit" : "never"); else sprintf_s(out, n, "%d min", v); break;
    case FK_RULE: sprintf_s(out, n, "%s", ED_RULES[v >= 0 && v < 12 ? v : 0]); break;
    case FK_FMT: sprintf_s(out, n, "%s%s", ED_FMTS[v >= 0 && v < 14 ? v : 0], v == 0 || v >= 7 ? "" : " - coming"); break;
    case FK_ENVMODE: sprintf_s(out, n, "%s", v == 1 ? "the game's random ones" : v == 2 ? "the ones below" : "none"); break;
    case FK_ENVSK: if (v < 0) sprintf_s(out, n, "none"); else sprintf_s(out, n, "%.30s", sc_skill_name(v)); break;
    case FK_SEC: sprintf_s(out, n, "%d s", v); break;
    case FK_SCHOOL: sprintf_s(out, n, "%s", ED_SCHOOLS[v >= 0 && v <= 5 ? v : 0]); break;
    case FK_HUE: if (v < 0) sprintf_s(out, n, "game color"); else sprintf_s(out, n, "hue %d", v); break;
    case FK_PCT: sprintf_s(out, n, "%d%%", v); break;
    case FK_SKILL: if (v < 0) sprintf_s(out, n, "none"); else sprintf_s(out, n, "%d %.26s", v, sc_skill_name(v)); break;
    case FK_PART: sprintf_s(out, n, "%s", ED_PARTS[v >= 0 && v <= 5 ? v : 0]); break;
    case FK_ONOFF: sprintf_s(out, n, "%s", v ? "on" : "off"); break;
    case FK_SHOW: sprintf_s(out, n, "%s", v ? "always" : "with the aura"); break;
    case FK_BODY: sprintf_s(out, n, "%s", ED_BODIES[v >= 0 && v < ED_NBODIES ? v : 0]); break;
    case FK_ANYSCHOOL:
        if (v <= 5) sprintf_s(out, n, "%s", ED_SCHOOLS[v < 0 ? 0 : v]);
        else if (v - 6 < g_edc[EDP_SCHOOL].n) sprintf_s(out, n, "made: %.20s", g_edc[EDP_SCHOOL].item[v - 6].name);
        else sprintf_s(out, n, "made #%d (none yet)", v - 5);
        break;
    case FK_QUEST: ed_read_quests(); sprintf_s(out, n, "%d %.26s", v, g_ed_qtitle[v & 255][0] ? g_ed_qtitle[v & 255] : "(unused)"); break;
    case FK_STAGE: sprintf_s(out, n, "%d %.26s", v, f_stage_title(v)); break;
    case FK_PLAYAS: if (v < 0) { sprintf_s(out, n, "yourself"); break; }   // falls through: a character
    case FK_CHARAN: if (v < 0) { sprintf_s(out, n, "none"); break; }    // falls through: a character
    case FK_CHARA:                                             // v5.13 rows 15-26: the charaset's own names (ED_VOICES)
        if (v >= 0 && v < (int)(sizeof(ED_CHARAS) / sizeof(*ED_CHARAS))) sprintf_s(out, n, "%s", ED_CHARAS[v]);
        else if (v >= 0 && v <= 26) sprintf_s(out, n, "%s (row %d)", ED_VOICES[v], v);
        else sprintf_s(out, n, "character row %d", v);
        break;
    case FK_PIC: if (v < 0) sprintf_s(out, n, "keep the quest's"); else sprintf_s(out, n, "picture %d", v); break;
    case FK_QLEVEL: sprintf_s(out, n, "%s", ED_QLEVELS[v >= 0 && v < 5 ? v : 0]); break;
    case FK_REINF: sprintf_s(out, n, "%s", ED_REINF[v >= 0 && v < 3 ? v : 0]); break;
    case FK_PARTNER: sprintf_s(out, n, "%s", ED_PARTNERS[v >= 0 && v < 13 ? v : 0]); break;
    case FK_QRULES:
        if (v < 0) { sprintf_s(out, n, "its own"); break; }
        ed_read_quests(); sprintf_s(out, n, "%d %.24s", v, g_ed_qtitle[v & 255][0] ? g_ed_qtitle[v & 255] : "(unused)"); break;
    case FK_RANK: sprintf_s(out, n, "%s", ED_RANKS[v >= 0 && v < 5 ? v : 0]); break;
    case FK_OUTFIT: sprintf_s(out, n, "outfit %d", v + 1); break;
    case FK_KEEP: if (v < 0) sprintf_s(out, n, "keep the quest's"); else sprintf_s(out, n, "%d", v); break;
    case FK_CREDITS: if (v <= 0) sprintf_s(out, n, "the quest's own rewards"); else sprintf_s(out, n, "%d credits", v); break;
    case FK_CAPS: sprintf_s(out, n, "%d each", v); break;
    case FK_BRAIN: sprintf_s(out, n, "%s", ED_BRAINS[v >= 0 && v <= 13 ? v : 0]); break;
    case FK_HEAD: if (v < 0) sprintf_s(out, n, page == EDP_OUTFIT ? "none" : "the body's own"); else sprintf_s(out, n, "%s", ED_BODIES[v < ED_NBODIES ? v : 0]); break;
    case FK_OCHAR: sprintf_s(out, n, "%s", ED_CHARAS[v >= 0 && v <= 10 ? v : 0]); break;                       // v5.61
    case FK_SKINT: if (v <= 0) sprintf_s(out, n, "the character's own"); else sprintf_s(out, n, "%s (%d of 15)", CO_SKINS_T[1 + (v - 1) * 6 / 14], v); break;
    case FK_OCOL: {                                             // v5.70 the slider: greys, then 36 hues
        static const char *GREY[5] = {"Black", "Dark gray", "Gray", "Light gray", "White"};
        static const char *HUE[12] = {"Red", "Orange", "Gold", "Yellow", "Lime", "Green", "Teal", "Cyan", "Sky blue", "Blue", "Purple", "Pink"};
        if (v <= 0) sprintf_s(out, n, "the outfit's own");
        else if (v <= 5) sprintf_s(out, n, "%s", GREY[v - 1]);
        else sprintf_s(out, n, "%s (hue %d)", HUE[((v - 6) * 10 + 15) / 30 % 12], (v - 6) * 10);
        break;
    }
    case FK_OFX: sprintf_s(out, n, "%s", ofx_name(v)); break;
    case FK_OFXPL: sprintf_s(out, n, "%s", v ? "One spot" : "Whole body (outline)"); break;
    case FK_OFXPCT: sprintf_s(out, n, "%d%%", v); break;
    case FK_OFXSHOW: sprintf_s(out, n, "%s", v == 1 ? "At aura level 10" : v == 2 ? "Grows with the aura" : "Always"); break;
    case FK_OFXSPD: if (v <= 0) sprintf_s(out, n, "Paused (holds still)"); else sprintf_s(out, n, "%d%%", v); break;
    case FK_OSLOT: if (v <= 0) sprintf_s(out, n, "the slot it started from"); else sprintf_s(out, n, "slot %d%s", v, v == 5 ? " (the OG slot)" : ""); break;
    case FK_OSTOCK: {
        const EdItem &it = g_edc[EDP_OUTFIT].item[g_edc[EDP_OUTFIT].cur];
        sprintf_s(out, n, "%s", v <= 0 ? "none chosen" : co_stock_label(it.v[0], v));
        break;
    }
    case FK_SIZE: sprintf_s(out, n, "%s", v ? "fitted to the moves" : "the body's own"); break;
    case FK_FROM: if (v < 0) sprintf_s(out, n, "base moves"); else sprintf_s(out, n, "%s", ED_MOVES[v <= 21 ? v : 0]); break;
    case FK_MOVES: sprintf_s(out, n, "%s", ED_MOVES[v >= 0 && v <= 21 ? v : 0]); break;
    case FK_VOICE: if (v < 0) sprintf_s(out, n, "the moves' voice"); else sprintf_s(out, n, "%s", ED_VOICES[v <= 27 ? v : 0]); break;
    case FK_EVADE: sprintf_s(out, n, "%s", v == 1 ? "Jump" : v == 2 ? "Dash (cartwheel / roll)" : "the moves' own"); break;
    case FK_THEME: sprintf_s(out, n, "%s", TH_NAMES[v >= 0 && v <= 10 ? v : 0]); break;
    case FK_BPART: sprintf_s(out, n, "%s", ED_BPARTS[v >= 0 && v <= 8 ? v : 0]); break;
    case FK_FACE: sprintf_s(out, n, "%s", v ? "the body's face + name" : "the dark '?' face"); break;
    case FK_LAYER: sprintf_s(out, n, "%s", v == 1 ? "layer over mine" : v == 2 ? "layer, keep my arms" : "replace mine"); break;
    case FK_DEG: sprintf_s(out, n, "%d deg", v); break;
    case FK_PIECES: sprintf_s(out, n, "%s", v ? "custom (below)" : "the part's own"); break;
    case FK_PIECE: if (v < 0) sprintf_s(out, n, "none"); else sprintf_s(out, n, "piece %d", v); break;
    case FK_SLOT: sprintf_s(out, n, "%d %s", v, ED_ANIM[v >= 0 && v < 130 ? v : 0]); break;
    case FK_WEAR: if (v <= 0) sprintf_s(out, n, "no (own slot only)"); else sprintf_s(out, n, "your outfit %d too", v); break;
    case FK_GENDER: sprintf_s(out, n, "%s", v ? "Female (Freia, Tsubutaki, Mikan)" : "Male"); break;
    default: sprintf_s(out, n, "%d", v); break;
    }
}

// v3.8: the builder matches skeletons strictly, then by shape (OG files, Freia, Tsubutaki all fit almost every body); the
// census of what still does not fit (build_character.py, 2026-10-25): Tsubutaki's current moves on Player, Edgar, Freia,
// Know, JD and Sammah. Heads go on any body (head_swap_any).
static bool ed_move_fits(int src, int body) {
    if (src == 8 && body <= 10) return !(body == 0 || body == 1 || body == 2 || body == 7 || body == 9 || body == 10);
    return true;
}
static bool ed_char_fit(const EdItem &it, char *why, size_t n) {          // a choice the builder would refuse, in words
    int body = it.v[CH_BODY];
    why[0] = 0;
    auto check = [&](int ch, const char *what) {
        if (ch >= 0 && !cb_moves_allowed(body, ch) && !why[0])     // v5.10 owner HARD RULE
            sprintf_s(why, n, "Not allowed: %s from %s on %s (male / female rule)", what, ED_MOVES[ch], ED_BODIES[body]);
        if (ch >= 0 && !ed_move_fits(ch, body) && !why[0])
            sprintf_s(why, n, "Does not fit: %s from %s on %s's body", what, ED_MOVES[ch], ED_BODIES[body]);
    };
    check(it.v[CH_MOVES], "the moves");
    for (int g = 0; g < CH_GN; g++) check(it.v[CH_G0 + g], CHAR_F[CH_G0 + g].label);
    for (int k = 0; k < 3; k++) check(it.v[CH_S0 + 2 * k + 1], ED_ANIM[it.v[CH_S0 + 2 * k] & 127]);
    return !why[0];
}

// ---------------------------------------------------------------- on-screen keyboard (owner-friendliness 1)
static const char ED_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -'!.+";
static const int ED_KCOLS = 11;
static volatile LONG g_ed_kx, g_ed_ky, g_ed_klower;
static char g_ed_kbuf[24];
static void ed_kb_open(const char *start) {
    strncpy_s(g_ed_kbuf, sizeof(g_ed_kbuf), start, _TRUNCATE);
    InterlockedExchange(&g_ed_kx, 0); InterlockedExchange(&g_ed_ky, 0);
    InterlockedExchange(&g_ed_kb, 1);
}
static void ed_kb_input(WORD pressed) {
    int nkeys = (int)strlen(ED_KEYS), rows = (nkeys + ED_KCOLS - 1) / ED_KCOLS;
    if (pressed & BTN_RIGHT) InterlockedExchange(&g_ed_kx, (g_ed_kx + 1) % ED_KCOLS);
    if (pressed & BTN_LEFT) InterlockedExchange(&g_ed_kx, (g_ed_kx + ED_KCOLS - 1) % ED_KCOLS);
    if (pressed & BTN_DOWN) InterlockedExchange(&g_ed_ky, (g_ed_ky + 1) % rows);
    if (pressed & BTN_UP) InterlockedExchange(&g_ed_ky, (g_ed_ky + rows - 1) % rows);
    if (pressed & BTN_Y) InterlockedExchange(&g_ed_klower, !g_ed_klower);
    size_t len = strlen(g_ed_kbuf);
    if ((pressed & BTN_X) && len) g_ed_kbuf[len - 1] = 0;
    if (pressed & BTN_A) {
        int k = g_ed_ky * ED_KCOLS + g_ed_kx;
        if (k < nkeys && len + 1 < sizeof(g_ed_kbuf)) {
            char ch = ED_KEYS[k];
            if (g_ed_klower && ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
            g_ed_kbuf[len] = ch; g_ed_kbuf[len + 1] = 0;
        }
    }
    if (pressed & (BTN_B | BTN_START)) {                       // done: the name is kept (empty: unchanged)
        EdCreator &c = g_edc[g_ed_page];
        if (g_ed_kbuf[0]) { strncpy_s(c.item[c.cur].name, sizeof(c.item[c.cur].name), g_ed_kbuf, _TRUNCATE); ed_save(g_ed_page); }
        InterlockedExchange(&g_ed_kb, 0);
    }
}

// ---------------------------------------------------------------- mode: rule table, starting capsules (game memory)
static int g_ed_rule_saved[RULE_N][4];
static bool g_ed_rules_written;
static unsigned char g_ed_caps_saved[3];
static bool g_ed_caps_written;
static int g_ed_armed_vals[4];                                 // the values written (to show and to re-check)

static bool ed_online() {
    if (g_ed_test_gs) return false;
    __try { int m = *(int *)(g_base + RVA_NETMODE); return m == 4 || m == 5; } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}

static unsigned char *ed_caps_byte(int k) {
    if (g_ed_test_code) return g_ed_test_code + k;
    static const ULONG_PTR at[3] = {RVA_CAPS_LOOP, RVA_CAPS_C1, RVA_CAPS_C2};
    return (unsigned char *)(g_base + at[k]);
}

static bool ed_caps_write(int caps) {                          // 3 = the game's own bytes
    unsigned char want = (unsigned char)caps;
    if (!g_ed_caps_written) {                                  // all three must be the game's own before any is written
        for (int k = 0; k < 3; k++)
            if (*ed_caps_byte(k) != 3) { logf("Edgar's Dream: capsule byte %d is %u, not the game's 3 - left alone", k, *ed_caps_byte(k)); return false; }
        if (want == 3) return true;
    }
    for (int k = 0; k < 3; k++) {
        unsigned char *p = ed_caps_byte(k);
        unsigned char now = *p;
        DWORD old = 0;
        if (!g_ed_test_code && !VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
        if (!g_ed_caps_written) g_ed_caps_saved[k] = now;
        *p = want;
        if (!g_ed_test_code) { VirtualProtect(p, 1, old, &old); FlushInstructionCache(GetCurrentProcess(), p, 1); }
    }
    g_ed_caps_written = want != 3;
    logf("Edgar's Dream: starting capsules -> %d (code bytes 0x14014f2f0 / f018 / f024)", caps);
    return true;
}

// v4.8 environments: Special Environment's list (GSDATA +0x33110 seconds, +0x33114 ids) kept and restored like the rules
static int g_ed_env_saved[33];
static bool g_ed_env_written;
static void ed_env_apply(char *gs, const EdItem *m) {
    int *t = (int *)(gs + 0x33110);
    if (m && m->v[6] == 2) {
        if (!g_ed_env_written) { memcpy(g_ed_env_saved, t, sizeof(g_ed_env_saved)); g_ed_env_written = true; }
        int k = 0;
        for (int i = 7; i <= 10; i++) if (m->v[i] > 0) t[1 + k++] = m->v[i];
        for (int i = k; i < 32; i++) t[1 + i] = 0;
        if (k) t[0] = m->v[11];
        else memcpy(t, g_ed_env_saved, sizeof(g_ed_env_saved));
    } else if (g_ed_env_written) {
        memcpy(t, g_ed_env_saved, sizeof(g_ed_env_saved));
        g_ed_env_written = false;
    }
}
// v5.1 the mode in use asks for 6 fighters (Players = 3 vs 3) - cwo_sixf2.inl adds fighters 5-6 at the match start
static bool sixf_mode_wants_six() {
    LONG k = g_ed_mode_on;
    return k >= 0 && k < g_edc[EDP_MODE].n && g_edc[EDP_MODE].item[k].v[5] == 7 && !ed_online() && g_sixf2_state == 1;
}
static int ed_mode_rule(const EdItem &m) {                     // environments need rule 5
    int r = m.v[6] ? 5 : (m.v[4] >= 0 && m.v[4] < 12 ? m.v[4] : 0);
    if (m.v[5] == 7 && r == 10) r = 0;                         // v5.1 3 vs 3: KO Battle's results (FUN_14012cb00) count 4 fighters
    return r;
}

static bool ed_rules_apply(const EdItem *m) {                  // m NULL: the game's own table back
    char *gs = g_ed_test_gs ? g_ed_test_gs : *(char **)(g_base + RVA_GSDATA);
    if (!gs) return false;
    int *t = (int *)(gs + RULE_TABLE);
    __try {
        if (m) {
            if (!g_ed_rules_written) { memcpy(g_ed_rule_saved, t, sizeof(g_ed_rule_saved)); g_ed_rules_written = true; }
            for (int r = 0; r < RULE_N; r++) {
                t[4 * r + 0] = m->v[1];                        // starting life
                if (t[4 * r + 2] < m->v[1]) t[4 * r + 2] = m->v[1];   // the clamp's top lets it through
                t[4 * r + 3] = m->v[0] > 0 ? m->v[0] : 99;     // minutes (no limit: 99, the clock is held)
            }
            ed_env_apply(gs, m);
            memcpy(g_ed_armed_vals, m->v, sizeof(g_ed_armed_vals));
        } else if (g_ed_rules_written) {
            memcpy(t, g_ed_rule_saved, sizeof(g_ed_rule_saved));
            g_ed_rules_written = false;
        }
        if (!m) ed_env_apply(gs, NULL);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// v4.8 what a mode does, in three lines (the Mode page and the lobby's Game Rule list)
static void ed_mode_desc(const EdItem &m, char *l1, char *l2, char *l3, size_t n) {
    int rule = ed_mode_rule(m);
    char t[24];
    if (m.v[0] > 0) sprintf_s(t, "%d min", m.v[0]); else strcpy_s(t, "no time limit");
    sprintf_s(l1, n, "%s on %s: %s, life %d, %d %s, %d starting capsules", ED_FMTS[m.v[5] >= 0 && m.v[5] < 14 ? m.v[5] : 0],
              ED_RULES[rule], t, m.v[1], m.v[2], m.v[2] == 1 ? "life" : "lives", m.v[3]);
    if (m.v[6] == 0) sprintf_s(l2, n, "Environments: none");
    else if (m.v[6] == 1) sprintf_s(l2, n, "Environments: the game's own, at random");
    else {
        char names[160] = "";
        int k = 0;
        for (int i = 7; i <= 10; i++) if (m.v[i] > 0) { sprintf_s(names + strlen(names), sizeof(names) - strlen(names), "%s%.18s", k++ ? ", " : "", sc_skill_name(m.v[i])); }
        if (k) sprintf_s(l2, n, "Environments: %s, a new one every %d s", names, m.v[11]);
        else sprintf_s(l2, n, "Environments: none picked yet (the game's own are used)");
    }
    char bans[160] = "";
    int b = 0;
    for (int i = 12; i <= 16; i++) if (m.v[i] > 0) { sprintf_s(bans + strlen(bans), sizeof(bans) - strlen(bans), "%s%.16s", b++ ? ", " : "", sc_skill_name(m.v[i])); }
    sprintf_s(l3, n, "Banned: %s", b ? bans : "none");   // v5.188.7 the refill row is not built: not mentioned
}

static void ed_mode_arm(int k) {                               // k = item index, -1 off
    if (ed_online()) { set_status("Online: modes stay off"); return; }
    const EdItem *m = k >= 0 && k < g_edc[EDP_MODE].n ? &g_edc[EDP_MODE].item[k] : NULL;
    bool ok = ed_rules_apply(m);
    bool caps = ed_caps_write(3);                               // v5.188.3 (owner 2026-11-15 - always 3)
    InterlockedExchange(&g_ed_mode_on, m ? k : -1);
    char msg[96];
    if (m) sprintf_s(msg, "Mode on: %.24s (%d min, life %d, %d lives, %d capsules)%s", m->name, m->v[0], m->v[1], m->v[2], m->v[3],
                     ok && caps ? "" : " - partly");
    else sprintf_s(msg, "Mode off: the game's own rules");
    set_status(msg);
    logf("Edgar's Dream: %s", msg);
    ed_state_save();
}

// ---------------------------------------------------------------- lives (game thread, every flush)
static int g_ed_deaths[8], g_ed_cap[8], g_ed_down[8];        // v5.1 up to 8 fighters
static PFN_REVIVE g_ed_revive_test;
static LONG g_ed_was_match;
static ULONG_PTR ed_player(int p) {
    if (g_ed_test_gs) return ((ULONG_PTR *)g_actor_test_tab)[p];
    return cwo_actors()[p];
}

// ---------------------------------------------------------------- v5.22 Survival (owner 2026-10-27). Create a Mode > Players = "Survival": in any
// offline match while the mode is on (Single Play, split screen, the 3 VS 3 LOBBY) the CPUs of the other side never stay
// down: each one comes back at full life (the game's own revive, as the lives above). When every one of them has been
// knocked down once, the next wave starts: the CPUs come back with more life (+4 a wave, 60 at most) and a sharper
// personality (wave 2 the Tough row, 3 Brutal, 4 on Brutal - weakest first: com\param\enemyparam.epa rows 82-84, written
// into each CPU brain's slots 30-38 as FUN_14017b020 does at the start). You and your side keep the mode's lives; the
// match ends the game's own way when your side is out (Records keeps the best wave).
// Game facts (Ghidra batch_065, pcode.py): FUN_14017b020(brain = fighter + 0x14408, ..., CPU record) copies bytes 1-9 of
// enemyparam row CPU record +0x18 (table *0x1404c0188, row r at +4 + 10 r) into brain +0x20ec .. +0x210c (4 bytes each);
// the program reads them and never writes them. Fighter +0x1c = team, +0x30 = driver (0 = the CPU brain).
static bool cpu_level_ready(int rec);                           // cwo_lobby.inl
static int g_sv_wave = 1, g_sv_kos, g_sv_enemies;
static const int SV_ROW[4] = {-1, 82, 83, 84};                  // wave 1 the game's own; 2 Tough; 3 Brutal; 4+ Brutal - weakest first
static const signed char *g_sv_t_tab;                           // self-test: a stand-in enemyparam table
static bool sv_mode(LONG k) { return k >= 0 && k < g_edc[EDP_MODE].n && g_edc[EDP_MODE].item[k].v[5] == 8; }
static void sv_strength(ULONG_PTR r) {                          // the wave's personality into one CPU's brain
    int row = SV_ROW[g_sv_wave - 1 < 3 ? g_sv_wave - 1 : 3];
    if (row < 0) return;
    __try {
        const signed char *t = g_sv_t_tab ? g_sv_t_tab : *(const signed char **)(g_base + 0x4c0188);
        if (!t || (!g_sv_t_tab && !cpu_level_ready(row))) return;
        int *slot = (int *)(r + 0x14408 + 0x20ec);
        for (int j = 0; j < 9; j++) slot[j] = t[4 + 10 * row + 1 + j];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void sv_ko(int nf, int myteam) {                         // an enemy CPU went down
    g_sv_kos++;
    if (g_sv_kos < (g_sv_enemies > 0 ? g_sv_enemies : 1)) return;
    g_sv_wave++;
    g_sv_kos = 0;
    for (int p = 0; p < nf; p++) {
        ULONG_PTR r = ed_player(p);
        __try { if (r && *(int *)(r + 0x20) && *(int *)(r + 0x30) == 0 && *(int *)(r + 0x1c) != myteam) sv_strength(r); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    char msg[80];
    sprintf_s(msg, "SURVIVAL: WAVE %d - the CPUs come back tougher", g_sv_wave);
    set_status(msg);
    logf("survival: wave %d (%d enemies)", g_sv_wave, g_sv_enemies);
}

// ---------------------------------------------------------------- v5.24 Shrinking arena / King of the Hill (owner 2026-10-27,
// idea 5 of the 8: "Shrinking arena / King of the Hill - a ring that closes in; or a zone that scores while you stand in
// it"). Create a Mode > Players = "Shrinking arena" or "King of the Hill", then any offline match while the mode is on.
//  - Shrinking arena: a ring of glowing markers around the fighters' starting area closes in to 4 m over 80 % of the
//    mode's time (3 minutes when it has no limit); outside it a fighter loses 1 life a second; at 0 they fall out.
//  - King of the Hill: a glowing zone (5 m) in the middle; each second exactly one side stands in it, that side scores;
//    60 points wins: the other side falls out (no lives left).
// Game facts (Ghidra batch_065, pcode.py; Stage Creator kill zones, cwo_forge.inl): fighter +0x4C life (< 1 = out),
// +0x50 its cap, +0x12E40 position, +0x1c team, +0x24 standing; FUN_140251d80(fighter, 0) is the game's own fall-off end
// (the fighter to its start point; with life < 1 the knock-out state 0x3c / 0x12). Time runs only while the world runs
// (bit 0 of *0x140530034 clear: photo mode's pause holds the ring too). The markers are a skill's looping effect (the
// Stage Creator's kill-zone look, Paralyze Barrier; the zone: Photon), visual only.
typedef void(__fastcall *PFN_FALLOUT)(ULONG_PTR, int);
static const ULONG_PTR RVA_FALLOUT = 0x251d80;
static const int AR_RING = 9, AR_KOTH = 10, AR_FX = 12, AR_WIN = 60;
static int g_ar_kind;                                           // 0 off, AR_RING, AR_KOTH (this match)
static float g_ar_c[3], g_ar_r0, g_ar_r, g_ar_dur, g_ar_t, g_ar_sec;
static int g_ar_score[8], g_ar_lead = -1;
static bool g_ar_done;
static unsigned g_ar_fx[AR_FX];
static PFN_FALLOUT g_ar_fall_test;                              // self-test stand-in
static LARGE_INTEGER g_ar_last;

static void ar_fx_stop() {
    if (g_ed_test_gs) return;
    FxGame g;
    if (fx_game(&g)) for (int i = 0; i < AR_FX; i++) if (fx_alive(g, g_ar_fx[i])) g.stop(g_ar_fx[i]);
    for (int i = 0; i < AR_FX; i++) g_ar_fx[i] = FX_NONE;
}
static void ar_fx_step(float r) {                               // the markers on the circle of radius r
    if (g_ed_test_gs) return;
    FxGame g;
    if (!fx_game(&g)) return;
    ULONG_PTR p0 = ed_player(0);
    if (!p0) return;
    LONG id = g_ar_kind == AR_KOTH ? 122 : 120;
    // v5.188.1 (owner 2026-11-15): every marker is a looping skill effect
    // with its own looping sound - the zone draws 4 markers (one each side) instead of 12; the shrinking ring keeps 12
    int nfx = g_ar_kind == AR_KOTH ? 4 : AR_FX;
    for (int i = 0; i < nfx; i++) {
        __declspec(align(16)) float m[16];
        memcpy(m, g.identity, sizeof(m));
        float a = 6.2831853f * i / nfx;
        m[12] = g_ar_c[0] + r * sinf(a); m[13] = g_ar_c[1] + 5.0f; m[14] = g_ar_c[2] + r * cosf(a);
        if (fx_alive(g, g_ar_fx[i])) { g.move(g_ar_fx[i], m); continue; }
        short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)id * 0x90);
        int req = g.effect_of(prof, id);
        if (req <= 0) continue;
        unsigned h = g.start(g.mgr, *(int *)(p0 + 0x14), req, 0, m, 0);
        if (h != FX_NONE) { g.loop(h); g_ar_fx[i] = h; }
    }
}
static int ar_local_index() {                                    // v5.173 this console's fighter online (*0x1404bbe00), -1 unknown
    __try { int i = *(int *)(g_base + 0x4bbe00); return i >= 0 && i < 8 ? i : -1; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void ar_fall(ULONG_PTR f) {                              // life 0, then the game's own fall-off end
    *(int *)(f + 0x4C) = 0;
    PFN_FALLOUT fall = g_ar_fall_test ? g_ar_fall_test : (PFN_FALLOUT)(g_base + RVA_FALLOUT);
    fall(f, 0);
}
static int g_ar_net_kind;                                       // v5.95 the lobby rule (no Create a Mode item): AR_RING / AR_KOTH
static void ar_begin(LONG k, int nf) {                          // the first frame of a match with the mode on
    static EdItem net_m;
    if (k < 0) { memset(&net_m, 0, sizeof(net_m)); net_m.v[5] = g_ar_net_kind; }
    const EdItem &m = k < 0 ? net_m : g_edc[EDP_MODE].item[k];
    g_ar_kind = m.v[5] == AR_RING || m.v[5] == AR_KOTH ? m.v[5] : 0;
    if (!g_ar_kind) return;
    float sum[3] = {0, 0, 0}, pts[8][3];
    int n = 0;
    for (int p = 0; p < nf && n < 8; p++) {
        __try {
            ULONG_PTR r = ed_player(p);
            if (!r || !*(int *)(r + 0x20)) continue;
            memcpy(pts[n], (const float *)(r + ACTOR_POS), 12);
            for (int j = 0; j < 3; j++) sum[j] += pts[n][j];
            n++;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (!n) { g_ar_kind = 0; return; }
    for (int j = 0; j < 3; j++) g_ar_c[j] = sum[j] / n;
    float far2 = 0;
    for (int i = 0; i < n; i++) { float dx = pts[i][0] - g_ar_c[0], dz = pts[i][2] - g_ar_c[2]; if (dx * dx + dz * dz > far2) far2 = dx * dx + dz * dz; }
    g_ar_r0 = sqrtf(far2) + 80.0f;
    g_ar_r = g_ar_kind == AR_KOTH ? 50.0f : g_ar_r0;
    g_ar_dur = (m.v[0] > 0 ? m.v[0] * 60.0f : 180.0f) * 0.8f;
    g_ar_t = g_ar_sec = 0;
    memset(g_ar_score, 0, sizeof(g_ar_score));
    g_ar_lead = -1; g_ar_done = false;
    for (int i = 0; i < AR_FX; i++) g_ar_fx[i] = FX_NONE;
    QueryPerformanceCounter(&g_ar_last);
    logf("arena: %s, centre (%.1f, %.1f, %.1f), ring %.0f, %d fighters", g_ar_kind == AR_RING ? "shrinking ring" : "king of the hill",
         g_ar_c[0], g_ar_c[1], g_ar_c[2], g_ar_r, n);
    set_status(g_ar_kind == AR_RING ? "SHRINKING ARENA: stay inside the ring" : "KING OF THE HILL: hold the glowing zone alone - 60 wins");
}
// one step of dt seconds (game thread, every frame of a match with the mode on)
static void ar_step(float dt, int nf, int lives) {
    if (!g_ar_kind || g_ar_done) return;
    g_ar_t += dt;
    g_ar_sec += dt;
    if (g_ar_kind == AR_RING) {
        float f = g_ar_dur > 0 ? g_ar_t / g_ar_dur : 1.0f;
        g_ar_r = g_ar_r0 - (g_ar_r0 - 40.0f) * (f > 1.0f ? 1.0f : f);
    }
    ar_fx_step(g_ar_r);
    if (g_ar_sec < 1.0f) return;
    g_ar_sec -= 1.0f;
    int inside[8] = {0}, teams = 0, team = -1;
    // v5.173 (owner 2026-11-13): online each console owns only its fighter - the game
    // sends the fighter at actor-table index *0x1404bbe00 (PlayerPos collect FUN_14027c080) and every PlayerPos apply
    // (FUN_14027c4f0) writes the sender's life into that fighter's +0x4C on the other machines. Life written here to another
    // player's fighter is overwritten, but the game's fall-out run on it showed that player falling on this screen only. Online
    // the ring's damage and the hill's end touch only this console's own fighter; every console runs the same rule for its own.
    int me = (!g_ed_test_gs && ed_online()) ? ar_local_index() : -1;
    for (int p = 0; p < nf; p++) {
        __try {
            ULONG_PTR r = ed_player(p);
            if (!r || !*(int *)(r + 0x20) || !*(int *)(r + 0x24)) continue;      // standing fighters only
            const float *pos = (const float *)(r + ACTOR_POS);
            float dx = pos[0] - g_ar_c[0], dz = pos[2] - g_ar_c[2];
            bool in = dx * dx + dz * dz <= g_ar_r * g_ar_r;
            if (g_ar_kind == AR_RING && !in && (me < 0 || p == me)) {
                int life = *(int *)(r + 0x4C) - 1;
                if (life < 1) { logf("arena: fighter %d ran out of life outside the ring", p + 1); ar_fall(r); }
                else *(int *)(r + 0x4C) = life;
            }
            int t = *(int *)(r + 0x1c);
            if (g_ar_kind == AR_KOTH && in && t >= 0 && t < 8 && !inside[t]++) { teams++; team = t; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_ar_kind == AR_KOTH && teams == 1) {
        g_ar_score[team]++;
        if (team != g_ar_lead || g_ar_score[team] % 10 == 0) {
            char msg[64]; sprintf_s(msg, "KING OF THE HILL: team %c holds it - %d / %d", 'A' + team, g_ar_score[team], AR_WIN); set_status(msg);
            g_ar_lead = team;
        }
        if (g_ar_score[team] >= AR_WIN) {                         // the other side falls out for good
            g_ar_done = true;
            logf("arena: team %d wins the hill", team);
            for (int p = 0; p < nf; p++) {
                __try {
                    ULONG_PTR r = ed_player(p);
                    if (!r || !*(int *)(r + 0x20) || *(int *)(r + 0x1c) == team) continue;
                    g_ed_deaths[p] = lives;                       // no lives left: the lives step leaves them out
                    if (*(int *)(r + 0x24) && (me < 0 || p == me)) ar_fall(r);   // v5.173 online: own fighter only
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            set_status("KING OF THE HILL: the hill is taken!");
        }
    }
}
static void ar_frame(LONG k, bool on, int nf, int lives) {      // called by the lives step
    static bool was;
    if (!on || !g_in_match) { if (was) { ar_fx_stop(); g_ar_kind = 0; } was = false; return; }
    if (!was) { was = true; ar_begin(k, nf); }
    LARGE_INTEGER now, fq;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&fq);
    float dt = (float)(now.QuadPart - g_ar_last.QuadPart) / (float)fq.QuadPart;
    g_ar_last = now;
    if (dt < 0 || dt > 0.25f) dt = 0.25f;
    bool held = false;
    if (!g_ed_test_gs) { __try { held = (*(unsigned *)(g_base + 0x530034) & 1) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    if (!held) ar_step(dt, nf, lives);
}

static int ar_self_test() {                                     // v5.24 the ring and the hill on stand-ins
    static char gs[0x34000];
    static unsigned char a[0x18000], b[0x18000];
    static ULONG_PTR tab[4];
    static int falls;
    struct F { static void __fastcall fall(ULONG_PTR f, int) { falls++; *(int *)(f + 0x24) = 0; } };
    char *keep_gs = g_ed_test_gs; ULONG_PTR *keep_tab = g_actor_test_tab; LONG keep_on = g_ed_mode_on, keep_in = g_in_match;
    int keep_n = g_edc[EDP_MODE].n; EdItem keep_item = g_edc[EDP_MODE].item[0];
    g_ed_test_gs = gs; g_actor_test_tab = tab; g_ar_fall_test = F::fall; falls = 0;
    memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b));
    tab[0] = (ULONG_PTR)a; tab[1] = (ULONG_PTR)b; tab[2] = tab[3] = 0;
    auto put = [](unsigned char *f, float x, float z, int team) {
        *(int *)(f + 0x20) = 1; *(int *)(f + 0x24) = 1; *(int *)(f + 0x1c) = team; *(int *)(f + 0x4C) = 20; *(int *)(f + 0x50) = 20;
        float *p = (float *)(f + ACTOR_POS); p[0] = x; p[1] = 0; p[2] = z;
    };
    EdItem &m = g_edc[EDP_MODE].item[0];
    memset(&m, 0, sizeof(m));
    g_edc[EDP_MODE].n = 1;
    // the ring: starts at the start points' farthest + 80, shrinks to 40 over 80 % of 5 minutes (240 s)
    m.v[0] = 5; m.v[1] = 20; m.v[2] = 1; m.v[5] = AR_RING;
    put(a, -100, 0, 0); put(b, 100, 0, 1);
    ar_begin(0, 4);
    bool ring0 = g_ar_kind == AR_RING && fabsf(g_ar_c[0]) < 0.01f && fabsf(g_ar_r0 - 180.0f) < 0.01f;
    for (int i = 0; i < 120; i++) ar_step(1.0f, 4, 1);         // half way: 110
    bool half = fabsf(g_ar_r - 110.0f) < 0.5f && *(int *)(a + 0x4C) == 20;
    float *pb = (float *)(b + ACTOR_POS); pb[0] = 200;          // b well outside: 1 life a second
    for (int i = 0; i < 5; i++) ar_step(1.0f, 4, 1);
    bool hurt = *(int *)(b + 0x4C) == 15 && *(int *)(a + 0x4C) == 20;
    for (int i = 0; i < 20; i++) ar_step(1.0f, 4, 1);
    bool fell = falls == 1 && *(int *)(b + 0x4C) == 0;
    for (int i = 0; i < 200; i++) ar_step(1.0f, 4, 1);
    bool floor_ = fabsf(g_ar_r - 40.0f) < 0.01f;
    // the hill: a in the zone alone scores; 60 wins, b falls out with no lives left
    m.v[5] = AR_KOTH; m.v[2] = 3;
    put(a, 10, 10, 0); put(b, 300, 0, 1); falls = 0;
    ar_begin(0, 4);
    g_ar_c[0] = g_ar_c[2] = 0;                                  // the zone at the stage's middle, a inside, b out
    memset(g_ed_deaths, 0, sizeof(g_ed_deaths));
    for (int i = 0; i < 59; i++) ar_step(1.0f, 4, 3);
    bool score = g_ar_score[0] == 59 && !g_ar_done && falls == 0;
    float *pa = (float *)(a + ACTOR_POS);
    pb[0] = 5; ar_step(1.0f, 4, 3);                            // both sides in it: nobody scores
    bool contested = g_ar_score[0] == 59;
    pb[0] = 300; ar_step(1.0f, 4, 3);
    bool win = g_ar_done && g_ar_score[0] == 60 && falls == 1 && g_ed_deaths[1] == 3 && g_ed_deaths[0] == 0;
    (void)pa;
    g_ed_test_gs = keep_gs; g_actor_test_tab = keep_tab; g_ar_fall_test = NULL; g_ed_mode_on = keep_on; g_in_match = keep_in;
    g_edc[EDP_MODE].n = keep_n; g_edc[EDP_MODE].item[0] = keep_item; g_ar_kind = 0; g_ar_done = false;
    memset(g_ed_deaths, 0, sizeof(g_ed_deaths));
    logf("self-test: arena ring %s, half way %s, outside hurts %s, falls out %s, smallest %s, hill score %s, contested %s, hill win %s",
         ring0 ? "ok" : "FAILED", half ? "ok" : "FAILED", hurt ? "ok" : "FAILED", fell ? "ok" : "FAILED", floor_ ? "ok" : "FAILED",
         score ? "ok" : "FAILED", contested ? "ok" : "FAILED", win ? "ok" : "FAILED");
    return ring0 && half && hurt && fell && floor_ && score && contested && win ? 1 : -67;
}

#include "cwo_nextmap.inl"                                 // v5.54 Next Map (host rotation in the Live lobby)
#include "cwo_bfrnet.inl"                                  // v5.57 BFR lobby features over the player-info packet
#include "cwo_rules.inl"                                   // v5.53 multiplayer rules + downed name plate

static void ed_lives_step() {
    LONG k = g_ed_mode_on;
    // v5.55 (AGENTS.md "Multiplayer rules online", owner 2026-11-01): an armed KO-rules mode also counts in a Live session, in
    // count-and-announce form (no loader revives, no forced knock-outs: every machine sees the same knock-downs)
    bool online = ed_online();
    int fk = k >= 0 && k < g_edc[EDP_MODE].n ? g_edc[EDP_MODE].item[k].v[5] : 0;
    g_ru_online = online;
    // v5.57 the Xbox Live lobby's mode (the host's choice in Change Rule, over the player-info packet: cwo_bfrnet.inl) wins
    // online over an armed Create a Mode mode
    int nk = online ? bn_rule() : 0;
    int netkind = nk == 1 ? RU_POINTS : nk == 2 ? RU_FIRST5 : nk == 3 ? RU_REVIVES : 0;
    // v5.95 the five new rules (online: the host's; offline: CwoLobbyRule.txt / the split-screen lobby later)
    g_ar_net_kind = nk == 4 ? AR_KOTH : nk == 5 ? AR_RING : 0;
    bool karmed = !netkind && k >= 0 && k < g_edc[EDP_MODE].n && (!online || fk == RU_POINTS || fk == RU_FIRST5 || fk == RU_REVIVES);
    bool on = g_in_match && !g_forge_active && (karmed || netkind);
    static EdItem net_item;                                    // the lobby mode's settings: the match's own clock, 3 revives a fighter
    if (netkind) { memset(&net_item, 0, sizeof(net_item)); net_item.v[1] = 20; net_item.v[2] = netkind == RU_REVIVES ? 3 : 9; net_item.v[5] = netkind; }
    const EdItem &mi = karmed ? g_edc[EDP_MODE].item[k] : net_item;
    if (g_in_match != g_ed_was_match) {                        // a new match: count from zero
        g_ed_was_match = g_in_match;
        memset(g_ed_deaths, 0, sizeof(g_ed_deaths)); memset(g_ed_cap, 0, sizeof(g_ed_cap)); memset(g_ed_down, 0, sizeof(g_ed_down));
        g_sv_wave = 1; g_sv_kos = 0; g_sv_enemies = 0;           // v5.22
        g_ru_kind = 0;                                         // v5.53
    }
    {                                                          // v5.24 the ring / the hill
        int f5 = on && karmed ? g_edc[EDP_MODE].item[k].v[5] : 0;
        if (g_ar_net_kind && g_in_match && !g_forge_active) ar_frame(-1, true, g_ed_test_gs ? 4 : cwo_nfighters(), 1);
        else ar_frame(k, on && karmed && (f5 == AR_RING || f5 == AR_KOTH), g_ed_test_gs ? 4 : cwo_nfighters(), on && karmed ? g_edc[EDP_MODE].item[k].v[2] : 1);
    }
    if (!on) { g_ru_kind = 0; return; }
    int lives = mi.v[2];
    int rk = mi.v[5];                                          // v5.53 the rules
    if (rk != RU_POINTS && rk != RU_FIRST5 && rk != RU_REVIVES) rk = 0;
    {
        static LARGE_INTEGER last;
        LARGE_INTEGER now, fq; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&fq);
        if (rk && g_ru_kind != rk) { ru_begin(rk, mi); last = now; }
        float dt = last.QuadPart ? (float)(now.QuadPart - last.QuadPart) / (float)fq.QuadPart : 0;
        last = now;
        if (dt < 0 || dt > 0.25f) dt = 0.25f;
        bool held = false;
        if (!g_ed_test_gs) { __try { held = (*(unsigned *)(g_base + 0x530034) & 1) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        if (rk && !held) ru_time(dt, g_ed_test_gs ? 4 : cwo_nfighters(), lives);
    }
    bool sv = karmed && sv_mode(k);                            // v5.22 Survival: the other side's CPUs never stay down
    int nf = g_ed_test_gs ? 4 : cwo_nfighters(), myteam = -1;
    if (sv) {
        __try { ULONG_PTR p0 = ed_player(0); if (p0) myteam = *(int *)(p0 + 0x1c); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        int e = 0;
        for (int p = 0; p < nf; p++) {
            __try { ULONG_PTR r = ed_player(p); if (r && *(int *)(r + 0x20) && *(int *)(r + 0x30) == 0 && *(int *)(r + 0x1c) != myteam) e++; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (e > g_sv_enemies) g_sv_enemies = e;
    }
    for (int p = 0; p < nf; p++) {
        __try {
            ULONG_PTR r = ed_player(p);
            if (!r || *(int *)(r + 0x20) == 0) continue;
            int *state = (int *)(r + 0x12F04), *count = (int *)(r + 0x12F08);
            bool enemy = sv && *(int *)(r + 0x30) == 0 && *(int *)(r + 0x1c) != myteam;
            if (*state != 0x1B) {
                if (rk == RU_REVIVES && g_ed_down[p] && *(int *)(r + 0x24)) {   // v5.53 up again by a partner (the game's revive)
                    g_ed_down[p] = 0;
                    if (!ru_revived(p, lives) && !online) { g_ed_deaths[p] = lives; ar_fall(r); logf("rules: player %d revived past their revives - out", p + 1); }
                    continue;
                }
                g_ed_down[p] = 0;
                if (*(int *)(r + 0x24) && *(int *)(r + 0x50) > g_ed_cap[p]) g_ed_cap[p] = *(int *)(r + 0x50);   // the life cap seen
                continue;
            }
            if (!g_ed_down[p]) {
                g_ed_down[p] = 1;
                g_ed_deaths[p]++;
                logf("Edgar's Dream: player %d down (%d of %d lives used)", p + 1, g_ed_deaths[p], lives);
                if (enemy) sv_ko(nf, myteam);
                if (rk) ru_down(r, *(int *)(r + 0x1c), nf, lives);   // v5.53; v5.57 the last hitter scores
                if (rk == RU_REVIVES) {                          // partners revive; out of revives = out
                    int t = *(int *)(r + 0x1c);
                    if (t >= 0 && t < 8 && g_ru_rev[p] >= lives) { g_ed_deaths[p] = lives; logf("rules: player %d down with no revives left (team %d)", p + 1, t); if (online) { char m2[64]; sprintf_s(m2, "Player %d has no revives left", p + 1); set_status(m2); } }
                }
            }
            if (rk == RU_REVIVES || online) continue;            // v5.53 no automatic get-up in this mode; v5.55 none online
            if (!enemy && g_ed_deaths[p] >= lives) continue;    // last life: the game takes them out as usual
            if (*count < 3) *count = 3;                          // the game would remove them at 0: wait here instead
            if (*(int *)(r + 0x1396C) != -1 || *(int *)(r + 0x13968) != 0) continue;   // still falling
            PFN_REVIVE revive = g_ed_revive_test ? g_ed_revive_test : (PFN_REVIVE)(g_base + RVA_REVIVE);
            revive(r);                                           // the game's own revive (as rule 10 calls it)
            int cap = g_ed_cap[p] > 0 ? g_ed_cap[p] : mi.v[1];
            if (enemy) { cap = mi.v[1] + 4 * (g_sv_wave - 1); if (cap > 60) cap = 60; }   // v5.22 the wave's life
            *(int *)(r + 0x50) = cap;                            // full life, not the halved cap
            *(int *)(r + 0x4C) = cap;
            g_ed_down[p] = 0;
            char msg[64];
            sprintf_s(msg, "Player %d is back up - %d lives left", p + 1, lives - g_ed_deaths[p]);
            if (enemy) sprintf_s(msg, "Wave %d: CPU %d is back (life %d)", g_sv_wave, p + 1, cap);
            else set_status(msg);
            logf("Edgar's Dream: %s (life %d)", msg, cap);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// ---------------------------------------------------------------- school / character in matches (game thread)
static int g_ed_hand_last[4], g_ed_hand_turn;
static const EdItem *ed_school_in_use() {
    LONG c = g_ed_char_on;
    if (c >= 0 && c < g_edc[EDP_CHAR].n) {                     // a character in use brings its school
        int s = g_edc[EDP_CHAR].item[c].v[1];
        if (s >= 6 && s - 6 < g_edc[EDP_SCHOOL].n) return &g_edc[EDP_SCHOOL].item[s - 6];
        return NULL;
    }
    LONG s = g_ed_school_on;
    return s >= 0 && s < g_edc[EDP_SCHOOL].n ? &g_edc[EDP_SCHOOL].item[s] : NULL;
}

static DWORD ed_aura_rgb(int hue, int sat, int val, float top = 176.0f) {   // dark values: the aura is drawn added on top
    float h = (float)hue / 60.0f, s = sat / 100.0f, v = val / 100.0f * top;
    int i = (int)h % 6;
    float f = h - (int)h, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r = 0, g = 0, b = 0;
    switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
                 case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; break; }
    return 0xFF000000u | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
}

#include "cwo_sfx.inl"                                         // v3.9 the school's colour and effect engine
static LONG g_ed_fx_owner;                                     // 1 while the aura effect is the school's
static void ed_school_step() {
    const EdItem *s = ed_school_in_use();
    bool on = s && g_in_match && !ed_online();
    // v3.9: the colour (aura script table entry 0) and the effect (placement, speed, pieces) - cwo_sfx.inl
    SfxSpec sp; memset(&sp, 0, sizeof(sp));
    if (on) {
        sp.on = true; sp.based_on = s->v[0]; sp.hue = s->v[1]; sp.sat = s->v[2]; sp.bri = s->v[3];
        sp.skill = s->v[4]; sp.part = s->v[5]; sp.always = s->v[6] != 0;
        sp.bodypart = s->v[11]; sp.up = s->v[12]; sp.side = s->v[13]; sp.fwd = s->v[14]; sp.tilt = s->v[15]; sp.turn = s->v[16];
        sp.size = s->v[17] ? s->v[17] : 100; sp.width = s->v[18] ? s->v[18] : 100; sp.height = s->v[19] ? s->v[19] : 100;
        sp.speed = s->v[20] ? s->v[20] : 100;
        // v5.9 (owner): a piece that is set is
        // used (the pieces row need not be switched too), and 'custom' with no piece set shows the part's own pieces
        for (int i = 0; i < 4; i++) { sp.piece[i] = s->v[22 + i]; if (sp.piece[i] >= 0) sp.pieces_custom = 1; }
        sp.replace_aura = s->v[26] != 0;
        sp.fx_hue = s->v[27]; sp.fx_str = s->v[28] ? s->v[28] : 100;
        LONG ch = g_ed_char_on;
        g_sfx_body = ch >= 0 && ch < g_edc[EDP_CHAR].n ? g_edc[EDP_CHAR].item[ch].v[CH_BODY] : 0;
    }
    if (g_ed_fx_owner) { g_fx_on = false; InterlockedExchange(&g_fx_change, 1); g_ed_fx_owner = 0; }   // the v3.4 route is retired
    sfx_step(sp);
    if (!on) { memset(g_ed_hand_last, 0, sizeof(g_ed_hand_last)); return; }
    __try {
        int skills[4], n = 0;
        for (int i = 7; i <= 10; i++) if (s->v[i] >= 0) skills[n++] = s->v[i];
        ULONG_PTR pd = n ? cwo_bdata()[0] : 0;
        for (int k = 0; pd && k < 4; k++) {                    // each new card in a hand slot becomes the next school skill
            char *slot = (char *)(pd + 0x350 + 0x50 * (ULONG_PTR)k);
            int sk = *(int *)slot ? *(int *)(slot + 0x28) : 0;
            if (!sk) { g_ed_hand_last[k] = 0; continue; }
            if (sk == g_ed_hand_last[k]) continue;
            bool mine = false;
            for (int i = 0; i < n; i++) mine |= skills[i] == sk;
            if (!mine) { sk = skills[g_ed_hand_turn++ % n]; *(int *)(slot + 0x28) = sk; }
            g_ed_hand_last[k] = sk;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------- spectator: the CPU plays player 1 (v3.5)
// View + LB in a match (controller thread) asks for a switch; the switch itself runs here on the game thread. The game's
// own brain is set up once per match (a record the loader builds; the game copies the names out of it), then only
// +0x30 changes: 0 = the brain drives, 1 = your pad. Never online, never in a Stage Creator match, never a boss.
static volatile LONG g_ed_spec_req;                            // a switch asked for (View + LB)
static int g_ed_spec_t_win = -1, g_ed_spec_t_code = 0;          // v5.188.42 / 44 self-test stand-ins: the winner, the result code
static bool g_ed_spec_mine, g_ed_spec_auto;                    // the CPU is ours now; this match's automatic start done
static ULONG_PTR g_ed_spec_brain;                              // the fighter whose brain we set up this match
static char g_ed_spec_rec[0x60];
static PFN_AI_INIT g_ed_ai_init_test;                          // self-test: stand-in brain set-up
static PFN_AI_POLL g_ed_ai_poll_test;                          // self-test: stand-in program start
static ULONGLONG g_ed_spec_t0;                                 // when the brain was set up (the program start is polled)
static int g_ed_ai_on_test, g_ed_ai_calls_test, g_ed_ai_polls_test;
static void ed_spec_record(int prog) {                         // the record the brain is set up from
    memset(g_ed_spec_rec, 0, sizeof(g_ed_spec_rec));
    bool copied = false;
    if (prog == 0 && !g_ed_test_gs) {                          // "opponent's": a CPU in this match (its own record)
        for (int p = 1; p < 4 && !copied; p++) {
            ULONG_PTR q = ed_player(p), rec = *(ULONG_PTR *)((char *)cwo_gaddr(RVA_FIGHTERS) + 0x48 * (ULONG_PTR)p + 0x20);
            if (q && rec && *(int *)(q + 0x30) == 0 && *(int *)(q + 0x142d0) != 0) {
                memcpy(g_ed_spec_rec, (const void *)rec, 0x5c);
                g_ed_spec_rec[0x5b] = 0;
                copied = !strncmp(g_ed_spec_rec + 0x46, "eneprog", 7);   // only a name of the game's own programs
                for (const char *c = g_ed_spec_rec + 0x46; copied && *c; c++) copied = isalnum((unsigned char)*c) || *c == '_' || *c == '.';
                if (!copied) { memset(g_ed_spec_rec, 0, sizeof(g_ed_spec_rec)); continue; }
                if (*(int *)(g_ed_spec_rec + 0x18) < 0 || *(int *)(g_ed_spec_rec + 0x18) > 15) *(int *)(g_ed_spec_rec + 0x18) = 0;
                logf("spectator: CPU program copied from fighter %d (%.20s, row %d)", p + 1, g_ed_spec_rec + 0x46, *(int *)(g_ed_spec_rec + 0x18));
            }
        }
    }
    if (!copied) {                                             // v5.5: the file name the game opens (com\ai\<name>.ssb, FUN_14017b350;
        strcpy_s(g_ed_spec_rec + 0x46, 0x16, ED_BRAINS[prog >= 1 && prog <= 13 ? prog : 6]);   // the file log: com\ai\chkprog000.ssb)
        strcat_s(g_ed_spec_rec + 0x46, 0x16, ".ssb");
    }
    g_ed_spec_rec[0x5b] = 0;
    memset(g_ed_spec_rec + 0x31, 0, 0x15);                     // no deck plan: the CPU plays your own deck
    // v5.38.1 (owner 2026-10-31; log: brain set up,
    // eneprog006 running): the record's personality row (+0x18) was 0, the empty row that decides nothing - the same
    // stand-still the 3 VS 3 lobby CPUs had (v5.36). The lobby's rule applies here too: Brutal unless CPU Level says otherwise.
    if (!copied || *(int *)(g_ed_spec_rec + 0x18) == 0) {
        cpu_level_apply(g_ed_spec_rec, cpu_level_pick());
        logf("spectator: personality row %d, program %.20s", *(int *)(g_ed_spec_rec + 0x18), g_ed_spec_rec + 0x46);
    }
}
static bool ed_spec_cpu(ULONG_PTR pl, bool cpu) {              // false: left as it was (and why on screen)
    if (!cpu) {
        if (g_ed_spec_mine) *(int *)(pl + 0x30) = 1;
        g_ed_spec_mine = false;
        set_status("Player 1: you again (View + LB: CPU)");
        logf("spectator: player 1 back to the pad");
        return true;
    }
    if (*(int *)(pl + 0x30) == 0) { set_status("Player 1 is already the game's CPU"); return false; }
    int on = g_ed_test_gs ? g_ed_ai_on_test : *(int *)(g_base + RVA_AI_ON);
    if (!on) { set_status("The game's CPU programs are not loaded in this match"); return false; }
    if (*(int *)(pl + 0x142d0) == 0) {                         // no brain yet this match: set it up once
        ULONGLONG base = *(ULONGLONG *)(pl + 0x12c48), next = *(ULONGLONG *)(pl + 0x12c50), size = *(ULONGLONG *)(pl + 0x12c58);
        if (next + 0x8000 >= base + size) {                    // the game would hang: refuse
            set_status("No room for a CPU in this fighter");
            logf("spectator: refused, arena full (base %llx next %llx size %llx)", base, next, size);
            return false;
        }
        ed_spec_record(g_edc[EDP_SPECT].n ? g_edc[EDP_SPECT].item[0].v[1] : 0);
        PFN_AI_INIT init = g_ed_ai_init_test ? g_ed_ai_init_test : (PFN_AI_INIT)(g_base + RVA_AI_INIT);
        init(pl + 0x142d0, pl, (ULONG_PTR)g_ed_spec_rec);
        if (*(int *)(pl + 0x142d0) == 0) { set_status("The CPU could not be set up (see the log)"); logf("spectator: brain set-up returned off"); return false; }
        g_ed_spec_brain = pl;
        g_ed_spec_t0 = GetTickCount64();
        logf("spectator: CPU brain set up for player 1 (%.20s)", g_ed_spec_rec + 0x46);
    }
    *(int *)(pl + 0x30) = 0;
    g_ed_spec_mine = true;
    set_status("Player 1: CPU playing (View + LB: you)");
    logf("spectator: player 1 is the CPU");
    return true;
}
// v5.188.27 (owner 2026-11-16): when player 1 goes
// to the CPU (View + LB, or the Spectator page's start), the free camera comes on with it (sticks fly, X HUD on / off, B
// camera off, Y next fighter); when player 1 comes back, it goes off. 1 = on, 2 = off (taken by ed_freecam_step)
static volatile LONG g_ed_spec_cam;
static void ed_spectator_step() {
    bool req = InterlockedExchange(&g_ed_spec_req, 0) != 0;
    if (!g_in_match || g_forge_active || ed_online()) {        // between matches the game builds new fighters
        if (!g_in_match) { g_ed_spec_mine = false; g_ed_spec_auto = false; g_ed_spec_brain = 0; }
        if (req && g_in_match) set_status(ed_online() ? "Online: player 1 stays you" : "Not in a Stage Creator match");
        return;
    }
    __try {
        ULONG_PTR pl = ed_player(0);
        if (!pl || !*(int *)(pl + 0x20)) return;
        if (g_ed_spec_brain == pl && *(int *)(pl + 0x142d0) == 0) {   // the brain is gone: a new match in the same place
            g_ed_spec_mine = false; g_ed_spec_auto = false; g_ed_spec_brain = 0;
        }
        if (g_ed_spec_mine && *(int *)(pl + 0x30) != 0) g_ed_spec_mine = false;   // the game took it back
        // v5.188.42 (hourly pass 10, in game: the CPU playing player 1 won, the result screen said YOU LOSE; the game's result
        // FUN_14012c8c0 had given player 1 rank 0 = won): the moment the match is decided (the winner *0x1404bbe04 is set; it
        // is -1 all match long - player 1's result entry 0x1404bc5c0 is not: it read 0 thirty seconds in, 5.188.42 in game),
        // player 1 goes back to the pad, so the result screen is player 1's own
        if (g_ed_spec_mine) {
            int win = g_ed_test_gs ? g_ed_spec_t_win : *(int *)(g_base + 0x4bbe04);   // v5.188.43 the winner only
            if (win != -1) {
                *(int *)(pl + 0x30) = 1;
                g_ed_spec_mine = false;
                InterlockedExchange(&g_ed_spec_cam, 2);
                // v5.188.44 (capstone, clean exe 9ba7411b: FUN_14012c790 right after the winner FUN_14012c8c0, once): the result
                // screen's code *0x1404bbec8 = 2 (lose), then 1 (win) if an actor of the winning team (+0x1c) has +0x30 != 0 -
                // the CPU-played player 1 had 0, so a win showed YOU LOSE (5.188.43 in game: handed back one frame too late).
                // Player 1's team won: the code is the win the game would have written for a pad player.
                int *code = g_ed_test_gs ? &g_ed_spec_t_code : (int *)(g_base + 0x4bbec8);
                if (*(int *)(pl + 0x1c) == win && *code == 2) { *code = 1; logf("spectator: player 1's team won - the result screen's code set to win (was lose: the CPU had player 1)"); }
                logf("spectator: the match is decided (winner %d) - player 1 back to the pad for the result screen", win);
            }
        }
        if (g_ed_spec_brain == pl && *(int *)(pl + 0x142d0) && !*(int *)(pl + AI_SUB + AI_STARTED)) {   // v5.9 start its program
            PFN_AI_POLL poll = g_ed_ai_init_test ? g_ed_ai_poll_test : (PFN_AI_POLL)(g_base + RVA_AI_POLL);
            if (poll) poll(pl + AI_SUB);
            if (*(int *)(pl + AI_SUB + AI_STARTED)) logf("spectator: the CPU program runs (%.20s, %llu ms)", g_ed_spec_rec + 0x46, GetTickCount64() - g_ed_spec_t0);
            else if (GetTickCount64() - g_ed_spec_t0 > 10000 && g_ed_spec_t0) {
                g_ed_spec_t0 = 0;
                logf("spectator: the CPU program has not started after 10 s (file descriptor flags %x)", *(unsigned *)(pl + AI_SUB + 0x2268));
                set_status("The CPU's program did not start (see the log)");
            }
        }
        bool start = g_edc[EDP_SPECT].n && g_edc[EDP_SPECT].item[0].v[0];
        if (start && !g_ed_spec_auto) { g_ed_spec_auto = true; if (!g_ed_spec_mine && ed_spec_cpu(pl, true)) InterlockedExchange(&g_ed_spec_cam, 1); }
        if (req) {
            bool to_cpu = !g_ed_spec_mine;
            if (ed_spec_cpu(pl, to_cpu)) InterlockedExchange(&g_ed_spec_cam, to_cpu ? 1 : 2);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("spectator: fault, nothing changed"); }
}

#include "cwo_freecam.inl"                                     // v5.9 the free camera (View + RB)
#include "cwo_arsenal.inl"                                     // v5.9 arsenal lists 1-3, the Xbox Live lobby's rule list
#include "cwo_arsdata.inl"                                    // v5.64 Arsenal Data per stage

static void ed_mode_keep() {                                   // a mode left on last time: write it once the game's data is there
    LONG k = g_ed_mode_on;
    if (k < 0 || k >= g_edc[EDP_MODE].n || g_in_match || ed_online()) return;
    const EdItem &m = g_edc[EDP_MODE].item[k];
    if (!g_ed_rules_written && *(char **)(g_base + RVA_GSDATA)) {
        if (ed_rules_apply(&m)) logf("Edgar's Dream: mode %.24s in use again (rule table written)", m.name);
    }
    static LONG tried = -1;                                    // one try per mode switched on (no log every frame)
    if (g_ed_caps_written && tried != k) { tried = k; ed_caps_write(3); }   // v5.188.3: put the game's 3 back, never 2
}

// v4.4 (owner): in an Edgar's Dream
// test (skill, school, character) the match clock stays full. Facts (Ghidra batch_065): the clock's start is
// *0x1404bc638 = minutes * 0x708 (FUN_140128cc0); the running clock *0x1404bc8c0 (30 ticks a second, shown / 0x1e by
// FUN_140154cb0) is loaded from it (FUN_14012ab20) and counted down by FUN_14012b7a0. Below 10 s it is left alone (the
// same word times the start and end of a match).
static void ed_ban_step() {                                   // v4.8 game thread
    LONG mk = g_ed_mode_on;
    if (mk < 0 || mk >= g_edc[EDP_MODE].n || !g_in_match || ed_online()) return;
    const EdItem &m = g_edc[EDP_MODE].item[mk];
    bool any = false;
    for (int i = 12; i <= 16; i++) any |= m.v[i] > 0;
    if (!any) return;
    __try {
        char *gs = *(char **)(g_base + RVA_GSDATA);
        if (!gs || *(unsigned *)gs != 0x44004) return;
        auto banned = [&](int id) { for (int i = 12; i <= 16; i++) if (m.v[i] > 0 && m.v[i] == id) return true; return false; };
        for (int p = 0; p < cwo_nfighters(); p++)               // v5.1 the capsule table may have moved (cwo_caps)
            for (int k = 0; k < 3; k++) {
                int *sk = (int *)cwo_caps(0xD10 + (ULONG_PTR)(p * 3 + k) * 0x48 + 0x18);
                if (*sk <= 0 || *sk >= 753 || !banned(*sk)) continue;
                short type = *(short *)(gs + 0x18 + 0x90 * *sk + 0x0E);
                int pick = -1, seen = 0;
                for (int id = 1; id < 375; id++) {                   // a random stock skill of the same capsule type
                    const char *r = gs + 0x18 + 0x90 * id;
                    if (!*(short *)(r + 2) || *(short *)(r + 0x0E) != type || banned(id)) continue;
                    if (rand() % ++seen == 0) pick = id;
                }
                if (pick > 0) { logf("mode: banned skill %d in player %d's capsule %d -> %d", *sk, p + 1, k + 1, pick); *sk = pick; }
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// v5.32 (owner 2026-10-27; "after I finished trying to use the skill editor, it crashed afterwards"). The v4.6 exit called
// native 4.211 (FUN_140210d10), which is NOT the Give Up: it only blocks the fighters' buttons for 5 frames
// (*0x1404bca2c = 5, counted down by FUN_14012b110, tested by FUN_140256920) and reloads the pad settings. So the match
// went on, the exit then held View for 3.6 s twice - the stuck cursor in the Give Up box. The game's own Give Up (Ghidra
// full pass 065 + the lifted Menu\pausemenu_en.ssb, 25_Menu_Research/pausemenu_2026-10-27): the box's OK runs fn_0b9c(129)
// = native 4.211, then 2.77(1, 129) (FUN_140207240: int *0x140533c10 [1] = 129), then 2.65 (FUN_1402461f0: the menus
// close, FUN_140246020 sets *0x140533744 = 3); every frame the battle's state function (FUN_140166d90 and its siblings)
// sees *0x140533744 == 3 with *0x140533c14 = 0x81 / 0x82 and ends the battle as lost (FUN_14012b300 / FUN_14012b4f0).
// The loader does the same last step: with no game menu open (*0x140533744 == 3) it sets *0x140533c14 = 0x81.
// v5.35 (owner 2026-10-28; log 16:25:44-53: 0x81 written four times, the match went on). The battle
// reads the result only in its paused sub-state: FUN_140166be0 / 140166ca0 / 140166d90 / 140166fb0 / 1401671d0 /
// 1401673b0 / 140167460 (no direct callers: the sub-state table 0x1403e7b70, entered by FUN_140167580, which opens the
// pause menu FUN_140246130(menu, 0x31)) each test *0x140533744 == 3 && *0x140533c14 != 0 and end the battle as lost for
// 0x81 / 0x82 (FUN_14012b4f0 / FUN_14012b300). With no pause menu open nothing reads it. So the loader does what the
// player does: Start opens the pause menu (the paused sub-state runs), then the box's own last steps - the result 0x81
// (2.77(1, 129)) and 2.65 = FUN_1402461f0 (pops the menu stack; FUN_140246020(menu, 1) sets *0x140533744 = 3).
static const ULONG_PTR RVA_MENU_STATE = 0x533744, RVA_MENU_RESULT = 0x533c14, FN_MENU_CLOSE = 0x2461f0;
typedef void (*PFN_MENU_CLOSE)();
static void ed_giveup_step() {                               // v4.6 game thread: the exit's Give Up
    static ULONGLONG asked_t, open_t;
    LONG g = g_fexit_giveup;
    // v5.117.3 (seen live 2026-11-08 11:11: a Stage Creator 'Exit without saving' stayed armed after its match and gave up the
    // NEXT split-screen match 1.7 s after START): the exit belongs to the match it was asked in - dropped when that ends
    static bool was_in;
    if (g && !g_in_match && was_in) { InterlockedExchange(&g_fexit_giveup, 0); logf("exit: the match ended - the pending Give Up dropped"); }
    was_in = g_in_match != 0;
    g = g_fexit_giveup;
    if (!g || !g_in_match) return;
    __try {
        ULONGLONG now = GetTickCount64();
        int state = *(int *)(g_base + RVA_MENU_STATE);
        if (g == 1) {                                        // open the game's pause menu (Start)
            if (state != 3) return;                          // some other game menu is open: wait for it to close
            f_press(0x1, 150);
            asked_t = now; open_t = 0;
            InterlockedExchange(&g_fexit_giveup, 2);
            logf("exit: Start - the game's pause menu asked");
            return;
        }
        if (g == 2) {                                        // the pause menu is up: Give Up, as its OK does
            if (state == 3) {
                if (now - asked_t > 4000) { InterlockedExchange(&g_fexit_giveup, 1); logf("exit: the pause menu did not open - Start again"); }
                return;
            }
            if (!open_t) open_t = now;
            if (now - open_t < 700) return;                  // its InitMenu has run (the box does it from a running menu)
            ULONG_PTR menu = *(ULONG_PTR *)(g_base + 0x533c58);   // the menu object (RVA_MENU_OBJ, cwo_slots.inl)
            int id = menu ? *(short *)(menu + 0x105f4) : -1;
            *(int *)(g_base + RVA_MENU_RESULT) = 0x81;
            ((PFN_MENU_CLOSE)(g_base + FN_MENU_CLOSE))();
            InterlockedExchange(&g_fexit_giveup, 0);
            logf("exit: the game's Give Up - result 0x81, menu %d closed by the game (code at 0x1402461f0, state now %d)", id, *(int *)(g_base + RVA_MENU_STATE));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedExchange(&g_fexit_giveup, 0); logf("exit: Give Up faulted"); }
}

static void ed_clock_hold() {
    LONG mk = g_ed_mode_on;                                    // v4.8: a mode with no time limit holds the clock too
    bool endless = mk >= 0 && mk < g_edc[EDP_MODE].n && g_edc[EDP_MODE].item[mk].v[0] == 0 && g_in_match && !ed_online();
    if (!endless && (!g_forge_active || g_ed_mode < 1 || !g_in_match)) return;
    __try {
        int full = *(int *)(g_base + 0x4bc638), *clock = (int *)(g_base + 0x4bc8c0);
        if (full > 600 && *clock >= 300 && *clock < full) *clock = full;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void cc_prev_request();                                 // v3.7 cwo_ccprev.inl
static void ed_step() {                                        // CwoOverlayFrame, game thread
    if (!g_ed_test_gs) ars_step();                             // v5.9 arsenal lists (menus; online too, owner 2026-10-25)
    if (!g_ed_loaded) { ed_load_all(); if (!g_ed_loaded) return; }
    {   // v5.10: whenever the Create a Character page comes on screen (Edgar's Dream reopened on it, back from a test), the
        // model comes with it; before, only the chooser started it and a reopened page stayed on the black backdrop
        static bool s_char_page;
        bool char_page = g_ed_open && g_ed_page == EDP_CHAR && g_forge_session && !g_in_match && !g_ed_test_gs;
        if (char_page && !s_char_page && !g_cc_prev) cc_prev_request();
        s_char_page = char_page;
    }
    ed_clock_hold();
    ed_giveup_step();
    ed_ban_step();
    ed_mode_keep();
    ed_lives_step();
    ed_school_step();
    ed_spectator_step();
    if (!g_ed_test_gs) ed_freecam_step();                      // v5.9 the free camera
    lob_rate_step();                                           // v5.2.1 the 3 VS 3 LOBBY's ratings after a match
    if (g_sixf2_state == 1 && !g_ed_test_gs) {
        sixf_diag_step();                                      // v5.5 match diagnostics (log only)
        sixf_end_step();                                       // v5.6 fighters 5-8 that left the battle
        sixf_lock_trace_step();                                // v5.6 lock-on tracer (log only)
    }
    if (!g_ed_test_gs) sixf_down_glow_step();                  // v5.6 downed fighters glow (team matches)
    if (!g_ed_test_gs) ru_plate_step();                        // v5.53 a downed fighter's HUD panel keeps flashing
    if (!g_ed_test_gs) nm_step();                              // v5.54 Next Map
}

// ---------------------------------------------------------------- input
static void cc_prev_request();                                 // v3.7 cwo_ccprev.inl
static void ed_input(WORD b, WORD pressed);
static void cc_input(WORD b, WORD pressed) {                   // the preview: the Character page, B leaves
    // v5.10: B (or Back) leaves to the chooser: Edgar's Dream opens there next time, one A from this page again
    if (pressed & BTN_B) { InterlockedExchange(&g_cc_back, 1); InterlockedExchange(&g_cc_prev, 3); rumble(1); return; }
    ed_input(b, pressed);
    if (g_ed_page != EDP_CHAR) { InterlockedExchange(&g_ed_page, EDP_CHAR); InterlockedExchange(&g_cc_back, 1); InterlockedExchange(&g_cc_prev, 3); }
}
static void co_input(WORD b, WORD pressed) {                   // v5.69 the outfit preview: its page, B leaves
    if (pressed & BTN_B) { InterlockedExchange(&g_co_prev, 3); rumble(1); return; }
    ed_input(b, pressed);
    // v5.115 (owner 2026-11-08): the page's
    // Back row switched the loader's page to its own chooser inside the preview - leaving the page now ends the preview and
    // the loader's page (the game's main menu opens its EDGAR'S DREAM list, co_back_step)
    if (g_ed_page != EDP_OUTFIT) { InterlockedExchange(&g_ed_page, EDP_OUTFIT); InterlockedExchange(&g_co_prev, 3); }
}
static void ed_reopen(int page) {                              // v3.9 back from a test: the creator page again
    InterlockedExchange(&g_ed_page, page);
    InterlockedExchange(&g_ed_wait_release, 1);
    InterlockedExchange(&g_ed_open, 1);
}
static EdItem g_ed_snap;                                        // v3.9 the item as it was when the test began
static void ed_try_now(int page) {                              // owner-friendliness 3: straight into the Skill Test Stage
    // v5.9 (owner 21:32: "click on try it now while I'm on the stage. The menu gets like messed up ... could not open skill
    // stage"): in a match already, the loader waited 20 s for a stage list that never came. Now it is on here and now.
    if (g_in_match && g_forge_active) {
        if (page == EDP_SCHOOL) InterlockedExchange(&g_ed_school_on, g_edc[page].cur);
        if (page == EDP_CHAR) InterlockedExchange(&g_ed_char_on, g_edc[page].cur);
        InterlockedExchange(&g_ed_live, 0);
        ed_state_save();
        set_status(page == EDP_SCHOOL ? "Your school is on: test it right here (R3 restarts the effect)" : "Your character's school is on: test it here");
        rumble(1);
        return;
    }
    int k = -1;
    for (int i = 0; i < g_mx_count; i++) if (!strcmp(g_mx[i].file, "pcblank")) k = i;
    if (k < 0) { set_status("The Skill Test Stage is not installed"); return; }
    if (page == EDP_SCHOOL) InterlockedExchange(&g_ed_school_on, g_edc[page].cur);
    if (page == EDP_CHAR) InterlockedExchange(&g_ed_char_on, g_edc[page].cur);
    InterlockedExchange(&g_mx_sel, k);
    mx_apply(k);
    f_cursor_slot7();
    InterlockedExchange(&g_ed_mode, 2);
    InterlockedExchange(&g_ed_try_page, page);
    g_ed_snap = g_edc[page].item[g_edc[page].cur];
    ed_close();
    ed_state_save();
    InterlockedExchange(&g_ed_auto, 1);                         // v3.9: the loader presses through stage, deck and 'debug'
    set_status(page == EDP_SCHOOL ? "Loading the Skill Test Stage for your school ..." : "Loading the Skill Test Stage ...");
    rumble(1);
}

// v5.13 Create a Quest: each outfit stays within its character's outfits (charaset row +0x3A, 6 when not known); the
// changed outfit wraps round, another one is put back to the first
static void ed_quest_outfits(EdItem &it, int fi, int dir) {
    static const int OUTF[5][2] = {{10, 5}, {11, 6}, {12, 7}, {22, 21}, {30, 29}};
    for (auto &p : OUTF) {
        int row = it.v[p[1]], no = row >= 0 ? lob_outfits_of_row(row) : 0;
        if (no <= 0) no = 6;
        int &o = it.v[p[0]];
        if (o >= no) o = fi == p[0] && dir < 0 ? no - 1 : 0;
        if (o < 0) o = no - 1;
    }
}
static void ed_change(int page, int row, int dir) {
    EdCreator &c = g_edc[page];
    if (page == EDP_CHAR && g_cc_prev == 2) cc_mark_dirty();   // v3.7: the preview rebuilds
    if (page == EDP_OUTFIT && g_co_prev == 2) co_mark_dirty();  // v5.61
    if (row == 0) {                                            // which item
        if (c.n) c.cur = (c.cur + dir + c.n) % c.n;
        return;
    }
    int fi = ed_fld(page, row);
    if (page == EDP_OUTFIT && fi >= 8) InterlockedExchange(&g_ofx_dirty, 1);   // v5.79 an effect row: shown at once
    if (row < 2 || fi < 0 || fi >= c.nf) return;
    const EdField &fd = c.f[fi];
    int &v = c.item[c.cur].v[fi];
    int step = fd.step;
    if ((fd.kind == FK_SKILL || fd.kind == FK_SLOT) && g_fheld & (BTN_LB | BTN_RB)) step = 10;   // shoulder held: fast
    if ((fd.kind == FK_OFXPCT || fd.kind == FK_OFXSPD) && g_trig_up > 0.5f) {   // v5.101 RT held: 25 % at a time, on the 25s
        int w = dir > 0 ? (v / 25 + 1) * 25 : ((v + 24) / 25 - 1) * 25;
        v = w > fd.hi ? fd.lo : w < fd.lo ? fd.hi : w;
        if (page == EDP_OUTFIT) InterlockedExchange(&g_ofx_dirty, 1);
        ed_save(page);
        return;
    }
    if (page == EDP_OUTFIT && fd.kind == FK_OFX) {             // v5.101 the presets, then the deck's skills
        int jumps = g_trig_up > 0.5f || (g_fheld & (BTN_LB | BTN_RB)) ? 10 : 1;   // v5.115 RT / a shoulder held: 10 skills
        for (int j = 0; j < jumps; j++) v = ofx_next(v, dir);
        ed_save(page);
        return;
    }
    if (fd.kind == FK_ENVSK) {                                // v4.8: the next / previous environment skill
        char *gs = *(char **)(g_base + RVA_GSDATA);
        int w = v;
        for (int tries = 0; tries < 760; tries++) {
            w += dir;
            if (w > fd.hi) w = fd.lo; else if (w < fd.lo) w = fd.hi;
            if (w < 0) break;
            bool env = false;
            __try { env = gs && *(unsigned *)gs == 0x44004 && *(short *)(gs + 0x18 + 0x90 * w + 2) && *(short *)(gs + 0x18 + 0x90 * w + 0x52) == 5; }
            __except (EXCEPTION_EXECUTE_HANDLER) { env = false; }
            if (env) break;
        }
        v = w;
    } else if (page == EDP_CHAR && fi == CH_BASE) {           // v5.10 male / female base: a fresh start in that half
        EdItem &it = c.item[c.cur];
        v = v ? 0 : 1;
        it.v[CH_BODY] = v ? 2 : 0; it.v[CH_COLOUR] = 0; it.v[CH_MOVES] = it.v[CH_BODY];
        for (int f : {(int)CH_HEAD, (int)CH_HEADX, (int)CH_MID, (int)CH_LOW, (int)CH_EXMOT}) it.v[f] = -1;
        for (int g = 0; g < CH_GN; g++) it.v[CH_G0 + g] = -1;
        for (int k = 0; k < 3; k++) it.v[CH_S0 + 2 * k + 1] = -1;
        set_status(v ? "Female base: only Freia, Tsubutaki and Mikan's bodies, parts and moves" : "Male base: only male bodies, parts and moves");
    } else if (page == EDP_CHAR && fi == CH_BODY) {           // v5.10 bodies of the base's half (Baroness is not offered)
        bool fem = c.item[c.cur].v[CH_BASE] != 0;
        int w = v;
        for (int tries = 0; tries < 24; tries++) {
            w += dir;
            if (w > fd.hi) w = fd.lo; else if (w < fd.lo) w = fd.hi;
            if (w != 15 && cb_female(w) == fem) break;
        }
        v = w;
    } else if (page == EDP_CHAR && (fd.kind == FK_MOVES || fd.kind == FK_FROM)) {   // v5.10 moves of the base's half only
        int body = c.item[c.cur].v[CH_BODY], w = v;
        for (int tries = 0; tries < 30; tries++) {
            w += dir;
            if (w > fd.hi) w = fd.lo; else if (w < fd.lo) w = fd.hi;
            if (cb_moves_allowed(body, w)) break;
        }
        v = w;
    } else if (page == EDP_CHAR && fd.kind == FK_HEAD) {      // v4.9 / v5.10 part rules: the same half only; Mac and
        int body = c.item[c.cur].v[CH_BODY], w = v;            // Baroness give none
        for (int tries = 0; tries < 24; tries++) {
            w += dir;
            if (w > fd.hi) w = fd.lo; else if (w < fd.lo) w = fd.hi;
            if (w < 0 || w == body || cb_part_allowed(body, w)) break;
        }
        v = w;
    } else if (page == EDP_QUEST && fd.kind == FK_PIC) {       // v5.13 only pictures the game has
        int w = v;
        for (int tries = 0; tries < 150; tries++) {
            w += dir;
            if (w > fd.hi) w = fd.lo; else if (w < fd.lo) w = fd.hi;
            if (ed_pic_ok(w)) break;
        }
        v = w;
    } else {
        v += dir * step;
        if (v > fd.hi) v = fd.lo; else if (v < fd.lo) v = fd.hi;   // wraps round
    }
    if (page == EDP_QUEST) ed_quest_outfits(c.item[c.cur], fi, dir);   // v5.13 outfits within the character's own
    ed_save(page);
    if (page == EDP_MODE && g_ed_mode_on == c.cur) ed_mode_arm(c.cur);   // the mode in use follows the change
}

static void ed_delete_now(int page) {                           // v4.4 the delete itself (v5.35 the catalog uses it too)
    EdCreator &c = g_edc[page];
    char msg[96];
    sprintf_s(msg, "Deleted '%.24s'", c.item[c.cur].name);
    int k = c.cur;
    for (int i = k; i + 1 < c.n; i++) c.item[i] = c.item[i + 1];
    c.n--;
    volatile LONG *on = page == EDP_CHAR ? &g_ed_char_on : page == EDP_SCHOOL ? &g_ed_school_on : page == EDP_MODE ? &g_ed_mode_on : NULL;
    if (on) {                                                  // what is in use follows the list
        LONG v = *on;
        if (v == k) { if (page == EDP_MODE) ed_mode_arm(-1); else InterlockedExchange(on, -1); }
        else if (v > k) InterlockedExchange(on, v - 1);
    }
    if (c.n == 0) { ed_defaults(page, c.item[0], "NEW"); c.n = 1; }
    if (c.cur >= c.n) c.cur = c.n - 1;
    ed_save(page);
    ed_state_save();
    if (page == EDP_CHAR && g_cc_prev == 2) cc_mark_dirty();
    set_status(msg); rumble(2);
    logf("Edgar's Dream: %s (page %d)", msg, page);
}

// v5.35 the catalog of created characters (Create a Character opens on it): rows = every character, '+ New character'
// while there is room, 'Back'. A: play as it now (in use; the loader builds it for the select screen's created cell),
// Y: change it (the page's rows), X twice: delete it, B: back to Edgar's Dream.
static int ed_cat_rows() { const EdCreator &c = g_edc[EDP_CHAR]; return c.n + (c.n < ed_cap(EDP_CHAR) ? 1 : 0) + 1; }
static void ed_cat_input(WORD pressed) {
    EdCreator &c = g_edc[EDP_CHAR];
    int rows = ed_cat_rows(), sel = g_ed_cat_sel;
    if (sel < 0 || sel >= rows) sel = 0;
    if (pressed & BTN_DOWN) sel = (sel + 1) % rows;
    if (pressed & BTN_UP) sel = (sel + rows - 1) % rows;
    InterlockedExchange(&g_ed_cat_sel, sel);
    if (pressed & BTN_B) { InterlockedExchange(&g_ed_cat, 0); InterlockedExchange(&g_ed_page, EDP_CHOOSER); ed_state_save(); return; }
    bool is_char = sel < c.n, is_new = !is_char && c.n < ed_cap(EDP_CHAR) && sel == c.n;
    char msg[96];
    if ((pressed & BTN_Y) && is_char) {                         // change it
        c.cur = sel;
        InterlockedExchange(&g_ed_cat, 0);
        InterlockedExchange(&g_ed_row, 0);
        if (g_cc_prev == 2) cc_mark_dirty();
        sprintf_s(msg, "Changing '%.24s' (My characters: the last rows)", c.item[sel].name);
        set_status(msg); rumble(1);
        return;
    }
    if ((pressed & BTN_X) && is_char) {                         // delete it (X twice)
        static ULONGLONG armed_at; static int armed_k = -1;
        ULONGLONG now = GetTickCount64();
        if (armed_k != sel || now - armed_at > 4000) {
            armed_k = sel; armed_at = now;
            sprintf_s(msg, "Press X again to delete '%.24s'", c.item[sel].name);
            set_status(msg); rumble(1);
            return;
        }
        armed_k = -1;
        c.cur = sel;
        ed_delete_now(EDP_CHAR);
        if (g_ed_cat_sel >= ed_cat_rows()) InterlockedExchange(&g_ed_cat_sel, ed_cat_rows() - 1);
        return;
    }
    if (!(pressed & BTN_A)) return;
    if (is_char) {                                              // play as it now
        c.cur = sel;
        InterlockedExchange(&g_ed_char_on, sel);
        ed_state_save();
        if (g_cc_prev == 2) cc_mark_dirty();
        sprintf_s(msg, "Playing as %.24s now: the created-character cell (top right of the select)", c.item[sel].name);
        set_status(msg); rumble(1);
        logf("Edgar's Dream: catalog - character %d '%s' in use", sel, c.item[sel].name);
        return;
    }
    if (is_new) {                                               // a new one, from the defaults
        ed_defaults(EDP_CHAR, c.item[c.n], "NEW");
        sprintf_s(c.item[c.n].name, "My Character %d", c.n + 1);
        c.cur = c.n++;
        ed_save(EDP_CHAR);
        InterlockedExchange(&g_ed_cat, 0);
        InterlockedExchange(&g_ed_row, 1);
        if (g_cc_prev == 2) cc_mark_dirty();
        set_status("New character - A on Name to name it, then pick its look and moves");
        rumble(1);
        return;
    }
    // v5.36 (owner): the
    // Back row is B - back to Edgar's Dream, not the page's rows
    InterlockedExchange(&g_ed_cat, 0);
    InterlockedExchange(&g_ed_page, EDP_CHOOSER);
    ed_state_save();
}

static void ed_action(int page, int ai) {
    EdCreator &c = g_edc[page];
    const char *a = c.actions[ai];
    char msg[96];
    if (!strcmp(a, "Back")) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); ed_state_save(); return; }
    if (!strcmp(a, "Save") || !strncmp(a, "Save (", 6)) {     // v5.69 not "Save and apply" / "Save to the outfit stock" (their own)
        ed_save(page);
        sprintf_s(msg, "Saved %.24s", c.item[c.cur].name);
        if (page == EDP_QUEST) {                               // v5.9 (owner)
            ed_read_quests();
            int q = c.item[c.cur].v[0] & 255;
            if (!g_ed_qtitle[q][0]) sprintf_s(msg, "Saved - but quest %d is an empty slot the game never offers: pick a used one", q);
            else sprintf_s(msg, "Saved %.20s: it takes quest %d's place at the next quest install", c.item[c.cur].name, q);
        }
        set_status(msg);
        rumble(1);
        return;
    }
    if (!strncmp(a, "Add the ready-made Training", 27)) {     // v5.13 (owner)
        if (c.n >= ed_cap(page)) { set_status("Create a Quest is full (16): delete one first"); rumble(2); return; }
        EdItem &t = c.item[c.n];
        ed_defaults(page, t, "Training");
        // quest 0's place (the owner's own Training mod used it), stage 7, 15 min; Freia (Tough) comes back again and
        // again (20 in all); an ally - Edgar, Brutal escort - stays near you; no partner; your life 20
        static const int TV[31] = {0, 7, 15, 1, 1, 2, 4, 5, 0, -1, 0, 0, 0, 1, 0, 0, 20, 2, 20, 1, 1, 1, 0, 4, -1, 0, -1, 0, 20, -1, 0};
        memcpy(t.v, TV, sizeof(TV));
        c.cur = c.n++;
        ed_save(page);
        set_status("Added 'Training': you + Edgar (follows you) vs Freia, who comes back. Change anyone, Save, then the quest install");
        rumble(1);
        return;
    }
    if (!strncmp(a, "Try it now", 10)) { ed_try_now(page); return; }
    if (!strncmp(a, "Delete this one", 15)) {                  // v4.4 (owner)
        static ULONGLONG armed_at; static int armed_page = -1, armed_k = -1;
        ULONGLONG now = GetTickCount64();
        if (armed_page != page || armed_k != c.cur || now - armed_at > 4000) {
            armed_page = page; armed_k = c.cur; armed_at = now;
            sprintf_s(msg, "Press A again to delete '%.24s'", c.item[c.cur].name);
            set_status(msg); rumble(1);
            return;
        }
        armed_page = -1;
        ed_delete_now(page);
        return;
    }
    if (!strncmp(a, "My characters", 13)) { InterlockedExchange(&g_ed_cat_sel, c.cur); InterlockedExchange(&g_ed_cat, 1); rumble(1); return; }
    if (page == EDP_OUTFIT) {                                  // v5.61 Create an Outfit
        const EdItem &it = c.item[c.cur];
        if (!strncmp(a, "Preview live", 12)) { if (!g_co_prev) co_prev_request(); return; }
        if (!strncmp(a, "Save and apply", 14)) { ed_save(page); co_wear(it.v, msg, sizeof(msg)); set_status(msg); rumble(1); return; }   // v5.69
        if (!strncmp(a, "Save to the outfit stock", 24)) { ed_save(page); co_stock_save(it, msg, sizeof(msg)); set_status(msg); rumble(1); return; }
        if (!strncmp(a, "Add the theme outfits", 21)) { co_themes_to_stock(it.v, msg, sizeof(msg)); set_status(msg); rumble(1); return; }   // v5.71
        if (!strncmp(a, "Apply the stock outfit", 22)) { co_stock_apply(it.v, msg, sizeof(msg)); set_status(msg); rumble(1); return; }
        if (!strncmp(a, "Take it off", 11)) { co_unwear(it.v, msg, sizeof(msg)); set_status(msg); rumble(1); return; }
    }
    if (!strncmp(a, "Preview live", 12)) { if (!g_cc_prev) cc_prev_request(); return; }
    if (!strcmp(a, "Use this school now")) {
        LONG v = g_ed_school_on == c.cur ? -1 : c.cur;
        InterlockedExchange(&g_ed_school_on, v);
        InterlockedExchange(&g_ed_char_on, -1);
        sprintf_s(msg, v >= 0 ? "School in use: %.24s (offline matches)" : "School off", c.item[c.cur].name);
        set_status(msg); ed_state_save(); rumble(1);
        return;
    }
    if (!strcmp(a, "Use this character now")) {
        LONG v = g_ed_char_on == c.cur ? -1 : c.cur;
        InterlockedExchange(&g_ed_char_on, v);
        sprintf_s(msg, v >= 0 ? "Playing as %.24s now: the created-character cell (top right of the select)" : "Character off", c.item[c.cur].name);
        set_status(msg); ed_state_save(); rumble(1);
        return;
    }
    // v5.1 (owner 2026-10-25): no split screen - the 3 vs 3 mode goes on, Edgar's Dream hands over to Single Play's own lists
    // (stage, your arsenal, any opponent) as an ordinary match, and the loader fills every empty fighter slot (cwo_sixf2.inl)
    // v5.2 (owner): the 3 VS 3 LOBBY page (cwo_lobby.inl)
    if (!strncmp(a, "3 VS 3 LOBBY", 12)) {                    // v5.3 the game's own lobby (vsmenu, build_lobby3.py)
        if (g_sixf2_state != 1) { set_status("3 vs 3 is not ready: six fighters is off or was refused (see the log)"); rumble(2); return; }
        lob3_request();
        rumble(1);
        return;
    }
    if (!strncmp(a, "3 VS 3 quick lobby", 18)) {              // v5.2.2's loader-drawn lobby, kept as the backup
        if (g_sixf2_state != 1) { set_status("3 vs 3 is not ready: six fighters is off or was refused (see the log)"); rumble(2); return; }
        lob_open(true);
        rumble(1);
        return;
    }
    if (!strncmp(a, "Play this mode", 14)) { ed_mode_arm(g_ed_mode_on == c.cur ? -1 : c.cur); rumble(1); return; }
    if (!strncmp(a, "Add the ready-made", 18)) {              // v4.8: the presets back, after the ones there
        static const struct { const char *n; int t, l, lives, caps, rule, fmt, env; } P[] = {
            {"Standard", 10, 20, 1, 3, 0, 0, 0}, {"3 vs 3", 15, 20, 3, 2, 0, 7, 0}, {"Long fight", 15, 40, 1, 3, 0, 0, 0},
            {"Quick KO", 3, 5, 1, 3, 0, 0, 0}, {"Storm arena", 10, 20, 1, 3, 5, 0, 1}, {"Endless duel", 0, 20, 3, 3, 0, 0, 0},
            {"Survival", 0, 20, 3, 3, 0, 8, 0}, {"Shrinking arena", 5, 20, 1, 3, 0, 9, 0},   // v5.22, v5.24
            {"King of the Hill", 0, 20, 3, 3, 0, 10, 0},
            {"Most KOs", 3, 20, 9, 3, 0, 11, 0}, {"First to 5 KOs", 0, 20, 9, 3, 0, 12, 0}, {"Survival revives", 0, 20, 3, 3, 0, 13, 0}};   // v5.53
        int added = 0;
        for (const auto &p : P) {
            if (c.n >= ed_cap(page)) break;
            bool have = false;                                 // v5.24: only the ones not in the list yet
            for (int i = 0; i < c.n; i++) have |= !strcmp(c.item[i].name, p.n);
            if (have) continue;
            EdItem &it = c.item[c.n];
            ed_defaults(page, it, p.n);
            it.v[0] = p.t; it.v[1] = p.l; it.v[2] = p.lives; it.v[3] = p.caps; it.v[4] = p.rule; it.v[5] = p.fmt; it.v[6] = p.env;
            c.n++; added++;
        }
        ed_save(page);
        sprintf_s(msg, added ? "Added %d ready-made modes (16 at most)" : c.n >= ed_cap(page) ? "The list is full (16): delete one first" : "You have every ready-made mode already", added);
        set_status(msg); rumble(1);
        return;
    }
}

static bool epg_pending_step(int page);                        // v5.29 cwo_edmenu.inl: an action asked for in the game's menu
static void ed_input_page(WORD b, WORD pressed);
// v5.152 a page opened from the SINGLE PLAY lobby (Spectator Mode, Draft Arsenal) goes back to the lobby when it leaves
static void ed_input(WORD b, WORD pressed) {
    LONG before = g_ed_page;
    ed_input_page(b, pressed);
    // v5.188.2 (owner 2026-11-15, BFR: OPTIONS > RECORDS, B "takes me back ... to the stage creator ... Edgar's Dream, create a
    // stage, skill, school ... It shouldn't show all that"): a page opened from OPTIONS closes on B - the game's OPTIONS list is
    // under it again; the CREATE chooser is never shown from there
    if (g_ed_from_opt && before != EDP_CHOOSER && g_ed_page == EDP_CHOOSER) {
        InterlockedExchange(&g_ed_from_opt, 0);
        ed_close();
        return;
    }
    LONG r = g_ed_ret_page;
    if (r && before != EDP_CHOOSER && before != r && g_ed_page == EDP_CHOOSER) {
        InterlockedExchange(&g_ed_page, r);
        InterlockedExchange(&g_ed_ret_page, 0);
    }
}
static void ed_input_page(WORD b, WORD pressed) {
    ed_load_all();
    if (epg_pending_step(g_ed_page)) return;                    // v5.29 (Single Play: its page is open now)
    static ULONGLONG rep_at;
    WORD lr = b & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN);
    ULONGLONG now = GetTickCount64();
    if (pressed & lr) rep_at = now + 400;                      // hold to repeat (v3.0 behaviour)
    else if (lr && now >= rep_at) { pressed |= lr; rep_at = now + 80; }
    // v5.112 (owner 2026-11-07): a choice from the
    // game's EDGAR'S DREAM menu is pressed only on the chooser; the page left open last time (Create an Outfit at 17:15)
    // kept it waiting until the 6 s fallback. A pending choice starts from the chooser.
    if (g_ed_auto_choose && g_ed_page != EDP_CHOOSER) {
        InterlockedExchange(&g_ed_page, EDP_CHOOSER);
        InterlockedExchange(&g_ed_kb, 0);
        InterlockedExchange(&g_ed_help, 0);
        InterlockedExchange(&g_ed_cat, 0);
    }
    if (g_ed_kb) { ed_kb_input(pressed); return; }
    if (g_ed_page == EDP_LOBBY) { lob_input(pressed); return; }   // v5.2
    if (g_ed_help) { if (pressed & (BTN_Y | BTN_B | BTN_A)) InterlockedExchange(&g_ed_help, 0); return; }
    if (g_ed_cat && g_ed_page == EDP_CHAR) { ed_cat_input(pressed); return; }   // v5.35 My characters
    if (pressed & BTN_Y) { InterlockedExchange(&g_ed_help, 1); return; }   // the help card for this page
    int page = g_ed_page;
    if (page == EDP_FUSE) { fu_input(pressed); return; }       // v5.17
    if (page == EDP_RECORDS) { rc_input(pressed); return; }    // v5.21
    if (page == EDP_MUSIC) { mu_input(pressed); return; }      // v5.23
    if (page == EDP_VISIONS) { vi_input(pressed); return; }    // v5.86
    if (page == EDP_VAULT) { vt_input(pressed); return; }      // v5.133
    if (page == EDP_DRAFT) { dr_input(pressed); return; }      // v5.25
    if (page == EDP_RAID) { br_input(pressed); return; }       // v5.26
    if (page == EDP_STAGE) { cs_input(pressed); return; }      // v5.32 CREATE A STAGE
    if (page == EDP_CHOOSER) {
        if (InterlockedExchange(&g_ed_auto_choose, 0)) pressed = BTN_A;   // v5.27 chosen in the game's Edgar's Dream menu
        if (!ed_allowed(g_ed_sel)) InterlockedExchange(&g_ed_sel, 0);   // v5.58 the BFR edition: only its entries
        if (pressed & (BTN_DOWN | BTN_UP)) {
            LONG s2 = g_ed_sel;
            for (int i = 0; i < ED_N; i++) { s2 = (pressed & BTN_DOWN) ? (s2 + 1) % ED_N : (s2 + ED_N - 1) % ED_N; if (ed_allowed(s2)) break; }
            InterlockedExchange(&g_ed_sel, s2);
        }
        if (pressed & BTN_B) { ed_close(); InterlockedExchange(&g_ed_mode, 0); ed_state_save(); return; }
        if (!(pressed & BTN_A)) return;
        int s = g_ed_sel;
        if (s == 0) {
            InterlockedExchange(&g_ed_mode, 0); ed_close(); ed_state_save();
            if (g_cs_go >= 0) { InterlockedExchange(&g_ed_auto, 31); return; }   // v5.32 chosen on the CREATE A STAGE page
            set_status("Create a Stage: pick the stage (LB/RB: templates)"); rumble(1); return;
        }
        if (s == 1) {
            int k = -1;
            for (int i = 0; i < g_mx_count; i++) if (!strcmp(g_mx[i].file, "pcblank")) k = i;
            if (k < 0) { set_status("The Skill Test Stage is not installed"); return; }
            InterlockedExchange(&g_mx_sel, k);
            mx_apply(k);
            f_cursor_slot7();
            InterlockedExchange(&g_ed_mode, 1);
            ed_close();
            ed_state_save();
            InterlockedExchange(&g_ed_auto, 11);                // v4.4: the stage and 'debug' by the loader; the deck is yours
            set_status("Loading the Skill Test Stage - pick your arsenal");
            rumble(1);
            return;
        }
        if (s == 7) { InterlockedExchange(&g_ed_page, EDP_FUSE); rumble(1); return; }   // v5.17 the Skill Fuser
        if (s == 8) { InterlockedExchange(&g_ed_page, EDP_RECORDS); rumble(1); return; }   // v5.21 Records
        if (s == 9) { InterlockedExchange(&g_ed_page, EDP_MUSIC); rumble(1); return; }     // v5.23 Stage Music
        if (s == 13) { InterlockedExchange(&g_ed_page, EDP_VISIONS); rumble(1); return; }  // v5.86 Visions
        if (s == 12) { InterlockedExchange(&g_ed_page, EDP_OUTFIT); InterlockedExchange(&g_ed_row, g_ed_rows_sel[EDP_OUTFIT]);
                       if (g_edc[EDP_OUTFIT].n > 0 && !g_co_prev) co_prev_request();   // v5.69 the model shown from the start
                       rumble(1); return; }   // v5.61
        if (s == 10) { InterlockedExchange(&g_ed_page, EDP_DRAFT); rumble(1); return; }    // v5.25 Draft Arsenal
        if (s == 11) { InterlockedExchange(&g_ed_page, EDP_RAID); rumble(1); return; }     // v5.26 Boss Raid
        static const int TO[ED_N] = {0, 0, EDP_SCHOOL, EDP_CHAR, EDP_QUEST, EDP_MODE, EDP_SPECT, 0, 0, 0, 0, 0, 0, 0};
        InterlockedExchange(&g_ed_page, TO[s]);
        InterlockedExchange(&g_ed_row, g_ed_rows_sel[TO[s]]);
        if (TO[s] == EDP_CHAR && !g_cc_prev) cc_prev_request();   // v3.8: the character is shown as soon as the page opens
        if (TO[s] == EDP_CHAR) { InterlockedExchange(&g_ed_cat_sel, g_edc[EDP_CHAR].cur); InterlockedExchange(&g_ed_cat, 1); }   // v5.35
        rumble(1);
        return;
    }
    EdCreator &c = g_edc[page];
    int rows = ed_rows(page), row = g_ed_row;
    if (pressed & BTN_DOWN) row = (row + 1) % rows;
    if (pressed & BTN_UP) row = (row + rows - 1) % rows;
    InterlockedExchange(&g_ed_row, row);
    g_ed_rows_sel[page] = row;
    if (pressed & BTN_B) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); ed_state_save(); return; }
    if (pressed & BTN_LEFT) ed_change(page, row, -1);
    if (pressed & BTN_RIGHT) ed_change(page, row, 1);
    if ((pressed & BTN_X) && row >= 2 && row - 2 < c.nf) {     // back to the default value
        c.item[c.cur].v[ed_fld(page, row)] = c.f[ed_fld(page, row)].def;
        ed_save(page);
        if (page == EDP_CHAR && g_cc_prev == 2) cc_mark_dirty();
        set_status("Back to the default");
    }
    if (!(pressed & BTN_A)) return;
    if (row == 0) {                                            // A on the item row: a new one, a copy of this one
        if (c.n >= ed_cap(page)) { set_status(page == EDP_CHAR ? "8 is the most: change one instead" : "16 is the most: change one instead"); return; }
        c.item[c.n] = c.item[c.cur];
        sprintf_s(c.item[c.n].name, "My %s %d", c.word, c.n + 1);
        c.cur = c.n++;
        ed_save(page);
        set_status("New one made (a copy) - A on Name to rename it");
        return;
    }
    if (row == 1) { ed_kb_open(c.item[c.cur].name); return; }
    if (row >= 2 + c.nf) ed_action(page, row - 2 - c.nf);
}

// ---------------------------------------------------------------- drawing (inside the forge overlay's text pass)
template <class PUT, class BOX> static void ed_draw(PUT &put, BOX &box, int x, int y, bool status, const char *st) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREEN = 0xFF80FF80, GREY = 0xFFB0B0B0;
    const int LINE = 16;
    char line[128], val[64];
    ed_load_all();
    int page = g_ed_page;
    if (page == EDP_LOBBY) { lob_draw(put, box, x - 16, y - 8); return; }   // v5.2 the 3 VS 3 LOBBY
    if (page == EDP_FUSE) { fu_draw(put, box, x, y); return; }              // v5.17 the Skill Fuser
    if (page == EDP_RECORDS) { rc_draw(put, box, x, y); return; }           // v5.21 Records
    if (page == EDP_MUSIC) { mu_draw(put, box, x, y); return; }             // v5.23 Stage Music
    if (page == EDP_VISIONS) { vi_draw(put, box, x, y); return; }           // v5.86 Visions
    if (page == EDP_VAULT) { vt_draw(put, box, x, y); return; }             // v5.133 Skill Vault
    if (page == EDP_DRAFT) { dr_draw(put, box, x, y); return; }             // v5.25 Draft Arsenal
    if (page == EDP_RAID) { br_draw(put, box, x, y); return; }              // v5.26 Boss Raid
    if (page == EDP_STAGE) { cs_draw(put, box, x, y); return; }             // v5.32 CREATE A STAGE
    if (g_ed_help) {                                           // the help card (owner)
        const char *const *h = page == EDP_CHOOSER ? CHOOSER_H : g_edc[page].howto;
        put(x, y, h[0], GOLD);
        y += LINE + 4;
        for (int i = 1; h[i]; i++, y += LINE) put(x, y, h[i], WHITE);
        put(x, y + 4, "Y / A / B: close this help", BLUE);
        return;
    }
    if (g_ed_kb) {
        EdCreator &c = g_edc[page];
        sprintf_s(line, "NAME THIS %s:  %s_", c.word, g_ed_kbuf);
        for (char *p = line; *p && p < line + 16; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
        put(x, y, line, GOLD);
        y += LINE + 4;
        int nkeys = (int)strlen(ED_KEYS);
        for (int r = 0; r * ED_KCOLS < nkeys; r++, y += LINE) {
            for (int col = 0; col < ED_KCOLS && r * ED_KCOLS + col < nkeys; col++) {
                char ch = ED_KEYS[r * ED_KCOLS + col];
                if (g_ed_klower && ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                char s2[4] = {ch == ' ' ? '_' : ch, 0};
                bool on = r == g_ed_ky && col == g_ed_kx;
                put(x + 24 * col, y, s2, on ? GOLD : WHITE);
                if (on) put(x + 24 * col - 8, y, ">", GOLD);
            }
        }
        put(x, y + 4, "A type  X delete  Y a/A  B or Start: done", BLUE);
        return;
    }
    if (g_ed_cat && page == EDP_CHAR) {                        // v5.35 My characters: every saved one
        EdCreator &c = g_edc[EDP_CHAR];
        sprintf_s(line, "MY CHARACTERS  %d/%d         (B: back)", c.n, ed_cap(EDP_CHAR));
        put(x, y, line, GOLD);
        y += LINE + 4;
        int rows = ed_cat_rows(), sel = g_ed_cat_sel;
        const char *desc = "A: play as it now   Y: change it   X twice: delete it";
        for (int r = 0; r < rows; r++, y += LINE) {
            if (r < c.n) {
                const EdItem &it = c.item[r];
                char sch[40] = "";
                ed_value(EDP_CHAR, CH_SCHOOL, it.v[CH_SCHOOL], sch, sizeof(sch));
                sprintf_s(val, "%s, %.16s%s", ED_BODIES[it.v[CH_BODY]], sch, g_ed_char_on == r ? "  IN USE" : "");
                sprintf_s(line, "%s %-22.22s %s", r == sel ? ">" : " ", it.name, val);
                put(x, y, line, r == sel ? GOLD : WHITE);
            } else if (r == c.n && c.n < ed_cap(EDP_CHAR)) {
                sprintf_s(line, "%s %s", r == sel ? ">" : " ", "+ New character");
                put(x, y, line, r == sel ? GOLD : BLUE);
                if (r == sel) desc = "A: make a new character";
            } else {
                sprintf_s(line, "%s %s", r == sel ? ">" : " ", "Back");
                put(x, y, line, r == sel ? GOLD : BLUE);
                if (r == sel) desc = "A: the Create a Character rows";
            }
        }
        put(x, y + 2, desc, BLUE);
        if (status) put(x, y + 2 + LINE, st, GREEN);
        return;
    }
    if (page == EDP_CHOOSER) {
        put(x, y, "EDGAR'S DREAM          (A: open   Y: help)", GOLD);
        y += LINE + 4;
        for (int r = 0; r < ED_N; r++) {
            if (!ed_allowed(r)) continue;                           // v5.58 the BFR edition
            sprintf_s(line, "%s %s", r == g_ed_sel ? ">" : " ", ED_NAMES[r]);
            put(x, y, line, r == g_ed_sel ? GOLD : WHITE);
            y += LINE;
        }
        put(x, y + 2, ED_DESC[g_ed_sel], BLUE);
        y += LINE + 2;
        LONG m = g_ed_mode_on, s = g_ed_school_on, ch = g_ed_char_on;   // what is in use, at a glance
        sprintf_s(line, "In use - mode: %.18s  school: %.14s  character: %.14s", m >= 0 ? g_edc[EDP_MODE].item[m].name : "game",
                  s >= 0 ? g_edc[EDP_SCHOOL].item[s].name : "-", ch >= 0 ? g_edc[EDP_CHAR].item[ch].name : "-");
        put(x, y + 2, line, GREY);
        if (status) put(x, y + 2 + LINE, st, GREEN);
        return;
    }
    EdCreator &c = g_edc[page];
    sprintf_s(line, "%s                 (B: back   Y: help)", c.title);
    put(x, y, line, GOLD);
    y += LINE + 4;
    int rows = ed_rows(page), sel = g_ed_row, win = 11;
    int top = sel >= win ? sel - (win - 1) : 0;
    const char *desc = "";
    for (int r = top; r < rows && r < top + win; r++, y += LINE) {
        const char *label;
        val[0] = 0;
        if (r == 0) {
            label = c.word;
            bool use = (page == EDP_MODE && g_ed_mode_on == c.cur) || (page == EDP_SCHOOL && g_ed_school_on == c.cur) ||
                       (page == EDP_CHAR && g_ed_char_on == c.cur);
            sprintf_s(val, "< %.20s >  %d/%d%s", c.item[c.cur].name, c.cur + 1, c.n, use ? "  IN USE" : "");
            if (r == sel) desc = "L/R: choose   A: make a new one (a copy)";
        } else if (r == 1) {
            label = "Name";
            sprintf_s(val, "%.22s", c.item[c.cur].name);
            if (r == sel) desc = "A: the keyboard";
        } else if (r - 2 < c.nf) {
            int fi = ed_fld(page, r);
            label = c.f[fi].label;
            ed_value(page, fi, c.item[c.cur].v[fi], val, sizeof(val));
            if (r == sel) desc = c.f[fi].help;
        } else {
            label = c.actions[r - 2 - c.nf];
            if (r == sel) desc = "A: do it";
        }
        // v5.175 (owner 2026-11-13; in game the Create an Outfit / Character preview's long values ran
        // over the turning model): beside the model, a narrower label column and every value fitted to 25 characters - cut at
        // its bracket or a word, never mid-word - and the help line wrapped at 44
        bool pv = g_cc_prev == 2 || g_co_prev == 2;
        if (pv && strlen(val) > 25) {
            char *br = strstr(val, " (");
            if (br && br - val >= 8 && br - val <= 25) *br = 0;
            else { val[25] = 0; char *sp = strrchr(val, ' '); if (sp && sp - val >= 10) *sp = 0; }
        }
        sprintf_s(line, pv ? "%s %-18s %s" : "%s %-22s %s", r == sel ? ">" : " ", label, val);
        put(x, y, line, r == sel ? GOLD : r >= 2 + c.nf ? BLUE : WHITE);
    }
    if ((g_cc_prev == 2 || g_co_prev == 2) && strlen(desc) > 44) {   // v5.175 two lines beside the model
        char d1[96]; strncpy_s(d1, desc, _TRUNCATE);
        size_t cut = 44; while (cut > 20 && d1[cut] != ' ') cut--;
        if (cut <= 20) cut = 44;
        const char *d2 = desc + cut; while (*d2 == ' ') d2++;
        d1[cut] = 0;
        put(x, y + 2, d1, BLUE); put(x, y + 2 + LINE, d2, BLUE); y += LINE;
    } else put(x, y + 2, desc, BLUE);
    if (status) put(x, y + 2 + LINE, st, GREEN);
    if (page == EDP_CHAR) {                                    // v3.6 the character sheet: what will be built
        const EdItem &it = c.item[c.cur];
        int by = y + 2 + 2 * LINE + 6, base = it.v[CH_MOVES];
        char why[96];
        sprintf_s(line, "LOOK  %s body, color %d%s%s, %s", ED_BODIES[it.v[CH_BODY]], it.v[CH_COLOUR],
                  it.v[CH_HEAD] >= 0 ? ", head of " : "", it.v[CH_HEAD] >= 0 ? ED_BODIES[it.v[CH_HEAD]] : "",
                  it.v[CH_SIZE] ? "height fitted" : "own height");
        put(x, by, line, WHITE);
        int mixed = 0;
        for (int g = 0; g < CH_GN; g++) mixed += it.v[CH_G0 + g] >= 0 && it.v[CH_G0 + g] != base;
        sprintf_s(line, "MOVES %s, %d group(s) from others. SLOT: top right of the select%s", ED_BODIES[base], mixed,
                  it.v[CH_WEAR] > 0 ? " + an outfit" : "");
        put(x, by + LINE, line, WHITE);
        if (!ed_char_fit(it, why, sizeof(why))) put(x, by + 2 * LINE, why, 0xFFFF6060);
    }
    if (page == EDP_MODE && c.n) {                             // v4.8 what this mode does, exactly
        char l1[160], l2[160], l3[160];
        ed_mode_desc(c.item[c.cur], l1, l2, l3, sizeof(l1));
        int by = y + 2 + 2 * LINE + 6;
        put(x, by, l1, WHITE);
        put(x, by + LINE, l2, WHITE);
        put(x, by + 2 * LINE, l3, WHITE);
        int f = c.item[c.cur].v[5];
        if (f == 7 && g_sixf2_state == 1)                          // v5.1 six fighters step 2 is in
            put(x, by + 3 * LINE, "3 vs 3: '3 VS 3 LOBBY' - stage, then character, outfit, arsenal and team for all 6.", 0xFF60FF60);
        else if (f == 7) put(x, by + 3 * LINE, "3 vs 3 needs six fighters on (CwoSixFighters.txt): off or refused, see the log.", 0xFFFF6060);
        else if (f == 8) put(x, by + 3 * LINE, "Survival: any offline match - the CPUs keep coming back, tougher every wave.", 0xFF60FF60);
        else if (f == 9) put(x, by + 3 * LINE, "Shrinking arena: any offline match - stay inside the closing ring (1 life a second outside).", 0xFF60FF60);
        else if (f == 10) put(x, by + 3 * LINE, "King of the Hill: any offline match - hold the glowing zone alone; 60 points wins.", 0xFF60FF60);
        else if (f != 0) put(x, by + 3 * LINE, "This player set-up comes with the next update; 1 vs 1 plays now.", 0xFFFFD040);
    }
    if (page == EDP_SCHOOL) {
        // v3.5.1 (owner):
        // every hue in a bar, a marker on yours, and a swatch of your colour at your strength and brightness
        const EdItem &it = c.item[c.cur];
        int hue = it.v[1], sat = it.v[2], bri = it.v[3], by = y + 2 + 2 * LINE + 6;
        put(x, by, "Color", WHITE);
        for (int k = 0; k < 36; k++) box(x + 64 + 8 * k, by, ed_aura_rgb(k * 10, 100, 100, 255.0f), 4, 16);
        if (hue >= 0) {
            put(x + 64 + 8 * (hue / 10), by + 16, "^", WHITE);
            box(x + 64 + 8 * 36 + 16, by - 4, ed_aura_rgb(hue, sat, bri, 255.0f), 12, 24);
            sprintf_s(line, "hue %d  strength %d%%  brightness %d%%   (row 'Aura color': L/R moves the marker)", hue, sat, bri);
        } else {
            sprintf_s(line, "game color: the school's own   (row 'Aura color': L/R picks a hue)");
        }
        put(x, by + 32, line, GREY);
    }
}

// ---------------------------------------------------------------- in-match line: the mode and the lives (Create a Mode goal)
template <class PUT> static void ed_draw_match(PUT &put, int x, int y) {
    LONG k = g_ed_mode_on;
    if (k < 0 || k >= g_edc[EDP_MODE].n || g_forge_active) return;
    const EdItem &m = g_edc[EDP_MODE].item[k];
    char line[96];
    int lives = m.v[2];
    if (g_six_match)                                           // v5.6 all six in a 3 vs 3
        sprintf_s(line, "MODE %.12s  lives P1 %d P2 %d P3 %d P4 %d P5 %d P6 %d", m.name, lives - g_ed_deaths[0],
                  lives - g_ed_deaths[1], lives - g_ed_deaths[2], lives - g_ed_deaths[3], lives - g_ed_deaths[4], lives - g_ed_deaths[5]);
    else
        sprintf_s(line, "MODE %.20s  lives P1 %d  P2 %d  P3 %d  P4 %d", m.name, lives - g_ed_deaths[0], lives - g_ed_deaths[1],
                  lives - g_ed_deaths[2], lives - g_ed_deaths[3]);
    put(x, y, line, 0xFFFFD040);
}

// ---------------------------------------------------------------- self-test (stand-in game: GSDATA, code bytes, a player)
// v4.4 the reader on a save whose header is longer than any buffer (the 'ace)' item of 2026-10-25): one item, its name whole
static int ed_reader_self_test() {
    EdCreator &c = g_edc[EDP_CHAR];
    EdCreator keep = c;
    EdItem keep_items[8];
    memcpy(keep_items, c.item, sizeof(keep_items));
    wchar_t p[MAX_PATH];
    ed_path("ed_reader_test.txt", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return -40;
    fputc('#', f);
    for (int i = 0; i < 3000; i++) fputc('x', f);
    fprintf(f, "ace)\nBLAZY\t0\t3\n");
    fclose(f);
    c.file = "ed_reader_test.txt";
    ed_load(EDP_CHAR);
    bool ok = c.n == 1 && !strcmp(c.item[0].name, "BLAZY") && c.item[0].v[1] == 3;
    c = keep;
    memcpy(c.item, keep_items, sizeof(keep_items));
    DeleteFileW(p);
    logf("self-test: creator file reader (over-long header) %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -41;
}

static int ed_self_test() {
    static char gs[0x34000];
    static unsigned char code[3] = {3, 3, 3};
    static unsigned char player[0x18000];
    static ULONG_PTR tab[4];
    int *t = (int *)(gs + RULE_TABLE);
    for (int r = 0; r < RULE_N; r++) { t[4 * r] = 20; t[4 * r + 1] = 0; t[4 * r + 2] = 99; t[4 * r + 3] = 10; }
    g_ed_test_gs = gs; g_ed_test_code = code;
    tab[0] = (ULONG_PTR)player;
    g_actor_test_tab = tab;
    // the stand-in revive: stands the player up and halves the cap, as FUN_140251f80 does
    struct R { static void __fastcall rev(ULONG_PTR r) { *(int *)(r + 0x24) = 1; *(int *)(r + 0x12F04) = 0; *(int *)(r + 0x50) = (*(int *)(r + 0x50) + 1) / 2; *(int *)(r + 0x4C) = *(int *)(r + 0x50); } };
    g_ed_revive_test = R::rev;
    for (int p = 1; p < EDP_N; p++) { g_edc[p].n = 0; g_edc[p].cur = 0; }
    g_ed_mode_on = -1; g_ed_loaded = true;                    // keep ed_step's loading out of the test
    LONG s_forge = g_forge_active; g_forge_active = 0;
    // presets, keyboard, save / load round trip (to a test file name)
    const char *real = g_edc[EDP_MODE].file;
    g_edc[EDP_MODE].file = "CwoModes_test.txt";
    wchar_t tp[MAX_PATH];
    ed_path("CwoModes_test.txt", tp);
    DeleteFileW(tp);
    ed_load(EDP_MODE);
    bool presets = g_edc[EDP_MODE].n == 4 && g_edc[EDP_MODE].item[1].v[0] == 15 && g_edc[EDP_MODE].item[1].v[2] == 3 && g_edc[EDP_MODE].item[1].v[3] == 3;   // 3 vs 3: 3 capsules since v5.188
    g_ed_page = EDP_MODE; g_edc[EDP_MODE].cur = 1;
    ed_kb_open("");
    g_ed_kx = 2; ed_kb_input(BTN_A);                             // C
    g_ed_ky = 0; g_ed_kx = 0; ed_kb_input(BTN_A);                // A
    ed_kb_input(BTN_X);                                         // delete A
    ed_kb_input(BTN_B);                                         // done
    bool kb = !strcmp(g_edc[EDP_MODE].item[1].name, "C") && !g_ed_kb;
    ed_load(EDP_MODE);
    bool round = g_edc[EDP_MODE].n == 4 && !strcmp(g_edc[EDP_MODE].item[1].name, "C") && g_edc[EDP_MODE].item[1].v[0] == 15;
    // values wrap and default
    g_ed_row = 2; ed_change(EDP_MODE, 2, 1); bool step = g_edc[EDP_MODE].item[0].v[0] == 11;   // cur 0 after load
    ed_change(EDP_MODE, 2, -1);
    // arming writes the rule table and the 3 code bytes; disarming puts the game's back
    g_in_match = 0;
    ed_mode_arm(1);
    bool armed = t[3] == 15 && t[4 * 12 + 3] == 15 && t[0] == 20 && code[0] == 3 && code[1] == 3 && code[2] == 3 && g_ed_mode_on == 1;   // always 3 capsules since v5.188.3
    // lives: 3 lives -> two KOs come back at full life, the third is left to the game
    *(int *)(player + 0x20) = 1; *(int *)(player + 0x24) = 1; *(int *)(player + 0x50) = 20; *(int *)(player + 0x4C) = 20;
    g_in_match = 1; ed_lives_step();
    bool lives_ok = true;
    for (int ko = 1; ko <= 3; ko++) {
        *(int *)(player + 0x24) = 0; *(int *)(player + 0x12F04) = 0x1B; *(int *)(player + 0x12F08) = 1; *(int *)(player + 0x4C) = 0;
        *(int *)(player + 0x1396C) = 5;                         // still falling: wait
        ed_lives_step();
        lives_ok &= *(int *)(player + 0x12F08) == 3 || ko == 3;
        lives_ok &= *(int *)(player + 0x24) == 0;
        *(int *)(player + 0x1396C) = -1; *(int *)(player + 0x13968) = 0;
        ed_lives_step();
        if (ko < 3) lives_ok &= *(int *)(player + 0x24) == 1 && *(int *)(player + 0x50) == 20 && *(int *)(player + 0x4C) == 20;
        else lives_ok &= *(int *)(player + 0x24) == 0 && g_ed_deaths[0] == 3;   // out: untouched
    }
    g_in_match = 0; ed_lives_step();
    ed_mode_arm(-1);
    bool disarmed = t[3] == 10 && t[0] == 20 && code[0] == 3 && code[1] == 3 && code[2] == 3 && g_ed_mode_on == -1;
    // the capsule bytes are left alone when they are not the game's
    code[1] = 7;
    ed_mode_arm(1);
    bool guard = code[0] == 3 && code[1] == 7 && code[2] == 3;
    ed_mode_arm(-1);
    code[1] = 3;
    // aura colour: hue 0 full strength is red, dark (added on top)
    DWORD red = ed_aura_rgb(0, 100, 100);
    bool colour = red == 0xFFB00000;
    // spectator: View + LB -> the brain is set up once, then only +0x30 changes; a full arena is refused
    struct A { static void __fastcall init(ULONG_PTR b, ULONG_PTR, ULONG_PTR rec) {
        g_ed_ai_calls_test++; *(int *)b = *(const char *)(rec + 0x46) ? 1 : 0; } };
    g_ed_ai_calls_test = 0;
    g_ed_ai_init_test = A::init; g_ed_ai_on_test = 1;
    memset(player, 0, sizeof(player));
    *(int *)(player + 0x20) = 1; *(int *)(player + 0x30) = 1;
    *(ULONGLONG *)(player + 0x12c48) = 0x100000; *(ULONGLONG *)(player + 0x12c50) = 0x110000; *(ULONGLONG *)(player + 0x12c58) = 0x60000;
    g_edc[EDP_SPECT].n = 1; ed_defaults(EDP_SPECT, g_edc[EDP_SPECT].item[0], "t"); g_edc[EDP_SPECT].item[0].v[1] = 6;
    g_in_match = 1;
    ed_spectator_step();
    bool sp_idle = *(int *)(player + 0x30) == 1 && g_ed_ai_calls_test == 0;
    g_ed_spec_req = 1; ed_spectator_step();
    bool sp_cpu = *(int *)(player + 0x30) == 0 && g_ed_ai_calls_test == 1 && !strcmp(g_ed_spec_rec + 0x46, "eneprog006.ssb") && !g_ed_spec_rec[0x31];
    // v5.9 the program is polled until it runs (the stand-in starts it on the 2nd poll), then left alone
    struct P { static int __fastcall poll(ULONG_PTR sub) { if (++g_ed_ai_polls_test >= 2) *(int *)(sub + AI_STARTED) = 1; return 0; } };
    g_ed_ai_poll_test = P::poll; g_ed_ai_polls_test = 0;
    ed_spectator_step(); ed_spectator_step(); ed_spectator_step();
    bool sp_run = g_ed_ai_polls_test == 2 && *(int *)(player + AI_SUB + AI_STARTED) == 1;
    sp_cpu = sp_cpu && sp_run;
    g_ed_spec_req = 1; ed_spectator_step();
    bool sp_you = *(int *)(player + 0x30) == 1;
    g_ed_spec_req = 1; ed_spectator_step();
    bool sp_again = *(int *)(player + 0x30) == 0 && g_ed_ai_calls_test == 1;   // no second set-up
    *(int *)(player + 0x1c) = 0; g_ed_spec_t_code = 2;
    g_ed_spec_t_win = 0; ed_spectator_step();                  // v5.188.42 the match is decided: player 1 back for the result
    sp_again = sp_again && *(int *)(player + 0x30) == 1 && !g_ed_spec_mine && g_ed_spec_t_code == 1;   // v5.188.44 + the win code
    g_ed_spec_t_win = -1;
    g_in_match = 0; ed_spectator_step();                        // match over; a new one, "starts as a CPU" on
    memset(player + 0x142d0, 0, 4); *(int *)(player + 0x30) = 1;
    g_edc[EDP_SPECT].item[0].v[0] = 1;
    g_in_match = 1; ed_spectator_step();
    bool sp_auto = *(int *)(player + 0x30) == 0 && g_ed_ai_calls_test == 2;
    g_in_match = 0; ed_spectator_step();
    memset(player + 0x142d0, 0, 4); *(int *)(player + 0x30) = 1;
    *(ULONGLONG *)(player + 0x12c50) = 0x15c000;                // arena nearly full
    g_in_match = 1; ed_spectator_step();
    bool sp_full = *(int *)(player + 0x30) == 1 && g_ed_ai_calls_test == 2;
    g_ed_ai_on_test = 0; g_in_match = 0; ed_spectator_step();
    memset(player + 0x142d0, 0, 4); *(ULONGLONG *)(player + 0x12c50) = 0x110000;
    g_in_match = 1; ed_spectator_step();
    bool sp_off = *(int *)(player + 0x30) == 1 && g_ed_ai_calls_test == 2;   // the game's CPU data not loaded: left alone
    g_in_match = 0; ed_spectator_step();
    g_ed_ai_init_test = NULL; g_ed_ai_poll_test = NULL;
    bool spect = sp_idle && sp_cpu && sp_you && sp_again && sp_auto && sp_full && sp_off;
    // v3.5.1: the main menu's A held when the chooser opens is not taken; after release the chooser works
    g_ed_wait_release = 1;
    bool gate = ed_release_gate(BTN_A) && g_ed_wait_release && ed_release_gate(0) && !g_ed_wait_release && !ed_release_gate(BTN_A);
    LONG s_open = g_ed_open; g_ed_open = 1; ed_close();
    gate &= !g_ed_open && g_ed_game_hold;
    g_ed_open = s_open; g_ed_game_hold = 0;
    spect &= gate;
    // v3.6: the character sheet's fit check (Freia / Tsubutaki moves only on their own bodies; a head needs the Player body)
    EdItem ch; memset(&ch, 0, sizeof(ch));
    for (int i = 0; i < CH_NF; i++) ch.v[i] = CHAR_F[i].def;
    char why[96];
    bool fit = ed_char_fit(ch, why, sizeof(why));
    ch.v[CH_G0 + 6] = 1; fit &= ed_char_fit(ch, why, sizeof(why));            // Edgar's evade on the Player: fits
    ch.v[CH_G0 + 8] = 2; fit &= !ed_char_fit(ch, why, sizeof(why));           // v5.10 Freia's float on the Player: refused (rule)
    ch.v[CH_G0 + 8] = -1;
    ch.v[CH_G0 + 9] = 8; fit &= !ed_char_fit(ch, why, sizeof(why));           // Tsubutaki's knock-down on the Player: refused
    ch.v[CH_G0 + 9] = -1;
    // v5.10 owner HARD RULE: parts and moves stay in their half (female: Freia 2, Tsubutaki 8, Mikan 16)
    fit &= cb_part_allowed(2, 8) && cb_part_allowed(16, 2) && !cb_part_allowed(2, 1) && !cb_part_allowed(0, 2) &&
           !cb_part_allowed(2, 15) && cb_moves_allowed(16, 13) && !cb_moves_allowed(2, 12) && cb_moves_allowed(0, 12) &&
           !cb_moves_allowed(0, 19) && cb_moves_allowed(2, -1);
    ch.v[CH_BODY] = 3; ch.v[CH_MOVES] = 12; ch.v[CH_HEAD] = 10;
    fit &= ed_char_fit(ch, why, sizeof(why));                                 // OG Edgar on Meister with Sammah's head
    char vb[64]; ed_value(EDP_CHAR, CH_S0, 33, vb, sizeof(vb)); fit &= !strcmp(vb, "33 DASH_A");
    spect &= fit;
    logf("self-test: character sheet fit check %s", fit ? "ok" : "FAILED");
    DeleteFileW(tp);
    g_edc[EDP_MODE].file = real;
    g_ed_test_gs = NULL; g_ed_test_code = NULL; g_actor_test_tab = NULL; g_ed_revive_test = NULL;
    g_ed_mode_on = -1; g_ed_page = 0; g_ed_row = 0; g_in_match = 0;
    for (int p = 1; p < EDP_N; p++) { g_edc[p].n = 0; g_edc[p].cur = 0; }
    g_ed_loaded = false; g_ed_loading = 0; g_forge_active = s_forge;
    logf("self-test: Edgar's Dream presets %s, keyboard %s, save+load %s, step %s, mode arm %s, lives %s, disarm %s, "
         "capsule guard %s, aura color %s (%08lX), spectator %s%s%s%s%s%s%s, chooser gate %s", presets ? "ok" : "FAILED", kb ? "ok" : "FAILED", round ? "ok" : "FAILED",
         step ? "ok" : "FAILED", armed ? "ok" : "FAILED", lives_ok ? "ok" : "FAILED", disarmed ? "ok" : "FAILED",
         guard ? "ok" : "FAILED", colour ? "ok" : "FAILED", (unsigned long)red,
         sp_idle ? "1" : "x", sp_cpu ? "2" : "x", sp_you ? "3" : "x", sp_again ? "4" : "x", sp_auto ? "5" : "x", sp_full ? "6" : "x", sp_off ? "7" : "x", gate ? "ok" : "FAILED");
    return presets && kb && round && step && armed && lives_ok && disarmed && guard && colour && spect ? 1 : -30;
}

static int ed_quest_self_test() {                              // v5.13 Create a Quest v2: order, values, pictures, outfits, old saves
    const EdCreator &qc = g_edc[EDP_QUEST];
    bool order = qc.nf == 31 && (int)(sizeof(QUEST_ORDER) / sizeof(*QUEST_ORDER)) == qc.nf;
    for (int f = 0; order && f < qc.nf; f++) {
        int seen = 0;
        for (int r = 0; r < qc.nf; r++) seen += QUEST_ORDER[r] == f;
        order = seen == 1 && ed_fld(EDP_QUEST, 2 + f) == QUEST_ORDER[f];
    }
    char v[64];
    bool vals = true;
    ed_value(EDP_QUEST, 13, 2, v, sizeof(v)); vals &= !strcmp(v, "Brutal");
    ed_value(EDP_QUEST, 19, 12, v, sizeof(v)); vals &= !strcmp(v, "you choose (any unlocked)");
    ed_value(EDP_QUEST, 21, -1, v, sizeof(v)); vals &= !strcmp(v, "none");
    ed_value(EDP_QUEST, 5, 23, v, sizeof(v)); vals &= !strcmp(v, "Guard (row 23)");
    ed_value(EDP_QUEST, 9, -1, v, sizeof(v)); vals &= !strcmp(v, "keep the quest's");
    ed_value(EDP_QUEST, 24, -1, v, sizeof(v)); vals &= !strcmp(v, "its own");
    ed_value(EDP_QUEST, 29, -1, v, sizeof(v)); vals &= !strcmp(v, "yourself");
    ed_value(EDP_QUEST, 29, 2, v, sizeof(v)); vals &= !strcmp(v, "Freia");
    bool pics = !ed_pic_ok(11) && !ed_pic_ok(105) && !ed_pic_ok(135) && ed_pic_ok(19) && ed_pic_ok(-1) && ed_pic_ok(143);
    int n = 0;
    for (int p = 0; p <= 143; p++) n += ed_pic_ok(p);
    pics &= n == 118;
    EdItem it;
    ed_defaults(EDP_QUEST, it, "t");
    int no = lob_outfits_of_row(2) > 0 ? lob_outfits_of_row(2) : 6;
    it.v[5] = 2; it.v[10] = no; it.v[11] = 31;
    ed_quest_outfits(it, 10, 1);
    bool outf = it.v[10] == 0 && it.v[11] == 0;
    it.v[10] = no; ed_quest_outfits(it, 10, -1);
    outf &= it.v[10] == no - 1;
    const char *real = g_edc[EDP_QUEST].file;                  // an 8-value save of v5.9 loads with the new defaults
    g_edc[EDP_QUEST].file = "CwoQuests_selftest.txt";
    wchar_t tp[MAX_PATH]; ed_path(g_edc[EDP_QUEST].file, tp);
    FILE *f = NULL;
    bool old = false;
    if (cwo_wfopen(&f, tp, L"w") == 0 && f) {
        fprintf(f, "Old\t20\t2\t10\t1\t2\t8\t9\t3\n");
        fclose(f);
        ed_load(EDP_QUEST);
        const EdItem &o = g_edc[EDP_QUEST].item[0];
        old = g_edc[EDP_QUEST].n == 1 && o.v[0] == 20 && o.v[6] == 9 && o.v[8] == 0 && o.v[9] == -1 && o.v[16] == 20 &&
              o.v[19] == 0 && o.v[21] == -1 && o.v[23] == 4 && o.v[24] == -1 && o.v[27] == 0 && o.v[28] == 20 && o.v[29] == -1;
        DeleteFileW(tp);
    }
    g_edc[EDP_QUEST].file = real; g_edc[EDP_QUEST].n = 0; g_edc[EDP_QUEST].cur = 0;
    logf("self-test: Create a Quest v2 order %s, values %s, pictures %s (%d), outfits %s, old saves %s", order ? "ok" : "FAILED",
         vals ? "ok" : "FAILED", pics ? "ok" : "FAILED", n, outf ? "ok" : "FAILED", old ? "ok" : "FAILED");
    return order && vals && pics && outf && old ? 1 : -74;
}

static int sv_self_test() {                                     // v5.22 Survival on stand-ins: the CPU always back, waves, the human's lives
    static char gs[0x34000];
    static unsigned char you[0x18000], cpu[0x18000];
    static ULONG_PTR tab[4];
    static signed char epa[4 + 10 * 86];
    memset(you, 0, sizeof(you)); memset(cpu, 0, sizeof(cpu)); memset(epa, 0, sizeof(epa));
    for (int j = 1; j <= 9; j++) { epa[4 + 10 * 82 + j] = (signed char)(10 + j); epa[4 + 10 * 83 + j] = (signed char)(20 + j); epa[4 + 10 * 84 + j] = (signed char)(30 + j); }
    tab[0] = (ULONG_PTR)you; tab[1] = (ULONG_PTR)cpu; tab[2] = tab[3] = 0;
    char *keep_gs = g_ed_test_gs; ULONG_PTR *keep_tab = g_actor_test_tab; PFN_REVIVE keep_rev = g_ed_revive_test;
    LONG keep_on = g_ed_mode_on, keep_in = g_in_match; int keep_n = g_edc[EDP_MODE].n; EdItem keep_item = g_edc[EDP_MODE].item[0];
    struct R { static void __fastcall rev(ULONG_PTR r) { *(int *)(r + 0x24) = 1; *(int *)(r + 0x12F04) = 0; *(int *)(r + 0x50) = (*(int *)(r + 0x50) + 1) / 2; *(int *)(r + 0x4C) = *(int *)(r + 0x50); } };
    g_ed_test_gs = gs; g_actor_test_tab = tab; g_ed_revive_test = R::rev; g_sv_t_tab = epa;
    EdItem &m = g_edc[EDP_MODE].item[0];
    memset(&m, 0, sizeof(m));
    m.v[1] = 20; m.v[2] = 3; m.v[5] = 8;                         // life 20, 3 lives, Survival
    g_edc[EDP_MODE].n = 1; g_ed_mode_on = 0;
    *(int *)(you + 0x20) = 1; *(int *)(you + 0x24) = 1; *(int *)(you + 0x30) = 1; *(int *)(you + 0x1c) = 0; *(int *)(you + 0x50) = 20;
    *(int *)(cpu + 0x20) = 1; *(int *)(cpu + 0x24) = 1; *(int *)(cpu + 0x30) = 0; *(int *)(cpu + 0x1c) = 1; *(int *)(cpu + 0x50) = 20;
    g_in_match = 0; ed_lives_step();
    g_in_match = 1; ed_lives_step();
    auto ko = [&](unsigned char *f) {                            // down, fallen, then the step that revives (or not)
        *(int *)(f + 0x24) = 0; *(int *)(f + 0x12F04) = 0x1B; *(int *)(f + 0x12F08) = 1; *(int *)(f + 0x1396C) = -1; *(int *)(f + 0x13968) = 0;
        ed_lives_step();
    };
    ko(cpu);
    const int *slot = (const int *)(cpu + 0x14408 + 0x20ec);
    bool wave2 = g_sv_wave == 2 && slot[0] == 11 && slot[8] == 19 && *(int *)(cpu + 0x24) == 1 && *(int *)(cpu + 0x50) == 24;
    ko(cpu);
    bool wave3 = g_sv_wave == 3 && slot[0] == 21 && *(int *)(cpu + 0x50) == 28;
    for (int i = 0; i < 12; i++) ko(cpu);
    bool later = g_sv_wave == 15 && slot[0] == 31 && *(int *)(cpu + 0x50) == 60 && *(int *)(cpu + 0x24) == 1;   // 60 at most
    for (int i = 0; i < 3; i++) ko(you);
    bool human = g_ed_deaths[0] == 3 && *(int *)(you + 0x24) == 0;   // your 3rd KO: out, the game's own end
    g_in_match = 0; ed_lives_step();
    bool reset = g_sv_wave == 1 && g_sv_kos == 0;
    g_ed_test_gs = keep_gs; g_actor_test_tab = keep_tab; g_ed_revive_test = keep_rev; g_sv_t_tab = NULL;
    g_ed_mode_on = keep_on; g_in_match = keep_in; g_edc[EDP_MODE].n = keep_n; g_edc[EDP_MODE].item[0] = keep_item;
    memset(g_ed_deaths, 0, sizeof(g_ed_deaths)); memset(g_ed_cap, 0, sizeof(g_ed_cap)); memset(g_ed_down, 0, sizeof(g_ed_down));
    logf("self-test: survival wave 2 %s, wave 3 %s, later waves %s, your lives %s, reset %s", wave2 ? "ok" : "FAILED", wave3 ? "ok" : "FAILED",
         later ? "ok" : "FAILED", human ? "ok" : "FAILED", reset ? "ok" : "FAILED");
    return wave2 && wave3 && later && human && reset ? 1 : -65;
}
