// Malfunctions - stoppages for the guns you shoot, cleared the way you would clear them for real.
//
//   Dud              the next trigger pull just clicks. Tap-rack: the dud is ejected, a fresh round chambered.
//   Failure to feed  the next round stops short of the chamber with the bolt out of battery. Tap-rack.
//   Stovepipe        the spent case is caught in the ejection port by the bolt. Rack it clear.
//   Double feed      two rounds jammed behind the chamber. Strip the magazine, rack, reload, rack.
//
// Built from the game's own gun mechanics (GunReceiver / GunChamberComponent / Rifle): every shot passes
// through GunChamberComponent:OnFired, where a malfunction is rolled; racking passes through
// GunHandleComponent:HandleBackServer / HandleForwardServer, where it is cleared. Only guns held by the
// local player are affected.
#define NOMINMAX
#include <GML/GML.hpp>
#include <windows.h>
#include <cmath>
#include <map>
#include <random>
#include <unordered_map>

using namespace gml;

#define MALFUNCTIONS_VERSION "0.5.0"
GML_PLUGIN("com.remarrow.malfunctions", "Malfunctions", MALFUNCTIONS_VERSION);

struct Vec { double x, y, z; };
struct Rot { double pitch, yaw, roll; };

enum Kind : int { None = 0, Dud = 1, FailureToFeed = 2, Stovepipe = 3, DoubleFeed = 4 };
static const char* KindName(int k) {
    static const char* n[] = {"none", "dud", "failure to feed", "stovepipe", "double feed"};
    return k >= 0 && k <= 4 ? n[k] : "?";
}

// How far back the bolt is held (0 = in battery, 1 = fully back) while each stoppage is in place.
static constexpr float kBoltFtf = 0.25f, kBoltStovepipe = 0.3f, kBoltDoubleFeed = 0.4f;
// A stoppage is applied this long after the shot, once the game's own cycle animation has played.
static constexpr ULONGLONG kSettleMs = 120;
static constexpr ULONGLONG kOnRackForward = ~0ull;  // applyAt: waiting for the bolt to go forward
// The charging handle / slide counts as racked back from here, and as forward again below kForward.
// Its travel is only watched this long after a stoppage is put in place: a pistol's slide runs back
// on its own as the shot cycles.
static constexpr float kBack = 0.9f, kForward = 0.1f, kAtRest = 0.15f;
static constexpr ULONGLONG kWatchHandleAfterMs = 250;

struct Jam {
    Kind kind = None;
    Kind armed = None;          // forced through the API: happens on the next shot
    ULONGLONG applyAt = 0;      // > 0: the stoppage still has to be put in place
    ULONGLONG watchFrom = 0;    // the handle's travel counts from here
    bool handleBack = false;    // racked back, not forward yet
    bool magOut = false;        // double feed: the magazine has been stripped
    bool clicked = false;       // dud: the hammer has already fallen on it
    bool triggerWasDown = false;
    ULONGLONG lastPull = 0;
    float boltLockWas = -1;     // the gun's own BoltLockedProgress, restored on clearing
    Object casing, round;       // the visible spent case / second round
};
static std::unordered_map<GUObject*, Jam> s_jams;

static ConfigEntry<bool> s_enabled, s_log;
static ConfigEntry<float> s_multiplier;
static std::mt19937 s_rng{std::random_device{}()};

#define DLOG(...) do { if (s_log.Value()) Log(__VA_ARGS__); } while (0)

// ------------------------------------------------------------------ the game's gun

static bool CallBool(Object o, const char* fn) {
    Params p(o, fn);
    p.Call();
    return p.Return<bool>();
}
static Object Receiver(Object component) { return component ? component.GetObj("Receiver") : Object(); }

// The part that cycles: a rifle's bolt or a handgun's slide. Both lock at a progress the gun
// carries (0 = in battery, 1 = fully back) and unlock when racked.
struct Action { const char* lockAt; const char* lock; const char* locked; const char* unlock; const char* handle; };
static const Action kBolt{"BoltLockedProgress", "LockBolt", "bIsBoltLocked", "UnlockBolt", "ChargingHandle"};
static const Action kSlide{"SlideLockedProgress", "LockSlide", "bIsSlideLocked", "UnlockSlide", "Slide"};
static const Action* ActionOf(Object gun) {
    if (gun.IsA("Rifle")) return &kBolt;
    if (gun.IsA("Handgun")) return &kSlide;
    return nullptr;  // bolt actions, pumps
}
static bool Supports(Object gun, Kind k) {
    if (k == Dud) return true;
    if (gun.IsA("BoltActionRifle") || gun.IsA("Shotgun")) return false;  // manually cycled: duds only
    return k == FailureToFeed || ActionOf(gun);
}

// Held by this machine's player (not an AI, not another player).
static bool HeldByLocalPlayer(Object gun) {
    Params h(gun, "GetHoldingCharacter");
    h.Call();
    Object who = h.Return<GUObject*>();
    return who && CallBool(who, "IsPlayerControlled") && CallBool(who, "IsLocallyControlled");
}
static int Ammo(Object gun) {
    Object well = gun.GetObj("MagWellComponent");
    if (!well) return 0;
    Params a(well, "GetCurrentAmmo");
    a.Call();
    return a.Return<int32_t>();
}
static void ChamberBlocked(Object gun, bool b) {
    Params p(gun.GetObj("ChamberComponent"), "SetChamberingBlocked");
    p.Set("bValue", b).Call();
}
static void ClearChamber(Object gun) {
    Params p(gun.GetObj("ChamberComponent"), "ForceClearChamber");
    p.Call();
}
static void LockActionAt(Object gun, Jam& j, float at) {
    const Action* a = ActionOf(gun);
    if (!a) return;
    if (j.boltLockWas < 0) j.boltLockWas = gun.Get<float>(a->lockAt);
    gun.Set(a->lockAt, at);
    Params p(gun, a->lock);
    p.Set("bManualLock", true).Call();
}
static bool ActionLocked(Object gun) {
    const Action* a = ActionOf(gun);
    return !a || gun.GetBool(a->locked);
}
// Restores the gun's own lock point; if the plugin's lock still holds the action, lets it go.
static void RestoreActionLock(Object gun, Jam& j) {
    const Action* a = ActionOf(gun);
    bool ours = j.boltLockWas >= 0;
    if (a && ours) gun.Set(a->lockAt, j.boltLockWas);
    j.boltLockWas = -1;
    if (a && ours && gun.GetBool(a->locked)) {
        Params u(gun, a->unlock);
        u.Call();
    }
}
// How far back the part you rack is (0 forward, 1 fully back): a rifle's charging handle, a handgun's
// slide, a pump or a bolt-action lever. Read straight off the part, whatever moved it.
static float HandleProgress(Object gun) {
    for (const char* prop : {"ChargingHandle", "Slide", "Handle", "Lever"})
        if (Object h = gun.GetObj(prop)) return h.Get<float>("CurrentProgress");
    return 0;
}
static void PlayAt(Object gun, const char* sound) {
    Object audio = gun.GetObj("AudioData");
    Object s = audio ? audio.GetObj(sound) : Object();
    if (!s) return;
    Params loc(gun, "K2_GetActorLocation");
    loc.Call();
    Params p(Lib("GameplayStatics"), "PlaySoundAtLocation");
    p.SetObj("WorldContextObject", gun).SetObj("Sound", s).Set("Location", loc.Return<Vec>())
        .Set("VolumeMultiplier", 1.0f).Set("PitchMultiplier", 1.0f).Call();
}

// ------------------------------------------------------------------ what you see

// The gun's own round meshes: the proxy round's mesh is the unfired one; the fired one is its
// "...Fired" sibling (falls back to the unfired mesh).
static Object RoundMesh(Object gun, bool spent) {
    Object proxy = gun.GetObj("ProxyRound");
    Object unfired = proxy ? proxy.GetObj("StaticMesh") : Object();
    if (!unfired || !spent) return unfired;
    std::string path = unfired.Path();
    for (auto [from, to] : {std::pair{"Unfired", "Fired"}, std::pair{"unfired", "fired"}}) {
        size_t at = path.rfind(from);
        if (at == std::string::npos) continue;
        std::string p = path;
        while ((at = p.find(from)) != std::string::npos) p.replace(at, strlen(from), to);
        if (Object m = API->LoadObject(p.c_str())) return m;
    }
    return unfired;
}
static Vec LocalOf(Object gun, Object comp) {
    Params wl(comp, "K2_GetComponentLocation");
    wl.Call();
    Params gt(gun, "GetTransform");
    gt.Call();
    Params inv(Lib("KismetMathLibrary"), "InverseTransformLocation");
    GML_PropInfo ri;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(gun.Struct(), "GetTransform"), "ReturnValue"), &ri);
    std::memcpy(inv.Ptr("T"), gt.Ptr("ReturnValue"), ri.size);
    inv.Set("Location", wl.Return<Vec>()).Call();
    return inv.Return<Vec>();
}
// A static mesh riding on the gun at `at` (gun space), rotated `r` relative to the gun.
static Object AddMesh(Object gun, Object mesh, Vec at, Rot r) {
    if (!mesh) return Object();
    Params mt(Lib("KismetMathLibrary"), "MakeTransform");
    mt.Set("Scale", Vec{1, 1, 1}).Call();
    Params add(gun, "AddComponentByClass");
    *(GUClass**)add.Ptr("Class") = FindClass("StaticMeshComponent");
    add.Set("bManualAttachment", true).Set("bDeferredFinish", false);
    GML_PropInfo ti;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &ti);
    std::memcpy(add.Ptr("RelativeTransform"), mt.Ptr("ReturnValue"), ti.size);
    add.Call();
    Object comp = add.Return<GUObject*>();
    if (!comp) return comp;
    Params sm(comp, "SetStaticMesh");
    sm.SetObj("NewMesh", mesh).Call();
    Params col(comp, "SetCollisionEnabled");
    col.Set("NewType", (uint8_t)0).Call();
    Params root(gun, "K2_GetRootComponent");
    root.Call();
    Params att(comp, "K2_AttachToComponent");
    att.SetObj("Parent", Object(root.Return<GUObject*>())).Set("LocationRule", (uint8_t)2).Set("RotationRule", (uint8_t)2)
        .Set("ScaleRule", (uint8_t)2).Set("bWeldSimulatedBodies", false).Call();
    Params rel(comp, "K2_SetRelativeLocationAndRotation");
    rel.Set("NewLocation", at).Set("NewRotation", r).Set("bSweep", false).Set("bTeleport", true).Call();
    return comp;
}
static void Remove(Object& comp) {
    if (comp) {
        Params d(comp, "K2_DestroyComponent");
        d.SetObj("Object", comp).Call();
    }
    comp = Object();
}

// ------------------------------------------------------------------ stoppages

// ------------------------------------------------------------------ how each gun runs
//
// Real-world rates (README, "How often real guns jam", has the sources). Each gun stops on average
// once every `mrbs` rounds - mean rounds between stoppages, as armies measure it: clean, lubricated,
// good magazines and ammunition. Where a gun has no published figure, it is estimated from its
// closest documented relative. How its stoppages split up depends on the kind of gun (Mix). Duds
// come from the ammunition and are the same in every gun. [Rates] Multiplier scales it all
// (1 = real life). On top of that:
//   fouling      sustained fire fouls the action: up to x2.5, fastest in direct-impingement guns,
//                cooling off over a minute or two;
//   suppressor   a can pushes more gas and fouling back into the action (direct impingement worst),
//                except in guns built around one;
//   limp wrist   a handgun held in one hand stovepipes and short-strokes more.

enum class Mechanism { Unknown, DirectImpingement, ShortPiston, LongPiston, Roller, Recoil, Manual };

// Shares of a gun's mechanical stoppages. A failure to extract leaves the case in the chamber for
// the next round to ram into: the double feed here, cleared the same way.
struct Mix { float ftf, stovepipe, doubleFeed; };
// The M4's extreme dust test: 239 of 882 stoppages magazine-related (feeding); the other 643 taken as
// half failures to eject, half failures to extract.
static constexpr Mix kRifleMix{0.27f, 0.365f, 0.365f};
// 24 SIG pistols over 176,240 rounds: 10 failures to feed, 2 to eject, 22 to extract.
static constexpr Mix kPistolMix{0.29f, 0.06f, 0.65f};
// Belt-fed: no magazine to double-feed from (estimate).
static constexpr Mix kBeltMix{0.4f, 0.6f, 0.0f};
static constexpr Mix kNoMix{0, 0, 0};

// A defective round: primers fail about 1 in 300,000 (Winchester: 99.9997% in the SAAMI drop test).
static constexpr double kDudRounds = 300000;

struct Profile {
    const char* key;          // [Guns] setting
    const char* match[2];     // receiver class name contains (or a GunKit pack name)
    const char* label;
    Mechanism action;
    float mrbs;               // mean rounds between stoppages, normal conditions; 0 = duds only
    const Mix* mix;
    bool builtForSuppressor;
};

static const Profile kProfiles[] = {
    {"AK", {"AK12", "AK74"}, "AK-12 (long-stroke piston)", Mechanism::LongPiston, 6000, &kRifleMix, false},
    {"HK416", {"HK416", nullptr}, "HK416 (short-stroke piston)", Mechanism::ShortPiston, 7200, &kRifleMix, false},
    {"M4A1", {"M4A1", nullptr}, "M4A1 (direct impingement)", Mechanism::DirectImpingement, 3600, &kRifleMix, false},
    {"MCX", {"MCX", nullptr}, "MCX (short-stroke piston)", Mechanism::ShortPiston, 5000, &kRifleMix, false},
    {"Tavor", {"Tavor", nullptr}, "Tavor X95 (long-stroke piston)", Mechanism::LongPiston, 5000, &kRifleMix, false},
    {"SR25", {"SR25", "M110"}, "SR-25 / M110 (direct impingement, 7.62)", Mechanism::DirectImpingement, 2500, &kRifleMix, false},
    {"G28", {"G28", nullptr}, "G28 (short-stroke piston, 7.62)", Mechanism::ShortPiston, 5000, &kRifleMix, false},
    {"KS1", {"KS1", nullptr}, "KS-1 (direct impingement tuned for a suppressor)", Mechanism::DirectImpingement, 4500, &kRifleMix, true},
    {"MP5SD", {"MP5SD", nullptr}, "MP5SD (roller-delayed, integral suppressor)", Mechanism::Roller, 5000, &kRifleMix, true},
    {"MP5", {"MP5", nullptr}, "MP5 (roller-delayed)", Mechanism::Roller, 5000, &kRifleMix, false},
    {"MP7", {"MP7", nullptr}, "MP7 (short-stroke piston)", Mechanism::ShortPiston, 5000, &kRifleMix, false},
    {"Glock", {"G19", nullptr}, "Glock 19 (short recoil)", Mechanism::Recoil, 3750, &kPistolMix, false},
    {"P320", {"P320", nullptr}, "P320 / M17 (short recoil)", Mechanism::Recoil, 1700, &kPistolMix, false},
    {"USP", {"USP", nullptr}, "USP .45 (short recoil)", Mechanism::Recoil, 20000, &kPistolMix, false},
    {"Staccato", {"Staccato", nullptr}, "Staccato 2011", Mechanism::Recoil, 2900, &kPistolMix, false},
    {"PKP", {"PKP", nullptr}, "PKP (long-stroke piston, belt-fed)", Mechanism::LongPiston, 2500, &kBeltMix, false},
    {"Shotgun870", {"870", nullptr}, "870 (pump: duds only)", Mechanism::Manual, 0, &kNoMix, false},
    {"R700", {"R700", "Remington700"}, "Remington 700 (bolt action: duds only)", Mechanism::Manual, 0, &kNoMix, false},
};
// GunKit packs, by pack name (a pack otherwise runs like its donor gun).
static const Profile kPackProfiles[] = {
    {"Noveske", {"Noveske Chainsaw", nullptr}, "Noveske Chainsaw (direct impingement, match grade)", Mechanism::DirectImpingement, 4000, &kRifleMix, false},
};
static const Profile kAverage{"Other", {nullptr, nullptr}, "an average carbine (M4A1)", Mechanism::Unknown, 3600, &kRifleMix, false};

static float FoulingRate(Mechanism a) {
    switch (a) {
    case Mechanism::DirectImpingement: return 1.0f;
    case Mechanism::ShortPiston: return 0.6f;
    case Mechanism::LongPiston: return 0.4f;
    case Mechanism::Roller: return 0.7f;
    case Mechanism::Recoil: return 0.5f;
    case Mechanism::Manual: return 0;
    default: return 0.7f;
    }
}
static float SuppressorFactor(Mechanism a) {
    switch (a) {
    case Mechanism::DirectImpingement: return 1.6f;
    case Mechanism::ShortPiston: return 1.2f;
    case Mechanism::LongPiston: return 1.1f;
    case Mechanism::Roller: return 1.3f;
    case Mechanism::Recoil: return 1.3f;  // a suppressed pistol needs its booster to cycle
    case Mechanism::Manual: return 1.0f;
    default: return 1.3f;
    }
}

static constexpr double kFoulingRounds = 40;      // this many rounds of sustained fire add x1
static constexpr double kFoulingMax = 1.5;        // ... up to +1.5 (x2.5)
static constexpr double kCoolingMs = 90000;       // fouling halves in about a minute

struct GunState {
    const Profile* profile = nullptr;
    double heat = 0;          // rounds of recent fire, decaying
    ULONGLONG heatAt = 0;
};
static std::unordered_map<GUObject*, GunState> s_guns;

static ConfigEntry<bool> s_perGun, s_fouling, s_suppressors, s_limpWrist;
static std::map<std::string, ConfigEntry<float>> s_gunScale;  // [Guns] <key>

static std::string PackOf(Object gun) {
    static auto fn = [] {
        HMODULE m = GetModuleHandleW(L"GunKit.dll");
        return m ? (int (*)(GUObject*, char*, int))GetProcAddress(m, "GunKit_PackOf") : nullptr;
    }();
    char name[128] = "";
    return fn && fn(gun.ptr, name, sizeof name) ? name : "";
}

static const Profile& ProfileOf(Object gun, GunState& g) {
    if (g.profile) return *g.profile;
    std::string pack = PackOf(gun);  // a GunKit gun: its own profile, else its donor's
    for (auto& p : kPackProfiles)
        if (!pack.empty() && pack == p.match[0]) g.profile = &p;
    std::string cls = Object((GUObject*)gun.Class()).Name();
    for (auto& p : kProfiles)
        for (const char* m : p.match)
            if (!g.profile && m && cls.find(m) != std::string::npos) g.profile = &p;
    if (!g.profile) g.profile = &kAverage;
    DLOG("{}{}: runs like {}, one stoppage per {} rounds", gun.Name(), pack.empty() ? "" : " (" + pack + ")", g.profile->label,
         g.profile->mrbs);
    return *g.profile;
}

static bool Suppressed(Object gun) {
    Params ga(gun, "GetAttachedActors");
    ga.Set("bResetArray", true).Set("bRecursivelyIncludeAttachedActors", false).Call();
    auto* arr = (GML_TArray*)ga.Ptr("OutActors");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object att(((GUObject**)arr->Data)[i]);  // the game's own flag (GM9, Osprey, SOCOM338... aren't named so)
        if (att.IsA("GunAttachment_Muzzle") && att.GetBool("bAttached") && att.GetBool("bIsSuppressor")) return true;
    }
    return false;
}

// This gun's chance of each Kind on its next shot, in percent.
struct Factors { double k[5] = {}; float fouling = 1, suppressor = 1, wrist = 1; const Profile* profile = &kAverage; };
static Factors FactorsOf(Object gun, GunState& g, bool suppressed) {
    Factors f;
    double scale = std::max(0.0f, s_multiplier.Value());
    f.k[Dud] = 100.0 / kDudRounds * scale;
    const Profile& p = s_perGun.Value() ? ProfileOf(gun, g) : kAverage;
    f.profile = &p;
    if (p.mrbs <= 0) return f;
    double mech = 100.0 / p.mrbs * scale;
    if (s_perGun.Value()) {
        auto user = s_gunScale.find(p.key);
        mech *= user != s_gunScale.end() ? std::max(0.0f, user->second.Value()) : 1.0f;
        if (s_fouling.Value())
            f.fouling = (float)(1 + std::min(kFoulingMax, g.heat / kFoulingRounds * FoulingRate(p.action)));
        if (s_suppressors.Value() && !p.builtForSuppressor && suppressed) f.suppressor = SuppressorFactor(p.action);
        mech *= f.fouling * f.suppressor;
    }
    f.k[FailureToFeed] = p.mix->ftf * mech;
    f.k[Stovepipe] = p.mix->stovepipe * mech;
    f.k[DoubleFeed] = p.mix->doubleFeed * mech;
    if (s_perGun.Value() && s_limpWrist.Value() && gun.IsA("Handgun") && !CallBool(gun, "IsHeldTwoHanded")) {
        f.wrist = 2.5f;
        f.k[Stovepipe] *= 2.5f;
        f.k[FailureToFeed] *= 1.5f;
    }
    return f;
}

// A shot heats and fouls the gun; the fouling cools off between bursts.
static void Fire(GunState& g, bool suppressed) {
    ULONGLONG now = GetTickCount64();
    if (g.heatAt) g.heat *= std::exp(-double(now - g.heatAt) / kCoolingMs);
    g.heat += suppressed ? 1.5 : 1.0;
    g.heatAt = now;
}

static Kind Roll(Object gun, const Factors& f) {
    std::uniform_real_distribution<double> pct(0.0, 100.0);
    double r = pct(s_rng), edge = 0;
    for (int k = Dud; k <= DoubleFeed; k++) {
        if (!Supports(gun, (Kind)k)) continue;
        edge += f.k[k];
        if (r < edge) return (Kind)k;
    }
    return None;
}

// Puts a stoppage in place once the shot's own cycle has played out.
static void Apply(Object gun, Jam& j) {
    j.applyAt = 0;
    j.watchFrom = GetTickCount64() + kWatchHandleAfterMs;
    j.handleBack = false;
    Object chamber = gun.GetObj("ChamberComponent");
    switch (j.kind) {
    case Dud:
        gun.SetBool("bIsFiringBlocked", true);
        break;
    case FailureToFeed:
        LockActionAt(gun, j, kBoltFtf);
        break;
    case Stovepipe: {
        // The bolt stops short on the case; out of battery, the gun can't fire until it is racked.
        LockActionAt(gun, j, kBoltStovepipe);
        // Base pinched between the bolt face and the chamber, mouth up and out of the port.
        Vec c = LocalOf(gun, gun.GetObj("RoundInChamberLocation"));
        j.casing = AddMesh(gun, RoundMesh(gun, true), {c.x - 2.2, c.y + 0.3, c.z + 0.2}, {58, 73, 0});
        break;
    }
    case DoubleFeed: {
        LockActionAt(gun, j, kBoltDoubleFeed);
        Vec c = LocalOf(gun, gun.GetObj("RoundEjectFromMagazineLocation"));
        // The second round rides under the first, nose up against it.
        j.round = AddMesh(gun, RoundMesh(gun, false), {c.x + 1.0, c.y, c.z - 0.7}, {9, 0, 0});
        break;
    }
    default: break;
    }
    if (chamber) { Params u(chamber, "UpdateProxyRound"); u.Call(); }
    DLOG("{}: {}", gun.Name(), KindName(j.kind));
}

static void Clear(Object gun, Jam& j, const char* how) {
    DLOG("{}: {} cleared ({})", gun.Name(), KindName(j.kind), how);
    Kind kind = j.kind;
    j.kind = None;  // first, so the unlock below isn't refused as the bolt release on a stoppage
    if (kind == Dud) gun.SetBool("bIsFiringBlocked", false);
    if (kind == FailureToFeed || kind == DoubleFeed) ChamberBlocked(gun, false);
    Remove(j.casing);
    Remove(j.round);
    RestoreActionLock(gun, j);
    Kind armed = j.armed;
    j = Jam{};
    j.armed = armed;
}

// GunChamberComponent:OnFired (pre): a round has just been fired. Rolls what goes wrong with the next.
static bool OnFired(Object chamber) {
    Object gun = Receiver(chamber);
    if (!gun || !s_enabled.Value() || !HeldByLocalPlayer(gun)) return false;
    GunState& g = s_guns[gun.ptr];
    bool suppressed = s_suppressors.Value() && Suppressed(gun);
    Factors f = FactorsOf(gun, g, suppressed);  // as the gun was when this round went off
    Fire(g, suppressed);
    Jam& j = s_jams[gun.ptr];
    if (j.kind != None) return false;
    Kind k = j.armed != None ? j.armed : Roll(gun, f);
    j.armed = None;
    if (k == None || Ammo(gun) <= 0) return false;  // nothing left to feed: nothing to jam
    j.kind = k;
    DLOG("{}: {} - {}, fouling x{:.2f}, suppressor x{:.2f}, grip x{:.2f}", gun.Name(), KindName(k), f.profile->label,
         f.fouling, f.suppressor, f.wrist);
    // On full auto the next shot can come within ~80 ms, so whatever stops it happens right here; the
    // bolt is held back once the cycle has played (a stovepipe's at once: the bolt never closes).
    j.applyAt = GetTickCount64() + (k == Stovepipe ? 1 : kSettleMs);
    if (k == Dud) gun.SetBool("bIsFiringBlocked", true);
    if (k == FailureToFeed || k == DoubleFeed) ChamberBlocked(gun, true);  // the next round stops short
    return false;  // the shot itself always cycles as the game plays it
}

// Racked all the way back: seen from the handle's own travel (Watch) or the game's HandleBackServer,
// whichever comes first.
static void RackBack(Object gun, Jam& j) {
    if (j.kind == None || j.handleBack) return;
    j.handleBack = true;
    if (j.kind == DoubleFeed && !j.magOut) {
        // Racking does not clear a double feed while the magazine holds the rounds in: the bolt
        // jams again on the way forward (RackForward), not while the handle is still held back.
        j.applyAt = kOnRackForward;
        Remove(j.round);
        DLOG("{}: double feed - strip the magazine first", gun.Name());
        return;
    }
    if (j.kind == DoubleFeed) ClearChamber(gun);
    Clear(gun, j, "racked");
}

// Forward again: with a double feed still in, the bolt jams again.
static void RackForward(Object gun, Jam& j) {
    if (!j.handleBack) return;
    j.handleBack = false;
    if (j.applyAt == kOnRackForward) j.applyAt = GetTickCount64() + kSettleMs;
}

static Jam* JamOfHandle(Object handle, Object& gun) {
    gun = Receiver(handle);
    auto it = gun ? s_jams.find(gun.ptr) : s_jams.end();
    return it == s_jams.end() ? nullptr : &it->second;
}

// The hammer falls on the dud: one click, then a dead trigger until it is racked. Seen both through
// GunReceiver:SetIsFiring and the trigger axis; whichever comes first plays it.
static void TriggerPulled(Object gun, Jam& j) {
    ULONGLONG now = GetTickCount64();
    if (now - j.lastPull < 250) return;
    j.lastPull = now;
    PlayAt(gun, j.clicked ? "DeadTriggerPulled" : "ShotsEmpty");
    DLOG("{}: {}", gun.Name(), j.clicked ? "dead trigger" : "click");
    j.clicked = true;
}

static void Watch() {
    ULONGLONG now = GetTickCount64();
    for (auto it = s_jams.begin(); it != s_jams.end();) {
        Object gun(it->first);
        Jam& j = it->second;
        if (!gun) { it = s_jams.erase(it); continue; }
        if (j.kind == None) { ++it; continue; }
        // Racking, read off the handle / slide itself: whatever path the game takes to move it, a full
        // pull clears the stoppage (a double feed only with the magazine out).
        float handle = HandleProgress(gun);
        bool settled = !j.applyAt || j.applyAt == kOnRackForward;
        if (settled && now >= j.watchFrom) {
            if (handle >= kBack) RackBack(gun, j);
            else if (handle <= kForward) RackForward(gun, j);
        }
        if (j.kind == None) { ++it; continue; }
        if (j.applyAt && now >= j.applyAt) Apply(gun, j);
        if (j.applyAt) { ++it; continue; }
        switch (j.kind) {
        case Dud: {
            gun.SetBool("bIsFiringBlocked", true);
            bool down = gun.Get<float>("CurrentTrigger") > 0.6f;
            if (down && !j.triggerWasDown) TriggerPulled(gun, j);
            j.triggerWasDown = down;
            break;
        }
        case FailureToFeed:
        case Stovepipe:
        case DoubleFeed:
            // Something let the bolt go with the handle at rest (not you racking it): it jams again.
            // While the handle is being pulled it is left alone, or a rack could never finish.
            if (!ActionLocked(gun) && !j.handleBack && handle < kAtRest)
                LockActionAt(gun, j, j.kind == FailureToFeed ? kBoltFtf : j.kind == Stovepipe ? kBoltStovepipe : kBoltDoubleFeed);
            if (j.kind == DoubleFeed && !j.magOut && !gun.GetObj("CurrentMagazine")) {
                j.magOut = true;
                Remove(j.round);  // the loose round falls out with the magazine
                DLOG("{}: double feed - magazine stripped, rack it clear", gun.Name());
            }
            break;
        default: break;
        }
        ++it;
    }
}

// ------------------------------------------------------------------ the P320 is not drop safe
//
// Before SIG's 2017 voluntary upgrade, a P320 dropped at -30 degrees, so that the back of the slide
// and frame struck a hard surface first, could fire: the impact stops the gun, and the trigger's own
// inertia carries it on rearward far enough to release the striker (SIG's statement; the upgrade
// lightened the trigger and sear). It was the only one of 11 pistols to fire in Omaha Outdoors' drop
// tests, and a brand-new one fired on two of three drops.
//
// The game reports no impact for a gun that merely falls, so every P320 nobody holds is watched while
// it is free to fall: a sudden stop in its fall is an impact. What struck is the corner of the gun
// that was lowest; how hard is its speed rear-first, along the bore, just before.

static ConfigEntry<bool> s_p320DropFire;
static ConfigEntry<float> s_dropFireChance;
static constexpr double kDropRearSpeed = 190;  // cm/s along the bore: about a 75 cm fall with the bore at 30 degrees
static constexpr double kImpactStop = 150;     // cm/s of fall lost in one frame: it hit something
static constexpr ULONGLONG kDropFireGapMs = 1000;  // one discharge per fall, however it bounces

// A P320 free to fall, as of the last frame.
struct FreeP320 {
    bool falling = false;
    double fall = 0;        // cm/s, + = down
    double rearFirst = 0;   // cm/s along the bore, + = moving back-end first
    bool slideRearLowest = false;
    ULONGLONG firedAt = 0;
};
static std::unordered_map<GUObject*, FreeP320> s_p320s;

static Vec GunSpace(Object gun, Vec v, bool direction) {  // a world location or direction in the gun's own space
    Params gt(gun, "GetTransform");
    gt.Call();
    Params inv(Lib("KismetMathLibrary"), direction ? "InverseTransformDirection" : "InverseTransformLocation");
    GML_PropInfo ri;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(gun.Struct(), "GetTransform"), "ReturnValue"), &ri);
    std::memcpy(inv.Ptr("T"), gt.Ptr("ReturnValue"), ri.size);
    inv.Set(direction ? "Direction" : "Location", v).Call();
    return inv.Return<Vec>();
}

// Whether the lowest corner of the gun's bounding box is its back top - the back of the slide.
static bool SlideRearLowest(Object gun) {
    Vec up = GunSpace(gun, {0, 0, 1}, true);  // world up, in gun space
    return up.x > 0 && up.z < 0;  // the back end and the top both point down
}

static void DropFire(Object gun, FreeP320& s) {
    bool chambered = CallBool(gun, "CanFireRound");
    DLOG("{} dropped: struck {}, rear-first at {:.0f} cm/s (needs {:.0f}), round chambered: {}", gun.Name(),
         s.slideRearLowest ? "on the back of the slide" : "elsewhere", s.rearFirst, kDropRearSpeed, chambered);
    if (!s.slideRearLowest || s.rearFirst < kDropRearSpeed || !chambered) return;
    ULONGLONG now = GetTickCount64();
    if (now - s.firedAt < kDropFireGapMs) return;
    std::uniform_real_distribution<double> pct(0.0, 100.0);
    if (pct(s_rng) >= s_dropFireChance.Value()) {
        DLOG("{}: the trigger held this time", gun.Name());
        return;
    }
    s.firedAt = now;
    Log("{} was dropped on the back of its slide and fired", gun.Name());
    Params fire(gun, "FireRoundLocal");
    fire.Call();  // zero overrides: out of its own muzzle, wherever that points
}

static void WatchDroppedP320s() {  // every frame
    if (!s_enabled.Value() || !s_p320DropFire.Value()) return;
    static ULONGLONG nextScan = 0;
    ULONGLONG now = GetTickCount64();
    if (now >= nextScan) {  // the P320s in the world, twice a second
        nextScan = now + 500;
        std::unordered_map<GUObject*, FreeP320> found;
        for (Object gun : FindAllOf("Firearm_Pistol_P320_C")) {
            auto it = s_p320s.find(gun.ptr);
            found[gun.ptr] = it != s_p320s.end() ? it->second : FreeP320{};
        }
        s_p320s.swap(found);
    }
    for (auto& [ptr, s] : s_p320s) {
        Object gun(ptr);
        if (!API->IsValid(ptr)) continue;
        Object root = gun.GetObj("StaticMeshComponent");
        Params holder(gun, "GetHoldingCharacter");
        holder.Call();
        Params sim(root, "IsSimulatingPhysics");
        if (root) sim.Call();
        // In a hand, holstered or set down by the game it isn't falling; and the server decides.
        if (!root || holder.Return<GUObject*>() || !sim.Return<bool>() || !CallBool(gun, "HasAuthority")) {
            s.falling = false;
            continue;
        }
        Params pv(root, "GetPhysicsLinearVelocity");
        pv.Call();
        Vec v = pv.Return<Vec>();
        if (s.falling && s.fall > kImpactStop && s.fall + v.z > kImpactStop) DropFire(gun, s);  // its fall stopped short
        s.falling = true;
        s.fall = -v.z;
        s.rearFirst = -GunSpace(gun, v, true).x;
        s.slideRearLowest = SlideRearLowest(gun);
    }
}

// ------------------------------------------------------------------ API for other plugins

// Arms `kind` (1 dud, 2 failure to feed, 3 stovepipe, 4 double feed; 0 disarms) for the gun's next
// shot. Returns 0 when the gun cannot have that stoppage (bolt actions and pumps only get duds). [GT]
extern "C" __declspec(dllexport) int Malfunctions_Arm(GUObject* receiver, int kind) {
    if (!Object(receiver) || kind < None || kind > DoubleFeed) return 0;
    if (kind != None && !Supports(Object(receiver), (Kind)kind)) return 0;
    s_jams[receiver].armed = (Kind)kind;
    return 1;
}
// The stoppage the gun is in right now (0 = none). [GT]
// How the gun runs right now: out[0..4] = its chance of each kind on the next shot, in percent
// (index = kind, 0 unused), `label` = the gun it runs like; returns the gun's recent fouling in rounds. [GT]
extern "C" __declspec(dllexport) float Malfunctions_Factors(GUObject* receiver, float* out, char* label, int capacity) {
    Object gun(receiver);
    if (!gun) return 0;
    GunState& g = s_guns[receiver];
    if (g.heatAt) {  // as it has cooled by now
        ULONGLONG now = GetTickCount64();
        g.heat *= std::exp(-double(now - g.heatAt) / kCoolingMs);
        g.heatAt = now;
    }
    Factors f = FactorsOf(gun, g, s_suppressors.Value() && Suppressed(gun));
    for (int k = 0; out && k < 5; k++) out[k] = (float)f.k[k];
    if (label && capacity > 0) strncpy_s(label, capacity, f.profile->label, _TRUNCATE);
    return (float)g.heat;
}
extern "C" __declspec(dllexport) int Malfunctions_Current(GUObject* receiver) {
    auto it = s_jams.find(receiver);
    if (it == s_jams.end()) return None;
    return it->second.applyAt && it->second.applyAt != kOnRackForward ? None : it->second.kind;
}

// ------------------------------------------------------------------ plugin

GML_AWAKE() {
    s_enabled = Config.Bind("General", "Enabled", true, "Guns you shoot can malfunction.");
    s_multiplier = Config.Bind("Rates", "Multiplier", 1.0f,
        "Every malfunction, x its real-world rate. 1 = real life: an M4A1 stops about once every 3,600\n"
        "rounds, a Glock once every 3,750, a P320 once every 1,700, and about 1 round in 300,000 is a dud.\n"
        "Raise it to see them more often (100 = an M4A1 about once a magazine).");
    s_log = Config.Bind("Debug", "Log", false, "Log every malfunction and how it was cleared.");
    s_perGun = Config.Bind("Realism", "PerGun", true,
        "Each gun stops as often as the real one does (the README lists the figures and their sources).\n"
        "Off: every gun runs like an M4A1. Duds come from the ammunition and are the same in all.");
    s_fouling = Config.Bind("Realism", "Fouling", true,
        "Sustained fire fouls the action: up to 2.5x the feeding and extraction stoppages, fastest in\n"
        "direct-impingement guns. It wears off over a minute or two between fights.");
    s_suppressors = Config.Bind("Realism", "Suppressors", true,
        "A suppressor pushes more gas and fouling into the action (direct impingement worst), except in\n"
        "guns built around one (MP5SD, KS-1).");
    s_limpWrist = Config.Bind("Realism", "LimpWrist", true,
        "A handgun held in one hand stovepipes 2.5x and fails to feed 1.5x as often.");
    s_p320DropFire = Config.Bind("DropFire", "P320", true,
        "The P320 is not drop safe, as P320s before SIG's 2017 upgrade weren't: dropped so the back of\n"
        "the slide and frame hit the ground first (rear end low, about -30 degrees) from about waist\n"
        "height or more, it can fire the round in its chamber.");
    s_dropFireChance = Config.Bind("DropFire", "Chance", 67.0f,
        "Percent of such drops that fire it (a new P320 fired on two of three drops).");
    for (const Profile* list : {kProfiles, kPackProfiles, &kAverage}) {
        size_t n = list == kProfiles ? std::size(kProfiles) : list == kPackProfiles ? std::size(kPackProfiles) : 1;
        for (size_t i = 0; i < n; i++)
            s_gunScale[list[i].key] = Config.Bind("Guns", list[i].key, 1.0f,
                list[i].mrbs > 0
                    ? std::format("Stoppages in the {}, x its real-world rate (about one per {} rounds; lower = more reliable).",
                                  list[i].label, (int)list[i].mrbs).c_str()
                    : std::format("The {}: only duds, which come from the ammunition (no effect).", list[i].label).c_str());
    }

    Hook("GunChamberComponent:OnFired", [](Object self, GUFunction*, void*) { return OnFired(self); });
    HookAfter("GunHandleComponent:HandleBackServer", [](Object self, GUFunction*, void*) {
        Object gun;
        if (Jam* j = JamOfHandle(self, gun); j && (!j->applyAt || j->applyAt == kOnRackForward) && GetTickCount64() >= j->watchFrom)
            RackBack(gun, *j);
    });
    HookAfter("GunHandleComponent:HandleForwardServer", [](Object self, GUFunction*, void*) {
        Object gun;
        if (Jam* j = JamOfHandle(self, gun)) RackForward(gun, *j);
    });
    Hook("GunReceiver:SetIsFiring", [](Object gun, GUFunction* fn, void* params) {
        auto it = s_jams.find(gun.ptr);
        if (it != s_jams.end() && it->second.kind == Dud && !it->second.applyAt && Param<bool>(fn, params, "Trigger"))
            TriggerPulled(gun, it->second);
        return false;
    });
    // The bolt / slide release can't drop the action on a stoppage. Only refused with the handle at
    // rest: while you rack, the game may let the bolt go through the same calls.
    for (const char* f : {"Rifle:ReleaseBoltServer", "Rifle:UnlockBolt", "Handgun:ReleaseSlideServer", "Handgun:UnlockSlide"})
        Hook(f, [](Object gun, GUFunction*, void*) {
            auto it = s_jams.find(gun.ptr);
            return it != s_jams.end() && it->second.kind >= FailureToFeed && !it->second.applyAt && !it->second.handleBack &&
                   HandleProgress(gun) < kAtRest;
        });
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void*) {
        s_p320s.clear();
        s_jams.clear();
        s_guns.clear();  // a new level's guns may reuse the old ones' addresses
    });
    On(GML_EVENT_TICK, [](void*) {
        // Every frame while a gun is jammed (trigger clicks, a handle to watch), otherwise ten times a second.
        static ULONGLONG last = 0;
        ULONGLONG now = GetTickCount64();
        bool jammed = false;
        for (auto& [g, j] : s_jams) jammed |= j.kind != None;
        if (jammed || now - last >= 100) { last = now; Watch(); }
        WatchDroppedP320s();
    });
    Message("Malfunctions {} - real-world rates x{}: {}, duds 1 in {:.0f} rounds", MALFUNCTIONS_VERSION, s_multiplier.Value(),
            s_perGun.Value() ? "each gun stops as often as the real one" : "every gun stops as often as an M4A1 (1 in 3600 rounds)",
            kDudRounds);
    return 0;
}
