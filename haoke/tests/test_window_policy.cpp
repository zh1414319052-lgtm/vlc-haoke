/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../window_policy.hpp"

#include <cstdlib>
#include <iostream>

static unsigned int checks;
#define CHECK(condition) do {                                                \
    ++checks;                                                               \
    if (!(condition)) {                                                     \
        std::cerr << "FAIL line " << __LINE__ << ": " << #condition << '\n';    \
        std::exit(1);                                                       \
    }                                                                       \
} while (0)

static void check_valid_area(const HaokeWindowGeometry &area)
{
    const HaokeWindowGeometry window = haoke_default_window_geometry(
        area.x, area.y, area.width, area.height);
    CHECK(window.width > 0 && window.height > 0);
    CHECK(window.width <= area.width && window.height <= area.height);
    CHECK(std::int64_t(window.x) >= area.x);
    CHECK(std::int64_t(window.y) >= area.y);
    CHECK(std::int64_t(window.x) + window.width
          <= std::int64_t(area.x) + area.width);
    CHECK(std::int64_t(window.y) + window.height
          <= std::int64_t(area.y) + area.height);
    // Check the user-visible coverage contract, not the implementation formula.
    CHECK(std::int64_t(window.width) * 5 >= std::int64_t(area.width) * 3);
    CHECK(std::int64_t(window.height) * 5 >= std::int64_t(area.height) * 3);
    CHECK(static_cast<long double>(window.width) * window.height
          >= 0.6L * area.width * area.height);

    const std::int64_t left = std::int64_t(window.x) - area.x;
    const std::int64_t right = std::int64_t(area.x) + area.width
        - window.x - window.width;
    const std::int64_t top = std::int64_t(window.y) - area.y;
    const std::int64_t bottom = std::int64_t(area.y) + area.height
        - window.y - window.height;
    // At an unrepresentable synthetic center, the int coordinate saturates.
    if (window.x != INT_MAX)
        CHECK(left == right || left + 1 == right);
    if (window.y != INT_MAX)
        CHECK(top == bottom || top + 1 == bottom);
}

static void test_coverage_and_bounds(void)
{
    static const HaokeWindowGeometry areas[] = {
        {0, 0, 1920, 1040}, {0, 40, 2560, 1400},
        {-1920, 0, 1920, 1080}, {2560, -1080, 1080, 1920},
        {-1080, -1920, 1080, 1920}, {0, 0, 3840, 2080},
        {0, 0, 7680, 4280}, {17, 39, 1921, 1081},
        {-23, -31, 1365, 767}, {0, 0, 601, 421},
        {-500, 80, 600, 420}, {5, 6, 320, 240},
        {INT_MIN, INT_MIN, 1, 1}, {INT_MAX, INT_MAX, 1, 1},
        {INT_MIN, INT_MIN, INT_MAX, INT_MAX},
        {0, 0, INT_MAX, INT_MAX},
        {INT_MAX, INT_MAX, INT_MAX, INT_MAX},
        {INT_MAX - 1, INT_MIN, INT_MAX, 769},
        {-1234567890, 1234567890, INT_MAX, 987654321}
    };
    for (const HaokeWindowGeometry &area : areas)
        check_valid_area(area);
    // Very small/odd work areas must remain usable and bounded independently
    // of the preferred 600x420 size.
    for (int width = 1; width <= 9; ++width)
        for (int height = 1; height <= 9; ++height)
            check_valid_area(HaokeWindowGeometry{-7, -11, width, height});
}

static void test_representative_default_sizes(void)
{
    const HaokeWindowGeometry landscape = haoke_default_window_geometry(
        0, 40, 1920, 1040);
    CHECK(landscape.width == 1536 && landscape.height == 832);
    CHECK(landscape.x == 192 && landscape.y == 144);
    const HaokeWindowGeometry portrait = haoke_default_window_geometry(
        -1080, -1920, 1080, 1920);
    CHECK(portrait.width == 864 && portrait.height == 1536);
    CHECK(portrait.x == -972 && portrait.y == -1728);
    const HaokeWindowGeometry odd = haoke_default_window_geometry(
        17, 39, 1921, 1081);
    CHECK(odd.width == 1537 && odd.height == 865);
    CHECK(odd.x == 209 && odd.y == 147);
    const HaokeWindowGeometry small = haoke_default_window_geometry(
        -320, 80, 320, 240);
    CHECK(small.width == 320 && small.height == 240);
    CHECK(small.x == -320 && small.y == 80);
    const HaokeWindowGeometry near_minimum = haoke_default_window_geometry(
        5, 7, 601, 421);
    CHECK(near_minimum.width == 600 && near_minimum.height == 420);
    CHECK(near_minimum.x == 5 && near_minimum.y == 7);
}

static void test_invalid_work_areas(void)
{
    static const HaokeWindowGeometry invalid[] = {
        {0, 0, 0, 0}, {0, 0, 0, 1080}, {0, 0, 1920, 0},
        {-100, 25, -1, 1080}, {25, -100, 1920, -1},
        {INT_MAX, INT_MIN, INT_MIN, INT_MAX},
        {INT_MIN, INT_MAX, INT_MAX, INT_MIN}
    };
    for (const HaokeWindowGeometry &area : invalid)
    {
        const HaokeWindowGeometry window = haoke_default_window_geometry(
            area.x, area.y, area.width, area.height);
        CHECK(window.width == 0 && window.height == 0);
    }
}

static void check_video_fit(int work_width, int work_height,
                            int budget_width, int budget_height,
                            int chrome_width, int chrome_height,
                            int video_width, int video_height)
{
    const HaokeWindowSize fit = haoke_fit_video_window(
        work_width, work_height, budget_width, budget_height,
        chrome_width, chrome_height, video_width, video_height);
    CHECK(fit.width > chrome_width && fit.height > chrome_height);
    CHECK(fit.width <= work_width && fit.height <= work_height);
    const int viewport_width = fit.width - chrome_width;
    const int viewport_height = fit.height - chrome_height;
    const long double cross_error = std::fabs(
        static_cast<long double>(viewport_width) * video_height
        - static_cast<long double>(viewport_height) * video_width);
    const int longest_video_dimension = video_width > video_height
        ? video_width : video_height;
    // One pixel in the shorter dimension is the unavoidable accuracy limit
    // when an extreme aspect ratio would require a subpixel viewport.
    CHECK(cross_error <= longest_video_dimension);
    const long double budget = static_cast<long double>(budget_width)
        * budget_height;
    const long double area = static_cast<long double>(fit.width) * fit.height;
    const long double rounding_margin = 2.0L * (work_width
        + static_cast<long double>(work_height)) + 1;
    CHECK(std::fabs(area - budget) <= rounding_margin
          || viewport_width >= work_width - chrome_width - 1
          || viewport_height >= work_height - chrome_height - 1);

    // Repeated size callbacks for an unchanged video must not jitter or grow.
    const HaokeWindowSize again = haoke_fit_video_window(
        work_width, work_height, fit.width, fit.height,
        chrome_width, chrome_height, video_width, video_height);
    if (again.width != fit.width || again.height != fit.height)
        std::cerr << "Unstable fit: work=" << work_width << 'x' << work_height
            << " chrome=" << chrome_width << 'x' << chrome_height
            << " video=" << video_width << 'x' << video_height
            << " first=" << fit.width << 'x' << fit.height
            << " second=" << again.width << 'x' << again.height << '\n';
    CHECK(again.width == fit.width && again.height == fit.height);
}

static void test_video_aspect_and_size_budget(void)
{
    const HaokeWindowSize small_pixels = haoke_fit_video_window(
        2560, 1330, 2048, 1095, 0, 88, 640, 360);
    const HaokeWindowSize big_pixels = haoke_fit_video_window(
        2560, 1330, 2048, 1095, 0, 88, 3840, 2160);
    CHECK(small_pixels.width == big_pixels.width);
    CHECK(small_pixels.height == big_pixels.height);
    CHECK(small_pixels.width > 1800 && small_pixels.height > 1000);
    CHECK(std::fabs(static_cast<long double>(small_pixels.width)
                    * small_pixels.height - 2048.0L * 1095) < 2500);

    const HaokeWindowSize manually_small = haoke_fit_video_window(
        2560, 1330, 800, 600, 0, 88, 640, 360);
    CHECK(manually_small.width < small_pixels.width);
    CHECK(manually_small.height < small_pixels.height);
    CHECK(std::fabs(static_cast<long double>(manually_small.width)
                    * manually_small.height - 800.0L * 600) < 1500);

    const HaokeWindowSize portrait = haoke_fit_video_window(
        2560, 1330, 2048, 1095, 0, 88, 720, 1280);
    CHECK(portrait.height == 1330);
    CHECK(portrait.width > 690 && portrait.width < 710);
    CHECK(static_cast<long double>(portrait.width) * portrait.height
          < 0.6L * 2560 * 1330);
    // Returning to landscape with the retained budget restores the original
    // large footprint, rather than inheriting the height-limited portrait area.
    const HaokeWindowSize landscape_again = haoke_fit_video_window(
        2560, 1330, 2048, 1095, 0, 88, 1920, 1080);
    CHECK(landscape_again.width == big_pixels.width);
    CHECK(landscape_again.height == big_pixels.height);

    static const int work[][2] = {
        {2560, 1330}, {1920, 1040}, {1365, 767}, {1080, 1880},
        {320, 240}, {63, 51}, {1, 1}, {INT_MAX, INT_MAX},
        {INT_MAX, 420}, {600, INT_MAX}
    };
    // Ratios represent display geometry, including SAR/rotation corrected by
    // the VLC core. They deliberately include 4:3, ultrawide and portrait.
    static const int videos[][2] = {
        {640, 360}, {3840, 2160}, {4, 3}, {3, 4}, {720, 1280},
        {239, 100}, {1, 1}, {INT_MAX, 1}, {1, INT_MAX},
        {INT_MAX, INT_MAX}, {720, 576}, {1024, 576}
    };
    for (const auto &area : work)
        for (const auto &video : videos)
        {
            check_video_fit(area[0], area[1], area[0], area[1],
                            0, 0, video[0], video[1]);
            check_video_fit(area[0], area[1], area[0] / 2 + 1,
                            area[1] / 2 + 1, 0, 0, video[0], video[1]);
            if (area[0] > 16 && area[1] > 88)
                check_video_fit(area[0], area[1], area[0] / 2 + 1,
                                area[1] / 2 + 1, 16, 88,
                                video[0], video[1]);
        }
}

static void test_video_invalid_inputs(void)
{
    static const int cases[][8] = {
        {0, 1080, 800, 600, 0, 88, 1920, 1080},
        {1920, 0, 800, 600, 0, 88, 1920, 1080},
        {-1, 1080, 800, 600, 0, 88, 1920, 1080},
        {1920, 1080, 0, 600, 0, 88, 1920, 1080},
        {1920, 1080, 800, -1, 0, 88, 1920, 1080},
        {1920, 1080, 800, 600, -1, 88, 1920, 1080},
        {1920, 1080, 800, 600, 0, -1, 1920, 1080},
        {1920, 1080, 800, 600, 1920, 88, 1920, 1080},
        {1920, 1080, 800, 600, 0, 1080, 1920, 1080},
        {1920, 1080, 800, 600, INT_MAX, 88, 1920, 1080},
        {1920, 1080, 800, 600, 0, 88, 0, 1080},
        {1920, 1080, 800, 600, 0, 88, 1920, INT_MIN}
    };
    for (const auto &value : cases)
    {
        const HaokeWindowSize fit = haoke_fit_video_window(
            value[0], value[1], value[2], value[3],
            value[4], value[5], value[6], value[7]);
        CHECK(fit.width == 0 && fit.height == 0);
    }
}

int main(void)
{
    test_coverage_and_bounds();
    test_representative_default_sizes();
    test_invalid_work_areas();
    test_video_aspect_and_size_budget();
    test_video_invalid_inputs();
    std::cout << "PASS: " << checks << " window-policy checks\n";
    return 0;
}
