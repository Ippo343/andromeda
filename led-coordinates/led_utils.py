import numpy as np

def print_arduino_header(coordinates_list, variable_name="relative_led_coordinates", row_size=None):
    """
    Formats and prints a flat list of coordinates as a 1D PROGMEM C++ array,
    with rows visually grouped by row_size.
    """
    total_leds = len(coordinates_list)
    header = f"const PROGMEM CartesianCoordinates {variable_name}[{total_leds}] = {{"
    footer = "};"

    # If no row_size provided, print as a single column (row_size=1)
    step = row_size if row_size is not None else 1

    print(header)

    # Format all coordinates first
    formatted = [f"{{ {int(np.round(c[0])):>4}, {int(np.round(c[1])):>4} }}" for c in coordinates_list]

    # Print in grouped rows
    for i in range(0, total_leds, step):
        chunk = formatted[i : i + step]
        line_content = ", ".join(chunk)

        # Add comma if this isn't the final element of the entire array
        suffix = "," if (i + step) < total_leds else ""
        print(f"  {line_content}{suffix}")

    print(footer)

def print_bounding_box(all_coords):
    """Calculates and prints the bounding box and screen size."""
    coords_array = np.array(all_coords)
    min_x, min_y = np.min(coords_array, axis=0)
    max_x, max_y = np.max(coords_array, axis=0)

    print(f"\nBounding box: ({round(min_x)}, {round(min_y)}) - ({round(max_x)}, {round(max_y)})")
    print(f"Screen size: {int(np.ceil(max_x - min_x))} by {int(np.ceil(max_y - min_y))}")
