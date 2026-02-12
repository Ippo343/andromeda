#!/usr/bin/env python3
# -*- coding: utf8 -*-

"""
This is a utility that estimates the physical coordinates of each LED in the structure of Andromeda MK1.

Knowing the position is required to write 2D effects that use the whole structure.
Since there are (7 * 23) LEDs in total, obviously I wasn't going to measure each of them individually
(not to mention that it would be really hard, since there is no obvious physical attachment point).

So instead we took a reference picture, as perpendicular as possible to the structure,
and then measured the pixel coordinates of each loop's centre and of the first and last LED in each strip.
Then we assume that all the remaining LEDs are equally spaced along a circle (roughly true)
and estimate all of their positions.
"""

import math
import numpy as np

import led_utils

LEDS_PER_STRIP = 23

# measured from the reference image
base_top_px = 152
base_bottom_px = 1052
base_height_px = (base_bottom_px - base_top_px)

# measured from the CAD drawing and verified with a ruler
base_height_mm = 592

# conversion factor to transform between pixels and meters,
# estimated using the dimensions of the base
mm_per_px = base_height_mm / base_height_px


def interpolate_strip(strip_center_px, first_led_px, last_led_px):
    """
    Computes the coordinates of all the LEDs on one strip, given the coordinates of its center, first and last LED.
    The inputs are all measured in pixels: the center's coordinates are relative to the global center,
    while the first and last LED's coordinates are relative to the center of the strip they belong to.
    Returns the coordinates of all LEDs in millimeters relative to the center of the structure.
    """

    # We measure the angles to the two ends of the strip
    # and then we assume that all the others are equally spaced in between.
    # Note that the strip doesn't close perfectly (the space between the first and last is not the same
    # as the space between consecutive LEDs) so we need to account for it.
    theta_first = math.atan2(first_led_px[1], first_led_px[0])
    theta_last = math.atan2(last_led_px[1], last_led_px[0])

    # We also need to estimate the radius, since the measurements we took are not that precise
    radius_first = np.linalg.norm(first_led_px)
    radius_last = np.linalg.norm(last_led_px)
    radius = (radius_first + radius_last) / 2

    # The angle covered by the strip, and the angle step between each LED
    theta_span = 2 * math.pi - (theta_last - theta_first)
    delta_theta = theta_span / (LEDS_PER_STRIP - 1)

    for n in range(LEDS_PER_STRIP):
        # Note the (-) here: our strips are wound clockwise, radians increase counterclockwise:
        # so increasing the LED's index must decrease the angle
        theta_n = theta_first - n * delta_theta
        rel_coords_n = (radius * math.cos(theta_n), radius * math.sin(theta_n))
        abs_coords_n = rel_coords_n + strip_center_px
        abs_coords_n *= mm_per_px

        # Round all the coordinates to the nearest millimeter,
        # as they will be stored as integers in the arduino anyway
        abs_coords_n = np.round(abs_coords_n)

        yield abs_coords_n


def main():
    # Coordinates of all centers, first and last LED in each strip,
    # measured in pixel (by hand, in paint, using the reference image)

    centres_px = [
        (573, 604),
        (573, 306),
        (830, 456),
        (830, 752),
        (573, 902),
        (316, 752),
        (316, 456)
    ]

    firsts_px = [
        (563, 506),
        (595, 400),
        (757, 520),
        (799, 662),
        (650, 845),
        (410, 764),
        (339, 549)
    ]

    lasts_px = [
        (528, 516),
        (629, 383),
        (789, 543),
        (766, 681),
        (621, 818),
        (407, 762),
        (372, 531)
    ]

    # First turn the tuples into numpy arrays so we can math them.
    # Also note that we flip the Y axis here: the measurement were taken on a picture, where Y points down,
    # but we want regular cartesian math, so Y must be flipped
    centres_px = [np.array((c[0], -c[1])) for c in centres_px]
    firsts_px = [np.array((c[0], -c[1])) for c in firsts_px]
    lasts_px = [np.array((c[0], -c[1])) for c in lasts_px]

    # Now make everything relative to the center of the structure
    c0 = centres_px[0]
    centres_px = [c - c0 for c in centres_px]
    firsts_px = [c - c0 for c in firsts_px]
    lasts_px = [c - c0 for c in lasts_px]

    # Now make all the LED's coordinates relative to the center of the strip they belong to
    for i in range(len(centres_px)):
        firsts_px[i] -= centres_px[i]
        lasts_px[i] -= centres_px[i]

    all_leds_flat = []

    # Calculate all coordinates
    for strip_data in zip(centres_px, firsts_px, lasts_px):
        strip_coords = list(interpolate_strip(*strip_data))
        all_leds_flat.extend(strip_coords)

    # --- UPDATED OUTPUT LOGIC ---

    # Define the row counts: 7 strips, each with 23 LEDs
    strip_counts = [LEDS_PER_STRIP] * len(centres_px)

    # Use the updated utility function with row_counts
    # We assign a specific variable name for clarity in the firmware
    led_utils.print_arduino_header(
        all_leds_flat,
        variable_name="coords_Andromeda",
        row_counts=strip_counts
    )

    led_utils.print_bounding_box(all_leds_flat)


if __name__ == "__main__":
    main()
