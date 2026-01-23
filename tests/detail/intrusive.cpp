#include <ilias/detail/intrusive.hpp>
#include <gtest/gtest.h>

using namespace ilias::intrusive;

struct ListElem : public Node<ListElem> {
    ListElem(int val) : value(val) {}
    int value;
};

TEST(Intrusive, List) {
    List<ListElem> list;

    {
        ListElem elem {1};

        list.push_back(elem);
        ASSERT_EQ(list.size(), 1);
        ASSERT_EQ(list.front().value, 1);
        ASSERT_TRUE(elem.isLinked());

        // Unlink it
        elem.unlink();
        ASSERT_FALSE(elem.isLinked());
        ASSERT_TRUE(list.empty());
    }

    ASSERT_TRUE(list.empty());

    {
        ListElem elem1 {1};
        ListElem elem2 {2};

        list.push_back(elem1);
        list.push_back(elem2);
        ASSERT_EQ(list.size(), 2);

        ASSERT_EQ(list.front().value, 1);
        ASSERT_EQ(list.back().value, 2);
    }

    ASSERT_TRUE(list.empty());

    {
        std::vector<ListElem> elems;
        for (int i = 0; i < 10; ++i) {
            auto &elem = elems.emplace_back(i);
            list.push_back(elem);
        }

        size_t i = 0;
        for (auto &elem : list) {
            ASSERT_EQ(elem.value, i++);
        }
    }
}

TEST(Intrusive, Rc) {
    struct Elem : public RefCounted<Elem> {
        int value = 0;
    };

    auto rc = Rc<Elem>::make();
    ASSERT_TRUE(rc);
    ASSERT_EQ(rc->value, 0);
    ASSERT_EQ(rc.use_count(), 1);

    rc = nullptr;
    ASSERT_FALSE(rc);
    ASSERT_EQ(rc.use_count(), 0);
}

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}