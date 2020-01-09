/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef _CHEATS_H_
#define _CHEATS_H_

#include "common.h"

extern bool gCheatsSandboxMode;
extern bool gCheatsDisableClearanceChecks;
extern bool gCheatsDisableSupportLimits;
extern bool gCheatsShowAllOperatingModes;
extern bool gCheatsShowVehiclesFromOtherTrackTypes;
extern bool gCheatsFastLiftHill;
extern bool gCheatsDisableBrakesFailure;
extern bool gCheatsDisableAllBreakdowns;
extern bool gCheatsBuildInPauseMode;
extern bool gCheatsIgnoreRideIntensity;
extern bool gCheatsDisableVandalism;
extern bool gCheatsDisableLittering;
extern bool gCheatsNeverendingMarketing;
extern bool gCheatsFreezeWeather;
extern bool gCheatsDisableTrainLengthLimit;
extern bool gCheatsDisablePlantAging;
extern bool gCheatsDisableRideValueAging;
extern bool gCheatsAutomaticRidePricing;
extern bool gCheatsEnableChainLiftOnAllTrack;
extern bool gCheatsAllowArbitraryRideTypeChanges;
extern bool gCheatsIgnoreResearchStatus;
extern bool gCheatsEnableAllDrawableTrackPieces;

enum class CheatType : int32_t
{
    SandboxMode,
    DisableClearanceChecks,
    DisableSupportLimits,
    ShowAllOperatingModes,
    ShowVehiclesFromOtherTrackTypes,
    DisableTrainLengthLimit,
    EnableChainLiftOnAllTrack,
    FastLiftHill,
    DisableBrakesFailure,
    DisableAllBreakdowns,
    UnlockAllPrices,
    BuildInPauseMode,
    IgnoreRideIntensity,
    DisableVandalism,
    DisableLittering,
    NoMoney,
    AddMoney,
    SetMoney,
    ClearLoan,
    SetGuestParameter,
    GenerateGuests,
    RemoveAllGuests,
    ExplodeGuests,
    GiveAllGuests,
    SetGrassLength,
    WaterPlants,
    DisablePlantAging,
    FixVandalism,
    RemoveLitter,
    SetStaffSpeed,
    RenewRides,
    MakeDestructible,
    FixRides,
    ResetCrashStatus,
    TenMinuteInspections,
    WinScenario,
    ForceWeather,
    FreezeWeather,
    OpenClosePark,
    HaveFun,
    SetForcedParkRating,
    NeverEndingMarketing,
    AllowArbitraryRideTypeChanges,
    OwnAllLand,
    DisableRideValueAging,
    AutomaticRidePricing,
    IgnoreResearchStatus,
    EnableAllDrawableTrackPieces,
    CreateDucks,
    RemoveDucks,
    Count,
};

enum
{
    GUEST_PARAMETER_HAPPINESS,
    GUEST_PARAMETER_ENERGY,
    GUEST_PARAMETER_HUNGER,
    GUEST_PARAMETER_THIRST,
    GUEST_PARAMETER_NAUSEA,
    GUEST_PARAMETER_NAUSEA_TOLERANCE,
    GUEST_PARAMETER_BATHROOM,
    GUEST_PARAMETER_PREFERRED_RIDE_INTENSITY
};

enum
{
    OBJECT_MONEY,
    OBJECT_PARK_MAP,
    OBJECT_BALLOON,
    OBJECT_UMBRELLA
};

#define CHEATS_GIVE_GUESTS_MONEY MONEY(1000, 00)
#define CHEATS_TRAM_INCREMENT 250
#define CHEATS_DUCK_INCREMENT 20
#define CHEATS_STAFF_FAST_SPEED 0xFF
#define CHEATS_STAFF_NORMAL_SPEED 0x60
#define CHEATS_STAFF_FREEZE_SPEED 0

void CheatsReset();
const char* CheatsGetName(CheatType cheatType);
void CheatsSet(CheatType cheatType, int32_t param1 = 0, int32_t param2 = 0);
void CheatsSerialise(class DataSerialiser& ds);

#endif
