//
// Native unit tests for WindowOutputStream: reassembling all windows must give
// back exactly the full output, for any chunk size.
//
#include <unity.h>
#include <string>
#include <vector>

#include "../../src/json/WindowOutputStream.h"

void setUp() {}
void tearDown() {}

static const std::string kDoc = R"({"core":{"name":"buttonplus_one","brightness":80},"displayitems":[{"x":0,"y":80}]})";

template <class S>
static void writeDoc(S& s) {
    for (const char c : kDoc) s.Put(c);
    s.Flush();
}

void test_counting_only() {
    WindowOutputStream counter;
    writeDoc(counter);
    TEST_ASSERT_EQUAL(kDoc.size(), counter.total());
    TEST_ASSERT_EQUAL(0, counter.copied());
}

static std::string reassemble(const size_t chunk) {
    std::string out;
    std::vector<uint8_t> buf(chunk);
    for (size_t index = 0;; ) {
        WindowOutputStream w(buf.data(), index, chunk);
        writeDoc(w);
        if (w.copied() == 0) break;
        out.append(reinterpret_cast<const char*>(buf.data()), w.copied());
        index += w.copied();
    }
    return out;
}

void test_reassembles_for_any_chunk_size() {
    for (size_t chunk : {1u, 2u, 7u, 16u, 64u, 1000u}) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(kDoc.c_str(), reassemble(chunk).c_str(), std::to_string(chunk).c_str());
    }
}

void test_window_past_end_copies_nothing() {
    uint8_t buf[8];
    WindowOutputStream w(buf, kDoc.size() + 5, sizeof buf);
    writeDoc(w);
    TEST_ASSERT_EQUAL(0, w.copied());
    TEST_ASSERT_EQUAL(kDoc.size(), w.total());
}

void test_last_partial_window() {
    uint8_t buf[16];
    const size_t skip = kDoc.size() - 3;
    WindowOutputStream w(buf, skip, sizeof buf);
    writeDoc(w);
    TEST_ASSERT_EQUAL(3, w.copied());
    TEST_ASSERT_EQUAL_MEMORY(kDoc.data() + skip, buf, 3);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_counting_only);
    RUN_TEST(test_reassembles_for_any_chunk_size);
    RUN_TEST(test_window_past_end_copies_nothing);
    RUN_TEST(test_last_partial_window);
    return UNITY_END();
}
