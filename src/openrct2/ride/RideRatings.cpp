/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "RideRatings.h"

#include "../Cheats.h"
#include "../OpenRCT2.h"
#include "../interface/Window.h"
#include "../localisation/Date.h"
#include "../world/Footpath.h"
#include "../world/Map.h"
#include "../world/Surface.h"
#include "Ride.h"
#include "RideData.h"
#include "Station.h"
#include "Track.h"
#include "../world/Park.h"

#include <algorithm>
#include <iterator>

enum
{
    RIDE_RATINGS_STATE_FIND_NEXT_RIDE,
    RIDE_RATINGS_STATE_INITIALISE,
    RIDE_RATINGS_STATE_2,
    RIDE_RATINGS_STATE_CALCULATE,
    RIDE_RATINGS_STATE_4,
    RIDE_RATINGS_STATE_5
};

enum
{
    PROXIMITY_WATER_OVER,                   // 0x0138B596
    PROXIMITY_WATER_TOUCH,                  // 0x0138B598
    PROXIMITY_WATER_LOW,                    // 0x0138B59A
    PROXIMITY_WATER_HIGH,                   // 0x0138B59C
    PROXIMITY_SURFACE_TOUCH,                // 0x0138B59E
    PROXIMITY_PATH_ZERO_OVER,               // 0x0138B5A0
    PROXIMITY_PATH_ZERO_TOUCH_ABOVE,        // 0x0138B5A2
    PROXIMITY_PATH_ZERO_TOUCH_UNDER,        // 0x0138B5A4
    PROXIMITY_PATH_TOUCH_ABOVE,             // 0x0138B5A6
    PROXIMITY_PATH_TOUCH_UNDER,             // 0x0138B5A8
    PROXIMITY_OWN_TRACK_TOUCH_ABOVE,        // 0x0138B5AA
    PROXIMITY_OWN_TRACK_CLOSE_ABOVE,        // 0x0138B5AC
    PROXIMITY_FOREIGN_TRACK_ABOVE_OR_BELOW, // 0x0138B5AE
    PROXIMITY_FOREIGN_TRACK_TOUCH_ABOVE,    // 0x0138B5B0
    PROXIMITY_FOREIGN_TRACK_CLOSE_ABOVE,    // 0x0138B5B2
    PROXIMITY_SCENERY_SIDE_BELOW,           // 0x0138B5B4
    PROXIMITY_SCENERY_SIDE_ABOVE,           // 0x0138B5B6
    PROXIMITY_OWN_STATION_TOUCH_ABOVE,      // 0x0138B5B8
    PROXIMITY_OWN_STATION_CLOSE_ABOVE,      // 0x0138B5BA
    PROXIMITY_TRACK_THROUGH_VERTICAL_LOOP,  // 0x0138B5BC
    PROXIMITY_PATH_TROUGH_VERTICAL_LOOP,    // 0x0138B5BE
    PROXIMITY_INTERSECTING_VERTICAL_LOOP,   // 0x0138B5C0
    PROXIMITY_THROUGH_VERTICAL_LOOP,        // 0x0138B5C2
    PROXIMITY_PATH_SIDE_CLOSE,              // 0x0138B5C4
    PROXIMITY_FOREIGN_TRACK_SIDE_CLOSE,     // 0x0138B5C6
    PROXIMITY_SURFACE_SIDE_CLOSE,           // 0x0138B5C8
    PROXIMITY_COUNT
};

struct ShelteredEights
{
    uint8_t TrackShelteredEighths;
    uint8_t TotalShelteredEighths;
};

using ride_ratings_calculation = void (*)(Ride* ride);

RideRatingCalculationData gRideRatingsCalcData;

static ride_ratings_calculation ride_ratings_get_calculate_func(uint8_t rideType);

static void ride_ratings_update_state();
static void ride_ratings_update_state_0();
static void ride_ratings_update_state_1();
static void ride_ratings_update_state_2();
static void ride_ratings_update_state_3();
static void ride_ratings_update_state_4();
static void ride_ratings_update_state_5();
static void ride_ratings_begin_proximity_loop();
static void ride_ratings_calculate(Ride* ride);
static void ride_ratings_calculate_value(Ride* ride);
static void ride_ratings_score_close_proximity(TileElement* inputTileElement);

static void ride_ratings_add(rating_tuple* rating, int32_t excitement, int32_t intensity, int32_t nausea);

/**
 * This is a small hack function to keep calling the ride rating processor until
 * the given ride's ratings have been calculated. What ever is currently being
 * processed will be overwritten.
 * Only purpose of this function currently is for testing.
 */
void ride_ratings_update_ride(const Ride& ride)
{
    if (ride.status != RIDE_STATUS_CLOSED)
    {
        gRideRatingsCalcData.current_ride = ride.id;
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_INITIALISE;
        while (gRideRatingsCalcData.state != RIDE_RATINGS_STATE_FIND_NEXT_RIDE)
        {
            ride_ratings_update_state();
        }
    }
}

/**
 *
 *  rct2: 0x006B5A2A
 */
void ride_ratings_update_all()
{
    if (gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR)
        return;

    ride_ratings_update_state();
}

static void ride_ratings_update_state()
{
    switch (gRideRatingsCalcData.state)
    {
        case RIDE_RATINGS_STATE_FIND_NEXT_RIDE:
            ride_ratings_update_state_0();
            break;
        case RIDE_RATINGS_STATE_INITIALISE:
            ride_ratings_update_state_1();
            break;
        case RIDE_RATINGS_STATE_2:
            ride_ratings_update_state_2();
            break;
        case RIDE_RATINGS_STATE_CALCULATE:
            ride_ratings_update_state_3();
            break;
        case RIDE_RATINGS_STATE_4:
            ride_ratings_update_state_4();
            break;
        case RIDE_RATINGS_STATE_5:
            ride_ratings_update_state_5();
            break;
    }
}

/**
 *
 *  rct2: 0x006B5A5C
 */
static void ride_ratings_update_state_0()
{
    int32_t currentRide = gRideRatingsCalcData.current_ride;

    currentRide++;
    if (currentRide == RIDE_ID_NULL)
    {
        currentRide = 0;
    }

    auto ride = get_ride(currentRide);
    if (ride != nullptr && ride->status != RIDE_STATUS_CLOSED)
    {
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_INITIALISE;
    }
    gRideRatingsCalcData.current_ride = currentRide;
}

/**
 *
 *  rct2: 0x006B5A94
 */
static void ride_ratings_update_state_1()
{
    gRideRatingsCalcData.proximity_total = 0;
    for (int32_t i = 0; i < PROXIMITY_COUNT; i++)
    {
        gRideRatingsCalcData.proximity_scores[i] = 0;
    }
    gRideRatingsCalcData.num_brakes = 0;
    gRideRatingsCalcData.num_reversers = 0;
    gRideRatingsCalcData.state = RIDE_RATINGS_STATE_2;
    gRideRatingsCalcData.station_flags = 0;
    ride_ratings_begin_proximity_loop();
}

/**
 *
 *  rct2: 0x006B5C66
 */
static void ride_ratings_update_state_2()
{
    const ride_id_t rideIndex = gRideRatingsCalcData.current_ride;
    auto ride = get_ride(rideIndex);
    if (ride == nullptr || ride->status == RIDE_STATUS_CLOSED)
    {
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }

    int32_t x = gRideRatingsCalcData.proximity_x / 32;
    int32_t y = gRideRatingsCalcData.proximity_y / 32;
    int32_t z = gRideRatingsCalcData.proximity_z / 8;
    int32_t trackType = gRideRatingsCalcData.proximity_track_type;

    TileElement* tileElement = map_get_first_element_at(TileCoordsXY{ x, y }.ToCoordsXY());
    if (tileElement == nullptr)
    {
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }
    do
    {
        if (tileElement->IsGhost())
            continue;
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
            continue;
        if (tileElement->base_height != z)
            continue;

        if (trackType == 255
            || (tileElement->AsTrack()->GetSequenceIndex() == 0 && trackType == tileElement->AsTrack()->GetTrackType()))
        {
            if (trackType == TRACK_ELEM_END_STATION)
            {
                int32_t entranceIndex = tileElement->AsTrack()->GetStationIndex();
                gRideRatingsCalcData.station_flags &= ~RIDE_RATING_STATION_FLAG_NO_ENTRANCE;
                if (ride_get_entrance_location(ride, entranceIndex).isNull())
                {
                    gRideRatingsCalcData.station_flags |= RIDE_RATING_STATION_FLAG_NO_ENTRANCE;
                }
            }

            ride_ratings_score_close_proximity(tileElement);

            CoordsXYE trackElement = {
                /* .x = */ gRideRatingsCalcData.proximity_x,
                /* .y = */ gRideRatingsCalcData.proximity_y,
                /* .element = */ tileElement,
            };
            CoordsXYE nextTrackElement;
            if (!track_block_get_next(&trackElement, &nextTrackElement, nullptr, nullptr))
            {
                gRideRatingsCalcData.state = RIDE_RATINGS_STATE_4;
                return;
            }

            x = nextTrackElement.x;
            y = nextTrackElement.y;
            z = nextTrackElement.element->GetBaseZ();
            tileElement = nextTrackElement.element;
            if (x == gRideRatingsCalcData.proximity_start_x && y == gRideRatingsCalcData.proximity_start_y
                && z == gRideRatingsCalcData.proximity_start_z)
            {
                gRideRatingsCalcData.state = RIDE_RATINGS_STATE_CALCULATE;
                return;
            }
            gRideRatingsCalcData.proximity_x = x;
            gRideRatingsCalcData.proximity_y = y;
            gRideRatingsCalcData.proximity_z = z;
            gRideRatingsCalcData.proximity_track_type = tileElement->AsTrack()->GetTrackType();
            return;
        }
    } while (!(tileElement++)->IsLastForTile());

    gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
}

/**
 *
 *  rct2: 0x006B5E4D
 */
static void ride_ratings_update_state_3()
{
    auto ride = get_ride(gRideRatingsCalcData.current_ride);
    if (ride == nullptr || ride->status == RIDE_STATUS_CLOSED)
    {
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }

    ride_ratings_calculate(ride);
    ride_ratings_calculate_value(ride);

    window_invalidate_by_number(WC_RIDE, gRideRatingsCalcData.current_ride);
    gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
}

/**
 *
 *  rct2: 0x006B5BAB
 */
static void ride_ratings_update_state_4()
{
    gRideRatingsCalcData.state = RIDE_RATINGS_STATE_5;
    ride_ratings_begin_proximity_loop();
}

/**
 *
 *  rct2: 0x006B5D72
 */
static void ride_ratings_update_state_5()
{
    auto ride = get_ride(gRideRatingsCalcData.current_ride);
    if (ride == nullptr || ride->status == RIDE_STATUS_CLOSED)
    {
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }

    int32_t x = gRideRatingsCalcData.proximity_x / 32;
    int32_t y = gRideRatingsCalcData.proximity_y / 32;
    int32_t z = gRideRatingsCalcData.proximity_z / 8;
    int32_t trackType = gRideRatingsCalcData.proximity_track_type;

    TileElement* tileElement = map_get_first_element_at(TileCoordsXY{ x, y }.ToCoordsXY());
    if (tileElement == nullptr)
    {
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }
    do
    {
        if (tileElement->IsGhost())
            continue;
        if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
            continue;
        if (tileElement->base_height != z)
            continue;

        if (trackType == 255 || trackType == tileElement->AsTrack()->GetTrackType())
        {
            ride_ratings_score_close_proximity(tileElement);

            x = gRideRatingsCalcData.proximity_x;
            y = gRideRatingsCalcData.proximity_y;
            track_begin_end trackBeginEnd;
            if (!track_block_get_previous(x, y, tileElement, &trackBeginEnd))
            {
                gRideRatingsCalcData.state = RIDE_RATINGS_STATE_CALCULATE;
                return;
            }

            x = trackBeginEnd.begin_x;
            y = trackBeginEnd.begin_y;
            z = trackBeginEnd.begin_z;
            if (x == gRideRatingsCalcData.proximity_start_x && y == gRideRatingsCalcData.proximity_start_y
                && z == gRideRatingsCalcData.proximity_start_z)
            {
                gRideRatingsCalcData.state = RIDE_RATINGS_STATE_CALCULATE;
                return;
            }
            gRideRatingsCalcData.proximity_x = x;
            gRideRatingsCalcData.proximity_y = y;
            gRideRatingsCalcData.proximity_z = z;
            gRideRatingsCalcData.proximity_track_type = trackBeginEnd.begin_element->AsTrack()->GetTrackType();
            return;
        }
    } while (!(tileElement++)->IsLastForTile());

    gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
}

/**
 *
 *  rct2: 0x006B5BB2
 */
static void ride_ratings_begin_proximity_loop()
{
    auto ride = get_ride(gRideRatingsCalcData.current_ride);
    if (ride == nullptr || ride->status == RIDE_STATUS_CLOSED)
    {
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
        return;
    }

    if (ride->type == RIDE_TYPE_MAZE)
    {
        gRideRatingsCalcData.state = RIDE_RATINGS_STATE_CALCULATE;
        return;
    }

    for (int32_t i = 0; i < MAX_STATIONS; i++)
    {
        if (!ride->stations[i].Start.isNull())
        {
            gRideRatingsCalcData.station_flags &= ~RIDE_RATING_STATION_FLAG_NO_ENTRANCE;
            if (ride_get_entrance_location(ride, i).isNull())
            {
                gRideRatingsCalcData.station_flags |= RIDE_RATING_STATION_FLAG_NO_ENTRANCE;
            }

            int32_t x = ride->stations[i].Start.x * 32;
            int32_t y = ride->stations[i].Start.y * 32;
            int32_t z = ride->stations[i].GetBaseZ();

            gRideRatingsCalcData.proximity_x = x;
            gRideRatingsCalcData.proximity_y = y;
            gRideRatingsCalcData.proximity_z = z;
            gRideRatingsCalcData.proximity_track_type = 255;
            gRideRatingsCalcData.proximity_start_x = x;
            gRideRatingsCalcData.proximity_start_y = y;
            gRideRatingsCalcData.proximity_start_z = z;
            return;
        }
    }

    gRideRatingsCalcData.state = RIDE_RATINGS_STATE_FIND_NEXT_RIDE;
}

static void proximity_score_increment(int32_t type)
{
    gRideRatingsCalcData.proximity_scores[type]++;
}

/**
 *
 *  rct2: 0x006B6207
 */
static void ride_ratings_score_close_proximity_in_direction(TileElement* inputTileElement, int32_t direction)
{
    int32_t x = gRideRatingsCalcData.proximity_x + CoordsDirectionDelta[direction].x;
    int32_t y = gRideRatingsCalcData.proximity_y + CoordsDirectionDelta[direction].y;
    if (x < 0 || y < 0 || x >= (32 * 256) || y >= (32 * 256))
        return;

    TileElement* tileElement = map_get_first_element_at({ x, y });
    if (tileElement == nullptr)
        return;
    do
    {
        if (tileElement->IsGhost())
            continue;

        switch (tileElement->GetType())
        {
            case TILE_ELEMENT_TYPE_SURFACE:
                if (gRideRatingsCalcData.proximity_base_height <= inputTileElement->base_height)
                {
                    if (inputTileElement->clearance_height <= tileElement->base_height)
                    {
                        proximity_score_increment(PROXIMITY_SURFACE_SIDE_CLOSE);
                    }
                }
                break;
            case TILE_ELEMENT_TYPE_PATH:
                if (abs((int32_t)inputTileElement->base_height - (int32_t)tileElement->base_height) <= 2)
                {
                    proximity_score_increment(PROXIMITY_PATH_SIDE_CLOSE);
                }
                break;
            case TILE_ELEMENT_TYPE_TRACK:
                if (inputTileElement->AsTrack()->GetRideIndex() != tileElement->AsTrack()->GetRideIndex())
                {
                    if (abs((int32_t)inputTileElement->base_height - (int32_t)tileElement->base_height) <= 2)
                    {
                        proximity_score_increment(PROXIMITY_FOREIGN_TRACK_SIDE_CLOSE);
                    }
                }
                break;
            case TILE_ELEMENT_TYPE_SMALL_SCENERY:
            case TILE_ELEMENT_TYPE_LARGE_SCENERY:
                if (tileElement->base_height < inputTileElement->clearance_height)
                {
                    if (inputTileElement->base_height > tileElement->clearance_height)
                    {
                        proximity_score_increment(PROXIMITY_SCENERY_SIDE_ABOVE);
                    }
                    else
                    {
                        proximity_score_increment(PROXIMITY_SCENERY_SIDE_BELOW);
                    }
                }
                break;
        }
    } while (!(tileElement++)->IsLastForTile());
}

static void ride_ratings_score_close_proximity_loops_helper(TileElement* inputTileElement, int32_t x, int32_t y)
{
    TileElement* tileElement = map_get_first_element_at({ x, y });
    if (tileElement == nullptr)
        return;
    do
    {
        if (tileElement->IsGhost())
            continue;

        switch (tileElement->GetType())
        {
            case TILE_ELEMENT_TYPE_PATH:
            {
                int32_t zDiff = (int32_t)tileElement->base_height - (int32_t)inputTileElement->base_height;
                if (zDiff >= 0 && zDiff <= 16)
                {
                    proximity_score_increment(PROXIMITY_PATH_TROUGH_VERTICAL_LOOP);
                }
            }
            break;

            case TILE_ELEMENT_TYPE_TRACK:
            {
                bool elementsAreAt90DegAngle = ((tileElement->GetDirection() ^ inputTileElement->GetDirection()) & 1) != 0;
                if (elementsAreAt90DegAngle)
                {
                    int32_t zDiff = (int32_t)tileElement->base_height - (int32_t)inputTileElement->base_height;
                    if (zDiff >= 0 && zDiff <= 16)
                    {
                        proximity_score_increment(PROXIMITY_TRACK_THROUGH_VERTICAL_LOOP);
                        if (tileElement->AsTrack()->GetTrackType() == TRACK_ELEM_LEFT_VERTICAL_LOOP
                            || tileElement->AsTrack()->GetTrackType() == TRACK_ELEM_RIGHT_VERTICAL_LOOP)
                        {
                            proximity_score_increment(PROXIMITY_INTERSECTING_VERTICAL_LOOP);
                        }
                    }
                }
            }
            break;
        }
    } while (!(tileElement++)->IsLastForTile());
}

/**
 *
 *  rct2: 0x006B62DA
 */
static void ride_ratings_score_close_proximity_loops(TileElement* inputTileElement)
{
    int32_t trackType = inputTileElement->AsTrack()->GetTrackType();
    if (trackType == TRACK_ELEM_LEFT_VERTICAL_LOOP || trackType == TRACK_ELEM_RIGHT_VERTICAL_LOOP)
    {
        int32_t x = gRideRatingsCalcData.proximity_x;
        int32_t y = gRideRatingsCalcData.proximity_y;
        ride_ratings_score_close_proximity_loops_helper(inputTileElement, x, y);

        int32_t direction = inputTileElement->GetDirection();
        x = gRideRatingsCalcData.proximity_x + CoordsDirectionDelta[direction].x;
        y = gRideRatingsCalcData.proximity_y + CoordsDirectionDelta[direction].y;
        ride_ratings_score_close_proximity_loops_helper(inputTileElement, x, y);
    }
}

/**
 *
 *  rct2: 0x006B5F9D
 */
static void ride_ratings_score_close_proximity(TileElement* inputTileElement)
{
    if (gRideRatingsCalcData.station_flags & RIDE_RATING_STATION_FLAG_NO_ENTRANCE)
    {
        return;
    }

    gRideRatingsCalcData.proximity_total++;
    int32_t x = gRideRatingsCalcData.proximity_x;
    int32_t y = gRideRatingsCalcData.proximity_y;
    TileElement* tileElement = map_get_first_element_at({ x, y });
    if (tileElement == nullptr)
        return;
    do
    {
        if (tileElement->IsGhost())
            continue;

        int32_t waterHeight;
        switch (tileElement->GetType())
        {
            case TILE_ELEMENT_TYPE_SURFACE:
                gRideRatingsCalcData.proximity_base_height = tileElement->base_height;
                if (tileElement->GetBaseZ() == gRideRatingsCalcData.proximity_z)
                {
                    proximity_score_increment(PROXIMITY_SURFACE_TOUCH);
                }
                waterHeight = tileElement->AsSurface()->GetWaterHeight();
                if (waterHeight != 0)
                {
                    int32_t z = waterHeight * 16;
                    if (z <= gRideRatingsCalcData.proximity_z)
                    {
                        proximity_score_increment(PROXIMITY_WATER_OVER);
                        if (z == gRideRatingsCalcData.proximity_z)
                        {
                            proximity_score_increment(PROXIMITY_WATER_TOUCH);
                        }
                        z += 16;
                        if (z == gRideRatingsCalcData.proximity_z)
                        {
                            proximity_score_increment(PROXIMITY_WATER_LOW);
                        }
                        z += 112;
                        if (z <= gRideRatingsCalcData.proximity_z)
                        {
                            proximity_score_increment(PROXIMITY_WATER_HIGH);
                        }
                    }
                }
                break;
            case TILE_ELEMENT_TYPE_PATH:
                // Bonus for normal path
                if (tileElement->AsPath()->GetPathEntryIndex() != 0)
                {
                    if (tileElement->clearance_height == inputTileElement->base_height)
                    {
                        proximity_score_increment(PROXIMITY_PATH_TOUCH_ABOVE);
                    }
                    if (tileElement->base_height == inputTileElement->clearance_height)
                    {
                        proximity_score_increment(PROXIMITY_PATH_TOUCH_UNDER);
                    }
                }
                else
                {
                    // Bonus for path in first object entry
                    if (tileElement->clearance_height <= inputTileElement->base_height)
                    {
                        proximity_score_increment(PROXIMITY_PATH_ZERO_OVER);
                    }
                    if (tileElement->clearance_height == inputTileElement->base_height)
                    {
                        proximity_score_increment(PROXIMITY_PATH_ZERO_TOUCH_ABOVE);
                    }
                    if (tileElement->base_height == inputTileElement->clearance_height)
                    {
                        proximity_score_increment(PROXIMITY_PATH_ZERO_TOUCH_UNDER);
                    }
                }
                break;
            case TILE_ELEMENT_TYPE_TRACK:
            {
                int32_t trackType = tileElement->AsTrack()->GetTrackType();
                if (trackType == TRACK_ELEM_LEFT_VERTICAL_LOOP || trackType == TRACK_ELEM_RIGHT_VERTICAL_LOOP)
                {
                    int32_t sequence = tileElement->AsTrack()->GetSequenceIndex();
                    if (sequence == 3 || sequence == 6)
                    {
                        if (tileElement->base_height - inputTileElement->clearance_height <= 10)
                        {
                            proximity_score_increment(PROXIMITY_THROUGH_VERTICAL_LOOP);
                        }
                    }
                }
                if (inputTileElement->AsTrack()->GetRideIndex() != tileElement->AsTrack()->GetRideIndex())
                {
                    proximity_score_increment(PROXIMITY_FOREIGN_TRACK_ABOVE_OR_BELOW);
                    if (tileElement->clearance_height == inputTileElement->base_height)
                    {
                        proximity_score_increment(PROXIMITY_FOREIGN_TRACK_TOUCH_ABOVE);
                    }
                    if (tileElement->clearance_height + 2 <= inputTileElement->base_height)
                    {
                        if (tileElement->clearance_height + 10 >= inputTileElement->base_height)
                        {
                            proximity_score_increment(PROXIMITY_FOREIGN_TRACK_CLOSE_ABOVE);
                        }
                    }
                    if (inputTileElement->clearance_height == tileElement->base_height)
                    {
                        proximity_score_increment(PROXIMITY_FOREIGN_TRACK_TOUCH_ABOVE);
                    }
                    if (inputTileElement->clearance_height + 2 == tileElement->base_height)
                    {
                        if ((uint8_t)(inputTileElement->clearance_height + 10) >= tileElement->base_height)
                        {
                            proximity_score_increment(PROXIMITY_FOREIGN_TRACK_CLOSE_ABOVE);
                        }
                    }
                }
                else
                {
                    trackType = tileElement->AsTrack()->GetTrackType();
                    bool isStation
                        = (trackType == TRACK_ELEM_END_STATION || trackType == TRACK_ELEM_MIDDLE_STATION
                           || trackType == TRACK_ELEM_BEGIN_STATION);
                    if (tileElement->clearance_height == inputTileElement->base_height)
                    {
                        proximity_score_increment(PROXIMITY_OWN_TRACK_TOUCH_ABOVE);
                        if (isStation)
                        {
                            proximity_score_increment(PROXIMITY_OWN_STATION_TOUCH_ABOVE);
                        }
                    }
                    if (tileElement->clearance_height + 2 <= inputTileElement->base_height)
                    {
                        if (tileElement->clearance_height + 10 >= inputTileElement->base_height)
                        {
                            proximity_score_increment(PROXIMITY_OWN_TRACK_CLOSE_ABOVE);
                            if (isStation)
                            {
                                proximity_score_increment(PROXIMITY_OWN_STATION_CLOSE_ABOVE);
                            }
                        }
                    }

                    if (inputTileElement->clearance_height == tileElement->base_height)
                    {
                        proximity_score_increment(PROXIMITY_OWN_TRACK_TOUCH_ABOVE);
                        if (isStation)
                        {
                            proximity_score_increment(PROXIMITY_OWN_STATION_TOUCH_ABOVE);
                        }
                    }
                    if (inputTileElement->clearance_height + 2 <= tileElement->base_height)
                    {
                        if (inputTileElement->clearance_height + 10 >= tileElement->base_height)
                        {
                            proximity_score_increment(PROXIMITY_OWN_TRACK_CLOSE_ABOVE);
                            if (isStation)
                            {
                                proximity_score_increment(PROXIMITY_OWN_STATION_CLOSE_ABOVE);
                            }
                        }
                    }
                }
            }
            break;
        } // switch tileElement->GetType
    } while (!(tileElement++)->IsLastForTile());

    uint8_t direction = inputTileElement->GetDirection();
    ride_ratings_score_close_proximity_in_direction(inputTileElement, (direction + 1) & 3);
    ride_ratings_score_close_proximity_in_direction(inputTileElement, (direction - 1) & 3);
    ride_ratings_score_close_proximity_loops(inputTileElement);

    switch (gRideRatingsCalcData.proximity_track_type)
    {
        case TRACK_ELEM_BRAKES:
            gRideRatingsCalcData.num_brakes++;
            break;
        case TRACK_ELEM_LEFT_REVERSER:
        case TRACK_ELEM_RIGHT_REVERSER:
            gRideRatingsCalcData.num_reversers++;
            break;
    }
}

static void ride_ratings_calculate(Ride* ride)
{
    auto calcFunc = ride_ratings_get_calculate_func(ride->type);
    if (calcFunc != nullptr)
    {
        calcFunc(ride);
    }

#ifdef ORIGINAL_RATINGS
    if (ride->ratings.excitement != -1)
    {
        // Address underflows allowed by original RCT2 code
        ride->ratings.excitement = max(0, ride->ratings.excitement);
        ride->ratings.intensity = max(0, ride->ratings.intensity);
        ride->ratings.nausea = max(0, ride->ratings.nausea);
    }
#endif
}

static void ride_ratings_calculate_value(Ride* ride)
{
    struct row
    {
        int32_t months, multiplier, divisor, summand;
    };
    static const row ageTableNew[] = {
        { 5, 3, 2, 0 },       // 1.5x
        { 13, 6, 5, 0 },      // 1.2x
        { 40, 1, 1, 0 },      // 1x
        { 64, 3, 4, 0 },      // 0.75x
        { 88, 9, 16, 0 },     // 0.56x
        { 104, 27, 64, 0 },   // 0.42x
        { 120, 81, 256, 0 },  // 0.32x
        { 128, 81, 512, 0 },  // 0.16x
        { 200, 81, 1024, 0 }, // 0.08x
        { 200, 9, 16, 0 }     // 0.56x "easter egg"
    };

#ifdef ORIGINAL_RATINGS
    static const row ageTableOld[] = {
        { 5, 1, 1, 30 },      // +30
        { 13, 1, 1, 10 },     // +10
        { 40, 1, 1, 0 },      // 1x
        { 64, 3, 4, 0 },      // 0.75x
        { 88, 9, 16, 0 },     // 0.56x
        { 104, 27, 64, 0 },   // 0.42x
        { 120, 81, 256, 0 },  // 0.32x
        { 128, 81, 512, 0 },  // 0.16x
        { 200, 81, 1024, 0 }, // 0.08x
        { 200, 9, 16, 0 }     // 0.56x "easter egg"
    };
#endif

    if (!ride_has_ratings(ride))
    {
        return;
    }

    // Start with the base ratings, multiplied by the ride type specific weights for excitement, intensity and nausea.
    int32_t value = (((ride->excitement * RideRatings[ride->type].excitement) * 32) >> 15)
        + (((ride->intensity * RideRatings[ride->type].intensity) * 32) >> 15)
        + (((ride->nausea * RideRatings[ride->type].nausea) * 32) >> 15);

    int32_t monthsOld = 0;
    if (!gCheatsDisableRideValueAging)
    {
        monthsOld = gDateMonthsElapsed - ride->build_date;
    }

    const row* ageTable = ageTableNew;
    size_t tableSize = std::size(ageTableNew);

#ifdef ORIGINAL_RATINGS
    ageTable = ageTableOld;
    tableSize = std::size(ageTableOld);
#endif

    row lastRow = ageTable[tableSize - 1];

    // Ride is older than oldest age in the table?
    if (monthsOld >= lastRow.months)
    {
        value = (value * lastRow.multiplier) / lastRow.divisor + lastRow.summand;
    }
    else
    {
        // Find the first hit in the table that matches this ride's age
        for (size_t it = 0; it < tableSize; it++)
        {
            row curr = ageTable[it];

            if (monthsOld < curr.months)
            {
                value = (value * curr.multiplier) / curr.divisor + curr.summand;
                break;
            }
        }
    }

    // Other ride of same type penalty
    const auto& rideManager = GetRideManager();
    auto otherRidesOfSameType = std::count_if(rideManager.begin(), rideManager.end(), [ride](const Ride& r) {
        return r.status == RIDE_STATUS_OPEN && r.type == ride->type;
    });
    if (otherRidesOfSameType > 1)
        value -= value / 4;

    ride->value = std::max(0, value);
    if (gCheatsAutomaticRidePricing && park_ride_prices_unlocked())
    {
        ride->price = std::max(0, value * 2);
    }
}

/**
 * I think this function computes ride upkeep? Though it is weird that the
 *  rct2: sub_65E621
 * inputs
 * - edi: ride ptr
 */
static uint16_t ride_compute_upkeep(Ride* ride)
{
    // data stored at 0x0057E3A8, incrementing 18 bytes at a time
    uint16_t upkeep = initialUpkeepCosts[ride->type];

    uint16_t trackCost = costPerTrackPiece[ride->type];
    uint8_t dropFactor = ride->drops;

    dropFactor >>= 6;
    dropFactor &= 3;
    upkeep += trackCost * dropFactor;

    uint32_t totalLength = ride_get_total_length(ride) >> 16;

    // The data originally here was 20's and 0's. The 20's all represented
    // rides that had tracks. The 0's were fixed rides like crooked house or
    // dodgems.
    // Data source is 0x0097E3AC
    if (hasRunningTrack[ride->type])
    {
        totalLength *= 20;
    }
    upkeep += (uint16_t)(totalLength >> 10);

    if (ride->lifecycle_flags & RIDE_LIFECYCLE_ON_RIDE_PHOTO)
    {
        // The original code read from a table starting at 0x0097E3AE and
        // incrementing by 0x12 bytes between values. However, all of these
        // values were 40. I have replaced the table lookup with the constant
        // 40 in this case.
        upkeep += 40;
    }

    // Add maintenance cost for reverser track pieces
    uint16_t reverserMaintenanceCost = 80;
    if (ride->type == RIDE_TYPE_REVERSER_ROLLER_COASTER)
    {
        reverserMaintenanceCost = 10;
    }
    upkeep += reverserMaintenanceCost * gRideRatingsCalcData.num_reversers;

    // Add maintenance cost for brake track pieces
    upkeep += 20 * gRideRatingsCalcData.num_brakes;

    // these seem to be adhoc adjustments to a ride's upkeep/cost, times
    // various variables set on the ride itself.

    // https://gist.github.com/kevinburke/e19b803cd2769d96c540
    upkeep += costPerVehicle[ride->type] * ride->num_vehicles;

    // either set to 3 or 0, extra boosts for some rides including mini golf
    if (chargeUpkeepForTrainLength[ride->type])
    {
        upkeep += 3 * ride->num_cars_per_train;
    }

    // slight upkeep boosts for some rides - 5 for mini railway, 10 for log
    // flume/rapids, 10 for roller coaster, 28 for giga coaster
    upkeep += costPerStation[ride->type] * ride->num_stations;

    if (ride->mode == RIDE_MODE_REVERSE_INCLINE_LAUNCHED_SHUTTLE)
    {
        upkeep += 30;
    }
    else if (ride->mode == RIDE_MODE_POWERED_LAUNCH_PASSTROUGH)
    {
        upkeep += 160;
    }
    else if (ride->mode == RIDE_MODE_LIM_POWERED_LAUNCH)
    {
        upkeep += 320;
    }
    else if (ride->mode == RIDE_MODE_POWERED_LAUNCH || ride->mode == RIDE_MODE_POWERED_LAUNCH_BLOCK_SECTIONED)
    {
        upkeep += 220;
    }

    // multiply by 5/8
    upkeep *= 10;
    upkeep >>= 4;
    return upkeep;
}

/**
 *
 *  rct2: 0x0065E7FB
 *
 * inputs
 * - bx: excitement
 * - cx: intensity
 * - bp: nausea
 * - edi: ride ptr
 */
static void ride_ratings_apply_adjustments(Ride* ride, rating_tuple* ratings)
{
    rct_ride_entry* rideEntry = get_ride_entry(ride->subtype);

    if (rideEntry == nullptr)
    {
        return;
    }

    // Apply ride entry multipliers
    ride_ratings_add(
        ratings, (((int32_t)ratings->excitement * rideEntry->excitement_multiplier) >> 7),
        (((int32_t)ratings->intensity * rideEntry->intensity_multiplier) >> 7),
        (((int32_t)ratings->nausea * rideEntry->nausea_multiplier) >> 7));

    // Apply total air time
#ifdef ORIGINAL_RATINGS
    if (RideData4[ride->type].flags & RIDE_TYPE_FLAG4_HAS_AIR_TIME)
    {
        uint16_t totalAirTime = ride->total_air_time;
        if (rideEntry->flags & RIDE_ENTRY_FLAG_LIMIT_AIRTIME_BONUS)
        {
            if (totalAirTime >= 96)
            {
                totalAirTime -= 96;
                ratings->excitement -= totalAirTime / 8;
                ratings->nausea += totalAirTime / 16;
            }
        }
        else
        {
            ratings->excitement += totalAirTime / 8;
            ratings->nausea += totalAirTime / 16;
        }
    }
#else
    if (RideData4[ride->type].flags & RIDE_TYPE_FLAG4_HAS_AIR_TIME)
    {
        int32_t excitementModifier;
        int32_t nauseaModifier;
        if (rideEntry->flags & RIDE_ENTRY_FLAG_LIMIT_AIRTIME_BONUS)
        {
            // Limit airtime bonus for heartline twister coaster (see issues #2031 and #2064)
            excitementModifier = std::min<uint16_t>(ride->total_air_time, 96) / 8;
        }
        else
        {
            excitementModifier = ride->total_air_time / 8;
        }
        nauseaModifier = ride->total_air_time / 16;

        ride_ratings_add(ratings, excitementModifier, 0, nauseaModifier);
    }
#endif
}

/**
 * Lowers excitement, the higher the intensity.
 *  rct2: 0x0065E7A3
 */
static void ride_ratings_apply_intensity_penalty(rating_tuple* ratings)
{
    static const ride_rating intensityBounds[] = { 1000, 1100, 1200, 1320, 1450 };
    ride_rating excitement = ratings->excitement;
    for (auto intensityBound : intensityBounds)
    {
        if (ratings->intensity >= intensityBound)
        {
            excitement -= excitement / 4;
        }
    }
    ratings->excitement = excitement;
}

/**
 *
 *  rct2: 0x00655FD6
 */
static void set_unreliability_factor(Ride* ride)
{
    // The bigger the difference in lift speed and minimum the higher the unreliability
    uint8_t minLiftSpeed = RideLiftData[ride->type].minimum_speed;
    ride->unreliability_factor += (ride->lift_hill_speed - minLiftSpeed) * 2;
}

static uint32_t get_proximity_score_helper_1(uint16_t x, uint16_t max, uint32_t multiplier)
{
    return (std::min(x, max) * multiplier) >> 16;
}

static uint32_t get_proximity_score_helper_2(uint16_t x, uint16_t additionIfNotZero, uint16_t max, uint32_t multiplier)
{
    uint32_t result = x;
    if (result != 0)
        result += additionIfNotZero;
    return (std::min<int32_t>(result, max) * multiplier) >> 16;
}

static uint32_t get_proximity_score_helper_3(uint16_t x, uint16_t resultIfNotZero)
{
    return x == 0 ? 0 : resultIfNotZero;
}

/**
 *
 *  rct2: 0x0065E277
 */
static uint32_t ride_ratings_get_proximity_score()
{
    const uint16_t* scores = gRideRatingsCalcData.proximity_scores;

    uint32_t result = 0;
    result += get_proximity_score_helper_1(scores[PROXIMITY_WATER_OVER], 60, 0x00AAAA);
    result += get_proximity_score_helper_1(scores[PROXIMITY_WATER_TOUCH], 22, 0x0245D1);
    result += get_proximity_score_helper_1(scores[PROXIMITY_WATER_LOW], 10, 0x020000);
    result += get_proximity_score_helper_1(scores[PROXIMITY_WATER_HIGH], 40, 0x00A000);
    result += get_proximity_score_helper_1(scores[PROXIMITY_SURFACE_TOUCH], 70, 0x01B6DB);
    result += get_proximity_score_helper_1(scores[PROXIMITY_PATH_ZERO_OVER] + 8, 12, 0x064000);
    result += get_proximity_score_helper_3(scores[PROXIMITY_PATH_ZERO_TOUCH_ABOVE], 40);
    result += get_proximity_score_helper_3(scores[PROXIMITY_PATH_ZERO_TOUCH_UNDER], 45);
    result += get_proximity_score_helper_2(scores[PROXIMITY_PATH_TOUCH_ABOVE], 10, 20, 0x03C000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_PATH_TOUCH_UNDER], 10, 20, 0x044000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_OWN_TRACK_TOUCH_ABOVE], 10, 15, 0x035555);
    result += get_proximity_score_helper_1(scores[PROXIMITY_OWN_TRACK_CLOSE_ABOVE], 5, 0x060000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_FOREIGN_TRACK_ABOVE_OR_BELOW], 10, 15, 0x02AAAA);
    result += get_proximity_score_helper_2(scores[PROXIMITY_FOREIGN_TRACK_TOUCH_ABOVE], 10, 15, 0x04AAAA);
    result += get_proximity_score_helper_1(scores[PROXIMITY_FOREIGN_TRACK_CLOSE_ABOVE], 5, 0x090000);
    result += get_proximity_score_helper_1(scores[PROXIMITY_SCENERY_SIDE_BELOW], 35, 0x016DB6);
    result += get_proximity_score_helper_1(scores[PROXIMITY_SCENERY_SIDE_ABOVE], 35, 0x00DB6D);
    result += get_proximity_score_helper_3(scores[PROXIMITY_OWN_STATION_TOUCH_ABOVE], 55);
    result += get_proximity_score_helper_3(scores[PROXIMITY_OWN_STATION_CLOSE_ABOVE], 25);
    result += get_proximity_score_helper_2(scores[PROXIMITY_TRACK_THROUGH_VERTICAL_LOOP], 4, 6, 0x140000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_PATH_TROUGH_VERTICAL_LOOP], 4, 6, 0x0F0000);
    result += get_proximity_score_helper_3(scores[PROXIMITY_INTERSECTING_VERTICAL_LOOP], 100);
    result += get_proximity_score_helper_2(scores[PROXIMITY_THROUGH_VERTICAL_LOOP], 4, 6, 0x0A0000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_PATH_SIDE_CLOSE], 10, 20, 0x01C000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_FOREIGN_TRACK_SIDE_CLOSE], 10, 20, 0x024000);
    result += get_proximity_score_helper_2(scores[PROXIMITY_SURFACE_SIDE_CLOSE], 10, 20, 0x028000);
    return result;
}

/**
 * Calculates how much of the track is sheltered in eighths.
 *  rct2: 0x0065E72D
 */
static ShelteredEights get_num_of_sheltered_eighths(Ride* ride)
{
    int32_t totalLength = ride_get_total_length(ride);
    int32_t shelteredLength = ride->sheltered_length;
    int32_t lengthEighth = totalLength / 8;
    int32_t lengthCounter = lengthEighth;
    uint8_t numShelteredEighths = 0;
    for (int32_t i = 0; i < 7; i++)
    {
        if (shelteredLength >= lengthCounter)
        {
            lengthCounter += lengthEighth;
            numShelteredEighths++;
        }
    }

    uint8_t trackShelteredEighths = numShelteredEighths;
    rct_ride_entry* rideType = get_ride_entry(ride->subtype);
    if (rideType == nullptr)
    {
        return { 0, 0 };
    }
    if (rideType->flags & RIDE_ENTRY_FLAG_COVERED_RIDE)
        numShelteredEighths = 7;

    return { trackShelteredEighths, numShelteredEighths };
}

static rating_tuple get_flat_turns_rating(Ride* ride)
{
    int32_t num3PlusTurns = get_turn_count_3_elements(ride, 0);
    int32_t num2Turns = get_turn_count_2_elements(ride, 0);
    int32_t num1Turns = get_turn_count_1_element(ride, 0);

    rating_tuple rating;
    rating.excitement = (num3PlusTurns * 0x28000) >> 16;
    rating.excitement += (num2Turns * 0x30000) >> 16;
    rating.excitement += (num1Turns * 63421) >> 16;

    rating.intensity = (num3PlusTurns * 81920) >> 16;
    rating.intensity += (num2Turns * 49152) >> 16;
    rating.intensity += (num1Turns * 21140) >> 16;

    rating.nausea = (num3PlusTurns * 0x50000) >> 16;
    rating.nausea += (num2Turns * 0x32000) >> 16;
    rating.nausea += (num1Turns * 42281) >> 16;

    return rating;
}

/**
 *
 *  rct2: 0x0065DF72
 */
static rating_tuple get_banked_turns_rating(Ride* ride)
{
    int32_t num3PlusTurns = get_turn_count_3_elements(ride, 1);
    int32_t num2Turns = get_turn_count_2_elements(ride, 1);
    int32_t num1Turns = get_turn_count_1_element(ride, 1);

    rating_tuple rating;
    rating.excitement = (num3PlusTurns * 0x3C000) >> 16;
    rating.excitement += (num2Turns * 0x3C000) >> 16;
    rating.excitement += (num1Turns * 73992) >> 16;

    rating.intensity = (num3PlusTurns * 0x14000) >> 16;
    rating.intensity += (num2Turns * 49152) >> 16;
    rating.intensity += (num1Turns * 21140) >> 16;

    rating.nausea = (num3PlusTurns * 0x50000) >> 16;
    rating.nausea += (num2Turns * 0x32000) >> 16;
    rating.nausea += (num1Turns * 48623) >> 16;

    return rating;
}

/**
 *
 *  rct2: 0x0065E047
 */
static rating_tuple get_sloped_turns_rating(Ride* ride)
{
    rating_tuple rating;

    int32_t num4PlusTurns = get_turn_count_4_plus_elements(ride, 2);
    int32_t num3Turns = get_turn_count_3_elements(ride, 2);
    int32_t num2Turns = get_turn_count_2_elements(ride, 2);
    int32_t num1Turns = get_turn_count_1_element(ride, 2);

    rating.excitement = (std::min(num4PlusTurns, 4) * 0x78000) >> 16;
    rating.excitement += (std::min(num3Turns, 6) * 273066) >> 16;
    rating.excitement += (std::min(num2Turns, 6) * 0x3AAAA) >> 16;
    rating.excitement += (std::min(num1Turns, 7) * 187245) >> 16;
    rating.intensity = 0;
    rating.nausea = (std::min(num4PlusTurns, 8) * 0x78000) >> 16;

    return rating;
}

/**
 *
 *  rct2: 0x0065E0F2
 */
static rating_tuple get_inversions_ratings(uint16_t inversions)
{
    rating_tuple rating;

    rating.excitement = (std::min<int32_t>(inversions, 6) * 0x1AAAAA) >> 16;
    rating.intensity = (inversions * 0x320000) >> 16;
    rating.nausea = (inversions * 0x15AAAA) >> 16;

    return rating;
}

static rating_tuple get_special_track_elements_rating(uint8_t type, Ride* ride)
{
    int32_t excitement = 0, intensity = 0, nausea = 0;

    if (type == RIDE_TYPE_GHOST_TRAIN)
    {
        if (ride->HasSpinningTunnel())
        {
            excitement += 40;
            intensity += 25;
            nausea += 55;
        }
    }
    else if (type == RIDE_TYPE_LOG_FLUME)
    {
        if (ride->HasLogReverser())
        {
            excitement += 48;
            intensity += 55;
            nausea += 65;
        }
    }
    else
    {
        if (ride->HasWaterSplash())
        {
            excitement += 50;
            intensity += 30;
            nausea += 20;
        }
        if (ride->HasWaterfall())
        {
            excitement += 55;
            intensity += 30;
        }
        if (ride->HasWhirlpool())
        {
            excitement += 35;
            intensity += 20;
            nausea += 23;
        }
    }

    uint8_t helixSections = ride_get_helix_sections(ride);

    int32_t helixesUpTo9 = std::min<int32_t>(helixSections, 9);
    excitement += (helixesUpTo9 * 254862) >> 16;

    int32_t helixesUpTo11 = std::min<int32_t>(helixSections, 11);
    intensity += (helixesUpTo11 * 148945) >> 16;

    int32_t helixesOver5UpTo10 = std::clamp<int32_t>(helixSections - 5, 0, 10);
    nausea += (helixesOver5UpTo10 * 0x140000) >> 16;

    rating_tuple rating = { (ride_rating)excitement, (ride_rating)intensity, (ride_rating)nausea };
    return rating;
}

/**
 *
 *  rct2: 0x0065DDD1
 */
static rating_tuple ride_ratings_get_turns_ratings(Ride* ride)
{
    int32_t excitement = 0, intensity = 0, nausea = 0;

    rating_tuple specialTrackElementsRating = get_special_track_elements_rating(ride->type, ride);
    excitement += specialTrackElementsRating.excitement;
    intensity += specialTrackElementsRating.intensity;
    nausea += specialTrackElementsRating.nausea;

    rating_tuple flatTurnsRating = get_flat_turns_rating(ride);
    excitement += flatTurnsRating.excitement;
    intensity += flatTurnsRating.intensity;
    nausea += flatTurnsRating.nausea;

    rating_tuple bankedTurnsRating = get_banked_turns_rating(ride);
    excitement += bankedTurnsRating.excitement;
    intensity += bankedTurnsRating.intensity;
    nausea += bankedTurnsRating.nausea;

    rating_tuple slopedTurnsRating = get_sloped_turns_rating(ride);
    excitement += slopedTurnsRating.excitement;
    intensity += slopedTurnsRating.intensity;
    nausea += slopedTurnsRating.nausea;

    auto inversions = (ride->type == RIDE_TYPE_MINI_GOLF) ? ride->holes : ride->inversions;
    rating_tuple inversionsRating = get_inversions_ratings(inversions);
    excitement += inversionsRating.excitement;
    intensity += inversionsRating.intensity;
    nausea += inversionsRating.nausea;

    rating_tuple rating = { (ride_rating)excitement, (ride_rating)intensity, (ride_rating)nausea };
    return rating;
}

/**
 *
 *  rct2: 0x0065E1C2
 */
static rating_tuple ride_ratings_get_sheltered_ratings(Ride* ride)
{
    int32_t shelteredLengthShifted = (ride->sheltered_length) >> 16;

    uint32_t shelteredLengthUpTo1000 = std::min(shelteredLengthShifted, 1000);
    uint32_t shelteredLengthUpTo2000 = std::min(shelteredLengthShifted, 2000);

    int32_t excitement = (shelteredLengthUpTo1000 * 9175) >> 16;
    int32_t intensity = (shelteredLengthUpTo2000 * 0x2666) >> 16;
    int32_t nausea = (shelteredLengthUpTo1000 * 0x4000) >> 16;

    /*eax = (ride->var_11C * 30340) >> 16;*/
    /*nausea += eax;*/

    if (ride->num_sheltered_sections & 0x40)
    {
        excitement += 20;
        nausea += 15;
    }

    if (ride->num_sheltered_sections & 0x20)
    {
        excitement += 20;
        nausea += 15;
    }

    uint8_t lowerVal = ride->num_sheltered_sections & 0x1F;
    lowerVal = std::min<uint8_t>(lowerVal, 11);
    excitement += (lowerVal * 774516) >> 16;

    rating_tuple rating = { (ride_rating)excitement, (ride_rating)intensity, (ride_rating)nausea };
    return rating;
}

/**
 *
 *  rct2: 0x0065DCDC
 */
static rating_tuple ride_ratings_get_gforce_ratings(Ride* ride)
{
    rating_tuple result = {
        /* .excitement = */ 0,
        /* .intensity = */ 0,
        /* .nausea = */ 0,
    };

    // Apply maximum positive G force factor
    result.excitement += (ride->max_positive_vertical_g * 5242) >> 16;
    result.intensity += (ride->max_positive_vertical_g * 52428) >> 16;
    result.nausea += (ride->max_positive_vertical_g * 17039) >> 16;

    // Apply maximum negative G force factor
    fixed16_2dp gforce = ride->max_negative_vertical_g;
    result.excitement += (std::clamp<fixed16_2dp>(gforce, -FIXED_2DP(2, 50), FIXED_2DP(0, 00)) * -15728) >> 16;
    result.intensity += ((gforce - FIXED_2DP(1, 00)) * -52428) >> 16;
    result.nausea += ((gforce - FIXED_2DP(1, 00)) * -14563) >> 16;

    // Apply lateral G force factor
    result.excitement += (std::min<fixed16_2dp>(FIXED_2DP(1, 50), ride->max_lateral_g) * 26214) >> 16;
    result.intensity += ride->max_lateral_g;
    result.nausea += (ride->max_lateral_g * 21845) >> 16;

// Very high lateral G force penalty
#ifdef ORIGINAL_RATINGS
    if (ride->max_lateral_g > FIXED_2DP(2, 80))
    {
        result.intensity += FIXED_2DP(3, 75);
        result.nausea += FIXED_2DP(2, 00);
    }
    if (ride->max_lateral_g > FIXED_2DP(3, 10))
    {
        result.excitement /= 2;
        result.intensity += FIXED_2DP(8, 50);
        result.nausea += FIXED_2DP(4, 00);
    }
#endif

    return result;
}

/**
 *
 *  rct2: 0x0065E139
 */
static rating_tuple ride_ratings_get_drop_ratings(Ride* ride)
{
    rating_tuple result = {
        /* .excitement = */ 0,
        /* .intensity = */ 0,
        /* .nausea = */ 0,
    };

    // Apply number of drops factor
    int32_t drops = ride->drops & 0x3F;
    result.excitement += (std::min(9, drops) * 728177) >> 16;
    result.intensity += (drops * 928426) >> 16;
    result.nausea += (drops * 655360) >> 16;

    // Apply highest drop factor
    ride_ratings_add(
        &result, ((ride->highest_drop_height * 2) * 16000) >> 16, ((ride->highest_drop_height * 2) * 32000) >> 16,
        ((ride->highest_drop_height * 2) * 10240) >> 16);

    return result;
}

/**
 * Calculates a score based on the surrounding scenery.
 *  rct2: 0x0065E557
 */
static int32_t ride_ratings_get_scenery_score(Ride* ride)
{
    int8_t i = ride_get_first_valid_station_start(ride);
    int32_t x, y;

    if (i == -1)
    {
        return 0;
    }

    if (ride->type == RIDE_TYPE_MAZE)
    {
        TileCoordsXYZD location = ride_get_entrance_location(ride, 0);
        x = location.x;
        y = location.y;
    }
    else
    {
        auto location = ride->stations[i].Start;
        x = location.x;
        y = location.y;
    }

    int32_t z = tile_element_height({ x * 32, y * 32 });

    // Check if station is underground, returns a fixed mediocre score since you can't have scenery underground
    if (z > ride->stations[i].GetBaseZ())
    {
        return 40;
    }

    // Count surrounding scenery items
    int32_t numSceneryItems = 0;
    for (int32_t yy = std::max(y - 5, 0); yy <= std::min(y + 5, 255); yy++)
    {
        for (int32_t xx = std::max(x - 5, 0); xx <= std::min(x + 5, 255); xx++)
        {
            // Count scenery items on this tile
            TileElement* tileElement = map_get_first_element_at(TileCoordsXY{ xx, yy }.ToCoordsXY());
            if (tileElement == nullptr)
                continue;
            do
            {
                if (tileElement->IsGhost())
                    continue;

                int32_t type = tileElement->GetType();
                if (type == TILE_ELEMENT_TYPE_SMALL_SCENERY || type == TILE_ELEMENT_TYPE_LARGE_SCENERY)
                    numSceneryItems++;
            } while (!(tileElement++)->IsLastForTile());
        }
    }

    return std::min(numSceneryItems, 47) * 5;
}

#pragma region Ride rating calculation helpers

static void ride_ratings_set(rating_tuple* ratings, int32_t excitement, int32_t intensity, int32_t nausea)
{
    ratings->excitement = 0;
    ratings->intensity = 0;
    ratings->nausea = 0;
    ride_ratings_add(ratings, excitement, intensity, nausea);
}

/**
 * Add to a ride rating with overflow protection.
 */
static void ride_ratings_add(rating_tuple* rating, int32_t excitement, int32_t intensity, int32_t nausea)
{
    int32_t newExcitement = rating->excitement + excitement;
    int32_t newIntensity = rating->intensity + intensity;
    int32_t newNausea = rating->nausea + nausea;
    rating->excitement = std::clamp<int32_t>(newExcitement, 0, INT16_MAX);
    rating->intensity = std::clamp<int32_t>(newIntensity, 0, INT16_MAX);
    rating->nausea = std::clamp<int32_t>(newNausea, 0, INT16_MAX);
}

static void ride_ratings_apply_length(rating_tuple* ratings, Ride* ride, int32_t maxLength, int32_t excitementMultiplier)
{
    ride_ratings_add(ratings, (std::min(ride_get_total_length(ride) >> 16, maxLength) * excitementMultiplier) >> 16, 0, 0);
}

static void ride_ratings_apply_synchronisation(rating_tuple* ratings, Ride* ride, int32_t excitement, int32_t intensity)
{
    if ((ride->depart_flags & RIDE_DEPART_SYNCHRONISE_WITH_ADJACENT_STATIONS) && ride_has_adjacent_station(ride))
    {
        ride_ratings_add(ratings, excitement, intensity, 0);
    }
}

static void ride_ratings_apply_train_length(rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier)
{
    ride_ratings_add(ratings, ((ride->num_cars_per_train - 1) * excitementMultiplier) >> 16, 0, 0);
}

static void ride_ratings_apply_max_speed(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier, int32_t nauseaMultiplier)
{
    int32_t modifier = ride->max_speed >> 16;
    ride_ratings_add(
        ratings, (modifier * excitementMultiplier) >> 16, (modifier * intensityMultiplier) >> 16,
        (modifier * nauseaMultiplier) >> 16);
}

static void ride_ratings_apply_average_speed(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier)
{
    int32_t modifier = ride->average_speed >> 16;
    ride_ratings_add(ratings, (modifier * excitementMultiplier) >> 16, (modifier * intensityMultiplier) >> 16, 0);
}

static void ride_ratings_apply_duration(rating_tuple* ratings, Ride* ride, int32_t maxDuration, int32_t excitementMultiplier)
{
    ride_ratings_add(ratings, (std::min(ride_get_total_time(ride), maxDuration) * excitementMultiplier) >> 16, 0, 0);
}

static void ride_ratings_apply_gforces(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier, int32_t nauseaMultiplier)
{
    rating_tuple subRating = ride_ratings_get_gforce_ratings(ride);
    ride_ratings_add(
        ratings, (subRating.excitement * excitementMultiplier) >> 16, (subRating.intensity * intensityMultiplier) >> 16,
        (subRating.nausea * nauseaMultiplier) >> 16);
}

static void ride_ratings_apply_turns(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier, int32_t nauseaMultiplier)
{
    rating_tuple subRating = ride_ratings_get_turns_ratings(ride);
    ride_ratings_add(
        ratings, (subRating.excitement * excitementMultiplier) >> 16, (subRating.intensity * intensityMultiplier) >> 16,
        (subRating.nausea * nauseaMultiplier) >> 16);
}

static void ride_ratings_apply_drops(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier, int32_t nauseaMultiplier)
{
    rating_tuple subRating = ride_ratings_get_drop_ratings(ride);
    ride_ratings_add(
        ratings, (subRating.excitement * excitementMultiplier) >> 16, (subRating.intensity * intensityMultiplier) >> 16,
        (subRating.nausea * nauseaMultiplier) >> 16);
}

static void ride_ratings_apply_sheltered_ratings(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier, int32_t nauseaMultiplier)
{
    rating_tuple subRating = ride_ratings_get_sheltered_ratings(ride);
    ride_ratings_add(
        ratings, (subRating.excitement * excitementMultiplier) >> 16, (subRating.intensity * intensityMultiplier) >> 16,
        (subRating.nausea * nauseaMultiplier) >> 16);
}

static void ride_ratings_apply_operation_option(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier, int32_t nauseaMultiplier)
{
    ride_ratings_add(
        ratings, (ride->operation_option * excitementMultiplier) >> 16, (ride->operation_option * intensityMultiplier) >> 16,
        (ride->operation_option * nauseaMultiplier) >> 16);
}

static void ride_ratings_apply_rotations(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier, int32_t nauseaMultiplier)
{
    ride_ratings_add(
        ratings, ride->rotations * excitementMultiplier, ride->rotations * intensityMultiplier,
        ride->rotations * nauseaMultiplier);
}

static void ride_ratings_apply_proximity(rating_tuple* ratings, int32_t excitementMultiplier)
{
    ride_ratings_add(ratings, (ride_ratings_get_proximity_score() * excitementMultiplier) >> 16, 0, 0);
}

static void ride_ratings_apply_scenery(rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier)
{
    ride_ratings_add(ratings, (ride_ratings_get_scenery_score(ride) * excitementMultiplier) >> 16, 0, 0);
}

static void ride_ratings_apply_highest_drop_height_penalty(
    rating_tuple* ratings, Ride* ride, int32_t minHighestDropHeight, int32_t excitementPenalty, int32_t intensityPenalty,
    int32_t nauseaPenalty)
{
    if (ride->highest_drop_height < minHighestDropHeight)
    {
        ratings->excitement /= excitementPenalty;
        ratings->intensity /= intensityPenalty;
        ratings->nausea /= nauseaPenalty;
    }
}

static void ride_ratings_apply_max_speed_penalty(
    rating_tuple* ratings, Ride* ride, int32_t minMaxSpeed, int32_t excitementPenalty, int32_t intensityPenalty,
    int32_t nauseaPenalty)
{
    if (ride->max_speed < minMaxSpeed)
    {
        ratings->excitement /= excitementPenalty;
        ratings->intensity /= intensityPenalty;
        ratings->nausea /= nauseaPenalty;
    }
}

static void ride_ratings_apply_num_drops_penalty(
    rating_tuple* ratings, Ride* ride, int32_t minNumDrops, int32_t excitementPenalty, int32_t intensityPenalty,
    int32_t nauseaPenalty)
{
    if ((ride->drops & 0x3F) < minNumDrops)
    {
        ratings->excitement /= excitementPenalty;
        ratings->intensity /= intensityPenalty;
        ratings->nausea /= nauseaPenalty;
    }
}

static void ride_ratings_apply_max_negative_g_penalty(
    rating_tuple* ratings, Ride* ride, int32_t maxMaxNegativeVerticalG, int32_t excitementPenalty, int32_t intensityPenalty,
    int32_t nauseaPenalty)
{
    if (ride->max_negative_vertical_g >= maxMaxNegativeVerticalG)
    {
        ratings->excitement /= excitementPenalty;
        ratings->intensity /= intensityPenalty;
        ratings->nausea /= nauseaPenalty;
    }
}

static void ride_ratings_apply_max_lateral_g_penalty(
    rating_tuple* ratings, Ride* ride, int32_t minMaxLateralG, int32_t excitementPenalty, int32_t intensityPenalty,
    int32_t nauseaPenalty)
{
    if (ride->max_lateral_g < minMaxLateralG)
    {
        ratings->excitement /= excitementPenalty;
        ratings->intensity /= intensityPenalty;
        ratings->nausea /= nauseaPenalty;
    }
}

static rating_tuple ride_ratings_get_excessive_lateral_g_penalty(Ride* ride)
{
    rating_tuple result{};
    if (ride->max_lateral_g > FIXED_2DP(2, 80))
    {
        result.intensity = FIXED_2DP(3, 75);
        result.nausea = FIXED_2DP(2, 00);
    }

    if (ride->max_lateral_g > FIXED_2DP(3, 10))
    {
        // Remove half of the ride_ratings_get_gforce_ratings
        result.excitement = (ride->max_positive_vertical_g * 5242) >> 16;

        // Apply maximum negative G force factor
        fixed16_2dp gforce = ride->max_negative_vertical_g;
        result.excitement += (std::clamp<fixed16_2dp>(gforce, -FIXED_2DP(2, 50), FIXED_2DP(0, 00)) * -15728) >> 16;

        // Apply lateral G force factor
        result.excitement += (std::min<fixed16_2dp>(FIXED_2DP(1, 50), ride->max_lateral_g) * 26214) >> 16;

        // Remove half of the ride_ratings_get_gforce_ratings
        result.excitement /= 2;
        result.excitement *= -1;
        result.intensity = FIXED_2DP(12, 25);
        result.nausea = FIXED_2DP(6, 00);
    }
    return result;
}

static void ride_ratings_apply_excessive_lateral_g_penalty(
    rating_tuple* ratings, Ride* ride, int32_t excitementMultiplier, int32_t intensityMultiplier, int32_t nauseaMultiplier)
{
#ifndef ORIGINAL_RATINGS
    rating_tuple subRating = ride_ratings_get_excessive_lateral_g_penalty(ride);
    ride_ratings_add(
        ratings, (subRating.excitement * excitementMultiplier) >> 16, (subRating.intensity * intensityMultiplier) >> 16,
        (subRating.nausea * nauseaMultiplier) >> 16);
#endif
}

static void ride_ratings_apply_first_length_penalty(
    rating_tuple* ratings, Ride* ride, int32_t minFirstLength, int32_t excitementPenalty, int32_t intensityPenalty,
    int32_t nauseaPenalty)
{
    if (ride->stations[0].SegmentLength < minFirstLength)
    {
        ratings->excitement /= excitementPenalty;
        ratings->intensity /= intensityPenalty;
        ratings->nausea /= nauseaPenalty;
    }
}

#pragma endregion

#pragma region Ride rating calculation functions

static void ride_ratings_calculate_spiral_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 30), RIDE_RATING(0, 30), RIDE_RATING(0, 30));
    ride_ratings_apply_length(&ratings, ride, 6000, 819);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 140434);
    ride_ratings_apply_max_speed(&ratings, ride, 51366, 85019, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 400497);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 36864, 30384, 49648);
    ride_ratings_apply_turns(&ratings, ride, 28235, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 43690, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
        ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
    {
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 40), 2, 2, 2);
        ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);
    }

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 36864, 30384, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_stand_up_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 17;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 50), RIDE_RATING(3, 00), RIDE_RATING(3, 00));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 10));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 123987, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 35746, 59578);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 34952, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 12850, 28398, 30427);
    ride_ratings_apply_proximity(&ratings, 17893);
    ride_ratings_apply_scenery(&ratings, ride, 5577);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 50), 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 35746, 59578);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_suspended_swinging_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 18;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 30), RIDE_RATING(2, 90), RIDE_RATING(3, 50));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 10));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 32768, 23831, 79437);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 48036);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6971);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 8, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xC0000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 60), 2, 2, 2);
    ride_ratings_apply_max_lateral_g_penalty(&ratings, ride, FIXED_2DP(1, 50), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0x1720000, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 32768, 23831, 79437);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_inverted_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 17;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 60), RIDE_RATING(2, 80), RIDE_RATING(3, 20));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 42), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 29789, 55606);
    ride_ratings_apply_turns(&ratings, ride, 26749, 29552, 57186);
    ride_ratings_apply_drops(&ratings, ride, 29127, 39009, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 15291, 35108);
    ride_ratings_apply_proximity(&ratings, 15657);
    ride_ratings_apply_scenery(&ratings, ride, 8366);

    if (ride->inversions == 0)
        ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 30), 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 29789, 55606);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_junior_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 13;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 40), RIDE_RATING(2, 50), RIDE_RATING(1, 80));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 20480, 23831, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 25700, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 9760);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 6, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x70000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 1, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 20480, 23831, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_miniature_railway(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 11;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 50), RIDE_RATING(0, 00), RIDE_RATING(0, 00));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_train_length(&ratings, ride, 140434);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, -6425, 6553, 23405);
    ride_ratings_apply_proximity(&ratings, 8946);
    ride_ratings_apply_scenery(&ratings, ride, 20915);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xC80000, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    auto shelteredEighths = get_num_of_sheltered_eighths(ride);
    if (shelteredEighths.TrackShelteredEighths >= 4)
        ride->excitement /= 4;

    ride->sheltered_eighths = shelteredEighths.TotalShelteredEighths;
}

static void ride_ratings_calculate_monorail(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 00), RIDE_RATING(0, 00), RIDE_RATING(0, 00));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_train_length(&ratings, ride, 93622);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 70849, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 218453);
    ride_ratings_apply_duration(&ratings, ride, 150, 21845);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 5140, 6553, 18724);
    ride_ratings_apply_proximity(&ratings, 8946);
    ride_ratings_apply_scenery(&ratings, ride, 16732);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xAA0000, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    auto shelteredEighths = get_num_of_sheltered_eighths(ride);
    if (shelteredEighths.TrackShelteredEighths >= 4)
        ride->excitement /= 4;

    ride->sheltered_eighths = shelteredEighths.TotalShelteredEighths;
}

static void ride_ratings_calculate_mini_suspended_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 15;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 80), RIDE_RATING(2, 50), RIDE_RATING(2, 70));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 45), RIDE_RATING(0, 15));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 34179, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 58254, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 19275, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 13943);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 6, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x80000, 2, 2, 2);
    ride_ratings_apply_max_lateral_g_penalty(&ratings, ride, FIXED_2DP(1, 30), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xC80000, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_boat_hire(Ride* ride)
{
    ride->unreliability_factor = 7;
    set_unreliability_factor(ride);

    // NOTE In the original game, the ratings were zeroed before calling set_unreliability_factor which is unusual as rest
    // of the calculation functions do this before hand. This is because set_unreliability_factor alters the value of ebx
    // (excitement). This is assumed to be a bug and therefore fixed.

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 90), RIDE_RATING(0, 80), RIDE_RATING(0, 90));

    // Most likely checking if the ride has does not have a circuit
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
    {
        ride_ratings_add(&ratings, RIDE_RATING(0, 20), 0, 0);
    }

    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 22310);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_wooden_wild_mouse(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 90), RIDE_RATING(2, 90), RIDE_RATING(2, 10));
    ride_ratings_apply_length(&ratings, ride, 6000, 873);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 8));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 655360);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 102400, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 29721, 43458, 45749);
    ride_ratings_apply_drops(&ratings, ride, 40777, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 17893);
    ride_ratings_apply_scenery(&ratings, ride, 5577);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 8, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x70000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 10), 2, 2, 2);
    ride_ratings_apply_max_lateral_g_penalty(&ratings, ride, FIXED_2DP(1, 50), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xAA0000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 3, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 102400, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_steeplechase(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 70), RIDE_RATING(2, 40), RIDE_RATING(1, 80));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 75), RIDE_RATING(0, 9));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 20480, 20852, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 25700, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 9760);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 4, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x80000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 50), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xF00000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 20480, 20852, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_car_ride(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 12;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 00), RIDE_RATING(0, 50), RIDE_RATING(0, 00));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 15), RIDE_RATING(0, 00));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_turns(&ratings, ride, 14860, 0, 11437);
    ride_ratings_apply_drops(&ratings, ride, 8738, 0, 0);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 12850, 6553, 4681);
    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 8366);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xC80000, 8, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_launched_freefall(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 70), RIDE_RATING(3, 00), RIDE_RATING(3, 50));

    if (ride->mode == RIDE_MODE_DOWNWARD_LAUNCH)
    {
        ride_ratings_add(&ratings, RIDE_RATING(0, 30), RIDE_RATING(0, 65), RIDE_RATING(0, 45));
    }

    int32_t excitementModifier = ((ride_get_total_length(ride) >> 16) * 32768) >> 16;
    ride_ratings_add(&ratings, excitementModifier, 0, 0);

#ifdef ORIGINAL_RATINGS
    ride_ratings_apply_operation_option(&ratings, ride, 0, 1355917, 451972);
#else
    // Only apply "launch speed" effects when the setting can be modified
    if (ride->mode == RIDE_MODE_UPWARD_LAUNCH)
    {
        ride_ratings_apply_operation_option(&ratings, ride, 0, 1355917, 451972);
    }
    else
    {
        // Fix #3282: When the ride mode is in downward launch mode, the intensity and
        //            nausea were fixed regardless of how high the ride is. The following
        //            calculation is based on roto-drop which is a similar mechanic.
        int32_t lengthFactor = ((ride_get_total_length(ride) >> 16) * 209715) >> 16;
        ride_ratings_add(&ratings, lengthFactor, lengthFactor * 2, lengthFactor * 2);
    }
#endif

    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 25098);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_bobsleigh_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 80), RIDE_RATING(3, 20), RIDE_RATING(2, 50));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 20), RIDE_RATING(0, 00));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 65536, 23831, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 5577);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xC0000, 2, 2, 2);
    ride_ratings_apply_max_lateral_g_penalty(&ratings, ride, FIXED_2DP(1, 20), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0x1720000, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 65536, 23831, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_observation_tower(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 15;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 50), RIDE_RATING(0, 00), RIDE_RATING(0, 10));
    ride_ratings_add(
        &ratings, ((ride_get_total_length(ride) >> 16) * 45875) >> 16, 0, ((ride_get_total_length(ride) >> 16) * 26214) >> 16);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 83662);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 7;

    auto shelteredEighths = get_num_of_sheltered_eighths(ride);
    if (shelteredEighths.TrackShelteredEighths >= 5)
        ride->excitement /= 4;
}

static void ride_ratings_calculate_looping_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = ride->IsPoweredLaunched() ? 20 : 15;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 00), RIDE_RATING(0, 50), RIDE_RATING(0, 20));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
        ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 14, 2, 2, 2);

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
    {
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 10), 2, 2, 2);
        ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);
    }

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_dinghy_slide(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 13;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 70), RIDE_RATING(2, 00), RIDE_RATING(1, 50));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 50), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 65536, 29789, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 5577);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x70000, 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0x8C0000, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 65536, 29789, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_mine_train_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 90), RIDE_RATING(2, 30), RIDE_RATING(2, 10));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 40960, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 29721, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 19275, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 21472);
    ride_ratings_apply_scenery(&ratings, ride, 16732);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 8, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 10), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0x1720000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 40960, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_chairlift(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14 + (ride->speed * 2);
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 60), RIDE_RATING(0, 40), RIDE_RATING(0, 50));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_turns(&ratings, ride, 7430, 3476, 4574);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, -19275, 21845, 23405);
    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 25098);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0x960000, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    if (ride->num_stations <= 1)
    {
        ratings.excitement = 0;
        ratings.intensity /= 2;
    }

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    auto shelteredEighths = get_num_of_sheltered_eighths(ride);
    if (shelteredEighths.TrackShelteredEighths >= 4)
        ride->excitement /= 4;

    ride->sheltered_eighths = shelteredEighths.TotalShelteredEighths;
}

static void ride_ratings_calculate_corkscrew_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 00), RIDE_RATING(0, 50), RIDE_RATING(0, 20));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
        ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
    {
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 40), 2, 2, 2);
        ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);
    }

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_maze(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 8;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 30), RIDE_RATING(0, 50), RIDE_RATING(0, 00));

    int32_t size = std::min<uint16_t>(ride->maze_tiles, 100);
    ride_ratings_add(&ratings, size, size * 2, 0);

    ride_ratings_apply_scenery(&ratings, ride, 22310);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_spiral_slide(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 8;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 50), RIDE_RATING(1, 40), RIDE_RATING(0, 90));

    // Unlimited slides boost
    if (ride->mode == RIDE_MODE_UNLIMITED_RIDES_PER_ADMISSION)
    {
        ride_ratings_add(&ratings, RIDE_RATING(0, 40), RIDE_RATING(0, 20), RIDE_RATING(0, 25));
    }

    ride_ratings_apply_scenery(&ratings, ride, 25098);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 2;
}

static void ride_ratings_calculate_go_karts(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 42), RIDE_RATING(1, 73), RIDE_RATING(0, 40));
    ride_ratings_apply_length(&ratings, ride, 700, 32768);

    if (ride->mode == RIDE_MODE_RACE && ride->num_vehicles >= 4)
    {
        ride_ratings_add(&ratings, RIDE_RATING(1, 40), RIDE_RATING(0, 50), 0);

        int32_t lapsFactor = (ride->num_laps - 1) * 30;
        ride_ratings_add(&ratings, lapsFactor, lapsFactor / 2, 0);
    }

    ride_ratings_apply_turns(&ratings, ride, 4458, 3476, 5718);
    ride_ratings_apply_drops(&ratings, ride, 8738, 5461, 6553);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 2570, 8738, 2340);
    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 16732);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    auto shelteredEighths = get_num_of_sheltered_eighths(ride);
    ride->sheltered_eighths = shelteredEighths.TotalShelteredEighths;

    if (shelteredEighths.TrackShelteredEighths >= 6)
        ride->excitement /= 2;
}

static void ride_ratings_calculate_log_flume(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 15;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 50), RIDE_RATING(0, 55), RIDE_RATING(0, 30));
    ride_ratings_apply_length(&ratings, ride, 2000, 7208);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_max_speed(&ratings, ride, 531372, 655360, 301111);
    ride_ratings_apply_duration(&ratings, ride, 300, 13107);
    ride_ratings_apply_turns(&ratings, ride, 22291, 20860, 4574);
    ride_ratings_apply_drops(&ratings, ride, 69905, 62415, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 22367);
    ride_ratings_apply_scenery(&ratings, ride, 11155);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 2, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_river_rapids(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 20), RIDE_RATING(0, 70), RIDE_RATING(0, 50));
    ride_ratings_apply_length(&ratings, ride, 2000, 6225);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 30), RIDE_RATING(0, 05));
    ride_ratings_apply_max_speed(&ratings, ride, 115130, 159411, 106274);
    ride_ratings_apply_duration(&ratings, ride, 500, 13107);
    ride_ratings_apply_turns(&ratings, ride, 29721, 22598, 5718);
    ride_ratings_apply_drops(&ratings, ride, 40777, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 31314);
    ride_ratings_apply_scenery(&ratings, ride, 13943);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 2, 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xC80000, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_dodgems(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 30), RIDE_RATING(0, 50), RIDE_RATING(0, 35));

    if (ride->num_vehicles >= 4)
    {
        ride_ratings_add(&ratings, RIDE_RATING(0, 40), 0, 0);
    }

    ride_ratings_add(&ratings, ride->operation_option, ride->operation_option / 2, 0);

    if (ride->num_vehicles >= 4)
    {
        ride_ratings_add(&ratings, RIDE_RATING(0, 40), 0, 0);
    }

    ride_ratings_apply_scenery(&ratings, ride, 5577);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 7;
}

static void ride_ratings_calculate_pirate_ship(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 10;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 50), RIDE_RATING(1, 90), RIDE_RATING(1, 41));

    ride_ratings_add(&ratings, ride->operation_option * 5, ride->operation_option * 5, ride->operation_option * 10);

    ride_ratings_apply_scenery(&ratings, ride, 16732);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_inverter_ship(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 50), RIDE_RATING(2, 70), RIDE_RATING(2, 74));

    ride_ratings_add(&ratings, ride->operation_option * 11, ride->operation_option * 22, ride->operation_option * 22);

    ride_ratings_apply_scenery(&ratings, ride, 11155);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_food_stall(Ride* ride)
{
    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
}

static void ride_ratings_calculate_drink_stall(Ride* ride)
{
    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
}

static void ride_ratings_calculate_shop(Ride* ride)
{
    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
}

static void ride_ratings_calculate_merry_go_round(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(0, 60), RIDE_RATING(0, 15), RIDE_RATING(0, 30));
    ride_ratings_apply_rotations(&ratings, ride, 5, 5, 5);
    ride_ratings_apply_scenery(&ratings, ride, 19521);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 7;
}

static void ride_ratings_calculate_information_kiosk(Ride* ride)
{
    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
}

static void ride_ratings_calculate_toilets(Ride* ride)
{
    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
}

static void ride_ratings_calculate_ferris_wheel(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(0, 60), RIDE_RATING(0, 25), RIDE_RATING(0, 30));
    ride_ratings_apply_rotations(&ratings, ride, 25, 25, 25);
    ride_ratings_apply_scenery(&ratings, ride, 41831);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_motion_simulator(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 21;
    set_unreliability_factor(ride);

    // Base ratings
    rating_tuple ratings;
    if (ride->mode == RIDE_MODE_FILM_THRILL_RIDERS)
    {
        ratings.excitement = RIDE_RATING(3, 25);
        ratings.intensity = RIDE_RATING(4, 10);
        ratings.nausea = RIDE_RATING(3, 30);
    }
    else
    {
        ratings.excitement = RIDE_RATING(2, 90);
        ratings.intensity = RIDE_RATING(3, 50);
        ratings.nausea = RIDE_RATING(3, 00);
    }

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 7;
}

static void ride_ratings_calculate_3d_cinema(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 21;
    set_unreliability_factor(ride);

    // Base ratings
    rating_tuple ratings;
    switch (ride->mode)
    {
        default:
        case RIDE_MODE_3D_FILM_MOUSE_TAILS:
            ratings.excitement = RIDE_RATING(3, 50);
            ratings.intensity = RIDE_RATING(2, 40);
            ratings.nausea = RIDE_RATING(1, 40);
            break;
        case RIDE_MODE_3D_FILM_STORM_CHASERS:
            ratings.excitement = RIDE_RATING(4, 00);
            ratings.intensity = RIDE_RATING(2, 65);
            ratings.nausea = RIDE_RATING(1, 55);
            break;
        case RIDE_MODE_3D_FILM_SPACE_RAIDERS:
            ratings.excitement = RIDE_RATING(4, 20);
            ratings.intensity = RIDE_RATING(2, 60);
            ratings.nausea = RIDE_RATING(1, 48);
            break;
    }

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths |= 7;
}

static void ride_ratings_calculate_top_spin(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 19;
    set_unreliability_factor(ride);

    // Base ratings
    rating_tuple ratings;
    switch (ride->mode)
    {
        default:
        case RIDE_MODE_BEGINNERS:
            ratings.excitement = RIDE_RATING(2, 00);
            ratings.intensity = RIDE_RATING(4, 80);
            ratings.nausea = RIDE_RATING(5, 74);
            break;
        case RIDE_MODE_INTENSE:
            ratings.excitement = RIDE_RATING(3, 00);
            ratings.intensity = RIDE_RATING(5, 75);
            ratings.nausea = RIDE_RATING(6, 64);
            break;
        case RIDE_MODE_BERSERK:
            ratings.excitement = RIDE_RATING(3, 20);
            ratings.intensity = RIDE_RATING(6, 80);
            ratings.nausea = RIDE_RATING(7, 94);
            break;
    }

    ride_ratings_apply_scenery(&ratings, ride, 11155);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_space_rings(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 7;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 50), RIDE_RATING(2, 10), RIDE_RATING(6, 50));
    ride_ratings_apply_scenery(&ratings, ride, 25098);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_reverse_freefall_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 25;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 00), RIDE_RATING(3, 20), RIDE_RATING(2, 80));
    ride_ratings_apply_length(&ratings, ride, 6000, 327);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 60), RIDE_RATING(0, 15));
    ride_ratings_apply_max_speed(&ratings, ride, 436906, 436906, 320398);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 41704, 59578);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 12850, 28398, 11702);
    ride_ratings_apply_proximity(&ratings, 17893);
    ride_ratings_apply_scenery(&ratings, ride, 11155);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 34, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_lift(Ride* ride)
{
    int32_t totalLength;

    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 15;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 11), RIDE_RATING(0, 35), RIDE_RATING(0, 30));

    totalLength = ride_get_total_length(ride) >> 16;
    ride_ratings_add(&ratings, (totalLength * 45875) >> 16, 0, (totalLength * 26214) >> 16);

    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 83662);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 7;

    if ((get_num_of_sheltered_eighths(ride).TrackShelteredEighths) >= 5)
        ride->excitement /= 4;
}

static void ride_ratings_calculate_vertical_drop_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 20), RIDE_RATING(0, 80), RIDE_RATING(0, 30));
    ride_ratings_apply_length(&ratings, ride, 4000, 1146);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_max_speed(&ratings, ride, 97418, 141699, 70849);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 40960, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 58254, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 20, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 10), 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 1, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 40960, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_cash_machine(Ride* ride)
{
    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
}

static void ride_ratings_calculate_twist(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 13), RIDE_RATING(0, 97), RIDE_RATING(1, 90));
    ride_ratings_apply_rotations(&ratings, ride, 20, 20, 20);
    ride_ratings_apply_scenery(&ratings, ride, 13943);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_haunted_house(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 8;
    set_unreliability_factor(ride);

    rating_tuple ratings = {
        /* .excitement =  */ RIDE_RATING(3, 41),
        /* .intensity =  */ RIDE_RATING(1, 53),
        /* .nausea =  */ RIDE_RATING(0, 10),
    };

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 7;
}

static void ride_ratings_calculate_flying_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 17;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(4, 35), RIDE_RATING(1, 85), RIDE_RATING(4, 33));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 38130, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
        ratings.excitement /= 2;

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 1, 1);

    if (ride->inversions == 0)
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 40), 2, 1, 1);

    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 1, 1);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 38130, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_virginia_reel(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 19;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 10), RIDE_RATING(1, 90), RIDE_RATING(3, 70));
    ride_ratings_apply_length(&ratings, ride, 6000, 873);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 655360);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 110592, 29789, 59578);
    ride_ratings_apply_turns(&ratings, ride, 52012, 26075, 45749);
    ride_ratings_apply_drops(&ratings, ride, 43690, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 22367);
    ride_ratings_apply_scenery(&ratings, ride, 11155);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xD20000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 110592, 29789, 59578);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_splash_boats(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 15;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 46), RIDE_RATING(0, 35), RIDE_RATING(0, 30));
    ride_ratings_apply_length(&ratings, ride, 2000, 7208);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_max_speed(&ratings, ride, 797059, 655360, 301111);
    ride_ratings_apply_duration(&ratings, ride, 500, 13107);
    ride_ratings_apply_turns(&ratings, ride, 22291, 20860, 4574);
    ride_ratings_apply_drops(&ratings, ride, 87381, 93622, 62259);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 22367);
    ride_ratings_apply_scenery(&ratings, ride, 11155);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 6, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_mini_helicopters(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 12;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 60), RIDE_RATING(0, 40), RIDE_RATING(0, 00));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 15), RIDE_RATING(0, 00));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_turns(&ratings, ride, 14860, 0, 4574);
    ride_ratings_apply_drops(&ratings, ride, 8738, 0, 0);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 12850, 6553, 4681);
    ride_ratings_apply_proximity(&ratings, 8946);
    ride_ratings_apply_scenery(&ratings, ride, 8366);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xA00000, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 6;
}

static void ride_ratings_calculate_lay_down_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 18;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 85), RIDE_RATING(1, 15), RIDE_RATING(2, 75));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 38130, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
    {
        ratings.excitement /= 4;
        ratings.intensity /= 2;
        ratings.nausea /= 2;
    }

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
    {
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 40), 2, 2, 2);
        ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);
    }

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 38130, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_suspended_monorail(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 15), RIDE_RATING(0, 23), RIDE_RATING(0, 8));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_train_length(&ratings, ride, 93622);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 70849, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 218453);
    ride_ratings_apply_duration(&ratings, ride, 150, 21845);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 5140, 6553, 18724);
    ride_ratings_apply_proximity(&ratings, 12525);
    ride_ratings_apply_scenery(&ratings, ride, 25098);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xAA0000, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    auto shelteredEighths = get_num_of_sheltered_eighths(ride);
    if (shelteredEighths.TrackShelteredEighths >= 4)
        ride->excitement /= 4;

    ride->sheltered_eighths = shelteredEighths.TotalShelteredEighths;
}

static void ride_ratings_calculate_reverser_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 19;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 40), RIDE_RATING(1, 80), RIDE_RATING(1, 70));
    ride_ratings_apply_length(&ratings, ride, 6000, 873);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 655360);

    int32_t numReversers = std::min<uint16_t>(gRideRatingsCalcData.num_reversers, 6);
    ride_rating reverserRating = numReversers * RIDE_RATING(0, 20);
    ride_ratings_add(&ratings, reverserRating, reverserRating, reverserRating);

    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 28672, 23831, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 43458, 45749);
    ride_ratings_apply_drops(&ratings, ride, 40777, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 22367);
    ride_ratings_apply_scenery(&ratings, ride, 11155);

    if (gRideRatingsCalcData.num_reversers < 1)
    {
        ratings.excitement /= 8;
    }

    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xC80000, 2, 1, 1);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 1, 1);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 28672, 23831, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_heartline_twister_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 18;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 40), RIDE_RATING(1, 70), RIDE_RATING(1, 65));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 20), RIDE_RATING(0, 04));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 97418, 123987, 70849);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 44683, 89367);
    ride_ratings_apply_turns(&ratings, ride, 26749, 52150, 57186);
    ride_ratings_apply_drops(&ratings, ride, 29127, 53052, 55705);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 34952, 35108);
    ride_ratings_apply_proximity(&ratings, 9841);
    ride_ratings_apply_scenery(&ratings, ride, 3904);

    if (ride->inversions == 0)
        ratings.excitement /= 4;

    ride_ratings_apply_num_drops_penalty(&ratings, ride, 1, 4, 1, 1);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 44683, 89367);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_mini_golf(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 0;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 50), RIDE_RATING(0, 90), RIDE_RATING(0, 00));
    ride_ratings_apply_length(&ratings, ride, 6000, 873);
    ride_ratings_apply_turns(&ratings, ride, 14860, 0, 0);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 5140, 6553, 4681);
    ride_ratings_apply_proximity(&ratings, 15657);
    ride_ratings_apply_scenery(&ratings, ride, 27887);

    // Apply golf holes factor
    ride_ratings_add(&ratings, (ride->holes) * 5, 0, 0);

    // Apply no golf holes penalty
    if (ride->holes == 0)
    {
        ratings.excitement /= 8;
        ratings.intensity /= 2;
        ratings.nausea /= 2;
    }

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_first_aid(Ride* ride)
{
    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;
}

static void ride_ratings_calculate_circus_show(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 9;
    set_unreliability_factor(ride);

    rating_tuple ratings = {
        /* .excitement = */ RIDE_RATING(2, 10),
        /* .intensity  = */ RIDE_RATING(0, 30),
        /* .nausea     = */ RIDE_RATING(0, 0),
    };

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 7;
}

static void ride_ratings_calculate_ghost_train(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 12;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 00), RIDE_RATING(0, 20), RIDE_RATING(0, 03));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 15), RIDE_RATING(0, 00));
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_turns(&ratings, ride, 14860, 0, 11437);
    ride_ratings_apply_drops(&ratings, ride, 8738, 0, 0);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 25700, 6553, 4681);
    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 8366);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xB40000, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_twister_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 15;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 50), RIDE_RATING(0, 40), RIDE_RATING(0, 30));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 32768, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
        ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
    {
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 40), 2, 2, 2);
        ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);
    }

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 32768, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_wooden_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 19;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 20), RIDE_RATING(2, 60), RIDE_RATING(2, 00));
    ride_ratings_apply_length(&ratings, ride, 6000, 873);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 655360);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 40960, 34555, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 43458, 45749);
    ride_ratings_apply_drops(&ratings, ride, 40777, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 22367);
    ride_ratings_apply_scenery(&ratings, ride, 11155);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 10), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0x1720000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 40960, 34555, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_side_friction_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 19;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 50), RIDE_RATING(2, 00), RIDE_RATING(1, 50));
    ride_ratings_apply_length(&ratings, ride, 6000, 873);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 655360);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 28672, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 43458, 45749);
    ride_ratings_apply_drops(&ratings, ride, 40777, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 22367);
    ride_ratings_apply_scenery(&ratings, ride, 11155);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 6, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x50000, 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xFA0000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 28672, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_wild_mouse(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 80), RIDE_RATING(2, 50), RIDE_RATING(2, 10));
    ride_ratings_apply_length(&ratings, ride, 6000, 873);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 8));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 655360);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 102400, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 29721, 43458, 45749);
    ride_ratings_apply_drops(&ratings, ride, 40777, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 17893);
    ride_ratings_apply_scenery(&ratings, ride, 5577);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 6, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x70000, 2, 2, 2);
    ride_ratings_apply_max_lateral_g_penalty(&ratings, ride, FIXED_2DP(1, 50), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xAA0000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 102400, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_multi_dimension_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 18;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 75), RIDE_RATING(1, 95), RIDE_RATING(4, 79));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 38130, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
        ratings.excitement /= 4;

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 1, 1);
    if (ride->inversions == 0)
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 40), 2, 1, 1);

    if (ride->inversions == 0)
        ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 1, 1);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 38130, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_giga_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 85), RIDE_RATING(0, 40), RIDE_RATING(0, 35));
    ride_ratings_apply_length(&ratings, ride, 6000, 819);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 140434);
    ride_ratings_apply_max_speed(&ratings, ride, 51366, 85019, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 400497);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 36864, 30384, 49648);
    ride_ratings_apply_turns(&ratings, ride, 28235, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 43690, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
        ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 16, 2, 2, 2);

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
    {
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 40), 2, 2, 2);
        ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);
    }

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 36864, 30384, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_roto_drop(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 24;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 80), RIDE_RATING(3, 50), RIDE_RATING(3, 50));

    int32_t lengthFactor = ((ride_get_total_length(ride) >> 16) * 209715) >> 16;
    ride_ratings_add(&ratings, lengthFactor, lengthFactor * 2, lengthFactor * 2);

    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 25098);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_flying_saucers(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 32;
    set_unreliability_factor(ride);

    rating_tuple ratings = {
        /* .excitement = */ RIDE_RATING(2, 40),
        /* .intensity =  */ RIDE_RATING(0, 55),
        /* .nausea =     */ RIDE_RATING(0, 39),
    };

    if (ride->num_vehicles >= 4)
    {
        ride_ratings_add(&ratings, RIDE_RATING(0, 40), 0, 0);
    }

    ride_ratings_add(&ratings, ride->time_limit, ride->time_limit / 2, 0);

    if (ride->num_vehicles >= 4)
    {
        ride_ratings_add(&ratings, RIDE_RATING(0, 40), 0, 0);
    }

    ride_ratings_apply_scenery(&ratings, ride, 5577);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_crooked_house(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 5;
    set_unreliability_factor(ride);

    rating_tuple ratings = {
        /* .excitement = */ RIDE_RATING(2, 15),
        /* .intensity  = */ RIDE_RATING(0, 62),
        /* .nausea     = */ RIDE_RATING(0, 34),
    };

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 7;
}

static void ride_ratings_calculate_monorail_cycles(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 4;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 40), RIDE_RATING(0, 20), RIDE_RATING(0, 00));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 15), RIDE_RATING(0, 00));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_turns(&ratings, ride, 14860, 0, 4574);
    ride_ratings_apply_drops(&ratings, ride, 8738, 0, 0);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 5140, 6553, 2340);
    ride_ratings_apply_proximity(&ratings, 8946);
    ride_ratings_apply_scenery(&ratings, ride, 11155);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0x8C0000, 2, 2, 2);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_compact_inverted_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = ride->mode == RIDE_MODE_REVERSE_INCLINE_LAUNCHED_SHUTTLE ? 31 : 21;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 15), RIDE_RATING(2, 80), RIDE_RATING(3, 20));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 42), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 30980, 55606);
    ride_ratings_apply_turns(&ratings, ride, 26749, 29552, 57186);
    ride_ratings_apply_drops(&ratings, ride, 29127, 39009, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 15291, 35108);
    ride_ratings_apply_proximity(&ratings, 15657);
    ride_ratings_apply_scenery(&ratings, ride, 8366);

    if (ride->inversions == 0)
        ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 30), 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 30980, 55606);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_water_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 70), RIDE_RATING(2, 80), RIDE_RATING(2, 10));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 20480, 23831, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 25700, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 9760);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 8, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x70000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 1, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 20480, 23831, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    if (!(ride->special_track_elements & RIDE_ELEMENT_TUNNEL_SPLASH_OR_RAPIDS))
        ratings.excitement /= 8;

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_air_powered_vertical_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 28;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(4, 13), RIDE_RATING(2, 50), RIDE_RATING(2, 80));
    ride_ratings_apply_length(&ratings, ride, 6000, 327);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 60), RIDE_RATING(0, 05));
    ride_ratings_apply_max_speed(&ratings, ride, 509724, 364088, 320398);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 35746, 59578);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 21845, 11702);
    ride_ratings_apply_proximity(&ratings, 17893);
    ride_ratings_apply_scenery(&ratings, ride, 11155);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 34, 2, 1, 1);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 35746, 59578);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_inverted_hairpin_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 14;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(3, 00), RIDE_RATING(2, 65), RIDE_RATING(2, 25));
    ride_ratings_apply_length(&ratings, ride, 6000, 873);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 8));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 364088, 655360);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 102400, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 29721, 43458, 45749);
    ride_ratings_apply_drops(&ratings, ride, 40777, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 16705, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 17893);
    ride_ratings_apply_scenery(&ratings, ride, 5577);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 8, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x70000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 10), 2, 2, 2);
    ride_ratings_apply_max_lateral_g_penalty(&ratings, ride, FIXED_2DP(1, 50), 2, 2, 2);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0xAA0000, 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 3, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 102400, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_magic_carpet(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 45), RIDE_RATING(1, 60), RIDE_RATING(2, 60));

    ride_ratings_add(&ratings, ride->operation_option * 10, ride->operation_option * 20, ride->operation_option * 20);

    ride_ratings_apply_scenery(&ratings, ride, 11155);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 0;
}

static void ride_ratings_calculate_submarine_ride(Ride* ride)
{
    ride->unreliability_factor = 7;
    set_unreliability_factor(ride);

    // NOTE Fixed bug from original game, see boat Hire.

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 20), RIDE_RATING(1, 80), RIDE_RATING(1, 40));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_proximity(&ratings, 11183);
    ride_ratings_apply_scenery(&ratings, ride, 22310);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    // Originally, this was always to zero, even though the default vehicle is completely enclosed.
    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_river_rafts(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 12;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(1, 45), RIDE_RATING(0, 25), RIDE_RATING(0, 34));
    ride_ratings_apply_length(&ratings, ride, 2000, 7208);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_max_speed(&ratings, ride, 531372, 655360, 301111);
    ride_ratings_apply_duration(&ratings, ride, 500, 13107);
    ride_ratings_apply_turns(&ratings, ride, 22291, 20860, 4574);
    ride_ratings_apply_drops(&ratings, ride, 78643, 93622, 62259);
    ride_ratings_apply_proximity(&ratings, 13420);
    ride_ratings_apply_scenery(&ratings, ride, 11155);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_enterprise(Ride* ride)
{
    ride->lifecycle_flags |= RIDE_LIFECYCLE_TESTED;
    ride->lifecycle_flags |= RIDE_LIFECYCLE_NO_RAW_STATS;
    ride->unreliability_factor = 22;
    set_unreliability_factor(ride);

    // Base ratings
    rating_tuple ratings = {
        /* .excitement = */ RIDE_RATING(3, 60),
        /* .intensity  = */ RIDE_RATING(4, 55),
        /* .nausea     = */ RIDE_RATING(5, 72),
    };

    ride_ratings_add(&ratings, ride->operation_option, ride->operation_option * 16, ride->operation_option * 16);

    ride_ratings_apply_scenery(&ratings, ride, 19521);

    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = 3;
}

static void ride_ratings_calculate_inverted_impulse_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 20;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(4, 00), RIDE_RATING(3, 00), RIDE_RATING(3, 20));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 42), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 29789, 55606);
    ride_ratings_apply_turns(&ratings, ride, 26749, 29552, 57186);
    ride_ratings_apply_drops(&ratings, ride, 29127, 39009, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 15291, 35108);
    ride_ratings_apply_proximity(&ratings, 15657);
    ride_ratings_apply_scenery(&ratings, ride, 9760);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 20, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 29789, 55606);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_mini_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 13;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 55), RIDE_RATING(2, 40), RIDE_RATING(1, 85));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 20480, 23831, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 25700, 30583, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 9760);
    ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 12, 2, 2, 2);
    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0x70000, 2, 2, 2);
    ride_ratings_apply_max_negative_g_penalty(&ratings, ride, FIXED_2DP(0, 50), 2, 2, 2);
    ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 20480, 23831, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_mine_ride(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 16;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 75), RIDE_RATING(1, 00), RIDE_RATING(1, 80));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 40960, 29789, 49648);
    ride_ratings_apply_turns(&ratings, ride, 29721, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 19275, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 21472);
    ride_ratings_apply_scenery(&ratings, ride, 16732);
    ride_ratings_apply_first_length_penalty(&ratings, ride, 0x10E0000, 2, 2, 2);

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 40960, 29789, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

static void ride_ratings_calculate_lim_launched_roller_coaster(Ride* ride)
{
    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_TESTED))
        return;

    ride->unreliability_factor = 25;
    set_unreliability_factor(ride);

    rating_tuple ratings;
    ride_ratings_set(&ratings, RIDE_RATING(2, 90), RIDE_RATING(1, 50), RIDE_RATING(2, 20));
    ride_ratings_apply_length(&ratings, ride, 6000, 764);
    ride_ratings_apply_synchronisation(&ratings, ride, RIDE_RATING(0, 40), RIDE_RATING(0, 05));
    ride_ratings_apply_train_length(&ratings, ride, 187245);
    ride_ratings_apply_max_speed(&ratings, ride, 44281, 88562, 35424);
    ride_ratings_apply_average_speed(&ratings, ride, 291271, 436906);
    ride_ratings_apply_duration(&ratings, ride, 150, 26214);
    ride_ratings_apply_gforces(&ratings, ride, 24576, 35746, 49648);
    ride_ratings_apply_turns(&ratings, ride, 26749, 34767, 45749);
    ride_ratings_apply_drops(&ratings, ride, 29127, 46811, 49152);
    ride_ratings_apply_sheltered_ratings(&ratings, ride, 15420, 32768, 35108);
    ride_ratings_apply_proximity(&ratings, 20130);
    ride_ratings_apply_scenery(&ratings, ride, 6693);

    if (ride->inversions == 0)
        ride_ratings_apply_highest_drop_height_penalty(&ratings, ride, 10, 2, 2, 2);

    ride_ratings_apply_max_speed_penalty(&ratings, ride, 0xA0000, 2, 2, 2);

    if (ride->inversions == 0)
    {
        ride_ratings_apply_max_negative_g_penalty(&ratings, ride, 10, 2, 2, 2);
        ride_ratings_apply_num_drops_penalty(&ratings, ride, 2, 2, 2, 2);
    }

    ride_ratings_apply_excessive_lateral_g_penalty(&ratings, ride, 24576, 35746, 49648);
    ride_ratings_apply_intensity_penalty(&ratings);
    ride_ratings_apply_adjustments(ride, &ratings);

    ride->ratings = ratings;

    ride->upkeep_cost = ride_compute_upkeep(ride);
    ride->window_invalidate_flags |= RIDE_INVALIDATE_RIDE_INCOME;

    ride->sheltered_eighths = get_num_of_sheltered_eighths(ride).TotalShelteredEighths;
}

#pragma endregion

#pragma region Ride rating calculation function table

// rct2: 0x0097E050
static const ride_ratings_calculation RideRatingsCalculateFuncTable[RIDE_TYPE_COUNT] = {
    ride_ratings_calculate_spiral_roller_coaster,          // SPIRAL_ROLLER_COASTER
    ride_ratings_calculate_stand_up_roller_coaster,        // STAND_UP_ROLLER_COASTER
    ride_ratings_calculate_suspended_swinging_coaster,     // SUSPENDED_SWINGING_COASTER
    ride_ratings_calculate_inverted_roller_coaster,        // INVERTED_ROLLER_COASTER
    ride_ratings_calculate_junior_roller_coaster,          // JUNIOR_ROLLER_COASTER
    ride_ratings_calculate_miniature_railway,              // MINIATURE_RAILWAY
    ride_ratings_calculate_monorail,                       // MONORAIL
    ride_ratings_calculate_mini_suspended_coaster,         // MINI_SUSPENDED_COASTER
    ride_ratings_calculate_boat_hire,                      // BOAT_HIRE
    ride_ratings_calculate_wooden_wild_mouse,              // WOODEN_WILD_MOUSE
    ride_ratings_calculate_steeplechase,                   // STEEPLECHASE
    ride_ratings_calculate_car_ride,                       // CAR_RIDE
    ride_ratings_calculate_launched_freefall,              // LAUNCHED_FREEFALL
    ride_ratings_calculate_bobsleigh_coaster,              // BOBSLEIGH_COASTER
    ride_ratings_calculate_observation_tower,              // OBSERVATION_TOWER
    ride_ratings_calculate_looping_roller_coaster,         // LOOPING_ROLLER_COASTER
    ride_ratings_calculate_dinghy_slide,                   // DINGHY_SLIDE
    ride_ratings_calculate_mine_train_coaster,             // MINE_TRAIN_COASTER
    ride_ratings_calculate_chairlift,                      // CHAIRLIFT
    ride_ratings_calculate_corkscrew_roller_coaster,       // CORKSCREW_ROLLER_COASTER
    ride_ratings_calculate_maze,                           // MAZE
    ride_ratings_calculate_spiral_slide,                   // SPIRAL_SLIDE
    ride_ratings_calculate_go_karts,                       // GO_KARTS
    ride_ratings_calculate_log_flume,                      // LOG_FLUME
    ride_ratings_calculate_river_rapids,                   // RIVER_RAPIDS
    ride_ratings_calculate_dodgems,                        // DODGEMS
    ride_ratings_calculate_pirate_ship,                    // PIRATE_SHIP
    ride_ratings_calculate_inverter_ship,                  // SWINGING_INVERTER_SHIP
    ride_ratings_calculate_food_stall,                     // FOOD_STALL
    ride_ratings_calculate_food_stall,                     // 1D
    ride_ratings_calculate_drink_stall,                    // DRINK_STALL
    ride_ratings_calculate_drink_stall,                    // 1F
    ride_ratings_calculate_shop,                           // SHOP
    ride_ratings_calculate_merry_go_round,                 // MERRY_GO_ROUND
    ride_ratings_calculate_shop,                           // 22
    ride_ratings_calculate_information_kiosk,              // INFORMATION_KIOSK
    ride_ratings_calculate_toilets,                        // TOILETS
    ride_ratings_calculate_ferris_wheel,                   // FERRIS_WHEEL
    ride_ratings_calculate_motion_simulator,               // MOTION_SIMULATOR
    ride_ratings_calculate_3d_cinema,                      // 3D_CINEMA
    ride_ratings_calculate_top_spin,                       // TOP_SPIN
    ride_ratings_calculate_space_rings,                    // SPACE_RINGS
    ride_ratings_calculate_reverse_freefall_coaster,       // REVERSE_FREEFALL_COASTER
    ride_ratings_calculate_lift,                           // LIFT
    ride_ratings_calculate_vertical_drop_roller_coaster,   // VERTICAL_DROP_ROLLER_COASTER
    ride_ratings_calculate_cash_machine,                   // CASH_MACHINE
    ride_ratings_calculate_twist,                          // TWIST
    ride_ratings_calculate_haunted_house,                  // HAUNTED_HOUSE
    ride_ratings_calculate_first_aid,                      // FIRST_AID
    ride_ratings_calculate_circus_show,                    // CIRCUS_SHOW
    ride_ratings_calculate_ghost_train,                    // GHOST_TRAIN
    ride_ratings_calculate_twister_roller_coaster,         // TWISTER_ROLLER_COASTER
    ride_ratings_calculate_wooden_roller_coaster,          // WOODEN_ROLLER_COASTER
    ride_ratings_calculate_side_friction_roller_coaster,   // SIDE_FRICTION_ROLLER_COASTER
    ride_ratings_calculate_wild_mouse,                     // WILD_MOUSE
    ride_ratings_calculate_multi_dimension_roller_coaster, // MULTI_DIMENSION_ROLLER_COASTER
    ride_ratings_calculate_multi_dimension_roller_coaster, // 38
    ride_ratings_calculate_flying_roller_coaster,          // FLYING_ROLLER_COASTER
    ride_ratings_calculate_flying_roller_coaster,          // 3A
    ride_ratings_calculate_virginia_reel,                  // VIRGINIA_REEL
    ride_ratings_calculate_splash_boats,                   // SPLASH_BOATS
    ride_ratings_calculate_mini_helicopters,               // MINI_HELICOPTERS
    ride_ratings_calculate_lay_down_roller_coaster,        // LAY_DOWN_ROLLER_COASTER
    ride_ratings_calculate_suspended_monorail,             // SUSPENDED_MONORAIL
    ride_ratings_calculate_lay_down_roller_coaster,        // 40
    ride_ratings_calculate_reverser_roller_coaster,        // REVERSER_ROLLER_COASTER
    ride_ratings_calculate_heartline_twister_coaster,      // HEARTLINE_TWISTER_COASTER
    ride_ratings_calculate_mini_golf,                      // MINI_GOLF
    ride_ratings_calculate_giga_coaster,                   // GIGA_COASTER
    ride_ratings_calculate_roto_drop,                      // ROTO_DROP
    ride_ratings_calculate_flying_saucers,                 // FLYING_SAUCERS
    ride_ratings_calculate_crooked_house,                  // CROOKED_HOUSE
    ride_ratings_calculate_monorail_cycles,                // MONORAIL_CYCLES
    ride_ratings_calculate_compact_inverted_coaster,       // COMPACT_INVERTED_COASTER
    ride_ratings_calculate_water_coaster,                  // WATER_COASTER
    ride_ratings_calculate_air_powered_vertical_coaster,   // AIR_POWERED_VERTICAL_COASTER
    ride_ratings_calculate_inverted_hairpin_coaster,       // INVERTED_HAIRPIN_COASTER
    ride_ratings_calculate_magic_carpet,                   // MAGIC_CARPET
    ride_ratings_calculate_submarine_ride,                 // SUBMARINE_RIDE
    ride_ratings_calculate_river_rafts,                    // RIVER_RAFTS
    nullptr,                                               // 50
    ride_ratings_calculate_enterprise,                     // ENTERPRISE
    nullptr,                                               // 52
    nullptr,                                               // 53
    nullptr,                                               // 54
    nullptr,                                               // 55
    ride_ratings_calculate_inverted_impulse_coaster,       // INVERTED_IMPULSE_COASTER
    ride_ratings_calculate_mini_roller_coaster,            // MINI_ROLLER_COASTER
    ride_ratings_calculate_mine_ride,                      // MINE_RIDE
    nullptr,                                               // 59
    ride_ratings_calculate_lim_launched_roller_coaster,    // LIM_LAUNCHED_ROLLER_COASTER
};

static ride_ratings_calculation ride_ratings_get_calculate_func(uint8_t rideType)
{
    return RideRatingsCalculateFuncTable[rideType];
}

#pragma endregion
