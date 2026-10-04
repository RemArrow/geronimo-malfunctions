// MalfunctionsDev - in-game research/test harness for the Malfunctions plugin. Not shipped.
// Loads TrainingGrounds, takes the player's loadout rifle, holds it in front of the camera and walks
// through the firing cycle: fire, rack, and the game's own blocking switches, logging the chamber,
// bolt and ammo state after each step ("STATE ..."). "SNAP <name>" = a game-rendered screenshot.
#include <GML/GML.hpp>
#include <windows.h>
#include <cmath>

using namespace gml;

GML_PLUGIN("com.remarrow.malfunctionsdev", "MalfunctionsDev", "0.1.0");

struct Vec { double x, y, z; };
struct Rot { double pitch, yaw, roll; };

static const char* kMap = "/Game/Maps/Training/TrainingGrounds";
static Object s_gun;
static int s_step = -1;
static double s_wait = 0;
static double s_dist = 60;
static Vec s_focus{0, 0, 0};
static double s_roll = 0;  // tilts the gun's top towards the camera
static bool Pistol() { return s_gun.IsA("Handgun"); }
static Object Handle() { return s_gun.GetObj(Pistol() ? "Slide" : "ChargingHandle"); }

static void Hold() {
    if (!s_gun || !API->IsValid(s_gun.ptr)) return;
    Params p(Lib("GameplayStatics"), "GetPlayerCameraManager");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    Object pcm = p.Return<GUObject*>();
    if (!pcm) return;
    Params lp(pcm, "GetCameraLocation"); lp.Call();
    Params rp(pcm, "GetCameraRotation"); rp.Call();
    Vec cam = lp.Return<Vec>();
    Rot rot = rp.Return<Rot>();
    double t = rot.yaw * 3.14159265358979 / 180;
    // s_focus (a point in gun space) sits s_dist in front of the camera. The gun's forward is the
    // camera's right and its right (ejection port) side faces the camera.
    Vec f{-std::sin(t), std::cos(t), 0}, r{-std::cos(t), -std::sin(t), 0};
    Vec at{cam.x + std::cos(t) * s_dist - s_focus.x * f.x - s_focus.y * r.x,
           cam.y + std::sin(t) * s_dist - s_focus.x * f.y - s_focus.y * r.y, cam.z - 2 - s_focus.z};
    Rot side{0, rot.yaw + 90, s_roll};
    Params mv(s_gun, "K2_SetActorLocationAndRotation");
    mv.Set("NewLocation", at).Set("NewRotation", side).Set("bSweep", false).Set("bTeleport", true).Call();
}

static bool CallBool(Object o, const char* fn) {
    Params p(o, fn);
    p.Call();
    return p.Return<bool>();
}

// A world location in the gun's own space.
static Vec Local(Vec w) {
    Params gt(s_gun, "GetTransform");
    gt.Call();
    Params inv(Lib("KismetMathLibrary"), "InverseTransformLocation");
    GML_PropInfo ri;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(s_gun.Struct(), "GetTransform"), "ReturnValue"), &ri);
    std::memcpy(inv.Ptr("T"), gt.Ptr("ReturnValue"), ri.size);
    inv.Set("Location", w).Call();
    return inv.Return<Vec>();
}
static double BoltX() {
    Object mov = s_gun.GetObj("Movables");
    Params sl(mov, "GetSocketLocation");
    *(uint64_t*)sl.Ptr("InSocketName") = API->MakeName("RootBoltCarrierGroup");
    sl.Call();
    return Local(sl.Return<Vec>()).x;
}
static void Where(const char* what, Object comp) {
    if (!comp) { Log("  {}: none", what); return; }
    Params wl(comp, "K2_GetComponentLocation"); wl.Call();
    Params wr(comp, "K2_GetComponentRotation"); wr.Call();
    Params ws(comp, "K2_GetComponentScale"); ws.Call();
    Params vis(comp, "IsVisible"); vis.Call();
    Vec l = Local(wl.Return<Vec>()), s = ws.Return<Vec>();
    Rot r = wr.Return<Rot>();
    Params gr(s_gun, "K2_GetActorRotation"); gr.Call();
    Rot g = gr.Return<Rot>();
    Params par(comp, "GetAttachParent"); par.Call();
    Params sock(comp, "GetAttachSocketName"); sock.Call();
    Object parent = par.Return<GUObject*>();
    char socket[128] = "";
    API->NameToString(sock.Return<uint64_t>(), socket, sizeof socket);
    Log("  {}: local ({:.2f}, {:.2f}, {:.2f}) rot-vs-gun (p {:.0f} y {:.0f} r {:.0f}) scale ({:.2f}, {:.2f}, {:.2f}) visible={} parent={} socket={}",
        what, l.x, l.y, l.z, r.pitch - g.pitch, r.yaw - g.yaw, r.roll - g.roll, s.x, s.y, s.z, vis.Return<bool>(),
        parent ? parent.Name() : "-", socket);
}
static void Proxy() {
    Object pr = s_gun.GetObj("ProxyRound");
    Where("ProxyRound", pr);
    if (pr) {
        Object mesh = pr.GetObj("StaticMesh");
        Log("  ProxyRound mesh {}", mesh ? mesh.Name() : "-");
    }
    Where("RoundEjectFromMagazineLocation", s_gun.GetObj("RoundEjectFromMagazineLocation"));
    Where("RoundInChamberLocation", s_gun.GetObj("RoundInChamberLocation"));
    Where("RoundEjectLocation", s_gun.GetObj("RoundEjectLocation"));
}

// The Malfunctions plugin's API (it must be installed alongside).
static int (*s_arm)(GUObject*, int);
static int (*s_current)(GUObject*);
static int Jammed() { return s_current ? s_current(s_gun.ptr) : -1; }
static void Arm(int kind) {
    if (!s_arm) { Error("Malfunctions.dll is not loaded"); return; }
    s_arm(s_gun.ptr, kind);
}

static void State(const char* what) {
    Object ch = s_gun.GetObj("ChamberComponent"), handle = Handle(), well = s_gun.GetObj("MagWellComponent");
    int ammo = -1;
    if (well) { Params a(well, "GetCurrentAmmo"); a.Call(); ammo = a.Return<int32_t>(); }
    Log("STATE [{}] jam={} chamber round={} spent={} loaded={} chamberBlocked={} | firing={} firingBlocked={} cyclingBlocked={} canFire={} mode={} | ammo={} | handle {:.2f} locked={} | bolt {:.2f} locked={} lockAt {:.2f} x {:.2f}",
        what, Jammed(), ch.Get<uint8_t>("RoundState"), ch.GetBool("bIsRoundSpent"), CallBool(ch, "HasRoundLoadedInChamber"),
        CallBool(ch, "IsChamberingBlocked"), s_gun.GetBool("bIsFiring"), s_gun.GetBool("bIsFiringBlocked"),
        s_gun.GetBool("bCyclingBlocked"), CallBool(s_gun, "CanFireRound"), s_gun.Get<uint8_t>("FireMode"), ammo,
        handle ? handle.Get<float>("CurrentProgress") : -1.0f, handle ? handle.GetBool("bIsLocked") : false,
        s_gun.Get<float>(Pistol() ? "CurrentSlideProgress" : "CurrentBoltProgress"), s_gun.GetBool(Pistol() ? "bIsSlideLocked" : "bIsBoltLocked"),
        s_gun.Get<float>(Pistol() ? "SlideLockedProgress" : "BoltLockedProgress"), BoltX());
}

static void Snap(const char* n) {
    Log("SNAP {}", n);
    API->ExecConsoleCommand("shot");
}

static void HandleTo(float p) {
    Params sp(Handle(), "SetProgress");
    sp.Set("Progress", p).Call();
}
static void Rack(bool back) {
    Object h = Handle();
    HandleTo(back ? 1.0f : 0.0f);
    Params p(h, back ? "HandleBackServer" : "HandleForwardServer");
    p.Call();
}
static void SetMode(uint8_t m) {
    Params p(s_gun, "UpdateFireModeReplicated");
    p.Set("NewFireMode", m).Call();
}
static void Firing(bool on) {
    Params p(s_gun, "SetIsFiring");
    p.Set("Trigger", on).Call();
}
static void FireLocal() {
    Params p(s_gun, "FireRoundLocal");
    p.Call();  // zero override direction/origin: the gun's own
}
static void ChamberBlock(bool b) {
    Params p(s_gun.GetObj("ChamberComponent"), "SetChamberingBlocked");
    p.Set("bValue", b).Call();
}
static void Lock(float at) {
    Params p(Handle(), "LockHandle");
    p.Set("Progress", at).Call();
}
static void Unlock() {
    Params p(Handle(), "UnlockHandle");
    p.Call();
}

static bool s_skipOnFired = false;
static float s_boltLockAt = 0;

static Object Pawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}
static void HoldBy(Object who) {
    Params p(s_gun, "SetHoldingCharacter");
    p.SetObj("_HoldingCharacter", who).Call();
}
// What a trigger pull would do: fire only if the gun says it can.
static void TryFire() {
    bool can = CallBool(s_gun, "CanFireRound");
    Log("  trigger: canFire={}", can);
    if (can) FireLocal();
}
static void StovepipeLock(float at) {
    s_boltLockAt = s_gun.Get<float>("BoltLockedProgress");
    s_gun.Set("BoltLockedProgress", at);
    Params p(s_gun, "LockBolt");
    p.Set("bManualLock", true).Call();
}
static void SpentInPort() {
    Object ch = s_gun.GetObj("ChamberComponent");
    ch.Set("RoundState", (uint8_t)3);
    ch.Set("bIsRoundSpent", true);
    Params p(ch, "UpdateProxyRound");
    p.Call();
}

static void BoltLockAt(float at) {
    if (s_gun.GetBool("bIsBoltLocked")) { Params u(s_gun, "UnlockBolt"); u.Call(); }
    s_gun.Set("BoltLockedProgress", at);
    Params p(s_gun, "LockBolt");
    p.Set("bManualLock", true).Call();
}
static void RestoreBoltLock() { s_gun.Set("BoltLockedProgress", s_boltLockAt); }

// A spent case caught in the ejection port: a StaticMeshComponent with the gun's own spent-round
// mesh, attached to the receiver at `at` (gun space) with rotation `r` relative to the gun.
static Object s_casing;
static void Casing(Vec at, Rot r) {
    Params mesh(s_gun, "GetProxyRoundMesh");
    mesh.Set("bSpent", true).Call();
    Params mt(Lib("KismetMathLibrary"), "MakeTransform");
    mt.Set("Scale", Vec{1, 1, 1}).Call();
    Params add(s_gun, "AddComponentByClass");
    *(GUClass**)add.Ptr("Class") = FindClass("StaticMeshComponent");
    add.Set("bManualAttachment", true).Set("bDeferredFinish", false);
    GML_PropInfo ti;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetMathLibrary").Struct(), "MakeTransform"), "ReturnValue"), &ti);
    std::memcpy(add.Ptr("RelativeTransform"), mt.Ptr("ReturnValue"), ti.size);
    add.Call();
    s_casing = add.Return<GUObject*>();
    if (!s_casing) { Error("no casing component"); return; }
    Params sm(s_casing, "SetStaticMesh");
    sm.SetObj("NewMesh", mesh.Return<GUObject*>()).Call();
    Params col(s_casing, "SetCollisionEnabled");
    col.Set("NewType", (uint8_t)0).Call();
    Params root(s_gun, "K2_GetRootComponent");
    root.Call();
    Params at2(s_casing, "K2_AttachToComponent");
    at2.SetObj("Parent", Object(root.Return<GUObject*>())).Set("LocationRule", (uint8_t)2).Set("RotationRule", (uint8_t)2)
        .Set("ScaleRule", (uint8_t)2).Set("bWeldSimulatedBodies", false).Call();
    Params rel(s_casing, "K2_SetRelativeLocationAndRotation");
    rel.Set("NewLocation", at).Set("NewRotation", r).Set("bSweep", false).Set("bTeleport", true).Call();
    Log("  casing mesh {} at ({:.2f}, {:.2f}, {:.2f}) p {:.0f} y {:.0f} r {:.0f}", Object(mesh.Return<GUObject*>()).Name(),
        at.x, at.y, at.z, r.pitch, r.yaw, r.roll);
}
static void NoCasing() {
    if (!s_casing) return;
    Params d(s_casing, "K2_DestroyComponent");
    d.SetObj("Object", s_casing).Call();
    s_casing = Object();
}
static Vec ChamberAt() {
    Params wl(s_gun.GetObj("RoundInChamberLocation"), "K2_GetComponentLocation");
    wl.Call();
    return Local(wl.Return<Vec>());
}

static void TriggerPull(bool down) { s_gun.Set("CurrentTrigger", down ? 1.0f : 0.0f); }
static void DropMag() { Params p(s_gun, "MagReleasePressed"); p.Call(); Params r(s_gun, "MagReleaseReleased"); r.Call(); }
static void NewMag() {
    Params p(s_gun.GetObj("MagWellComponent"), "SpawnAndInsertMagazine");
    p.Set("AcceptedMagazineIndex", (int32_t)0).Call();
}
static void BoltRelease() { Params p(s_gun, Pistol() ? "ReleaseSlideServer" : "ReleaseBoltServer"); p.Call(); }

// One step per entry: action, then wait, then log the state. "@name" stages also dump the proxy
// round and take a game-rendered screenshot once their state is logged.
struct Stage { const char* name; void (*act)(); double wait; };
static const Stage kStages[] = {
    {"initial", [] {}, 0.5},
    {"semi, held by the player, close-up", [] { SetMode(1); HoldBy(Pawn()); s_dist = 22; s_focus = ChamberAt(); s_focus.x -= 3; }, 0.5},
    {"normal shot", [] { TryFire(); }, 0.8},
    // dud
    {"DUD armed, shot", [] { Arm(1); TryFire(); }, 0.8},
    {"dud: trigger pull", [] { TriggerPull(true); }, 0.3},
    {"dud: trigger release", [] { TriggerPull(false); }, 0.3},
    {"dud: trigger (SetIsFiring)", [] { Firing(true); }, 0.3},
    {"dud: trigger off", [] { Firing(false); }, 0.3},
    {"dud: trigger again", [] { Firing(true); }, 0.3},
    {"dud: trigger off", [] { Firing(false); }, 0.3},
    {"dud: fire", [] { TryFire(); }, 0.5},
    {"dud: rack back", [] { Rack(true); }, 0.5},
    {"dud: rack forward", [] { Rack(false); }, 0.8},
    {"dud: fire after clearing", [] { TryFire(); }, 0.8},
    // failure to feed
    {"FTF armed, shot", [] { Arm(2); TryFire(); }, 0.8},
    {"@ftf", [] {}, 0.2},
    {"@ftf_top", [] { s_roll = 50; }, 0.3},
    {"camera back", [] { s_roll = 0; }, 0.2},
    {"ftf: fire", [] { TryFire(); }, 0.5},
    {"ftf: bolt release", [] { BoltRelease(); }, 0.5},
    {"ftf: rack back", [] { Rack(true); }, 0.5},
    {"ftf: rack forward", [] { Rack(false); }, 0.8},
    {"ftf: fire after clearing", [] { TryFire(); }, 0.8},
    // stovepipe
    {"STOVEPIPE armed, shot", [] { Arm(3); TryFire(); }, 0.8},
    {"@stovepipe", [] {}, 0.2},
    {"@stovepipe_top", [] { s_roll = 50; }, 0.3},
    {"camera back", [] { s_roll = 0; }, 0.2},
    {"stovepipe: fire", [] { TryFire(); }, 0.5},
    {"stovepipe: rack back", [] { Rack(true); }, 0.5},
    {"stovepipe: rack forward", [] { Rack(false); }, 0.8},
    {"stovepipe: fire after clearing", [] { TryFire(); }, 0.8},
    // on full auto the next shot comes ~80 ms later: it must already be stopped
    {"auto: STOVEPIPE armed, shot", [] { SetMode(2); Arm(3); TryFire(); }, 0.08},
    {"auto: next shot", [] { TryFire(); }, 0.08},
    {"auto: rack back", [] { Rack(true); }, 0.5},
    {"auto: rack forward", [] { Rack(false); }, 0.8},
    {"auto: DUD armed, shot", [] { Arm(1); TryFire(); }, 0.08},
    {"auto: next shot", [] { TryFire(); }, 0.08},
    {"auto: rack back", [] { Rack(true); }, 0.5},
    {"auto: rack forward, semi", [] { Rack(false); SetMode(1); }, 0.8},
    // double feed
    {"DOUBLE FEED armed, shot", [] { Arm(4); TryFire(); }, 0.8},
    {"@doublefeed", [] {}, 0.2},
    {"@doublefeed_top", [] { s_roll = 50; }, 0.3},
    {"camera back", [] { s_roll = 0; }, 0.2},
    {"df: rack back (mag in)", [] { Rack(true); }, 0.5},
    {"df: rack forward (mag in)", [] { Rack(false); }, 0.8},
    {"df: fire", [] { TryFire(); }, 0.5},
    {"df: strip the magazine", [] { DropMag(); }, 1.5},
    {"df: rack back", [] { Rack(true); }, 0.5},
    {"df: rack forward", [] { Rack(false); }, 0.8},
    {"df: new magazine", [] { NewMag(); }, 1.5},
    {"df: rack back", [] { Rack(true); }, 0.5},
    {"df: rack forward", [] { Rack(false); }, 0.8},
    {"df: fire after clearing", [] { TryFire(); }, 0.8},
};

// random.flag beside the DLL: no arming - the plugin's own roll (run it with [Rates] Multiplier set very high).
static const Stage kRandom[] = {
    {"initial", [] {}, 0.5},
    {"semi, held by the player", [] { SetMode(1); HoldBy(Pawn()); }, 0.5},
    {"shot 1", [] { TryFire(); }, 0.8},
    {"rack back", [] { Rack(true); }, 0.5},
    {"rack forward", [] { Rack(false); }, 0.8},
    {"shot 2", [] { TryFire(); }, 0.8},
    {"rack back", [] { Rack(true); }, 0.5},
    {"rack forward", [] { Rack(false); }, 0.8},
    {"shot 3", [] { TryFire(); }, 0.8},
};
// realism.flag: how the gun runs - its profile, then fouling building up over sustained fire and
// cooling off (at the real-world rates nothing is likely to jam during its 60 shots).
static void Factors(const char* when) {
    static auto fn = (float (*)(GUObject*, float*, char*, int))GetProcAddress(GetModuleHandleW(L"Malfunctions.dll"), "Malfunctions_Factors");
    if (!fn) { Error("Malfunctions_Factors missing"); return; }
    float k[5] = {};
    char label[128] = "";
    float heat = fn(s_gun.ptr, k, label, sizeof label);
    Log("FACTORS [{}] {} | fouling {:.1f} rounds | per shot: dud {:.5f}% ftf {:.4f}% stovepipe {:.4f}% double feed {:.4f}%", when, label,
        heat, k[1], k[2], k[3], k[4]);
}
static int Ammo() {
    Object well = s_gun.GetObj("MagWellComponent");
    if (!well) return 0;
    Params a(well, "GetCurrentAmmo");
    a.Call();
    return a.Return<int32_t>();
}
static void Burst() {
    for (int i = 0; i < 10; i++) {
        if (Ammo() < 3) NewMag();
        if (CallBool(s_gun, "CanFireRound")) FireLocal();
    }
}
#define SHOT {"shot", [] { if (!s_gun.GetObj("CurrentMagazine")) NewMag(); else if (Ammo() < 2) DropMag(); else if (CallBool(s_gun, "CanFireRound")) FireLocal(); }, 0.09},
#define SHOTS10 SHOT SHOT SHOT SHOT SHOT SHOT SHOT SHOT SHOT SHOT
// A SureFire RC3 on the gun, as far as the plugin looks: attached to it and flagged attached.
static void Suppress() {
    GUClass* c = API->LoadClass("/Game/Weapons/Attachments/Muzzles/Muzzle_Suppressor_Surefire_RC3/Blueprints/Muzzle_Suppressor_Surefire_RC3.Muzzle_Suppressor_Surefire_RC3_C");
    if (!c) { Error("RC3 class not found"); return; }
    Params gt(s_gun, "GetTransform");
    gt.Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(s_gun.Struct(), "GetTransform"), "ReturnValue"), &xi);
    Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
    b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", c);
    std::memcpy(b.Ptr("SpawnTransform"), gt.Ptr("ReturnValue"), xi.size);
    b.Set("CollisionHandlingOverride", (uint8_t)1).Call();
    Object att = b.Return<GUObject*>();
    if (!att) { Error("could not spawn the RC3"); return; }
    Params f(Lib("GameplayStatics"), "FinishSpawningActor");
    f.SetObj("Actor", att);
    std::memcpy(f.Ptr("SpawnTransform"), gt.Ptr("ReturnValue"), xi.size);
    f.Call();
    if (Object root = att.GetObj("RootComponent")) { Params sp(root, "SetSimulatePhysics"); sp.Set("bSimulate", false).Call(); }
    Params at(att, "K2_AttachToActor");
    at.SetObj("ParentActor", s_gun).Set("LocationRule", (uint8_t)1).Set("RotationRule", (uint8_t)1).Set("ScaleRule", (uint8_t)1)
        .Set("bWeldSimulatedBodies", false).Call();
    att.SetBool("bAttached", true);
    Log("SUPPRESSOR {} attached, bIsSuppressor={}", att.Name(), att.GetBool("bIsSuppressor"));
}
static const Stage kRealism[] = {
    {"initial", [] {}, 0.5},
    {"semi, held by the player", [] { SetMode(1); HoldBy(Pawn()); }, 0.5},
    {"at rest", [] { Factors("at rest"); }, 0.2},
    {"suppressor on", [] { Suppress(); }, 0.5},
    {"suppressed", [] { Factors("suppressed, at rest"); }, 0.2},
    SHOTS10 SHOTS10 SHOTS10 SHOTS10 SHOTS10 SHOTS10
    {"after sustained fire", [] { Factors("after sustained fire"); }, 15.0},
    {"15 s later", [] { Factors("15 s later"); }, 0.2},
};

// pull.flag: racking the way a hand does it - the handle pulled back in steps (the game's own
// HandleBackServer / HandleForwardServer only at the ends of the travel), and a half pull first.
static void GameBack() { Params p(Handle(), "HandleBackServer"); p.Call(); }
static void GameForward() { Params p(Handle(), "HandleForwardServer"); p.Call(); }
#define HAND_RACK(tag) \
    {tag " pull 30%", [] { HandleTo(0.3f); }, 0.08}, \
    {tag " pull 60%", [] { HandleTo(0.6f); }, 0.08}, \
    {tag " pull 100%", [] { HandleTo(1.0f); }, 0.3}, \
    {tag " game: handle back", [] { GameBack(); }, 0.3}, \
    {tag " release 50%", [] { HandleTo(0.5f); }, 0.08}, \
    {tag " release 0%", [] { HandleTo(0.0f); }, 0.2}, \
    {tag " game: handle forward", [] { GameForward(); }, 0.6},
static const Stage kPull[] = {
    {"initial", [] {}, 0.5},
    {"semi, held by the player", [] { SetMode(1); HoldBy(Pawn()); }, 0.5},
    {"FTF armed, shot", [] { Arm(2); TryFire(); }, 0.8},
    {"ftf: half pull 60%", [] { HandleTo(0.6f); }, 0.3},
    {"ftf: half pull let go", [] { HandleTo(0.0f); }, 0.5},
    {"ftf: fire (still jammed?)", [] { TryFire(); }, 0.5},
    HAND_RACK("ftf:")
    {"ftf: fire after clearing", [] { TryFire(); }, 0.8},
    {"STOVEPIPE armed, shot", [] { Arm(3); TryFire(); }, 0.8},
    {"stovepipe: fire", [] { TryFire(); }, 0.5},
    HAND_RACK("stovepipe:")
    {"stovepipe: fire after clearing", [] { TryFire(); }, 0.8},
    {"DUD armed, shot", [] { Arm(1); TryFire(); }, 0.8},
    {"dud: fire", [] { TryFire(); }, 0.5},
    HAND_RACK("dud:")
    {"dud: fire after clearing", [] { TryFire(); }, 0.8},
    {"DOUBLE FEED armed, shot", [] { Arm(4); TryFire(); }, 0.8},
    HAND_RACK("df (mag in):")
    {"df: fire (still jammed?)", [] { TryFire(); }, 0.5},
    {"df: strip the magazine", [] { DropMag(); }, 1.5},
    HAND_RACK("df (mag out):")
    {"df: new magazine", [] { NewMag(); }, 1.5},
    HAND_RACK("df (reload):")
    {"df: fire after clearing", [] { TryFire(); }, 0.8},
};

// loop.flag: stovepipe, rack, ten times over (an intermittent fault hunt).
#define STOVEPIPE_CYCLE \
    {"STOVEPIPE armed, shot", [] { Arm(3); TryFire(); }, 0.8}, \
    {"stovepipe: rack back", [] { Rack(true); }, 0.5}, \
    {"stovepipe: rack forward", [] { Rack(false); }, 0.8}, \
    {"fresh magazine", [] { DropMag(); }, 0.6}, \
    {"fresh magazine in", [] { NewMag(); }, 1.0},
static const Stage kLoop[] = {
    {"initial", [] {}, 0.5},
    {"semi, held by the player", [] { SetMode(1); HoldBy(Pawn()); }, 0.5},
    STOVEPIPE_CYCLE STOVEPIPE_CYCLE STOVEPIPE_CYCLE STOVEPIPE_CYCLE STOVEPIPE_CYCLE
    STOVEPIPE_CYCLE STOVEPIPE_CYCLE STOVEPIPE_CYCLE STOVEPIPE_CYCLE STOVEPIPE_CYCLE
};
// exp.flag: what the game does when racked with a spent case in the chamber, and after a shot
// whose GunChamberComponent:OnFired was skipped - without the plugin involved.
static const Stage kExp[] = {
    {"initial", [] {}, 0.5},
    {"semi, held by the player", [] { SetMode(1); HoldBy(Pawn()); }, 0.5},
    {"shot", [] { TryFire(); }, 0.8},
    {"spent case in the chamber", [] { SpentInPort(); }, 0.5},
    {"rack back (spent case)", [] { Rack(true); }, 0.5},
    {"rack forward", [] { Rack(false); }, 0.8},
    {"shot, OnFired skipped", [] { s_skipOnFired = true; TryFire(); }, 0.8},
    {"rack back (after skipped OnFired)", [] { s_skipOnFired = false; Rack(true); }, 0.5},
    {"rack forward", [] { Rack(false); }, 0.8},
    {"shot", [] { TryFire(); }, 0.8},
};
// drop.flag: a P320 of its own, loaded and chambered, dropped from in front of the camera: upside
// down with the rear end 30 degrees low (the drop that fired old P320s), then upright ones that
// shouldn't. Each drop is picked up again, reloaded and re-chambered.
static bool s_free = false;  // falling: Hold() leaves it alone
static int s_ammoBefore = 0;
static double s_dropZ = 0;
static Vec s_holdAt{};
static double GunZ() { Params l(s_gun, "K2_GetActorLocation"); l.Call(); return l.Return<Vec>().z; }
static void Physics(bool on) {
    if (Object root = s_gun.GetObj("StaticMeshComponent")) { Params sp(root, "SetSimulatePhysics"); sp.Set("bSimulate", on).Call(); }
}
// Let go of it the way a hand does: the right motion controller grips it where it is and drops it,
// so the game's own release runs (physics, collision).
static void Drop(double pitch, double roll) {
    Physics(false);
    HoldBy(Object());
    double dist = s_dist;
    s_dist = 150;  // clear of the player's hands
    Hold();
    s_dist = dist;
    { Params up(s_gun, "K2_GetActorLocation"); up.Call(); Vec p = up.Return<Vec>(); p.z += 200;  // out of the trace's way
      Params mv(s_gun, "K2_SetActorLocation"); mv.Set("NewLocation", p).Set("bSweep", false).Set("bTeleport", true).Call(); p.z -= 200;
      s_holdAt = p; }
    // 120 cm above whatever is below.
    Vec at = s_holdAt;
    Params tr(Lib("KismetSystemLibrary"), "LineTraceSingle");
    tr.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Start", Vec{at.x, at.y, at.z + 60}).Set("End", Vec{at.x, at.y, at.z - 1000})
        .Set("TraceChannel", (uint8_t)0).Set("bTraceComplex", false).Set("bIgnoreSelf", true).Call();
    if (tr.Return<bool>()) {
        GML_PropInfo hi;
        API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(Lib("KismetSystemLibrary").Struct(), "LineTraceSingle"), "OutHit"), &hi);
        Params bh(Lib("GameplayStatics"), "BreakHitResult");
        std::memcpy(bh.Ptr("Hit"), tr.Ptr("OutHit"), hi.size);
        bh.Call();
        Vec floor = bh.Get<Vec>("ImpactPoint");
        Object what = bh.Get<GUObject*>("HitActor");
        Log("  below: {} at z {:.1f}", what ? what.Name() : "?", floor.z);
        at.z = floor.z + 120;
    } else {
        Log("  below: nothing within 10 m");
    }
    Params rp(s_gun, "K2_GetActorRotation");
    rp.Call();
    Rot r = rp.Return<Rot>();
    Params mv(s_gun, "K2_SetActorLocationAndRotation");
    mv.Set("NewLocation", at).Set("NewRotation", Rot{pitch, r.yaw, roll}).Set("bSweep", false).Set("bTeleport", true).Call();
    static int n = 0;
    static std::string name;
    name = "drop" + std::to_string(++n);
    Snap(name.c_str());
    s_free = true;
    s_dropZ = GunZ();
    Params well(s_gun.GetObj("MagWellComponent"), "GetCurrentAmmo");
    well.Call();
    s_ammoBefore = well.Return<int32_t>();
    Params vr(Pawn(), "GetVRComponents");
    vr.Call();
    Object hand = vr.Get<GUObject*>("RightController");
    if (!hand) { Error("no right controller"); Physics(true); return; }
    Params gt(s_gun, "GetTransform");
    gt.Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(s_gun.Struct(), "GetTransform"), "ReturnValue"), &xi);
    Params g(hand, "GripObjectByInterface");
    g.SetObj("ObjectToGrip", s_gun);
    std::memcpy(g.Ptr("WorldOffset"), gt.Ptr("ReturnValue"), xi.size);
    g.Set("bWorldOffsetIsRelative", false).Set("bIsSlotGrip", false).Call();
    Params d(hand, "DropObjectByInterface");
    d.SetObj("ObjectToDrop", s_gun).Set("GripIDToDrop", (uint8_t)0).Call();
    Object root = s_gun.GetObj("StaticMeshComponent");
    Params sim(root, "IsSimulatingPhysics");
    sim.Call();
    Log("  gripped {} dropped {} | simulating {} | holder {}", g.Return<bool>(), d.Return<bool>(), sim.Return<bool>(),
        [] { Params h(s_gun, "GetHoldingCharacter"); h.Call(); Object o = h.Return<GUObject*>(); return o ? o.Name() : std::string("none"); }());
}
static void PickUp() {
    Params well(s_gun.GetObj("MagWellComponent"), "GetCurrentAmmo");
    well.Call();
    int ammo = well.Return<int32_t>();
    Log("DROP RESULT: {} (magazine {} -> {}), fell {:.0f} cm", ammo < s_ammoBefore ? "FIRED" : "did not fire", s_ammoBefore, ammo, s_dropZ - GunZ());
    Physics(false);
    s_free = false;
    HoldBy(Pawn());
}
static void Recharge() {
    if (!s_gun.GetObj("CurrentMagazine")) NewMag();
    if (!CallBool(s_gun, "CanFireRound")) { Rack(true); Rack(false); }
}
#define DROP(tag, pitch, roll) \
    {tag, [] { Drop(pitch, roll); }, 2.5}, \
    {"pick up", [] { PickUp(); }, 0.5}, \
    {"recharge", [] { Recharge(); }, 0.8},
static const Stage kDrop[] = {
    {"initial", [] {}, 0.5},
    {"held, magazine in", [] { HoldBy(Pawn()); NewMag(); }, 0.8},
    {"rack back", [] { Rack(true); }, 0.5},
    {"rack forward", [] { Rack(false); }, 0.8},
    DROP("drop: upside down, rear 30 low", 30, 180)
    DROP("drop: upside down, rear 30 low", 30, 180)
    DROP("drop: upside down, rear 30 low", 30, 180)
    DROP("drop: upside down, rear 30 low", 30, 180)
    DROP("drop: upside down, rear 30 low", 30, 180)
    DROP("drop: upside down, rear 30 low", 30, 180)
    DROP("drop: upright, flat", 0, 0)
    DROP("drop: upright, rear 30 low", 30, 0)
    DROP("drop: upside down, flat", 0, 180)
    DROP("drop: muzzle down", -80, 0)
};
static const Stage* s_stages = kStages;
static int s_count = (int)(sizeof kStages / sizeof kStages[0]);
static Object SpawnP320() {
    GUClass* c = API->LoadClass("/Game/Weapons/SIGP320/Blueprints/Firearm_Pistol_P320.Firearm_Pistol_P320_C");
    Object pawn = Pawn();
    if (!c || !pawn) { Error("no P320 class or no pawn"); return {}; }
    Params gt(pawn, "GetTransform");
    gt.Call();
    GML_PropInfo xi;
    API->GetPropertyInfo(API->FindProperty((GUStruct*)API->FindFunction(pawn.Struct(), "GetTransform"), "ReturnValue"), &xi);
    Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
    b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", c);
    std::memcpy(b.Ptr("SpawnTransform"), gt.Ptr("ReturnValue"), xi.size);
    b.Set("CollisionHandlingOverride", (uint8_t)1).Call();
    Object gun = b.Return<GUObject*>();
    if (!gun) return {};
    Params f(Lib("GameplayStatics"), "FinishSpawningActor");
    f.SetObj("Actor", gun);
    std::memcpy(f.Ptr("SpawnTransform"), gt.Ptr("ReturnValue"), xi.size);
    f.Call();
    return gun;
}

// <name>.flag next to this DLL switches a run (Steam relaunches the game, so no environment variables):
// random.flag = the plugin's own roll, secondary.flag = the loadout's secondary gun.
static bool Flag(const wchar_t* name) {
    wchar_t path[MAX_PATH] = L"";
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Flag, &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return false;
    swprintf_s(slash + 1, MAX_PATH - (slash + 1 - path), L"%s.flag", name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void Step(double dt) {
    if ((s_wait -= dt) > 0) return;
    int n = s_count;
    if (s_step == 0) {
        Object gear = FindFirstOf("BP_GearManagerComponent_C");
        s_gun = Flag(L"drop") ? SpawnP320() : gear ? gear.GetObj(Flag(L"secondary") ? "SecondaryGun" : "PrimaryGun") : Object();
        if (!s_gun) { Error("no gun"); Log("DEV TEST COMPLETE"); s_step = 99; return; }
        Params det(s_gun, "K2_DetachFromActor");
        det.Set("LocationRule", (uint8_t)1).Set("RotationRule", (uint8_t)1).Set("ScaleRule", (uint8_t)1).Call();
        if (Object root = s_gun.GetObj("StaticMeshComponent")) { Params sp(root, "SetSimulatePhysics"); sp.Set("bSimulate", false).Call(); }
        Log("gun {} [{}]", s_gun.Name(), Object((GUObject*)s_gun.Class()).Name());
        if (HMODULE m = GetModuleHandleW(L"Malfunctions.dll")) {
            s_arm = (int (*)(GUObject*, int))GetProcAddress(m, "Malfunctions_Arm");
            s_current = (int (*)(GUObject*))GetProcAddress(m, "Malfunctions_Current");
        }
        Log("Malfunctions API: {}", s_arm && s_current ? "found" : "MISSING");
        if (Flag(L"drop")) {
            s_stages = kDrop;
            s_count = (int)(sizeof kDrop / sizeof kDrop[0]);
            Log("drop run");
        } else if (Flag(L"realism")) {
            s_stages = kRealism;
            s_count = (int)(sizeof kRealism / sizeof kRealism[0]);
            Log("realism run");
        } else if (Flag(L"pull")) {
            s_stages = kPull;
            s_count = (int)(sizeof kPull / sizeof kPull[0]);
            Log("hand-rack run");
        } else if (Flag(L"loop")) {
            s_stages = kLoop;
            s_count = (int)(sizeof kLoop / sizeof kLoop[0]);
            Log("loop run");
        } else if (Flag(L"exp")) {
            s_stages = kExp;
            s_count = (int)(sizeof kExp / sizeof kExp[0]);
            Log("experiment run");
        } else if (Flag(L"random")) {
            s_stages = kRandom;
            s_count = (int)(sizeof kRandom / sizeof kRandom[0]);
            Log("random-roll run");
        }
        s_step = 1;
        s_wait = 1.0;
        return;
    }
    int i = s_step - 1;
    static bool snapped = false;
    if (i > 0 && !snapped) {  // the previous stage's result
        const char* prev = s_stages[i - 1].name;
        State(prev);
        if (prev[0] == '@') {
            Proxy();
            Snap(prev + 1);
            snapped = true;  // let the shot render before the next stage changes anything
            s_wait = 0.15;
            return;
        }
    }
    snapped = false;
    if (i >= n) {
        Log("DEV TEST COMPLETE");
        s_step = 99;
        return;
    }
    Log("> {}", s_stages[i].name);
    s_stages[i].act();
    s_wait = s_stages[i].wait;
    s_step++;
}

GML_AWAKE() {
    for (const char* f : {"GunChamberComponent:OnFired", "Rifle:LockBolt", "Rifle:UnlockBolt", "Rifle:ReleaseBoltServer"})
        Hook(f, [f](Object, GUFunction*, void*) {
            bool skip = s_skipOnFired && strstr(f, "OnFired");
            Log("  {}{}", f, skip ? " (skipped)" : "");
            return skip;
        });
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void* gm) {
        std::string level = Object((GUObject*)gm).Path();
        if (level.find("/Game/Maps/MainMenu/") == 0) {
            Params p(Lib("GameplayStatics"), "OpenLevel");
            p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("LevelName", API->MakeName(kMap)).Set("bAbsolute", true).Call();
        } else if (level.find(kMap) == 0) {
            s_step = 0;
            s_wait = 6;
        }
    });
    On(GML_EVENT_TICK, [](void*) {
        static LARGE_INTEGER freq{}, last{};
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); last = now; }
        double dt = double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
        last = now;
        if (s_step >= 0 && s_step < 99) Step(dt);
        if (!s_free) Hold();
        static double traceT = 0;
        if (s_free && (traceT += dt) >= 0.1) {  // the fall, ten times a second
            traceT = 0;
            Object root = s_gun.GetObj("StaticMeshComponent");
            Params pv(root, "GetPhysicsLinearVelocity"); pv.Call();
            Params cv(root, "GetComponentVelocity"); cv.Call();
            Params rr(s_gun, "K2_GetActorRotation"); rr.Call();
            Vec p = pv.Return<Vec>(), c = cv.Return<Vec>();
            Rot r = rr.Return<Rot>();
            static double lastZ = 0;
            if (std::fabs(GunZ() - lastZ) < 0.5) return;
            lastZ = GunZ();
            Log("  fall: z {:.1f} | physics v ({:.0f}, {:.0f}, {:.0f}) component v z {:.0f} | rot (p {:.0f} y {:.0f} r {:.0f}) | P320s found {}",
                GunZ(), p.x, p.y, p.z, c.z, r.pitch, r.yaw, r.roll, FindAllOf("Firearm_Pistol_P320_C").size());
        }
    });
    Hook("GunReceiver:OnImpactServer", [](Object gun, GUFunction* fn, void* params) {
        if (gun.ptr == s_gun.ptr) {
            Vec v = Param<Vec>(fn, params, "ImpactVelocity");
            Log("  OnImpactServer: velocity ({:.0f}, {:.0f}, {:.0f}) = {:.0f} cm/s", v.x, v.y, v.z, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
        }
        return false;
    });
    return 0;
}
