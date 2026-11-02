/* Compile-only regression: VLC checked-delay macro must survive Qt includes. */
constexpr int check_delay(int delay) { return delay + 7; }
constexpr int msleep(int delay) { return delay; }
#define msleep(d) msleep(check_delay(d))
#include "../update_controller.hpp"
#ifndef msleep
#error VLC msleep macro was lost by the updater header
#endif
static_assert(msleep(5) == 12, "VLC checked-delay macro must be restored exactly");
