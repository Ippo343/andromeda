import numpy as np
import led_utils

class Segment:
    def __init__(self, name, start_coord, end_coord, led_count):
        """
        Define a segment by its endpoints and LED count.

        Args:
            name: Descriptive name for this segment
            start_coord: (x, y) tuple in mm for first LED
            end_coord: (x, y) tuple in mm for last LED
            led_count: Total number of LEDs in this segment
        """
        self.name = name
        self.start = np.array(start_coord, dtype=float)
        self.end = np.array(end_coord, dtype=float)
        self.led_count = led_count

        # Calculate pitch (distance between LEDs)
        self.length = np.linalg.norm(self.end - self.start)
        if led_count > 1:
            self.pitch = self.length / (led_count - 1)
        else:
            self.pitch = 0.0

    def generate(self):
        """
        Generate LED coordinates for this segment.
        Returns: numpy array of shape (led_count, 2)
        """
        if self.led_count == 1:
            return np.array([self.start])
        return np.linspace(self.start, self.end, self.led_count)

    def print_info(self):
        """Print segment information including computed pitch."""
        print(f"  Segment: {self.name}")
        print(f"    Start: ({self.start[0]:.2f}, {self.start[1]:.2f}) mm")
        print(f"    End:   ({self.end[0]:.2f}, {self.end[1]:.2f}) mm")
        print(f"    LEDs:  {self.led_count}")
        print(f"    Length: {self.length:.2f} mm")
        print(f"    Pitch:  {self.pitch:.3f} mm ({1000.0/self.pitch:.1f} LED/m)" if self.pitch > 0 else "    Pitch: N/A (single LED)")


class Model:
    def __init__(self, name, segments):
        """
        Define a model as a collection of segments.

        Args:
            name: Model name (e.g., "L10", "L25")
            segments: List of Segment objects
        """
        self.name = name
        self.segments = segments

    def generate(self):
        """
        Generate all LED coordinates for this model.
        Returns: (coordinates array, list of LED counts per segment)
        """
        all_coords = []
        counts = []

        for segment in self.segments:
            coords = segment.generate()
            all_coords.extend(coords)
            counts.append(segment.led_count)

        return all_coords, counts

    def print_info(self):
        """Print model information including all segment details."""
        print(f"Model: {self.name}")
        print(f"Total segments: {len(self.segments)}")
        print(f"Total LEDs: {sum(s.led_count for s in self.segments)}")
        print()

        for segment in self.segments:
            segment.print_info()
            print()


def main():
    # --- Configuration ---

    # Example: L10 - 10cm square with 13 LEDs per side (144 LED/m)
    # Coordinates are approximate, pitch should be ~6.944mm for 144 LED/m
    l10_segments = [
        Segment("Top",    start_coord=(55, 55),   end_coord=(-55, 55),   led_count=13),
        Segment("Left",   start_coord=(-55, 55),  end_coord=(-55, -55),  led_count=13),
        Segment("Bottom", start_coord=(-55, -55), end_coord=(55, -55),   led_count=13),
        Segment("Right",  start_coord=(55, -55),  end_coord=(55, 55),    led_count=13),
    ]

    # Example: L25 - 25cm square with 15 LEDs per side (60 LED/m)
    # Pitch should be ~16.667mm for 60 LED/m
    l25_segments = [
        Segment("Top",    start_coord=(130, 130),   end_coord=(-130, 130),   led_count=15),
        Segment("Left",   start_coord=(-130, 130),  end_coord=(-130, -130),  led_count=15),
        Segment("Bottom", start_coord=(-130, -130), end_coord=(130, -130),   led_count=15),
        Segment("Right",  start_coord=(130, -130),  end_coord=(130, 130),    led_count=15),
    ]

    # Example: L70 - Composite with outer and inner rectangles (60 LED/m)
    # Pitch should be ~16.667mm for 60 LED/m
    l70_segments = [
        # Outer rectangle: 70cm x 50cm
        Segment("Outer_Top",    start_coord=(355, 255),   end_coord=(-355, 255),   led_count=42),
        Segment("Outer_Left",   start_coord=(-355, 255),  end_coord=(-355, -255),  led_count=30),
        Segment("Outer_Bottom", start_coord=(-355, -255), end_coord=(355, -255),   led_count=42),
        Segment("Outer_Right",  start_coord=(355, -255),  end_coord=(355, 255),    led_count=30),
        # Inner rectangle: 60cm x 40cm
        Segment("Inner_Top",    start_coord=(305, 205),   end_coord=(-305, 205),   led_count=36),
        Segment("Inner_Left",   start_coord=(-305, 205),  end_coord=(-305, -205),  led_count=24),
        Segment("Inner_Bottom", start_coord=(-305, -205), end_coord=(305, -205),   led_count=36),
        Segment("Inner_Right",  start_coord=(305, -205),  end_coord=(305, 205),    led_count=24),
    ]

    models = [
        Model("L10", l10_segments),
        Model("L25", l25_segments),
        Model("L70", l70_segments),
    ]

    # --- Generation & Printing ---

    print("="*60)
    print("LED SEGMENT ANALYSIS")
    print("="*60)
    print()

    for model in models:
        model.print_info()
        print("-"*60)
        print()

    print("\n" + "="*60)
    print("ARDUINO COORDINATE OUTPUT")
    print("="*60)
    print()
    print("// Auto-generated LED coordinates for L-Family")
    print("// Generated by L_family.py\n")

    for model in models:
        print(f"// --- Model: {model.name} ---")

        coords, counts = model.generate()
        var_name = f"coords_{model.name}"

        led_utils.print_arduino_header(coords, variable_name=var_name, row_counts=counts)
        led_utils.print_bounding_box(coords)
        print("\n" + "-"*40 + "\n")


if __name__ == "__main__":
    main()