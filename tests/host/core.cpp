#include <array>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>

#include <solar/core.hpp>

namespace
{

std::size_t allocation_count = 0;

} // namespace

void* operator new(std::size_t size)
{
    ++allocation_count;
    if (auto* memory = std::malloc(size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace
{

enum class ParseError
{
    Empty,
    Invalid,
};

struct MoveOnlyError
{
    int reason;

    constexpr explicit MoveOnlyError(int value) : reason(value) {}
    MoveOnlyError(const MoveOnlyError&) = delete;
    constexpr MoveOnlyError(MoveOnlyError&&) = default;
    MoveOnlyError& operator=(const MoveOnlyError&) = delete;
    constexpr MoveOnlyError& operator=(MoveOnlyError&&) = default;
};

constexpr solar::Status status_of(ParseError error)
{
    switch (error) {
    case ParseError::Empty:
        return solar::Status::Empty;
    case ParseError::Invalid:
        return solar::Status::Invalid;
    }
    return solar::Status::Error;
}

constexpr solar::Result<int, ParseError> parse(bool valid)
{
    if (!valid) {
        return solar::fail(ParseError::Invalid);
    }
    return 21;
}

constexpr bool expected_is_constexpr()
{
    const auto result = parse(true)
                            .transform([](int value) { return value * 2; })
                            .and_then([](int value) -> solar::Result<int, ParseError> {
                                return value == 42 ? solar::Result<int, ParseError>{value}
                                                   : solar::fail(ParseError::Invalid);
                            });
    return result && *result == 42;
}

template <typename T> struct IsIntegral : std::is_integral<T>
{};

template <typename T> struct AddPointer
{
    using type = T*;
};

using Values = solar::TypeList<int, float, int, long>;
using UniqueValues = solar::unique_t<Values>;
using IntegralValues = solar::filter_t<Values, IsIntegral>;
using PointerValues = solar::transform_t<solar::TypeList<int, float>, AddPointer>;

static_assert(expected_is_constexpr());
static_assert(solar::FixedString{"solar"}.view() == "solar");
static_assert(solar::FixedString{""}.empty());
static_assert(solar::Name<"core">::view() == "core");
static_assert(solar::list_size_v<Values> == 4);
static_assert(solar::list_count_v<int, Values> == 2);
static_assert(solar::contains_v<float, Values>);
static_assert(!solar::unique_types_v<Values>);
static_assert(std::is_same_v<UniqueValues, solar::TypeList<int, float, long>>);
static_assert(std::is_same_v<IntegralValues, solar::TypeList<int, int, long>>);
static_assert(std::is_same_v<PointerValues, solar::TypeList<int*, float*>>);
static_assert(std::is_same_v<solar::concat_t<solar::TypeList<int>, solar::TypeList<float>>,
                             solar::TypeList<int, float>>);
static_assert(std::is_same_v<solar::type_at_t<2, Values>, int>);
static_assert(status_of(ParseError::Invalid) == solar::Status::Invalid);
static_assert(solar::status_from_errno(-EINVAL) == solar::Status::Invalid);
static_assert(solar::status_from_errno(EIO) == solar::Status::Error);
static_assert(solar::to_native_errno(solar::Status::Busy) == -EBUSY);
static_assert(std::is_same_v<solar::Result<int>, std::expected<int, solar::Status>>);

void test_expected_operations()
{
    const auto transformed = parse(true).transform([](int value) { return value + 1; });
    assert(transformed && *transformed == 22);

    const auto chained = parse(true).and_then(
        [](int value) -> solar::Result<long, ParseError> { return static_cast<long>(value * 2); });
    assert(chained && *chained == 42);

    const auto recovered =
        parse(false).or_else([](ParseError) -> solar::Result<int, ParseError> { return 7; });
    assert(recovered && *recovered == 7);

    const auto mapped = parse(false).transform_error(status_of);
    assert(!mapped && mapped.error() == solar::Status::Invalid);

    solar::Result<void> complete{};
    solar::Result<void> incomplete = solar::fail(solar::Status::NotReady);
    assert(complete);
    assert(!incomplete && incomplete.error() == solar::Status::NotReady);
}

void test_move_only_results()
{
    solar::Result<std::unique_ptr<int>> value = std::make_unique<int>(42);
    auto moved = std::move(value).transform([](std::unique_ptr<int> item) { return *item; });
    assert(moved && *moved == 42);

    solar::Result<int, MoveOnlyError> failure = solar::fail(MoveOnlyError{17});
    auto mapped =
        std::move(failure).transform_error([](MoveOnlyError error) { return error.reason; });
    assert(!mapped && mapped.error() == 17);
}

void test_type_iteration()
{
    std::array<std::size_t, 3> sizes{};
    std::size_t index = 0;
    solar::for_each_type<solar::TypeList<char, short, int>>(
        [&]<typename T> { sizes[index++] = sizeof(T); });
    assert((sizes == std::array{sizeof(char), sizeof(short), sizeof(int)}));
}

void test_result_operations_do_not_allocate()
{
    const auto allocations_before = allocation_count;
    const auto result =
        parse(true)
            .transform([](int value) { return value * 2; })
            .and_then([](int value) -> solar::Result<int, ParseError> { return value; })
            .transform_error(status_of);

    assert(result && *result == 42);
    assert(allocation_count == allocations_before);
}

} // namespace

int main()
{
    test_expected_operations();
    test_move_only_results();
    test_type_iteration();
    test_result_operations_do_not_allocate();
    return 0;
}
