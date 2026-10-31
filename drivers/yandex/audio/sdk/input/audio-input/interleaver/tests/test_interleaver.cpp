#include <yandex_io/modules/audio_input/interleaver/interleaver.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace quasar;

Y_UNIT_TEST_SUITE_F(TestInterleaver, QuasarUnitTestFixture) {
    Y_UNIT_TEST(testNormalCase)
    {
        Interleaver w;

        using FloatVec = std::vector<float>;
        FloatVec a{1.0, 2.0};
        w.add(a, 1);

        FloatVec bbb{3.0, 3.1, 3.2, 4.0, 4.1, 4.2};
        w.add(bbb, 3);

        FloatVec cc{5.0, 5.1, 6.0, 6.1};
        w.add(cc, 2);

        FloatVec d{0.0};
        w.add(d, 0);

        FloatVec r;
        w.merge(r);

        const FloatVec etalon{
            1.0, 3.0, 3.1, 3.2, 5.0, 5.1,
            2.0, 4.0, 4.1, 4.2, 6.0, 6.1};
        UNIT_ASSERT_VALUES_EQUAL(r.size(), etalon.size());
        for (size_t i = 0; i < etalon.size(); ++i) {
            UNIT_ASSERT_VALUES_EQUAL_C(r[i], etalon[i], std::to_string(i));
        }
    }

    Y_UNIT_TEST(testExceptions)
    {
        Interleaver w;

        using FloatVec = std::vector<float>;
        FloatVec a{1.0, 2.0, 3.0, 4.0};
        UNIT_ASSERT_EXCEPTION(w.add(a, 3), std::runtime_error);
        w.add(a, 2);

        FloatVec b{2.0, 3.0, 4.0};
        UNIT_ASSERT_EXCEPTION(w.add(b, 1), std::runtime_error);
    }
}
