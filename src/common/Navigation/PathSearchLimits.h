/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * Released under GNU GPL v2 license, or (at your option) any later version.
 */

#ifndef ACORE_PATH_SEARCH_LIMITS_H
#define ACORE_PATH_SEARCH_LIMITS_H

namespace Movement
{
// Opt-in limits for checking a complete long-distance route. Ordinary movement
// keeps its shared query and existing polygon/point limits.
inline constexpr int LongPathSearchNodes = 16384;
inline constexpr int LongPathSearchPolygons = 1024;
}

#endif
