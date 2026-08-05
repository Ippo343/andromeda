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
    def __init__(self, name, segments, flip_x=False, flip_y=False, frame_width_mm=None, frame_height_mm=None):
        """
        Define a model as a collection of segments.

        Args:
            name: Model name (e.g., "L10", "L25")
            segments: List of Segment objects
            flip_x: If True, flip the X axis (multiply all X coordinates by -1)
            flip_y: If True, flip the Y axis (multiply all Y coordinates by -1)
            frame_width_mm: Frame width in mm. If provided with frame_height_mm,
                           coordinates will be recentered to frame center
            frame_height_mm: Frame height in mm
        """
        self.name = name
        self.segments = segments
        self.flip_x = flip_x
        self.flip_y = flip_y
        self.frame_width_mm = frame_width_mm
        self.frame_height_mm = frame_height_mm

    def generate(self):
        """
        Generate all LED coordinates for this model.
        Applies transformations: flip_x and flip_y, then recenter to frame.
        Returns: (coordinates array, list of LED counts per segment)
        """
        all_coords = []
        counts = []

        for segment in self.segments:
            coords = segment.generate()
            all_coords.extend(coords)
            counts.append(segment.led_count)

        # Convert to numpy array for transformation
        all_coords = np.array(all_coords)

        # Apply recentering to frame if dimensions are provided
        if self.frame_width_mm is not None and self.frame_height_mm is not None:
            # Current coordinates are from corner (0,0)
            # Shift so that frame center is at (0,0)
            all_coords[:, 0] -= self.frame_width_mm / 2.0
            all_coords[:, 1] -= self.frame_height_mm / 2.0

        # Apply flip transformations
        if self.flip_x:
            all_coords[:, 0] = -all_coords[:, 0]
        if self.flip_y:
            all_coords[:, 1] = -all_coords[:, 1]

        return all_coords, counts

    def print_info(self):
        """Print model information including all segment details."""
        print(f"Model: {self.name}")
        print(f"Total segments: {len(self.segments)}")
        print(f"Total LEDs: {sum(s.led_count for s in self.segments)}")

        # Print transformation settings
        transformations = []
        if self.flip_x:
            transformations.append("X-axis flipped")
        if self.flip_y:
            transformations.append("Y-axis flipped")
        if transformations:
            print(f"Transformations: {', '.join(transformations)}")

        if self.frame_width_mm is not None and self.frame_height_mm is not None:
            print(f"Frame dimensions: {self.frame_width_mm} x {self.frame_height_mm} mm")
            print(f"Coordinates recentered to frame center")

        print()

        for segment in self.segments:
            segment.print_info()
            print()


def main():
    # L10 - 10cm square with 13 LEDs per side (144 LED/m)
    # 13 LEDs at 6.944mm pitch → span = 12 * 6.944 = 83.333mm → half = 41.667mm
    # Start: top-right corner, anticlockwise: Top→Left→Bottom→Right
    H = 41.667
    l10_segments = [
        Segment("Top",    start_coord=( H,  H), end_coord=(-H,  H), led_count=13),
        Segment("Left",   start_coord=(-H,  H), end_coord=(-H, -H), led_count=13),
        Segment("Bottom", start_coord=(-H, -H), end_coord=( H, -H), led_count=13),
        Segment("Right",  start_coord=( H, -H), end_coord=( H,  H), led_count=13),
    ]

    # L70 - Composite with outer and inner rectangles (60 LED/m)
    # Pitch should be ~16.667mm for 60 LED/m
    # Normal orientation (X right, Y up), 720x520mm frame
    l70_segments = [
        # Front rectangle: 70cm x 50cm
        Segment("Front_Right",  start_coord=(20, 35),   end_coord=(20, 495),   led_count=29),
        Segment("Front_Top",    start_coord=(20, 500),  end_coord=(695, 500),  led_count=42),
        Segment("Front_Left",   start_coord=(700, 500), end_coord=(700, 25),   led_count=30),
        Segment("Front_Bottom", start_coord=(700, 20),  end_coord=(35, 20),    led_count=41),
        # Back rectangle: 60cm x 40cm
        Segment("Back_Bottom",  start_coord=(50, 30),   end_coord=(667, 30),   led_count=38),
        Segment("Back_Right",   start_coord=(690, 60),  end_coord=(690, 455),  led_count=25),
        Segment("Back_Top",     start_coord=(665, 483), end_coord=(67, 483),   led_count=37),
        Segment("Back_Left",    start_coord=(40, 453),  end_coord=(37, 56),    led_count=25),
    ]

    models = [
        Model("L10", l10_segments),
        Model("L70", l70_segments, flip_x=True, frame_width_mm=720, frame_height_mm=520),  # Just recenter
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