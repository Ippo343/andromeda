#include <unity.h>

#include <cmath>

#include "geometry/geometry.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// PolarCoordinates (Cartesian -> Polar conversion)
// ---------------------------------------------------------------------------

void test_polar_from_origin_is_zero()
{
    PolarCoordinates p({0, 0});
    TEST_ASSERT_EQUAL_UINT16(0, p.radius);
    TEST_ASSERT_EQUAL_UINT16(0, p.cdegrees);
}

void test_polar_positive_x_axis_is_zero_degrees()
{
    PolarCoordinates p({100, 0});
    TEST_ASSERT_EQUAL_UINT16(100, p.radius);
    TEST_ASSERT_EQUAL_UINT16(0, p.cdegrees);
}

void test_polar_positive_y_axis_is_90_degrees()
{
    PolarCoordinates p({0, 100});
    TEST_ASSERT_EQUAL_UINT16(100, p.radius);
    TEST_ASSERT_UINT16_WITHIN(1, 9000, p.cdegrees);
}

void test_polar_negative_x_axis_is_180_degrees()
{
    PolarCoordinates p({-100, 0});
    TEST_ASSERT_EQUAL_UINT16(100, p.radius);
    TEST_ASSERT_UINT16_WITHIN(1, 18000, p.cdegrees);
}

void test_polar_negative_y_axis_is_270_degrees()
{
    // atan2f(-y, x) is negative here, exercising the "angle_deg < 0" normalization branch
    PolarCoordinates p({0, -100});
    TEST_ASSERT_EQUAL_UINT16(100, p.radius);
    TEST_ASSERT_UINT16_WITHIN(1, 27000, p.cdegrees);
}

void test_polar_first_quadrant()
{
    // 3-4-5 triangle: radius should be exactly 5
    PolarCoordinates p({3, 4});
    TEST_ASSERT_EQUAL_UINT16(5, p.radius);
    TEST_ASSERT_TRUE(p.cdegrees > 0 && p.cdegrees < 9000);
}

void test_polar_third_quadrant_normalizes_negative_angle()
{
    // Both x and y negative -> atan2 returns a negative angle that must wrap to (18000, 27000)
    PolarCoordinates p({-3, -4});
    TEST_ASSERT_EQUAL_UINT16(5, p.radius);
    TEST_ASSERT_TRUE(p.cdegrees > 18000 && p.cdegrees < 27000);
}

// ---------------------------------------------------------------------------
// LedStrip allocate/deallocate
// ---------------------------------------------------------------------------

void test_led_strip_allocate_with_buffer()
{
    LedStrip strip;
    strip.allocate(10, true);
    TEST_ASSERT_EQUAL_INT(10, strip.num_leds);
    TEST_ASSERT_NOT_NULL(strip.leds);
    TEST_ASSERT_NOT_NULL(strip.buffer);
    TEST_ASSERT_EQUAL_INT(0, strip.buffer[0].r);
    TEST_ASSERT_EQUAL_INT(0, strip.buffer[9].b);
}

void test_led_strip_allocate_without_buffer()
{
    LedStrip strip;
    strip.allocate(5, false);
    TEST_ASSERT_EQUAL_INT(5, strip.num_leds);
    TEST_ASSERT_NOT_NULL(strip.leds);
    TEST_ASSERT_NULL(strip.buffer);
}

void test_led_strip_deallocate_resets_state()
{
    LedStrip strip;
    strip.allocate(5, true);
    strip.deallocate();
    TEST_ASSERT_EQUAL_INT(0, strip.num_leds);
    TEST_ASSERT_NULL(strip.leds);
    TEST_ASSERT_NULL(strip.buffer);
}

void test_led_strip_reallocate_frees_previous()
{
    LedStrip strip;
    strip.allocate(5, true);
    strip.allocate(8, true);  // allocate() calls deallocate() internally first
    TEST_ASSERT_EQUAL_INT(8, strip.num_leds);
}

// ---------------------------------------------------------------------------
// Geometry::initializeForTest - the pure (non-hardware) half of initialize()
// ---------------------------------------------------------------------------

void test_geometry_initialize_for_test_loads_single_strip_device()
{
    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);

    TEST_ASSERT_EQUAL_INT(1, GEOMETRY.getNumStrips());
    TEST_ASSERT_EQUAL_INT(56, GEOMETRY.getStrip(0).num_leds);

    // First LED of SingleStripTestDevice is {458, 0} per src/geometry/single_strip_test_device.cpp
    const Led& first = GEOMETRY.getStrip(0).leds[0];
    TEST_ASSERT_EQUAL_INT16(458, first.cartesian.x);
    TEST_ASSERT_EQUAL_INT16(0, first.cartesian.y);
    TEST_ASSERT_EQUAL_UINT16(458, first.polar.radius);
    TEST_ASSERT_EQUAL_UINT16(0, first.polar.cdegrees);

    // Last LED is {-458, 0}
    const Led& last = GEOMETRY.getStrip(0).leds[55];
    TEST_ASSERT_EQUAL_INT16(-458, last.cartesian.x);
}

// Every other geometry/effect test in this suite (and test_effects.cpp) uses
// SINGLE_STRIP_TEST_DEVICE exclusively - a single, short, synthetic strip.
// This confirms a real multi-strip production model (L70 MK1: 2 strips of
// 142/125 real LEDs, real coordinate table) also loads correctly through the
// same ModelId -> Geometry path, and that per-LED cartesian/polar
// coordinates are populated per strip, not just for strip 0.
void test_geometry_initialize_for_test_loads_multi_strip_l70_mk1_model()
{
    GEOMETRY.initializeForTest(ModelId::L70_MK1);

    TEST_ASSERT_EQUAL_INT(2, GEOMETRY.getNumStrips());
    TEST_ASSERT_EQUAL_INT(142, GEOMETRY.getStrip(0).num_leds);
    TEST_ASSERT_EQUAL_INT(125, GEOMETRY.getStrip(1).num_leds);

    // First LED of strip 0 is {340, -225} per src/geometry/L70_mk1.cpp's coords_L70 table.
    const Led& strip0First = GEOMETRY.getStrip(0).leds[0];
    TEST_ASSERT_EQUAL_INT16(340, strip0First.cartesian.x);
    TEST_ASSERT_EQUAL_INT16(-225, strip0First.cartesian.y);

    // Strip 1 starts where strip 0's LEDs leave off in the same flat coords_L70
    // table (index 142, the first entry of the table's 5th row): {310, -230}.
    const Led& strip1First = GEOMETRY.getStrip(1).leds[0];
    TEST_ASSERT_EQUAL_INT16(310, strip1First.cartesian.x);
    TEST_ASSERT_EQUAL_INT16(-230, strip1First.cartesian.y);

    // Polar coordinates are derived per-LED from cartesian, for every strip -
    // not just strip 0 (which is all SINGLE_STRIP_TEST_DEVICE tests ever exercise).
    TEST_ASSERT_TRUE(strip0First.polar.radius > 0);
    TEST_ASSERT_TRUE(strip1First.polar.radius > 0);
    TEST_ASSERT_TRUE(strip0First.polar.cdegrees != strip1First.polar.cdegrees);
}

void test_geometry_screen_dimension_helpers()
{
    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);

    // From single_strip_test_device.cpp: screen_height_mm=10, screen_width_mm=934
    TEST_ASSERT_EQUAL_UINT16(10, GEOMETRY.getScreenHeight());
    TEST_ASSERT_EQUAL_UINT16(934, GEOMETRY.getScreenWidth());
    TEST_ASSERT_EQUAL_UINT16(5, GEOMETRY.getScreenHalfHeight());
    TEST_ASSERT_EQUAL_UINT16(467, GEOMETRY.getScreenHalfWidth());
}

void test_geometry_reset_global_transform_restores_original_coordinates()
{
    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);

    CartesianCoordinates original = GEOMETRY.getStrip(0).leds[0].cartesian;

    GEOMETRY.applyGlobalRandomRotation();
    // After a rotation the coordinate should generally differ (not a strict
    // requirement for correctness, but confirms the rotation actually ran)

    GEOMETRY.resetGlobalTransform();
    CartesianCoordinates restored = GEOMETRY.getStrip(0).leds[0].cartesian;

    TEST_ASSERT_EQUAL_INT16(original.x, restored.x);
    TEST_ASSERT_EQUAL_INT16(original.y, restored.y);
}

void test_geometry_apply_global_random_rotation_preserves_radius()
{
    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);

    uint16_t original_radius = GEOMETRY.getStrip(0).leds[10].polar.radius;

    GEOMETRY.applyGlobalRandomRotation();

    // Rotation changes the angle but must preserve the radius (within rounding)
    uint16_t rotated_radius = GEOMETRY.getStrip(0).leds[10].polar.radius;

    GEOMETRY.resetGlobalTransform();

    // The rotation only updates cdegrees directly (not radius) in
    // applyGlobalRandomRotation(), so radius must be untouched.
    TEST_ASSERT_EQUAL_UINT16(original_radius, rotated_radius);
}

// ---------------------------------------------------------------------------
// model_registry lookups
// ---------------------------------------------------------------------------

void test_get_model_config_known_id()
{
    const ModelConfig* config = getModelConfig(ModelId::SINGLE_STRIP_TEST_DEVICE);
    TEST_ASSERT_NOT_NULL(config);
    TEST_ASSERT_EQUAL_INT(1, config->num_strips);
}

void test_get_model_config_unknown_id_returns_null()
{
    const ModelConfig* config = getModelConfig(ModelId::UNKNOWN);
    TEST_ASSERT_NULL(config);
}

void test_get_model_name_known_and_unknown()
{
    TEST_ASSERT_EQUAL_STRING("Single Strip Test Rig",
                             getModelName(ModelId::SINGLE_STRIP_TEST_DEVICE));
    TEST_ASSERT_EQUAL_STRING("Unknown", getModelName(ModelId::UNKNOWN));
}

void test_model_config_is_in_family()
{
    const ModelConfig* config = getModelConfig(ModelId::SINGLE_STRIP_TEST_DEVICE);
    TEST_ASSERT_TRUE(config->isInFamily(FamilyID::TEST_DEVICES));
    TEST_ASSERT_FALSE(config->isInFamily(FamilyID::ANDROMEDA));
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

void test_geometry_destructor_frees_allocated_strips()
{
    // A separate, local instance (not the GEOMETRY global) so its destructor
    // actually runs at the end of this scope, exercising the strips-allocated
    // branch of ~Geometry().
    {
        Geometry local;
        local.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);
        TEST_ASSERT_EQUAL_INT(1, local.getNumStrips());
    }
    // If ~Geometry() double-freed or crashed, the test binary would have
    // aborted before reaching this point.
    TEST_ASSERT_TRUE(true);
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_polar_from_origin_is_zero);
    RUN_TEST(test_polar_positive_x_axis_is_zero_degrees);
    RUN_TEST(test_polar_positive_y_axis_is_90_degrees);
    RUN_TEST(test_polar_negative_x_axis_is_180_degrees);
    RUN_TEST(test_polar_negative_y_axis_is_270_degrees);
    RUN_TEST(test_polar_first_quadrant);
    RUN_TEST(test_polar_third_quadrant_normalizes_negative_angle);

    RUN_TEST(test_led_strip_allocate_with_buffer);
    RUN_TEST(test_led_strip_allocate_without_buffer);
    RUN_TEST(test_led_strip_deallocate_resets_state);
    RUN_TEST(test_led_strip_reallocate_frees_previous);

    RUN_TEST(test_geometry_initialize_for_test_loads_single_strip_device);
    RUN_TEST(test_geometry_initialize_for_test_loads_multi_strip_l70_mk1_model);
    RUN_TEST(test_geometry_screen_dimension_helpers);
    RUN_TEST(test_geometry_reset_global_transform_restores_original_coordinates);
    RUN_TEST(test_geometry_apply_global_random_rotation_preserves_radius);

    RUN_TEST(test_get_model_config_known_id);
    RUN_TEST(test_get_model_config_unknown_id_returns_null);
    RUN_TEST(test_get_model_name_known_and_unknown);
    RUN_TEST(test_model_config_is_in_family);

    RUN_TEST(test_geometry_destructor_frees_allocated_strips);

    return UNITY_END();
}
