/* Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-License-Identifier: GPL-2.0-or-later
 * Default normal-window geometry. User-saved geometry takes precedence;
 * call this only when no valid saved geometry can be restored.
 */
#ifndef HAOKE_WINDOW_POLICY_HPP
#define HAOKE_WINDOW_POLICY_HPP

#include <climits>
#include <cmath>
#include <cstdint>

struct HaokeWindowGeometry
{
    int x;
    int y;
    int width;
    int height;
};

struct HaokeWindowSize
{
    int width;
    int height;
};

/* Fit a display aspect ratio, not the video's native pixel size. The current
 * outer-window area is the user's size budget; it is not a new minimum.
 * chrome_* includes all non-video space, including non-client decorations
 * when work_* describes the space available to the complete outer window.
 *
 * This policy never crops/stretches the video. A portrait video may cover less
 * than 60% of a landscape work area because the maximum aspect-correct window
 * is height-limited. The caller owns normal/maximized/fullscreen state,
 * positioning and any application-specific minimum-control constraints.
 * Invalid/infeasible chrome inputs return zero so the caller can skip resize.
 */
inline HaokeWindowSize haoke_fit_video_window(
    int work_width, int work_height, int current_width, int current_height,
    int chrome_width, int chrome_height, int video_width, int video_height)
{
    if (work_width <= 0 || work_height <= 0
        || current_width <= 0 || current_height <= 0
        || chrome_width < 0 || chrome_height < 0
        || chrome_width >= work_width || chrome_height >= work_height
        || video_width <= 0 || video_height <= 0)
        return HaokeWindowSize{0, 0};

    const int max_video_width = work_width - chrome_width;
    const int max_video_height = work_height - chrome_height;
    const long double ratio = static_cast<long double>(video_width)
        / video_height;
    if (current_width <= work_width && current_height <= work_height
        && current_width > chrome_width && current_height > chrome_height)
    {
        const int viewport_width = current_width - chrome_width;
        const int viewport_height = current_height - chrome_height;
        const long double cross_error = std::fabs(
            static_cast<long double>(viewport_width) * video_height
            - static_cast<long double>(viewport_height) * video_width);
        const int longest = video_width > video_height
            ? video_width : video_height;
        const bool subpixel_short_edge = ratio >= 1
            ? viewport_height == 1 && viewport_width / ratio < 1
            : viewport_width == 1 && viewport_height * ratio < 1;
        // Keep an already nearest-pixel fit. Re-solving the rounded area can
        // otherwise walk a pixel on every unchanged core-size callback.
        if (cross_error <= 0.5L * longest || subpixel_short_edge)
            return HaokeWindowSize{current_width, current_height};
    }
    const long double budget = static_cast<long double>(current_width)
        * current_height;
    long double low = 0;
    long double high = max_video_height;
    if (high * ratio > max_video_width)
        high = max_video_width / ratio;

    // Monotonic outer area; bounded search avoids overflow/cancellation in a
    // quadratic formula at extreme video ratios or int-sized work areas.
    for (unsigned int i = 0; i < 80; ++i)
    {
        const long double height = (low + high) / 2;
        const long double area = (height * ratio + chrome_width)
            * (height + chrome_height);
        if (area < budget)
            low = height;
        else
            high = height;
    }

    const long double video_height_fit = (low + high) / 2;
    // Round the longer video dimension first. The shorter dimension is then
    // nearest-pixel aspect-correct, without a feedback loop between resizes.
    int width;
    int height;
    if (ratio >= 1)
    {
        const long double value = std::floor(video_height_fit * ratio + 0.5L);
        width = value < 1 ? 1 : value > max_video_width
            ? max_video_width : static_cast<int>(value);
        const long double other = std::floor(width / ratio + 0.5L);
        height = other < 1 ? 1 : other > max_video_height
            ? max_video_height : static_cast<int>(other);
    }
    else
    {
        const long double value = std::floor(video_height_fit + 0.5L);
        height = value < 1 ? 1 : value > max_video_height
            ? max_video_height : static_cast<int>(value);
        const long double other = std::floor(height * ratio + 0.5L);
        width = other < 1 ? 1 : other > max_video_width
            ? max_video_width : static_cast<int>(other);
    }
    return HaokeWindowSize{width + chrome_width, height + chrome_height};
}

inline HaokeWindowGeometry haoke_default_window_geometry(
    int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0)
        return HaokeWindowGeometry{x, y, 0, 0};

    const std::int64_t available_width = width;
    const std::int64_t available_height = height;
    // n - floor(n / 5) is ceil(4*n / 5), without multiplying large inputs.
    std::int64_t window_width = available_width - available_width / 5;
    std::int64_t window_height = available_height - available_height / 5;
    if (window_width < 600)
        window_width = 600;
    if (window_height < 420)
        window_height = 420;
    // Small displays take priority over the preferred minimum dimensions.
    if (window_width > available_width)
        window_width = available_width;
    if (window_height > available_height)
        window_height = available_height;

    const std::int64_t centered_x = std::int64_t(x)
        + (available_width - window_width) / 2;
    const std::int64_t centered_y = std::int64_t(y)
        + (available_height - window_height) / 2;
    // A synthetic work area may extend beyond the int coordinate domain.
    // Saturate an unrepresentable center; ordinary desktop centers are exact.
    const int result_x = centered_x > INT_MAX ? INT_MAX
        : centered_x < INT_MIN ? INT_MIN : static_cast<int>(centered_x);
    const int result_y = centered_y > INT_MAX ? INT_MAX
        : centered_y < INT_MIN ? INT_MIN : static_cast<int>(centered_y);
    return HaokeWindowGeometry{result_x, result_y,
        static_cast<int>(window_width), static_cast<int>(window_height)};
}

#endif // HAOKE_WINDOW_POLICY_HPP
