# Malfunctions for Geronimo

A [GML](https://github.com/RemArrow/geronimo-mod-loader) plugin that makes the guns you shoot jam
as often as the real ones do. You clear each stoppage the way you would in real life. Nothing is
pressed for you and nothing is replaced. It works through the game's own gun mechanics, so it
applies to every gun in the game, including mod guns like the
[Noveske Chainsaw](https://github.com/RemArrow/noveske-chainsaw). And the P320 is
[not drop safe](#the-p320-is-not-drop-safe).

| Stoppage | What you see and hear | How to clear it |
|---|---|---|
| **Dud** | The trigger clicks and nothing happens. Further pulls feel dead. | Tap-rack: the dud is ejected and a fresh round chambered. |
| **Failure to feed** | The bolt or slide stops out of battery, with the round short of the chamber. | Tap-rack. |
| **Stovepipe** | The spent case is caught upright in the ejection port by the bolt or slide. | Rack it clear. The case is thrown out. |
| **Double feed** | The bolt is held back on two rounds behind the chamber. Racking with the magazine in jams it again. | Strip the magazine, rack, reload, rack. |

The bolt or slide release won't drop the action on a stoppage. A partial pull of the charging
handle doesn't clear it either: rack it all the way.

## Which guns

- **Rifles and handguns** get all four stoppages.
- **Bolt actions and pump shotguns** only get duds, because you cycle them by hand anyway.
- Only the gun **you** are holding is affected. AI and other players' guns never jam.
- A gun with an empty magazine never jams: there's nothing left to feed.

## How often real guns jam

Each gun stops as often as its real counterpart: on average once every so many rounds ("mean
rounds between stoppages", MRBS, as armies measure it), for a clean, lubricated gun with good
magazines and ammunition. Where there is a published test, the figure comes from it. Where there
isn't, it is an estimate from the closest documented gun, marked as such.

| Gun | Action | One stoppage per | Basis |
|---|---|---|---|
| M4A1 (URGI, Block II) | direct impingement | 3,600 rounds | **Measured.** US Army: over 3,600 MRBS after 62 improvements (the original requirement was 600). The 2013 carbine competition asked for 3,592. [1] [2] |
| HK416 | short-stroke piston | 7,200 | **Scaled.** In the Army's 2007 extreme dust test it had 233 stoppages in 60,000 rounds, against the M4's 882. Taken here as twice the M4A1 in normal use. [3] |
| AK-12 | long-stroke piston | 6,000 | **Estimate.** No published figure. Kalashnikovs are the reliability benchmark: the AK-47 was the most reliable rifle in the Army's 1966 SAWS trials. [4] |
| MCX | short-stroke piston | 5,000 | **Estimate.** The HK416's operating system, without a published figure. |
| Tavor X95 | long-stroke piston | 5,000 | **Estimate.** |
| G28 | short-stroke piston, 7.62 | 5,000 | **Estimate.** The HK416's 7.62 sibling, running full-power rounds. |
| SR-25, M110 | direct impingement, 7.62 | 2,500 | **Estimate.** The M110 drew field reports of reliability problems. [5] |
| KS-1 | direct impingement, tuned for a suppressor | 4,500 | **Estimate.** Knight's improved bolt and gas system over the M4's. |
| MP5, MP5SD | roller-delayed | 5,000 | **Estimate.** |
| MP7 | short-stroke piston | 5,000 | **Estimate.** |
| Glock 19 | short recoil | 3,750 | **Measured.** A Gen4 Glock 17 endurance test: 19 stoppages in 71,260 rounds. [6] |
| P320 | short recoil | 1,700 | **Measured.** The Army's M17 (a P320) in production testing: 1,566 MRBS with ball and 1,880 with hollow points, against a requirement of 2,000. [7] [8] |
| USP .45 | short recoil | 20,000 | **Measured.** The HK45, the USP .45's successor, fired 22,275 rounds without a stoppage in an endurance test. [9] |
| Staccato 2011 | short recoil | 2,900 | **Measured.** A Staccato P endurance test: 7 gun-caused malfunctions in 20,000 rounds (5 of them from one bad magazine). [10] |
| PKP | long-stroke piston, belt-fed | 2,500 | **Estimate.** Belt-fed guns stop more often than rifles. The PKM scored low on MRBS in a 1970s US Army evaluation (one gun, poor ammunition). [11] |
| 870, Remington 700 | pump, bolt action | duds only | You cycle them by hand. |
| [Noveske Chainsaw](https://github.com/RemArrow/noveske-chainsaw) (mod) | direct impingement | 7,200 | It is an HK416 underneath and runs as one. |

**What kind of stoppage.** How a gun's stoppages split up comes from the same kind of data:

- **Rifles and submachine guns:** 27 % failures to feed, 36.5 % stovepipes, 36.5 % double feeds. In
  the M4's dust test, 239 of the 882 stoppages were magazine-related [12]. The rest (failures to
  eject or extract) are split evenly here.
- **Handguns:** 29 % failures to feed, 6 % stovepipes, 65 % double feeds. Over 176,240 rounds, 24
  SIG pistols had 10 failures to feed, 2 to eject and 22 to extract (light primer strikes and bad
  ammunition left out) [13].
- **Belt-fed:** 40 % failures to feed, 60 % stovepipes, no double feeds (estimate).

A failure to extract leaves the case in the chamber for the next round to ram into. That is the
double feed here, cleared the same way.

**Duds** come from the ammunition and are the same in every gun: 1 round in 300,000. Winchester puts
its primers at 99.9997 % reliable [14].

**In play**, at real-world rates an M4A1 stops about once every 120 magazines, so you can go a long
time without one. Conditions matter far more than the gun. In the extreme dust test, the M4 stopped
once every 68 rounds, about 50 times its normal rate [3]. Here, sustained fire and suppressors
raise the rates (below). To see stoppages more often, raise `[Rates] Multiplier`.

On top of that, while you play:

- **Fouling:** sustained fire fouls the action. Each round adds to it, and it wears off over a minute
  or two (half in about a minute). Every 40 rounds of fouling adds up to ×1 to the mechanical
  stoppages, capped at ×2.5 overall. A direct-impingement gun fouls fastest (×1.0 rate), then
  roller-delayed (×0.7), short-stroke piston (×0.6), short recoil (×0.5) and long-stroke piston (×0.4).
- **Suppressors:** a suppressor (the game's own `bIsSuppressor`) sends more gas and fouling into the
  action. That's ×1.6 for direct impingement, ×1.3 for roller-delayed guns and pistols, ×1.2 for
  short-stroke and ×1.1 for long-stroke pistons. A suppressed round also fouls 1.5× as much. Guns
  built around a suppressor (MP5SD, KS-1) aren't affected.
- **Limp wrist:** a handgun held in one hand stovepipes 2.5× and fails to feed 1.5× as often.

## The P320 is not drop safe

Before SIG's 2017 voluntary upgrade, a P320 dropped at about −30°, so that the back of the slide and
frame struck a hard surface first, could fire. The impact stops the gun, and the trigger's own
inertia carries it on rearward far enough to release the striker. SIG's upgrade lightened the
trigger and sear [16]. It was the only one of 11 pistols to fire in Omaha Outdoors' 2017 drop tests
[15], and a brand-new P320 fired on two of three drops [17].

In the game, the P320 behaves like those pre-upgrade guns:

- **When:** it falls with a round chambered and nobody holding it (dropped, thrown or knocked off
  something). The back of the slide must hit first, with the gun moving rear-first along its bore
  at 190 cm/s or more. That is about a 75 cm fall with the bore at 30° to the ground.
- **How often:** two times in three (`[DropFire] Chance`).
- **What happens:** the chambered round fires out of the muzzle, wherever it points. The game then
  cycles the gun as for any shot.
- **What doesn't fire it:** drops on the grip, muzzle or sides, and flat drops. Neither does
  anything that happens to it while it's in a hand or a holster.

Only the machine with authority over the gun (the host) decides. `[DropFire] P320 = false` makes it
drop safe again.

## How it works

1. **Every shot** passes through `GunChamberComponent:OnFired`, broadcast after the round is fired.
   The plugin hooks it and rolls a stoppage for the gun's next round (or uses one armed through the
   [API](#api-for-other-plugins)).
2. **Dud:** the gun's `bIsFiringBlocked` is set, so `CanFireRound` is false while a live round sits in
   the chamber. Trigger pulls play the gun's own `ShotsEmpty` click, then its `DeadTriggerPulled`.
3. **Failure to feed / double feed:** the chamber's `SetChamberingBlocked` is set before the gun
   cycles. The next round then stops in the game's `Chambering` state, and the game draws it there.
   The bolt (`Rifle:LockBolt`) or slide (`Handgun:LockSlide`) is then locked out of battery at a
   custom lock point: 25 % of its travel for a failure to feed, 40 % for a double feed. A double
   feed adds a second round from the gun's own round mesh under the first.
4. **Stovepipe:** the shot cycles as the game plays it, then the bolt or slide is locked 30 % back,
   out of battery, so the gun can't fire. The gun's own fired-case mesh is attached standing up in
   the ejection port.
5. **Clearing:** a full rack clears the stoppage. It is read off the charging handle or slide itself
   (`CurrentProgress` past 90 %), whatever path the game takes to move it, and also from the game's
   `GunHandleComponent:HandleBackServer`. A half pull doesn't clear it. The plugin restores
   everything it changed and lets go of its bolt lock; the game then ejects and chambers as it always
   does. For a double feed, the magazine must be out before a rack clears it. While you are pulling
   the handle, the plugin never re-locks the bolt and never refuses the game's own unlock.
6. **Full auto:** whatever stops the next shot is applied inside the `OnFired` hook. The follow-up
   round 80 ms later is already blocked.

## Requirements

- Geronimo (Steam, UE 5.7.4) with [GML 2.2.0](https://github.com/RemArrow/geronimo-mod-loader/releases/tag/v2.2.0) or newer.

## Install

Close the game, download `Malfunctions-<version>.zip` from
[Releases](https://github.com/RemArrow/geronimo-malfunctions/releases) and extract it into
`...\GERONIMO\Geronimo\Binaries\Win64\`. It adds `GML\plugins\Malfunctions\Malfunctions.dll`. To
remove it, delete that folder. Its settings are created at the first launch (see
[Settings](#settings-gmlconfigcomremarrowmalfunctionscfg)).

## Build from source

```bat
git clone --recursive <this repo>
build.bat            rem build into build\
build.bat install    rem also copy into the game (refuses while the game runs)
```

The build needs Visual Studio 2022 (C++ desktop workload) and the GML SDK: the `external/gml`
submodule, or `GML_SDK` pointing at its `include` folder. Set `GML_GAME_DIR` to your
`...\Geronimo\Binaries\Win64` if the game is not in the default Steam library. Installing copies
`Malfunctions.dll` to `GML\plugins\Malfunctions\`.

## Settings: `GML\config\com.remarrow.malfunctions.cfg`

| Key | Default | |
|---|---|---|
| `[General] Enabled` | true | guns you shoot can malfunction |
| `[Rates] Multiplier` | 1.0 | every stoppage, × its real-world rate (1 = real life) |
| `[Realism] PerGun` | true | each gun stops as often as the real one (off: every gun runs like an M4A1) |
| `[Realism] Fouling` | true | sustained fire raises the mechanical stoppages |
| `[Realism] Suppressors` | true | a suppressor raises them |
| `[Realism] LimpWrist` | true | a one-handed handgun stovepipes and fails to feed more |
| `[Guns] AK`, `HK416`, `M4A1`, ... | 1.0 | per gun, × its real-world rate (lower = more reliable) |
| `[DropFire] P320` | true | the P320 can fire when dropped on the back of the slide |
| `[DropFire] Chance` | 67 | percent of such drops that fire it |
| `[Debug] Log` | false | log every stoppage, why (gun, fouling, suppressor, grip) and how it was cleared |

At `Multiplier = 1`, an M4A1 stops about once every 3,600 rounds. At 10 it's once every 360 rounds
(about every 12 magazines), and at 100 about once a magazine. GML reads the `.cfg` at launch, so edit
it with the game closed. The `[Chances]` section of versions before 0.4.0 is no longer read.

## API for other plugins

`Malfunctions.dll` exports two C functions (call them on the game thread):

```c
// Arms `kind` (1 dud, 2 failure to feed, 3 stovepipe, 4 double feed; 0 disarms) for the gun's next
// shot. Returns 0 when the gun cannot have that stoppage.
int Malfunctions_Arm(GUObject* receiver, int kind);
// The stoppage the gun is in right now (0 = none).
int Malfunctions_Current(GUObject* receiver);
// How the gun runs right now: out[1..4] = its chance of each stoppage on the next shot, in percent
// (dud, failure to feed, stovepipe, double feed), label = the gun it runs like. Returns its fouling
// in rounds.
float Malfunctions_Factors(GUObject* receiver, float* out, char* label, int capacity);
```

Look them up with `GetModuleHandleW(L"Malfunctions.dll")` and `GetProcAddress`.

## Testing

`tests/MalfunctionsDev` is the in-game harness (built, never installed). It loads TrainingGrounds
and holds the loadout gun in front of the camera as the player's. It arms each stoppage through the
API, fires real shots and tries the wrong fixes (the bolt release, racking with the magazine in)
before the right one. The chamber, bolt and ammo state are logged at every step, with game-rendered
screenshots of each stoppage. Flag files next to the harness DLL switch the run:

- `secondary.flag`: use the loadout's sidearm.
- `random.flag`: no arming; the plugin's own roll is tested (set `[Rates] Multiplier` very high).
- `loop.flag`: ten stovepipe and rack cycles.
- `realism.flag`: how the gun runs. It logs its profile and its chances per shot, then again with a
  suppressor on, after 60 rounds of sustained fire, and 15 s later as it cools.
- `drop.flag`: a P320 of its own, loaded and chambered, let go by the player's right motion
  controller (so the game's own release runs) 120 cm above the floor. Six drops go upside down with
  the rear end 30° low, then four control drops: upright flat, upright rear-low, upside down flat,
  and muzzle down. Each is logged with the round count before and after.
- `pull.flag`: racking the way a hand does it. The handle is pulled back in steps, with the game's own
  rack calls only at the ends of the travel, and a half pull comes first.

Verified in game on the HK416 (also as the Noveske Chainsaw), the M4A1 URGI and the Glock 19:
- all four stoppages and their clearing, including by a hand-style rack;
- a half pull leaves the stoppage in place;
- the blocked bolt and slide release;
- the dud's click and dead trigger;
- full auto, and the random roll.

## Multiplayer

Everyone needs the plugin. The host fires every round in the game, a client's too: a client's shot
reaches the host, and the shot cycles the gun there, so **the host rolls and applies every player's
stoppages**. While a client's gun is stopped, the host blocks it (the gun's replicated
`bIsFiringBlocked`), so it won't fire; on the client's own machine the plugin gives the dead trigger,
a click and then nothing, as on a dud. Racking clears it: the charging handle and slide report to the
host (`HandleBackServer` / `HandleForwardServer`), which clears it and lets the gun fire again.

Verified in a two-player test (two copies of the game on one PC, `Utils\MPDev`, 2026-10-13): a stovepipe
forced on the client's gun on the host stopped it there, the client's next shot did not fire, the
client's copy of the gun showed it blocked, its trigger went dead, and a rack cleared it and the next
shot fired.

## Known limitations

- Multiplayer: on a client's own machine a stoppage shows only as the dead trigger. The bolt held
  back, the case in the ejection port and the double feed's second round are drawn on the host
  only (the game doesn't replicate the bolt's lock).
- Duds can't come up on the first round after a reload; the roll happens when a round is fired.

## Sources

1. The Firearm Blog, [Evolution of the M4 Carbine](https://www.thefirearmblog.com/blog/2009/11/25/evolution-of-the-m4-carbine/) (2009)
2. Military.com, [Army: Gun Makers Didn't Meet Reliability Standard](https://365.military.com/daily-news/2013/06/14/army-gun-makers-didnt-meet-reliability-standard.html) (2013)
3. Military.com, [M4 comes in last place in dust test](https://www.military.com/defensetech/2007/12/17/m4-comes-in-last-place-in-dust-test) (2007); US Army, [M-4 carbine has high Soldier confidence despite test](https://www.army.mil/article-amp/6649/m_4_carbine_has_high_soldier_confidence_despite_test) (2007)
4. Sandboxx, [Exploring the Army's obscure and radical SAWS program](https://www.sandboxx.us/news/exploring-the-armys-obscure-and-radical-saws-program/)
5. The Firearm Blog, [Problems with the Semi-Automatic M110 Sniper Rifle?](https://www.thefirearmblog.com/blog/2011/08/17/problems-with-the-semi-automatic-m110-sniper-rifle) (2011)
6. pistol-training.com, [Gen4 Glock 17 Endurance Test: Month 16](https://pistol-training.com/gen4-glock-17-endurance-test-month-16/)
7. DOT&E, [FY2018 Annual Report: Modular Handgun System](https://www.dote.osd.mil/Portals/97/pub/reports/FY2018/army/2018mhs.pdf)
8. The Firearm Blog, [New Report & Testing Confirms SIG Sauer's Modular Handgun System is Reliable](https://www.thefirearmblog.com/blog/2019/02/11/sig-sauers-m18-reliable/) (2019)
9. pistol-training.com, [HK45 Endurance Test: Week Seventeen](https://pistol-training.com/hk45-endurance-test-week-seventeen/)
10. Outdoor Life, [Staccato P Endurance Report: 20,000 Round Long-Term Test](https://www.outdoorlife.com/guns/staccato-p-endurance/)
11. The Firearm Blog, [1970s U.S. Army Test of the PKM](https://www.thefirearmblog.com/blog/1970s-u-s-army-test-of-the-pkm-44826922)
12. Stars and Stripes, [M-4 performs worst in extreme dust test](https://www.stripes.com/news/2007-12-19/m-4-performs-worst-in-extreme-dust-test-1935257.html) (2007)
13. pistol-training.com, [Five Years of SIG](https://pistol-training.com/five-years-of-sig/)
14. Guns & Ammo, [Understanding Misfires](https://www.gunsandammo.com/editorial/understanding-misfires/248092)
15. The Firearm Blog, [P320 the only pistol of 11 that fires when dropped in Omaha Outdoors test](https://www.thefirearmblog.com/blog/2017/08/14/glock-beats-sig-p320-pistol-11-fires-dropped-comprehensive-omaha-outdoors-test/) (2017)
16. ConcealedCarry.com, [Sig Sauer P320 Drop Test Misfire Drama](https://www.concealedcarry.com/?p=297745) (2017, quoting SIG's statement); Shooting Wire, [SIG's P320: Owning An Issue](https://www.shootingwire.com/features/231953) (2017)
17. Athlon Outdoors, [Sig Issues Voluntary Upgrade of P320 Pistol Amid Drop Test Fails](https://athlonoutdoors.com/article/sig-voluntary-upgrade-p320-pistol-drop/) (2017)

## Changes

- **0.6.0:** multiplayer. The host decides every player's stoppages (before, a client's guns never
  stopped: their shots cycle on the host, where only the host's own gun was looked at), blocks a
  stopped gun for its player, and the plugin on a client gives the dead trigger.
- **0.5.0:** the P320 is not drop safe. Like a pre-2017 P320, dropped so the back of the slide hits
  first, it fires its chambered round two times in three.

- **0.4.0:** real-world rates. Each gun stops as often as its real counterpart (from published tests
  where there are any; see [How often real guns jam](#how-often-real-guns-jam)), and its stoppages
  split up as real ones do. Duds come from the ammunition: 1 round in 300,000. `[Rates] Multiplier`
  replaces the `[Chances]` percentages. `Malfunctions_Factors` now reports chances per shot.
- **0.3.0:** each gun malfunctions like the real one (built-in profiles for every gun in the game
  and the Noveske), plus fouling from sustained fire, suppressors and one-handed pistols.
  `Malfunctions_Factors` reports how a gun runs right now.
- **0.2.0:** stoppages come about seven times as often. Fixed: a stoppage could leave the gun
  unusable, because the plugin re-locked the bolt or slide while you were racking it (and could
  refuse the game's own unlock), so a rack never finished. Clearing is now read off the handle or
  slide itself. A stovepipe no longer skips the game's shot cycle or rewrites the chamber. That was
  the code path of 0.1.0's one fault in testing (once in 25 pistol stovepipe racks).
- **0.1.0:** first version.
