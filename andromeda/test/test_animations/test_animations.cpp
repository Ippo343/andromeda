#include <unity.h>

#include <set>
#include <string>

// animations.cpp declares most of its animation classes locally (not in a
// header), so we #include the .cpp directly to reach them for white-box
// testing of getRandomAnimation() - mirrors test_effects.cpp's approach for
// getRandomEffect(). animations.cpp is deliberately excluded from
// platformio.ini's native build_src_filter since its run() methods drive
// real delay()/millis() loops (out of scope for native tests); this test
// only exercises getRandomAnimation()'s selection logic, never run().
#include "../../src/animations.cpp"

// AbstractAnimation::GetName()/run() are declared in animation-base.h but
// never defined anywhere in production code - every real animation
// overrides both, so this dead code path never needed a definition until a
// native test needed the base class's vtable to link (mirrors
// test_mission_control.cpp's identical workaround).
const char* AbstractAnimation::GetName() { return "AbstractAnimation"; }
void AbstractAnimation::run() {}

void setUp() { GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE); }
void tearDown() {}

// ---------------------------------------------------------------------------
// getRandomAnimation() factory
// ---------------------------------------------------------------------------

void test_get_random_animation_produces_valid_animations()
{
    std::set<std::string> namesSeen;
    for (int i = 0; i < 300; i++)
    {
        AbstractAnimation* anim = getRandomAnimation();
        TEST_ASSERT_NOT_NULL(anim);
        TEST_ASSERT_NOT_NULL(anim->GetName());
        namesSeen.insert(anim->GetName());
        delete anim;
    }
    // 5 possible animations; 300 draws (never repeating consecutively) should
    // realistically hit all of them.
    TEST_ASSERT_TRUE(namesSeen.size() >= 4);
}

void test_get_random_animation_never_repeats_consecutively()
{
    std::string previous;
    for (int i = 0; i < 100; i++)
    {
        AbstractAnimation* anim = getRandomAnimation();
        std::string name = anim->GetName();
        delete anim;

        if (i > 0) { TEST_ASSERT_TRUE(name != previous); }
        previous = name;
    }
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_get_random_animation_produces_valid_animations);
    RUN_TEST(test_get_random_animation_never_repeats_consecutively);

    return UNITY_END();
}
