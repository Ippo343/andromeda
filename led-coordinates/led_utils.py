import numpy as np

def print_arduino_header(coordinates_list, variable_name="relative_led_coordinates", row_counts=None):
    """
    Formats and prints a flat list of coordinates as a 1D PROGMEM C++ array.

    Args:
        coordinates_list: Flat list of (x,y) tuples or numpy arrays.
        variable_name: Name of the C++ array.
        row_counts: Optional list of integers. Each integer defines the length of a
                    visual 'row' in the output. Useful for rectangles where
                    sides have different LED counts.
    """
    total_leds = len(coordinates_list)
    header = f"const PROGMEM CartesianCoordinates {variable_name}[{total_leds}] = {{"
    footer = "};"

    print(header)

    # Pre-format all coordinates to strings
    formatted = [f"{{ {int(np.round(c[0])):>4}, {int(np.round(c[1])):>4} }}" for c in coordinates_list]

    current_idx = 0

    # If no specific row counts provided, default to a single block
    if row_counts is None:
        row_counts = [total_leds]

    for count in row_counts:
        # Safety check to prevent index errors if counts don't match total
        if current_idx >= total_leds:
            break

        chunk = formatted[current_idx : current_idx + count]
        line_content = ", ".join(chunk)

        # Check if we still have more LEDs after this chunk
        current_idx += count
        suffix = "," if current_idx < total_leds else ""

        # Print the visual row
        print(f"  {line_content}{suffix}")

    print(footer)

def print_bounding_box(all_coords):
    coords_array = np.array(all_coords)
    min_x, min_y = np.min(coords_array, axis=0)
    max_x, max_y = np.max(coords_array, axis=0)

    width = max_x - min_x
    height = max_y - min_y

    print(f"\nBounding box: ({round(min_x)}, {round(min_y)}) - ({round(max_x)}, {round(max_y)})")
    print(f"Screen size: {int(np.ceil(width))}mm by {int(np.ceil(height))}mm")
