# Karasheka

![Gameplay: two computer karateka fight across the living room](docs/gameplay.gif)

A retro 2D karate game written in C++17 with SDL2. Two karateka fight it out
in an apartment: the living room, the kitchen, the bedroom and the study.
The floor is flat, but the furniture is fair game. Jump onto a chair, the
table or the fridge to get out of the way, then dive kick down on your
opponent. The higher you dive from, the harder it hits. Light furniture
breaks under a heavy landing.

Each fighter is played with nine keys laid out like a numeric keypad, plus
a strike key and a guard key, in the spirit of the one-button joystick
karate games of the 1980s: the nine directions on their own move, with strike
they attack, and with guard they block high, mid or low.

The whole game is drawn at 320 x 180 in crisp pixels and scaled up to the
window. Every fighter, room and piece of furniture is drawn by code, and every
sound (including the shouts) is synthesized, so the game ships with no asset
files.

A sharper 720p version of the clip above is in
[docs/gameplay.mp4](docs/gameplay.mp4).

## How to play

Each fighter has nine direction keys laid out like a numeric keypad, plus two
modifiers: **strike** and **guard**. Three key layouts are available, and no
two share a key. In two-player mode each player picks one in the menu. A
single player can use all of them at once, for example the numpad with the
right hand and Left Shift / Left Ctrl with the left.

| Layout | Directions | Strike | Guard |
| --- | --- | --- | --- |
| Left hand | `Q W E` / `A S D` / `Z X C` | Left Shift | Left Ctrl |
| Right hand | `Y U I` / `H J K` / `N M ,` | Space | L |
| Numpad | `7 8 9` / `4 5 6` / `1 2 3` | Right Shift (or Num 0) | Right Ctrl (or Num Enter) |

Holding two keys combines them, so 4 + 8 works like 7. Moving and jumping go
the way the key points on screen. Attacks are relative to your opponent:
"forward" is toward them, "back" is away. The tables below are for the
fighter on the left; for the fighter on the right, they mirror.

**On their own:**

| | | |
| --- | --- | --- |
| 7 jump back | 8 jump up | 9 jump forward |
| 4 walk back | 5 mid guard | 6 walk forward |
| 1 roll back | 2 crouch | 3 roll forward |

**With the strike key held** (or strike alone for the punch):

| | | |
| --- | --- | --- |
| 7 backflip kick | 8 uppercut | 9 flying kick |
| 4 roundhouse | 5 punch | 6 front kick |
| 1 low punch | 2 low kick | 3 sweep |

**With the guard key held:** up (7, 8 or 9) guards high, nothing (or the
middle row) guards mid, and down (1, 2 or 3) guards low. You stand still while
you guard.

**In the air:** strike is an air kick, and down + strike is a dive kick.

Keys meant together can be pressed in either order, up to 4 frames (about
1/15 s) apart: 8 then strike is still an uppercut rather than a jump, strike
then 6 is still a front kick rather than a punch, and 2, 6, strike is a sweep
rather than a roll. The same goes for the guard key (8 then guard is a high
guard, not a jump). A strike pressed a moment before you are free to act
(during a landing, say) still comes out.

| Key | Action |
| --- | --- |
| Esc | Pause (resume, show the keys, or quit to the menu) |
| F1 | Show or hide the keys (pauses the fight while open) |
| F2 | Sound on / off |
| F11 or Alt+Enter | Toggle fullscreen |

### The menu

The menu runs a computer fight in the background. Use the arrow keys to pick
a line and change it:

* **Mode:** 1 player (against the computer), 2 players on one keyboard, or
  watch two computers fight.
* **CPU:** Easy, Normal or Hard.
* **Keys:** the layout for each human player.
* **Rounds:** 1 round, best of 3 or best of 5.
* **Time:** 30, 60 or 99 seconds a round, or no limit.
* **Room:** tour the flat (a new room every round) or stay in one room.

Your choices are saved in `settings.ini` in the per-user folder SDL picks
(on Windows, `%APPDATA%\Karasheka\Karasheka\`).

## Rules

* Each fighter starts a round with 100 health. A round ends with a knockout,
  or when time runs out (the fighter with more health left wins). A double
  knockout or a tie gives both fighters the round.
* Every attack lands **high**, **mid** or **low**, and only the guard at
  that height blocks it (see the table below). Any attack from the air is
  high, and so is any attack from a fighter standing a good bit higher than
  you, such as on a table. An attack from below at a fighter standing on
  furniture is low. You can't guard in the air or with your back to the
  attacker.
* No attack lands sooner than 12 frames (200 ms) after the attacker starts
  it, or after they leave the ground for an attack from the air. A guard
  takes effect the moment you press it, so you can see an attack coming and
  pick the right guard in time.
* Crouching on its own blocks nothing, but high attacks (the roundhouse and
  the uppercut) pass over a crouching fighter. The sweep and the low kick go
  under a jump.
* Some hits knock you down. So does any hit in the air. You're invulnerable
  while you're down and while you get up.
* A roll passes under high attacks and through your opponent, so you come
  up on the other side.

### Moves

Frames are 1/60 s: how long a move takes to come out (startup), how long it
can hit (active), and how long you're stuck afterwards (recovery).

| Move | Keys | Startup / active / recovery | Damage | Guard with | Notes |
| --- | --- | --- | --- | --- | --- |
| Punch | strike (or 5 + strike) | 12 / 3 / 10 | 6 | mid | Reverse punch, the quickest. |
| Front kick | forward + strike | 14 / 4 / 13 | 9 | mid | Good reach. |
| Roundhouse | back + strike | 18 / 4 / 18 | 14 | high | Head height, steps in, knocks down. |
| Uppercut | 8 + strike | 13 / 5 / 16 | 11 | high | Anti-air, knocks down. |
| Low punch | down-back + strike | 12 / 3 / 9 | 5 | mid | A poke from a crouch. |
| Low kick | 2 + strike | 13 / 3 / 11 | 7 | low | |
| Sweep | down-forward + strike | 16 / 5 / 20 | 10 | low | Knocks down. |
| Flying kick | up-forward + strike | 12 / until landing | 12 | high | Leaps forward, knocks down. |
| Backflip kick | up-back + strike | 4 / 8 | 9 | high | Kicks up while flipping away; only reaches someone above you. |
| Air kick | strike in the air | 4 / until landing | 9 | high | Not before 8 frames in the air. |
| Dive kick | down + strike in the air | 3 / until landing | 10 to 24 | high | Steep dive, not before 9 frames in the air. Damage grows with height, knocks down. |

### Furniture

Every piece of furniture is a platform you land on from above. You walk in
front of it, jump onto it, and walk off its edge to drop down. A standing
jump rises about 44 pixels (a fighter is about 50 tall), enough for a table
or a counter. To reach the top of the fridge, the wardrobe or a bookshelf,
jump from something lower next to it.

Chairs, stools, the tables, the nightstand, the desk, the TV and the monitor
break after a few heavy landings: a fighter knocked down onto them, or a
dive kick that lands on them. Whatever stands on a piece goes down with it.
Each round starts with the room as new.

| Room | Furniture |
| --- | --- |
| Living room | bookshelf, sofa, coffee table, armchair, TV cabinet with a TV on it |
| Kitchen | fridge, counter, a table with two chairs, a stool |
| Bedroom | wardrobe, bed, nightstand, dresser, chair |
| Study | two bookshelves, a desk with a monitor, an office chair, a filing cabinet |

### Computer opponents

A computer player uses the same keys as you do. It sees you a few frames
late (24 frames on Easy, 12 on Normal, 7 on Hard) and decides from that what
to do:

* It guards attacks it sees coming, usually at the right height.
* It meets jump-ins with an uppercut.
* It punishes moves that miss, with a move fast enough to land in time.
* It picks attacks by range and by what you're doing, aiming at a height
  you're not guarding.
* It climbs onto furniture and waits for you to wander into diving range.

Easy is meant to lose to a beginner. It is slow and timid, misjudges range,
rarely guards (often at the wrong height), stays off the furniture, and
mostly throws plain punches and kicks rather than flying kicks, sweeps,
roundhouses and attacks from the air. Normal and Hard do all of the above,
Hard faster and more accurately.

The levels differ in reaction time, how often they guard, how often they
attack, and how well they judge range.

## How it works

* **One skeleton for everything.** A fighter is a stick figure with joint
  angles (`src/core/pose.cpp`). The same skeleton draws the fighter, places
  its hurt boxes (head, torso, thighs, shins) and puts the strike point on
  the fist or foot. A kick hits exactly where the foot is drawn, and ducking
  works because the crouching body is actually lower. Moves blend between
  chamber, strike and recovery poses.
* **Pixel art from shapes.** Limbs are thick capsules with a dark outline and
  three-tone shading, rasterized without anti-aliasing into a 320 x 180
  frame (`src/gfx/art.cpp`). The rooms (wallpaper, windows, perspective
  floors) and the furniture sprites are drawn the same way. The frame is
  blown up by a whole number with no filtering, then filtered down to the
  window, so pixels stay crisp at any window size.
* **Deterministic.** The fight runs in fixed 1/60 s ticks with its own
  seeded random generator and no SDL, so the tests and the recording tool
  play whole matches headlessly.

## Building

You need CMake 3.16+, a C++17 compiler and SDL2 (2.0.10 or newer). If CMake
cannot find SDL2, it downloads and builds it automatically. Turn that off with
`-DKARA_FETCH_SDL=OFF`.

**Windows (Visual Studio + vcpkg):**

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
build\Release\karasheka.exe
```

`vcpkg.json` makes vcpkg install SDL2 automatically. `SDL2.dll` is copied next
to the executable, and the procedurally drawn icon is embedded in the `.exe`.

**Linux:**

```sh
sudo apt install build-essential cmake libsdl2-dev   # or your distro's equivalent
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/karasheka
```

**macOS:**

```sh
brew install cmake sdl2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/karasheka
```

The game is built and tested on Windows and on Ubuntu 22.04 (GCC). The macOS
steps use only SDL2 and the standard library, but have not been tried yet.

### Windows release and itch.io

`tools/release_windows.sh` builds a self-contained release: SDL2 and the C++
runtime are linked statically (vcpkg triplet `x64-windows-static`), so players
need no DLLs or Visual C++ redistributable. It runs the tests, checks the
`.exe` imports only system DLLs, stages `dist/karasheka-<version>-windows/`
(the game, a player readme, licenses and an `.itch.toml` launch manifest) and
zips it. Given an itch.io project, it also uploads the folder to the
`windows` channel with [butler](https://itch.io/docs/butler/):

```sh
tools/release_windows.sh                       # build, test and package only
butler login                                   # once
tools/release_windows.sh your-name/karasheka   # ... and upload
```

The version comes from `project(Karasheka VERSION ...)` in `CMakeLists.txt`
and is shown on the menu.

### Linux package (.deb)

`tools/build_deb.sh` builds a Debian/Ubuntu package from the files git
tracks, with the packaging in `debian/`. It runs the tests during the build
and installs the game in `/usr/games`, with a desktop entry, an icon, AppStream
metadata and a man page (all in `data/`). Build it on the oldest release you
want to support, since the package's library dependencies come from the system
that builds it: a package built on Ubuntu 22.04 installs on 22.04 and newer.

```sh
tools/build_deb.sh --install-deps      # build/deb/karasheka_<version>-1_amd64.deb
sudo apt install ./build/deb/karasheka_1.0.0-1_amd64.deb
```

`debian/changelog` must name the same version as `CMakeLists.txt`; the script
stops if they disagree. On itch.io the package goes to the `linux-deb`
channel.

## Tests

`kara_tests` checks the game without opening a window:

* The controls map every key and chord to the right move, mirrored for the
  fighter on the right.
* The skeleton stands on the floor at about 50 pixels tall, lies flat when
  knocked down, and crouches below the roundhouse.
* Hits and guards: each ground attack is blocked by the guard at its height
  and by no other; a jump-in air kick only by the high guard. Crouching ducks
  a roundhouse but blocks nothing. A roll passes through the opponent.
* Chords: keys meant together, pressed up to 4 frames apart in either order,
  give the move they make together (8 then strike is an uppercut, strike then
  6 a front kick, 2, 6, strike a sweep, 8 then guard a high guard). Letting
  go of a key early does not change the move.
* Strike timing: no attack lands on a fighter on the floor sooner than 12
  frames after it starts (or after leaving the ground), and the right guard
  pressed as late as the frame it lands blocks it.
* Furniture: a jump lands on the kitchen table and walking off its edge
  drops to the floor. A dive kick from the table hits harder than one from
  a floor jump. A fighter knocked down onto the stool breaks it and falls
  through to the floor.
* Whole computer matches (Hard vs Hard, Easy vs Easy, Hard vs Normal) all
  finish, and the computers land a variety of moves and use the furniture.
  Each pairing prints statistics such as moves tried and landed, blocks,
  furniture landings, time in the air and knockouts. The same seed always
  plays the same match, and Hard beats Easy.
* Against a beginner: a scripted beginner (slow to react, simple attacks,
  guesses at guarding, ignores the furniture) plays each level. It must beat
  Easy in most matches and lose to Hard in most, and each level must do
  better against it than the one below.

```sh
ctest --test-dir build -C Release
build/Release/kara_tests 20                 # play 20 matches per pairing instead of 6
build/Release/kara_tests probe 12345 40     # health after 40 s of a Hard vs Hard fight
```

## Command-line options

```
--size WxH             window (or capture) size (default 1280x720)
--screenshot FILE.bmp  play a CPU fight offscreen, save one frame and exit
--scene NAME           menu | intro | fight | ko | over | help | pause | cover |
                       poses (every pose on one sheet, for tuning the skeleton)
--time SECONDS         fight time before a fight / cover capture (default 8)
--seed N               match seed (captures default to 12345; --record defaults to
                       the best of 120 auditioned seeds)
--record FILE|-        play the showcase clip, writing raw RGB24 frames
--fps N                frame rate for --record (default 30)
```

Captures are drawn in software, so they need no window or graphics driver.

### Regenerating the clip, the covers and the screenshots

`tools/make_media.sh` makes `docs/gameplay.mp4` (1280 x 720, 30 fps),
`docs/gameplay.gif` (800 px wide, 15 fps), the store screenshots in
`docs/store/screenshots/` (1280 x 720), `docs/cover.png` (1280 x 640, the
size GitHub uses for a repository's social preview) and
`docs/store/itch-cover.png` (630 x 500, itch.io's cover size; the logo and
the crop adapt to the narrower frame). It also writes two sheets for
checking the result by eye: `dist/gameplay-sheet.png` (a frame every 2.5 s)
and `dist/screenshots-sheet.png`. Every capture uses a fixed seed, so a
re-run reproduces the same files. You need bash and ffmpeg with libx264.
The store text is in `docs/store/tagline.txt` and
`docs/store/description.md`.

```sh
tools/make_media.sh                      # uses build/Release/karasheka or build/karasheka
tools/make_media.sh path/to/karasheka
```

For the clip, the game first plays the opening 32 seconds of 120 candidate
seeds headlessly. It keeps the seed with the most going on: a dive kick, a
piece of furniture breaking, a flying kick, a sweep, a landing on furniture
and a knockout. Matches are deterministic, so the recording replays exactly
what was auditioned.

## Layout

```
src/core/   skeleton and poses, moves and fight rules, rooms and furniture, computer players (no SDL)
src/gfx/    software framebuffer, 5x7 pixel font, procedural art, anti-aliased canvas for the icon (no SDL)
src/app/    SDL2 game: menus, input layouts, effects, rendering, synthesized audio, settings
tools/      icongen (app icon as a Windows .ico at build time), make_media.sh (clip, covers, screenshots),
            release_windows.sh and release/ (Windows package and itch.io upload)
tests/      headless tests
docs/       gameplay.mp4, gameplay.gif, cover.png; store/ (itch cover, screenshots, tagline, description)
```
