import numpy as np
import led_utils

LEDS_PER_METER = 144
LED_PITCH = 1000.0 / LEDS_PER_METER
LEDS_PER_SIDE = 13
SIDE_LENGTH = LED_PITCH * (LEDS_PER_SIDE - 1)

# TODO: verify this, it's a guess
CORNER_BUFFER = 5


def generate_coordinates():
    half_s = SIDE_LENGTH / 2
    buffer = CORNER_BUFFER

    sides = [
        ((half_s,  half_s + buffer), (-half_s,  half_s + buffer)), # Top
        ((-half_s - buffer,  half_s), (-half_s - buffer, -half_s)), # Left
        ((-half_s, -half_s - buffer), (half_s, -half_s - buffer)),  # Bottom
        ((half_s + buffer, -half_s), (half_s + buffer,  half_s))   # Right
    ]

    all_coords = []
    for start, end in sides:
        side_coords = np.linspace(start, end, LEDS_PER_SIDE)
        all_coords.append(side_coords)

    # vstack creates one long (52, 2) array
    return np.vstack(all_coords)


if __name__ == "__main__":
    # Generate the points using the refined linspace logic
    coords = generate_coordinates()

    # Output: 4 rows of 13 coordinates
    led_utils.print_arduino_header(coords, row_size=LEDS_PER_SIDE)
    led_utils.print_bounding_box(coords)