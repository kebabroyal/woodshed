#ifndef WOODSHED_TUNING_HPP
#define WOODSHED_TUNING_HPP

#include "woodshed/audio_take.hpp"

namespace woodshed
{

// Offset of the take's tuning from A440 equal temperament, in cents (-50 to 50).
// 0 when peaks are too few or disagree. About 0.15 s per 5 minutes.
double detect_tuning(const audio_take &take);

}

#endif
