#pragma once
#include "zero_racers_camera.h"

constexpr unsigned ZrMaxPlayers=4;
// Native 10C70..10D20: twelve four-field updates per beat, three READY
// beeps after the lead-in, then GO; the label is visible for eight updates.
constexpr unsigned ZrCountdownBeat=48,ZrCountdownVisible=32,ZrCountdownFields=192;
enum class ZrRacePhase:uint32_t { Empty, Ready, Countdown, Racing, Results };
enum class ZrRacerStatus:uint32_t { Absent, Racing, Finished, Crashed, Forfeit, Survived };
struct ZrRacerResult {
    ZrRacerStatus status=ZrRacerStatus::Absent;
    uint32_t finish_field=0; // zero for every non-finisher, never a fabricated time
};
struct ZrSessionState {
    uint32_t version=1;
    ZrRacePhase phase=ZrRacePhase::Empty;
    uint32_t players=0,tick=0,race_fields=0,countdown=0,physics_phase=0,menu_mask=0;
    std::array<uint32_t,ZrMaxPlayers> blocked_buttons{};
    ZrWorldState world;
    std::array<ZrActorState,6> actors{};
    std::array<ZrActorControls,ZrMaxPlayers> controls{};
    std::array<ZrRacerResult,ZrMaxPlayers> result{};
};
static_assert(sizeof(ZrActorState)==308 && sizeof(ZrWorldState)==2316
              && sizeof(ZrSessionState)==4276,"Canonical session layout must not contain padding");
enum { ZrInputForfeit=1 };
struct ZrSeatInput {uint16_t pad=0;uint16_t command=0;};
struct ZrSessionEvents {
    std::array<ZrActorEvents,ZrMaxPlayers> human{};
    std::array<ZrEnvironmentEvents,ZrMaxPlayers> surface{},wall{},repair{};
    std::array<ZrEnvironmentEvents,6> contact{};
    bool physics=false,results=false;
};
// Machine index is the original selection row (0..3), not raw handling index.
bool ZrSessionInitialize(ZrSessionState&,unsigned course,
                         const std::array<unsigned,ZrMaxPlayers>& machines,unsigned players,
                         uint32_t seed,const uint8_t* rom,size_t);
bool ZrSessionStart(ZrSessionState&);
bool ZrSessionValidate(const ZrSessionState&,const uint8_t* rom,size_t);
struct ZrStandings {
    std::array<unsigned,ZrMaxPlayers> seats{};
    // Indexed by seat. Zero means unclassified; equal-field finishes tie.
    std::array<unsigned,ZrMaxPlayers> places{};
};
ZrStandings ZrSessionStandings(const ZrSessionState&);
unsigned ZrSessionClock(const ZrSessionState&,unsigned seat);
// One sealed display-field input bundle. The same function drives live play
// and rollback. Native physics runs once per three fields; menus never pause.
bool ZrSessionStep(ZrSessionState&,const std::array<ZrSeatInput,ZrMaxPlayers>&,
                    const uint8_t* rom,size_t,ZrSessionEvents&,ZrNativeCall native=nullptr);
void ZrWorldSyncRacerModels(ZrWorldState&,const std::array<ZrActorState,6>&);
void ZrWorldAdvanceEffects(ZrWorldState&);
