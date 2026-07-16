#include <array>
#include <atomic>
#include <span>

#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

#if defined(CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS)
static_assert(solar::parameters::persistence::Adapter<solar::parameters::settings::ZephyrStore>);
#endif

namespace fixture
{

struct FakeStore
{
    struct Entry
    {
        solar::parameters::persistence::Key key{};
        std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_ENCODED_RECORD_BYTES> bytes{};
        std::size_t size{};
        std::uint64_t saves{};
        bool occupied{};
    };

    static std::array<Entry, 12> entries;
    inline static std::atomic_bool fail_next_save{};
    inline static std::atomic_uint64_t fail_save_stable_id{};
    inline static std::atomic_bool block_next_save{};
    inline static std::atomic_uint initializes{};
    inline static std::atomic_uint erases{};
    inline static solar::kernel::BinarySemaphore save_started{};
    inline static solar::kernel::BinarySemaphore allow_save{};

    static void reset()
    {
        entries = {};
        fail_next_save.store(false, std::memory_order_release);
        fail_save_stable_id.store(0, std::memory_order_release);
        block_next_save.store(false, std::memory_order_release);
        initializes.store(0, std::memory_order_release);
        erases.store(0, std::memory_order_release);
        save_started.reset();
        allow_save.reset();
    }

    [[nodiscard]] static solar::Result<void> initialize()
    {
        initializes.fetch_add(1, std::memory_order_release);
        return {};
    }

    [[nodiscard]] static solar::Result<std::size_t> load(solar::parameters::persistence::Key key,
                                                         std::span<std::byte> output)
    {
        for (const auto& entry : entries) {
            if (entry.occupied && entry.key.kind == key.kind &&
                entry.key.stable_id == key.stable_id) {
                if (output.size() < entry.size) {
                    return solar::fail(solar::Status::NoBuffer);
                }
                for (std::size_t index = 0; index < entry.size; ++index) {
                    output[index] = entry.bytes[index];
                }
                return entry.size;
            }
        }
        return solar::fail(solar::Status::NotFound);
    }

    [[nodiscard]] static solar::Result<void> save(solar::parameters::persistence::Key key,
                                                  std::span<const std::byte> input)
    {
        if (fail_save_stable_id.load(std::memory_order_acquire) == key.stable_id) {
            fail_save_stable_id.store(0, std::memory_order_release);
            return solar::fail(solar::Status::Error);
        }
        if (fail_next_save.exchange(false, std::memory_order_acq_rel)) {
            return solar::fail(solar::Status::Error);
        }
        if (block_next_save.exchange(false, std::memory_order_acq_rel)) {
            save_started.give();
            const auto released =
                allow_save.take(solar::kernel::Timeout::after(std::chrono::seconds{1}));
            if (released != solar::Status::Ok) {
                return solar::fail(released);
            }
        }
        Entry* destination = nullptr;
        for (auto& entry : entries) {
            if (entry.occupied && entry.key.kind == key.kind &&
                entry.key.stable_id == key.stable_id) {
                destination = &entry;
                break;
            }
            if (destination == nullptr && !entry.occupied) {
                destination = &entry;
            }
        }
        if (destination == nullptr || input.size() > destination->bytes.size()) {
            return solar::fail(solar::Status::NoSpace);
        }
        destination->key = key;
        destination->size = input.size();
        destination->occupied = true;
        ++destination->saves;
        for (std::size_t index = 0; index < input.size(); ++index) {
            destination->bytes[index] = input[index];
        }
        return {};
    }

    [[nodiscard]] static solar::Result<void> erase(solar::parameters::persistence::Key key)
    {
        for (auto& entry : entries) {
            if (entry.occupied && entry.key.kind == key.kind &&
                entry.key.stable_id == key.stable_id) {
                entry.occupied = false;
                entry.size = 0;
                erases.fetch_add(1, std::memory_order_release);
                return {};
            }
        }
        erases.fetch_add(1, std::memory_order_release);
        return {};
    }

    template <typename ParameterT> static void seed(typename ParameterT::Value value)
    {
        seed_version<ParameterT>(
            value,
            solar::descriptor_traits<solar::parameters::Tag, ParameterT>::descriptor.version);
    }

    template <typename ParameterT>
    static void seed_version(typename ParameterT::Value value, std::uint16_t version)
    {
        std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_ENCODED_RECORD_BYTES> bytes{};
        using Codec = solar::parameters::detail::codec_for_t<ParameterT>;
        std::array<std::byte, Codec::encoded_size> payload{};
        auto payload_size = Codec::encode(value, payload);
        zassert_true(payload_size.has_value());
        auto encoded = solar::parameters::persistence::detail::encode_record(
            solar::parameters::detail::persistence_key<ParameterT>(), version, payload, bytes);
        zassert_true(encoded.has_value());
        auto saved = save(solar::parameters::detail::persistence_key<ParameterT>(),
                          std::span<const std::byte>{bytes}.first(*encoded));
        zassert_true(saved.has_value());
    }

    template <typename ParameterT>
    [[nodiscard]] static solar::Result<typename ParameterT::Value> value()
    {
        std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_ENCODED_RECORD_BYTES> bytes{};
        auto loaded = load(solar::parameters::detail::persistence_key<ParameterT>(), bytes);
        if (!loaded) {
            return solar::fail(loaded.error());
        }
        auto record = solar::parameters::persistence::detail::decode_record(
            std::span<const std::byte>{bytes}.first(*loaded));
        if (!record) {
            return solar::fail(record.error());
        }
        return solar::parameters::detail::codec_for_t<ParameterT>::decode(record->payload);
    }

    [[nodiscard]] static std::uint64_t saves(solar::parameters::persistence::Key key)
    {
        for (const auto& entry : entries) {
            if (entry.occupied && entry.key.kind == key.kind &&
                entry.key.stable_id == key.stable_id) {
                return entry.saves;
            }
        }
        return 0;
    }
};

inline std::array<FakeStore::Entry, 12> FakeStore::entries{};

struct DriveKp
{
    using Value = float;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.kp",
        .description = "Drive proportional gain",
    };
    static constexpr Value default_value = 1.0F;
    using Validation = solar::parameters::Range<0.0F, 10.0F, solar::parameters::Clamp>;
};

struct DriveKi
{
    using Value = float;
    static constexpr solar::parameters::Descriptor descriptor{.name = "drive.ki"};
    static constexpr Value default_value = 0.1F;
    using Validation = solar::parameters::Range<0.0F, 2.0F, solar::parameters::Reject>;
};

struct AtomicCounter
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{.name = "atomic.counter"};
    static constexpr Value default_value = 3;
    using Storage = solar::parameters::Atomic;
};

struct SerialNumber
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{.name = "factory.serial"};
    static constexpr Value default_value = 301;
    using Access = solar::parameters::ReadOnly;
    using Storage = solar::parameters::Immutable;
};

struct CalibrationAuthority
{};

struct CalibrationOffset
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "calibration.offset"};
    static constexpr Value default_value = 0;
    using Access = solar::parameters::Privileged<CalibrationAuthority>;
    using Validation = solar::parameters::Range<-100, 100, solar::parameters::Reject>;
};

struct RobotNumber
{
    using Value = std::uint16_t;
    static constexpr solar::parameters::Descriptor descriptor{.name = "robot.number"};
    static constexpr Value default_value = 7;
};

enum class ControlMode : std::uint8_t
{
    Idle,
    Manual,
    Autonomous,
};

struct DriveMode
{
    using Value = ControlMode;
    static constexpr solar::parameters::Descriptor descriptor{.name = "drive.mode"};
    static constexpr Value default_value = Value::Idle;
    using Validation = solar::parameters::OneOf<Value::Idle, Value::Manual, Value::Autonomous>;
};

struct NonnegativeEven
{
    [[nodiscard]] static constexpr solar::Result<int, solar::parameters::ValidationError>
    normalize(int candidate) noexcept
    {
        if (candidate < 0) {
            return solar::fail(solar::parameters::ValidationError{});
        }
        return candidate % 2 == 0 ? candidate : candidate + 1;
    }
};

struct EvenLimit
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "drive.even-limit"};
    static constexpr Value default_value = 2;
    using Validation = solar::parameters::Custom<NonnegativeEven>;
};

struct HookFailureProbe
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "hook.failure-probe"};
    static constexpr Value default_value = 0;
};

struct LoadedGain
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "loaded.gain",
        .stable_id = solar::parameters::Id{0x9001},
    };
    static constexpr Value default_value = 10;
    using Persistence = solar::parameters::Manual<FakeStore>;
};

struct ImmediateGain
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "immediate.gain",
        .stable_id = solar::parameters::Id{0x9002},
    };
    static constexpr Value default_value = 20;
    using Persistence = solar::parameters::Immediate<FakeStore>;
};

struct ManualGain
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "manual.gain",
        .stable_id = solar::parameters::Id{0x9003},
    };
    static constexpr Value default_value = 30;
    using Persistence = solar::parameters::Manual<FakeStore>;
};

struct DeferredGain
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "deferred.gain",
        .stable_id = solar::parameters::Id{0x9004},
    };
    static constexpr Value default_value = 40;
    using Persistence =
        solar::parameters::Deferred<FakeStore, solar::parameters::delay::Milliseconds<20>>;
};

struct MigratedGain
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "migrated.gain",
        .stable_id = solar::parameters::Id{0x9007},
        .version = 2,
    };
    static constexpr Value default_value = 70;
    using Persistence = solar::parameters::Manual<FakeStore>;

    struct Migration
    {
        [[nodiscard]] static solar::Result<Value> migrate(std::uint16_t version,
                                                          std::span<const std::byte> payload)
        {
            if (version != 1) {
                return solar::fail(solar::Status::NotSupported);
            }
            return solar::parameters::ScalarCodec<Value>::decode(payload).transform(
                [](Value old_value) { return old_value + 1; });
        }
    };
};

struct FallbackGain
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "fallback.gain",
        .stable_id = solar::parameters::Id{0x9008},
    };
    static constexpr Value default_value = 80;
    using Persistence = solar::parameters::Manual<FakeStore>;
};

#if defined(CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS)
struct SettingsGain
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "settings.gain",
        .stable_id = solar::parameters::Id{0x9010},
    };
    static constexpr Value default_value = 90;
    using Persistence = solar::parameters::Manual<solar::parameters::settings::ZephyrStore>;
};
#endif

struct TuningGroup;

struct GroupKp
{
    using Value = std::uint16_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "group.kp",
        .stable_id = solar::parameters::Id{0x9005},
    };
    static constexpr Value default_value = 5;
    using Persistence = solar::parameters::Transactional<TuningGroup>;
};

struct GroupKi
{
    using Value = std::uint16_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "group.ki",
        .stable_id = solar::parameters::Id{0x9006},
    };
    static constexpr Value default_value = 6;
    using Persistence = solar::parameters::Transactional<TuningGroup>;
};

struct TuningGroup
{
    using Members = solar::parameters::Members<GroupKp, GroupKi>;
    using Store = FakeStore;
    static constexpr solar::parameters::GroupId stable_id{0x9100};
    static constexpr std::uint16_t version = 1;
};

struct ImmediateGroup;

struct ImmediateGroupFirst
{
    using Value = std::uint16_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "immediate-group.first",
        .stable_id = solar::parameters::Id{0x9011},
    };
    static constexpr Value default_value = 11;
    using Persistence = solar::parameters::Transactional<ImmediateGroup>;
};

struct ImmediateGroupSecond
{
    using Value = std::uint16_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "immediate-group.second",
        .stable_id = solar::parameters::Id{0x9012},
    };
    static constexpr Value default_value = 12;
    using Persistence = solar::parameters::Transactional<ImmediateGroup>;
};

struct ImmediateGroup
{
    using Members = solar::parameters::Members<ImmediateGroupFirst, ImmediateGroupSecond>;
    using Store = FakeStore;
    using Commit = solar::parameters::Immediate<FakeStore>;
    static constexpr solar::parameters::GroupId stable_id{0x9101};
    static constexpr std::uint16_t version = 1;
};

struct DeferredGroup;

struct DeferredGroupFirst
{
    using Value = std::uint16_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "deferred-group.first",
        .stable_id = solar::parameters::Id{0x9013},
    };
    static constexpr Value default_value = 13;
    using Persistence = solar::parameters::Transactional<DeferredGroup>;
};

struct DeferredGroupSecond
{
    using Value = std::uint16_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "deferred-group.second",
        .stable_id = solar::parameters::Id{0x9014},
    };
    static constexpr Value default_value = 14;
    using Persistence = solar::parameters::Transactional<DeferredGroup>;
};

struct DeferredGroup
{
    using Members = solar::parameters::Members<DeferredGroupFirst, DeferredGroupSecond>;
    using Store = FakeStore;
    using Commit =
        solar::parameters::Deferred<FakeStore, solar::parameters::delay::Milliseconds<20>>;
    static constexpr solar::parameters::GroupId stable_id{0x9102};
    static constexpr std::uint16_t version = 1;
};

inline std::atomic_bool init_read_succeeded{};
inline std::atomic_uint change_runs{};
inline std::atomic_uint last_revision{};
inline std::atomic_int last_old_tenths{};
inline std::atomic_int last_new_tenths{};
inline std::atomic_bool isr_read_succeeded{};
inline std::atomic_uint isr_value{};
inline std::atomic_bool transaction_snapshot_succeeded{};
inline std::atomic_uint failed_change_runs{};

struct DriveController
{
    static constexpr solar::component::Descriptor descriptor{.name = "drive-controller"};
    using Parameters = solar::parameters::Parameters<
        DriveKp, DriveKi, AtomicCounter, SerialNumber, CalibrationOffset, DriveMode, EvenLimit,
        HookFailureProbe, LoadedGain, ImmediateGain, ManualGain, DeferredGain, MigratedGain,
        FallbackGain, GroupKp, GroupKi, ImmediateGroupFirst, ImmediateGroupSecond,
        DeferredGroupFirst, DeferredGroupSecond
#if defined(CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS)
        ,
        SettingsGain
#endif
        >;
    using ParameterChanges = solar::parameters::Changes<DriveKp, HookFailureProbe>;

    static solar::Result<void> init();
    static solar::Result<void> start();
    static solar::Status changed(const solar::parameters::Change<DriveKp>& change);
    static solar::Status changed(const solar::parameters::Change<HookFailureProbe>& change);
};

using Blueprint =
    solar::Blueprint<solar::Facilities<DriveController>, solar::Parameters<RobotNumber>,
                     solar::parameters::Configuration<solar::parameters::PersistenceGroups<
                         TuningGroup, ImmediateGroup, DeferredGroup>>>;
using System = solar::System<Blueprint>;

#if defined(CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS)
static_assert(System::ParameterCatalog::size == 22);
#else
static_assert(System::ParameterCatalog::size == 21);
#endif
static_assert(System::ParameterChangeCatalog::size == 2);
static_assert(solar::contains_v<typename System::ParameterFacility, typename System::Builtins>);
static_assert(
    solar::contains_v<System::ParameterFacility, System::Graph::DependenciesOf<DriveController>>);
static_assert(System::ExecutionCatalog::template contains<
              typename System::ParameterFacility::DeferredRegistration>);

} // namespace fixture

namespace failure_fixture
{

struct Store
{
    [[nodiscard]] static solar::Result<void> initialize()
    {
        return {};
    }
    [[nodiscard]] static solar::Result<std::size_t> load(solar::parameters::persistence::Key,
                                                         std::span<std::byte>)
    {
        return solar::fail(solar::Status::ProtocolError);
    }
    [[nodiscard]] static solar::Result<void> save(solar::parameters::persistence::Key,
                                                  std::span<const std::byte>)
    {
        return {};
    }
    [[nodiscard]] static solar::Result<void> erase(solar::parameters::persistence::Key)
    {
        return {};
    }
};

struct StrictParameter
{
    using Value = std::uint32_t;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "strict.parameter",
        .stable_id = solar::parameters::Id{0x9F01},
    };
    static constexpr Value default_value = 1;
    using Persistence = solar::parameters::Manual<Store>;
    using LoadFailure = solar::parameters::load::FailBoot;
};

struct Application;
using System = solar::System<solar::Blueprint<solar::Parameters<StrictParameter>>>;

} // namespace failure_fixture

SOLAR_BIND_SYSTEM(fixture::System);
SOLAR_BIND_SYSTEM_FOR(failure_fixture::Application, failure_fixture::System);

solar::Result<void> fixture::DriveController::init()
{
    auto value = solar::parameters::get<DriveKp>();
    init_read_succeeded.store(value.has_value() && *value == 1.0F, std::memory_order_release);
    if (!value) {
        return solar::fail(value.error().status);
    }
    return {};
}

solar::Result<void> fixture::DriveController::start()
{
    auto first = solar::parameters::set<DriveKp>(2.0F);
    if (!first) {
        return solar::fail(first.error().status);
    }
    auto second = solar::parameters::set<DriveKp>(3.0F);
    if (!second) {
        return solar::fail(second.error().status);
    }
    return {};
}

solar::Status fixture::DriveController::changed(const solar::parameters::Change<DriveKp>& change)
{
    change_runs.fetch_add(1, std::memory_order_release);
    last_revision.store(static_cast<unsigned>(change.revision), std::memory_order_release);
    last_old_tenths.store(static_cast<int>(change.old_value * 10.0F), std::memory_order_release);
    last_new_tenths.store(static_cast<int>(change.new_value * 10.0F), std::memory_order_release);
    if (change.transaction) {
        auto snapshot = solar::parameters::snapshot<DriveKp, DriveKi>();
        transaction_snapshot_succeeded.store(snapshot.has_value() &&
                                                 snapshot->get<DriveKp>() == 4.0F &&
                                                 snapshot->get<DriveKi>() == 0.4F,
                                             std::memory_order_release);
    }
    return solar::Status::Ok;
}

solar::Status fixture::DriveController::changed(const solar::parameters::Change<HookFailureProbe>&)
{
    failed_change_runs.fetch_add(1, std::memory_order_release);
    return solar::Status::Error;
}

namespace
{

struct ConcurrencyContext
{
    std::atomic_bool running{true};
    std::atomic_uint mismatches{};
    std::atomic_uint failures{};
};

struct ContentionContext
{
    solar::kernel::BinarySemaphore ready{};
    solar::kernel::BinarySemaphore release{};
    std::atomic<solar::Status> status{solar::Status::Error};
};

struct SaveContext
{
    std::atomic<solar::Status> status{solar::Status::Error};
};

void hold_parameter_gate(void* raw) noexcept
{
    auto& context = *static_cast<ContentionContext*>(raw);
    auto gate = solar::kernel::unique_lock(fixture::System::ParameterFacility::write_gate);
    context.status.store(gate ? solar::Status::Ok : gate.error(), std::memory_order_release);
    context.ready.give();
    if (gate) {
        (void)context.release.take(solar::kernel::Timeout::after(std::chrono::seconds{1}));
        (void)(*gate).unlock();
    }
}

void hold_persistence_gate(void* raw) noexcept
{
    auto& context = *static_cast<ContentionContext*>(raw);
    auto gate = solar::kernel::unique_lock(fixture::System::ParameterFacility::persistence_gate);
    context.status.store(gate ? solar::Status::Ok : gate.error(), std::memory_order_release);
    context.ready.give();
    if (gate) {
        (void)context.release.take(solar::kernel::Timeout::after(std::chrono::seconds{1}));
        (void)(*gate).unlock();
    }
}

void transaction_writer(void* raw) noexcept
{
    auto& context = *static_cast<ConcurrencyContext*>(raw);
    for (std::uint16_t value = 1; value <= 200; ++value) {
        auto result =
            solar::parameters::set_all(solar::parameters::assign<fixture::GroupKp>(value),
                                       solar::parameters::assign<fixture::GroupKi>(value));
        if (!result) {
            context.failures.fetch_add(1, std::memory_order_release);
        }
    }
    context.running.store(false, std::memory_order_release);
}

void save_manual_gain(void* raw) noexcept
{
    auto& context = *static_cast<SaveContext*>(raw);
    auto saved = solar::parameters::save<fixture::ManualGain>();
    context.status.store(saved ? solar::Status::Ok : saved.error().status,
                         std::memory_order_release);
}

void snapshot_reader(void* raw) noexcept
{
    auto& context = *static_cast<ConcurrencyContext*>(raw);
    while (context.running.load(std::memory_order_acquire)) {
        auto snapshot = solar::parameters::snapshot<fixture::GroupKp, fixture::GroupKi>();
        if (!snapshot || snapshot->get<fixture::GroupKp>() != snapshot->get<fixture::GroupKi>()) {
            context.mismatches.fetch_add(1, std::memory_order_release);
        }
        k_yield();
    }
}

void read_atomic_from_isr(const void*)
{
    auto value = solar::parameters::get_isr<fixture::AtomicCounter>();
    fixture::isr_read_succeeded.store(value.has_value(), std::memory_order_release);
    if (value) {
        fixture::isr_value.store(*value, std::memory_order_release);
    }
}

void* setup()
{
    fixture::FakeStore::reset();
    fixture::FakeStore::seed<fixture::LoadedGain>(55);
    fixture::FakeStore::seed_version<fixture::MigratedGain>(60, 1);
    {
        const std::array<std::byte, 4> corrupt{};
        auto saved = fixture::FakeStore::save(
            solar::parameters::detail::persistence_key<fixture::FallbackGain>(), corrupt);
        zassert_true(saved.has_value());
    }
    {
        std::array<std::byte, sizeof(std::uint16_t) * 2> payload{};
        zassert_true(solar::parameters::ScalarCodec<std::uint16_t>::encode(
                         7, std::span<std::byte>{payload}.first(sizeof(std::uint16_t)))
                         .has_value());
        zassert_true(solar::parameters::ScalarCodec<std::uint16_t>::encode(
                         8, std::span<std::byte>{payload}.subspan(sizeof(std::uint16_t)))
                         .has_value());
        std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES> record{};
        auto encoded = solar::parameters::persistence::detail::encode_record(
            {.kind = solar::parameters::persistence::RecordKind::Group,
             .stable_id = fixture::TuningGroup::stable_id.raw()},
            fixture::TuningGroup::version, payload, record);
        zassert_true(encoded.has_value());
        zassert_true(
            fixture::FakeStore::save({.kind = solar::parameters::persistence::RecordKind::Group,
                                      .stable_id = fixture::TuningGroup::stable_id.raw()},
                                     std::span<const std::byte>{record}.first(*encoded))
                .has_value());
    }
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    return nullptr;
}

void teardown(void*)
{
    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
}

} // namespace

ZTEST_SUITE(solar_parameters, nullptr, setup, nullptr, nullptr, teardown);

ZTEST(solar_parameters, test_facility_is_ready_for_component_init_and_activates_startup_change)
{
    zassert_true(fixture::init_read_succeeded.load(std::memory_order_acquire));
    zassert_equal(fixture::change_runs.load(std::memory_order_acquire), 1);
    zassert_equal(fixture::last_old_tenths.load(std::memory_order_acquire), 10);
    zassert_equal(fixture::last_new_tenths.load(std::memory_order_acquire), 30);
    zassert_equal(fixture::last_revision.load(std::memory_order_acquire), 2);

    const auto change =
        solar::parameters::change_record<fixture::DriveController, fixture::DriveKp>();
    zassert_true(change.has_value());
    zassert_equal(change->deferred, 1);
    zassert_equal(change->coalesced, 1);
    zassert_equal(change->invocations, 1);
    zassert_false(change->pending);
}

ZTEST(solar_parameters, test_validation_equal_suppression_and_records)
{
    auto baseline = solar::parameters::set<fixture::DriveKp>(5.0F);
    zassert_true(baseline.has_value());

    const auto before = fixture::change_runs.load(std::memory_order_acquire);
    auto clamped = solar::parameters::set<fixture::DriveKp>(20.0F);
    zassert_true(clamped.has_value());
    zassert_true(clamped->changed);
    zassert_true(clamped->adjusted);
    zassert_equal(clamped->effective_value, 10.0F);
    zassert_equal(fixture::change_runs.load(std::memory_order_acquire), before + 1);

    auto equal = solar::parameters::set<fixture::DriveKp>(10.0F);
    zassert_true(equal.has_value());
    zassert_false(equal->changed);
    zassert_equal(fixture::change_runs.load(std::memory_order_acquire), before + 1);

    const auto ki_before = solar::parameters::get<fixture::DriveKi>();
    zassert_true(ki_before.has_value());
    auto rejected = solar::parameters::set<fixture::DriveKi>(-1.0F);
    zassert_false(rejected.has_value());
    zassert_equal(rejected.error().reason, solar::parameters::Reason::ValidationRejected);
    const auto ki_after = solar::parameters::get<fixture::DriveKi>();
    zassert_true(ki_after.has_value());
    zassert_equal(*ki_after, *ki_before);

    const auto record = solar::parameters::record<fixture::DriveKp>();
    zassert_true(record.has_value());
    zassert_true(record->updates >= 3);
    zassert_true(record->unchanged >= 1);
    zassert_true(record->validation_adjustments >= 1);
}

ZTEST(solar_parameters, test_one_of_custom_validation_and_post_commit_hook_failure)
{
    auto mode = solar::parameters::set<fixture::DriveMode>(fixture::ControlMode::Autonomous);
    zassert_true(mode.has_value());
    zassert_equal(mode->effective_value, fixture::ControlMode::Autonomous);

    auto invalid_mode =
        solar::parameters::set<fixture::DriveMode>(static_cast<fixture::ControlMode>(99));
    zassert_false(invalid_mode.has_value());
    zassert_equal(invalid_mode.error().reason, solar::parameters::Reason::ValidationRejected);

    auto adjusted = solar::parameters::set<fixture::EvenLimit>(5);
    zassert_true(adjusted.has_value());
    zassert_true(adjusted->adjusted);
    zassert_equal(adjusted->effective_value, 6);

    auto rejected = solar::parameters::set<fixture::EvenLimit>(-1);
    zassert_false(rejected.has_value());
    auto still_adjusted = solar::parameters::get<fixture::EvenLimit>();
    zassert_true(still_adjusted.has_value());
    zassert_equal(*still_adjusted, 6);

    const auto before = fixture::failed_change_runs.load(std::memory_order_acquire);
    auto committed = solar::parameters::set<fixture::HookFailureProbe>(8);
    zassert_true(committed.has_value());
    zassert_true(committed->changed);
    zassert_equal(committed->change_failures, 1);
    zassert_equal(fixture::failed_change_runs.load(std::memory_order_acquire), before + 1);

    auto value = solar::parameters::get<fixture::HookFailureProbe>();
    zassert_true(value.has_value());
    zassert_equal(*value, 8);
    auto record = solar::parameters::record<fixture::HookFailureProbe>();
    zassert_true(record.has_value());
    zassert_equal(record->change_failures, 1);
    zassert_equal(record->last_error.reason, solar::parameters::Reason::ChangeHandlerFailed);
}

ZTEST(solar_parameters, test_snapshot_root_parameter_and_privileged_access)
{
    auto kp_update = solar::parameters::set<fixture::DriveKp>(10.0F);
    zassert_true(kp_update.has_value());

    auto snapshot =
        solar::parameters::snapshot<fixture::DriveKp, fixture::DriveKi, fixture::RobotNumber>();
    zassert_true(snapshot.has_value());
    zassert_equal(snapshot->get<fixture::DriveKp>(), 10.0F);
    zassert_equal(snapshot->get<fixture::DriveKi>(), 0.1F);
    zassert_equal(snapshot->get<fixture::RobotNumber>(), 7);

    fixture::CalibrationAuthority authority;
    auto update = solar::parameters::set<fixture::CalibrationOffset>(17, authority);
    zassert_true(update.has_value());
    zassert_equal(update->effective_value, 17);

    auto descriptor = solar::parameters::descriptor<fixture::DeferredGain>();
    zassert_true(descriptor.has_value());
    zassert_equal(descriptor->get().storage, solar::parameters::StorageKind::Mutex);
    zassert_equal(descriptor->get().persistence, solar::parameters::PersistenceKind::Deferred);
    zassert_equal(descriptor->get().encoded_size, sizeof(std::uint32_t));
    zassert_equal(solar::parameters::descriptors().size(), fixture::System::ParameterCatalog::size);
}

ZTEST(solar_parameters, test_atomic_parameter_supports_thread_and_isr_reads)
{
    auto updated = solar::parameters::set<fixture::AtomicCounter>(42);
    zassert_true(updated.has_value());
    auto read = solar::parameters::try_get<fixture::AtomicCounter>();
    zassert_true(read.has_value());
    zassert_equal(*read, 42);

    fixture::isr_read_succeeded.store(false, std::memory_order_release);
    irq_offload(&read_atomic_from_isr, nullptr);
    zassert_true(fixture::isr_read_succeeded.load(std::memory_order_acquire));
    zassert_equal(fixture::isr_value.load(std::memory_order_acquire), 42);
}

ZTEST(solar_parameters, test_concurrent_transactions_keep_snapshots_coherent_and_try_is_bounded)
{
    zassert_true(solar::parameters::set_all(solar::parameters::assign<fixture::GroupKp>(0),
                                            solar::parameters::assign<fixture::GroupKi>(0))
                     .has_value());
    ConcurrencyContext context{};
    solar::kernel::Thread<4096> reader;
    solar::kernel::Thread<4096> writer;
    zassert_equal(reader.launch(&snapshot_reader, &context,
                                {.priority = solar::kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(writer.launch(&transaction_writer, &context,
                                {.priority = solar::kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(writer.join(solar::kernel::Timeout::after(std::chrono::seconds{1})),
                  solar::Status::Ok);
    zassert_equal(reader.join(solar::kernel::Timeout::after(std::chrono::seconds{1})),
                  solar::Status::Ok);
    zassert_equal(context.failures.load(std::memory_order_acquire), 0);
    zassert_equal(context.mismatches.load(std::memory_order_acquire), 0);

    ContentionContext contention{};
    solar::kernel::Thread<2048> holder;
    zassert_equal(holder.launch(&hold_parameter_gate, &contention,
                                {.priority = solar::kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(
        contention.ready.take(solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
        solar::Status::Ok);
    auto blocked = solar::parameters::try_set<fixture::DriveKp>(2.5F);
    contention.release.give();
    zassert_equal(holder.join(solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
                  solar::Status::Ok);
    zassert_equal(contention.status.load(std::memory_order_acquire), solar::Status::Ok);
    zassert_false(blocked.has_value());
    zassert_equal(blocked.error().reason, solar::parameters::Reason::WouldBlock);
}

ZTEST(solar_parameters, test_reset_uses_normal_commit_and_hook_path)
{
    const auto before = fixture::change_runs.load(std::memory_order_acquire);
    auto reset = solar::parameters::reset<fixture::DriveKp>();
    zassert_true(reset.has_value());
    zassert_true(reset->changed);
    zassert_equal(reset->effective_value, fixture::DriveKp::default_value);
    zassert_equal(fixture::change_runs.load(std::memory_order_acquire), before + 1);
}

ZTEST(solar_parameters, test_persistence_load_immediate_manual_deferred_and_reset)
{
    auto loaded = solar::parameters::get<fixture::LoadedGain>();
    zassert_true(loaded.has_value());
    zassert_equal(*loaded, 55);
    auto loaded_record = solar::parameters::record<fixture::LoadedGain>();
    zassert_true(loaded_record.has_value());
    zassert_equal(loaded_record->load_outcome, solar::parameters::LoadOutcome::Loaded);

    auto immediate = solar::parameters::set<fixture::ImmediateGain>(21);
    zassert_true(immediate.has_value());
    zassert_equal(immediate->persistence, solar::parameters::PersistenceState::Clean);
    auto immediate_stored = fixture::FakeStore::value<fixture::ImmediateGain>();
    zassert_true(immediate_stored.has_value());
    zassert_equal(*immediate_stored, 21);

    fixture::FakeStore::fail_next_save.store(true, std::memory_order_release);
    auto failed = solar::parameters::set<fixture::ImmediateGain>(22);
    zassert_false(failed.has_value());
    auto unchanged = solar::parameters::get<fixture::ImmediateGain>();
    zassert_true(unchanged.has_value());
    zassert_equal(*unchanged, 21);

    auto manual = solar::parameters::set<fixture::ManualGain>(31);
    zassert_true(manual.has_value());
    zassert_equal(manual->persistence, solar::parameters::PersistenceState::Dirty);
    zassert_true(solar::parameters::save<fixture::ManualGain>().has_value());
    auto manual_stored = fixture::FakeStore::value<fixture::ManualGain>();
    zassert_true(manual_stored.has_value());
    zassert_equal(*manual_stored, 31);

    auto first = solar::parameters::set<fixture::DeferredGain>(41);
    zassert_true(first.has_value());
    k_sleep(K_MSEC(5));
    auto second = solar::parameters::set<fixture::DeferredGain>(42);
    zassert_true(second.has_value());
    k_sleep(K_MSEC(35));
    auto deferred_stored = fixture::FakeStore::value<fixture::DeferredGain>();
    zassert_true(deferred_stored.has_value());
    zassert_equal(*deferred_stored, 42);

    fixture::FakeStore::fail_next_save.store(true, std::memory_order_release);
    zassert_true(solar::parameters::set<fixture::DeferredGain>(43).has_value());
    k_sleep(K_MSEC(30));
    auto deferred_failed = solar::parameters::record<fixture::DeferredGain>();
    zassert_true(deferred_failed.has_value());
    zassert_true(deferred_failed->dirty);
    zassert_equal(deferred_failed->persistence, solar::parameters::PersistenceState::Failed);
    zassert_true(deferred_failed->save_failures >= 1);
    zassert_true(solar::parameters::save<fixture::DeferredGain>().has_value());
    auto deferred_retried = fixture::FakeStore::value<fixture::DeferredGain>();
    zassert_true(deferred_retried.has_value());
    zassert_equal(*deferred_retried, 43);

    auto reset = solar::parameters::reset<fixture::ImmediateGain>();
    zassert_true(reset.has_value());
    zassert_true(fixture::FakeStore::value<fixture::ImmediateGain>().error() ==
                 solar::Status::NotFound);
    zassert_true(fixture::FakeStore::erases.load(std::memory_order_acquire) >= 1);

#if defined(CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS)
    auto settings_value = solar::parameters::get<fixture::SettingsGain>();
    zassert_true(settings_value.has_value());
    zassert_equal(*settings_value, fixture::SettingsGain::default_value);
    auto settings_record = solar::parameters::record<fixture::SettingsGain>();
    zassert_true(settings_record.has_value());
    zassert_equal(settings_record->load_outcome, solar::parameters::LoadOutcome::MissingDefault);
#endif
}

ZTEST(solar_parameters, test_newer_revision_stays_dirty_across_older_save_completion)
{
    zassert_true(solar::parameters::set<fixture::ManualGain>(33).has_value());
    fixture::FakeStore::block_next_save.store(true, std::memory_order_release);

    SaveContext context{};
    solar::kernel::Thread<2048> saver;
    zassert_equal(saver.launch(&save_manual_gain, &context,
                               {.priority = solar::kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(fixture::FakeStore::save_started.take(
                      solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
                  solar::Status::Ok);

    zassert_true(solar::parameters::set<fixture::ManualGain>(34).has_value());
    fixture::FakeStore::allow_save.give();
    zassert_equal(saver.join(solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
                  solar::Status::Ok);
    zassert_equal(context.status.load(std::memory_order_acquire), solar::Status::Ok);

    auto pending = solar::parameters::record<fixture::ManualGain>();
    zassert_true(pending.has_value());
    zassert_true(pending->dirty);
    zassert_true(pending->pending_revision > pending->persisted_revision);

    zassert_true(solar::parameters::save<fixture::ManualGain>().has_value());
    auto stored = fixture::FakeStore::value<fixture::ManualGain>();
    zassert_true(stored.has_value());
    zassert_equal(*stored, 34);
}

ZTEST(solar_parameters, test_save_all_and_flush_cover_manual_and_deferred_state)
{
    zassert_true(solar::parameters::set<fixture::LoadedGain>(12).has_value());
    zassert_true(solar::parameters::set<fixture::ManualGain>(35).has_value());
    fixture::FakeStore::fail_save_stable_id.store(fixture::LoadedGain::descriptor.stable_id->raw(),
                                                  std::memory_order_release);
    auto partial = solar::parameters::save_all();
    zassert_false(partial.has_value());
    auto saved_after_failure = fixture::FakeStore::value<fixture::ManualGain>();
    zassert_true(saved_after_failure.has_value());
    zassert_equal(*saved_after_failure, 35);

    zassert_true(solar::parameters::set<fixture::ManualGain>(36).has_value());
    auto saved = solar::parameters::try_save_all();
    zassert_true(saved.has_value());
    zassert_true(saved->visited >= 4);
    zassert_true(saved->saved >= 1);
    auto manual = fixture::FakeStore::value<fixture::ManualGain>();
    zassert_true(manual.has_value());
    zassert_equal(*manual, 36);

    zassert_true(solar::parameters::set<fixture::DeferredGain>(46).has_value());
    ContentionContext contention{};
    solar::kernel::Thread<2048> holder;
    zassert_equal(holder.launch(&hold_persistence_gate, &contention,
                                {.priority = solar::kernel::Priority::preemptive<1>()}),
                  solar::Status::Ok);
    zassert_equal(
        contention.ready.take(solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
        solar::Status::Ok);
    auto blocked = solar::parameters::try_flush();
    zassert_false(blocked.has_value());
    zassert_equal(blocked.error().reason, solar::parameters::Reason::WouldBlock);
    contention.release.give();
    zassert_equal(holder.join(solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
                  solar::Status::Ok);

    auto flushed = solar::parameters::flush();
    zassert_true(flushed.has_value());
    zassert_true(flushed->visited >= 1);
    zassert_true(flushed->saved >= 1);
    auto deferred = fixture::FakeStore::value<fixture::DeferredGain>();
    zassert_true(deferred.has_value());
    zassert_equal(*deferred, 46);
}

ZTEST(solar_parameters, test_flush_times_out_on_in_flight_backend_work)
{
    fixture::FakeStore::block_next_save.store(true, std::memory_order_release);
    zassert_true(solar::parameters::set<fixture::DeferredGain>(47).has_value());
    zassert_equal(fixture::FakeStore::save_started.take(
                      solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
                  solar::Status::Ok);

    auto timed_out = solar::parameters::flush();
    zassert_false(timed_out.has_value());
    zassert_equal(timed_out.error().status, solar::Status::Timeout);
    zassert_equal(timed_out.error().reason, solar::parameters::Reason::PersistenceUnavailable);

    fixture::FakeStore::allow_save.give();
    k_sleep(K_MSEC(5));
    auto record = solar::parameters::record<fixture::DeferredGain>();
    zassert_true(record.has_value());
    zassert_false(record->dirty);
}

ZTEST(solar_parameters, test_boot_fallback_migration_group_load_and_fail_boot_policy)
{
    auto migrated = solar::parameters::get<fixture::MigratedGain>();
    zassert_true(migrated.has_value());
    zassert_equal(*migrated, 61);
    auto migrated_record = solar::parameters::record<fixture::MigratedGain>();
    zassert_true(migrated_record.has_value());
    zassert_equal(migrated_record->load_outcome, solar::parameters::LoadOutcome::Migrated);
    zassert_true(migrated_record->dirty);

    auto fallback = solar::parameters::get<fixture::FallbackGain>();
    zassert_true(fallback.has_value());
    zassert_equal(*fallback, fixture::FallbackGain::default_value);
    auto fallback_record = solar::parameters::record<fixture::FallbackGain>();
    zassert_true(fallback_record.has_value());
    zassert_equal(fallback_record->load_outcome,
                  solar::parameters::LoadOutcome::DefaultAfterFailure);
    zassert_equal(fallback_record->last_error.reason, solar::parameters::Reason::CorruptRecord);

    auto group = solar::parameters::snapshot<fixture::GroupKp, fixture::GroupKi>();
    zassert_true(group.has_value());
    zassert_equal(group->get<fixture::GroupKp>(), 7);
    zassert_equal(group->get<fixture::GroupKi>(), 8);

    auto failed = failure_fixture::System::boot<failure_fixture::Application>();
    zassert_false(failed.has_value());
    zassert_equal(failed.error().status, solar::Status::ProtocolError);
}

ZTEST(solar_parameters, test_transactional_group_uses_one_durable_image)
{
    auto updated = solar::parameters::set_all(solar::parameters::assign<fixture::GroupKp>(15),
                                              solar::parameters::assign<fixture::GroupKi>(16));
    zassert_true(updated.has_value());

    auto kp_record = solar::parameters::record<fixture::GroupKp>();
    auto ki_record = solar::parameters::record<fixture::GroupKi>();
    zassert_true(kp_record.has_value());
    zassert_true(ki_record.has_value());
    zassert_true(kp_record->dirty);
    zassert_true(ki_record->dirty);

    zassert_true(solar::parameters::save<fixture::GroupKp>().has_value());
    std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES> bytes{};
    auto loaded =
        fixture::FakeStore::load({.kind = solar::parameters::persistence::RecordKind::Group,
                                  .stable_id = fixture::TuningGroup::stable_id.raw()},
                                 bytes);
    zassert_true(loaded.has_value());
    auto envelope = solar::parameters::persistence::detail::decode_record(
        std::span<const std::byte>{bytes}.first(*loaded));
    zassert_true(envelope.has_value());
    zassert_equal(envelope->key.kind, solar::parameters::persistence::RecordKind::Group);
    zassert_equal(envelope->payload.size(), sizeof(std::uint16_t) * 2);
    auto kp = solar::parameters::ScalarCodec<std::uint16_t>::decode(
        envelope->payload.first(sizeof(std::uint16_t)));
    auto ki = solar::parameters::ScalarCodec<std::uint16_t>::decode(
        envelope->payload.subspan(sizeof(std::uint16_t), sizeof(std::uint16_t)));
    zassert_true(kp.has_value());
    zassert_true(ki.has_value());
    zassert_equal(*kp, 15);
    zassert_equal(*ki, 16);
}

ZTEST(solar_parameters, test_immediate_and_deferred_groups_each_write_one_image)
{
    const auto read_group = [](solar::parameters::GroupId id) {
        std::array<std::byte, CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES> bytes{};
        auto loaded = fixture::FakeStore::load(
            {.kind = solar::parameters::persistence::RecordKind::Group, .stable_id = id.raw()},
            bytes);
        zassert_true(loaded.has_value());
        auto envelope = solar::parameters::persistence::detail::decode_record(
            std::span<const std::byte>{bytes}.first(*loaded));
        zassert_true(envelope.has_value());
        auto first = solar::parameters::ScalarCodec<std::uint16_t>::decode(
            envelope->payload.first(sizeof(std::uint16_t)));
        auto second = solar::parameters::ScalarCodec<std::uint16_t>::decode(
            envelope->payload.subspan(sizeof(std::uint16_t), sizeof(std::uint16_t)));
        zassert_true(first.has_value());
        zassert_true(second.has_value());
        return std::array<std::uint16_t, 2>{*first, *second};
    };

    const solar::parameters::persistence::Key immediate_key{
        .kind = solar::parameters::persistence::RecordKind::Group,
        .stable_id = fixture::ImmediateGroup::stable_id.raw(),
    };
    const auto immediate_before = fixture::FakeStore::saves(immediate_key);
    auto immediate =
        solar::parameters::set_all(solar::parameters::assign<fixture::ImmediateGroupFirst>(21),
                                   solar::parameters::assign<fixture::ImmediateGroupSecond>(22));
    zassert_true(immediate.has_value());
    zassert_equal(fixture::FakeStore::saves(immediate_key), immediate_before + 1);
    const auto immediate_values = read_group(fixture::ImmediateGroup::stable_id);
    zassert_equal(immediate_values[0], 21);
    zassert_equal(immediate_values[1], 22);

    const solar::parameters::persistence::Key deferred_key{
        .kind = solar::parameters::persistence::RecordKind::Group,
        .stable_id = fixture::DeferredGroup::stable_id.raw(),
    };
    const auto deferred_before = fixture::FakeStore::saves(deferred_key);
    auto deferred =
        solar::parameters::set_all(solar::parameters::assign<fixture::DeferredGroupFirst>(31),
                                   solar::parameters::assign<fixture::DeferredGroupSecond>(32));
    zassert_true(deferred.has_value());
    k_sleep(K_MSEC(35));
    zassert_equal(fixture::FakeStore::saves(deferred_key), deferred_before + 1);
    const auto deferred_values = read_group(fixture::DeferredGroup::stable_id);
    zassert_equal(deferred_values[0], 31);
    zassert_equal(deferred_values[1], 32);
}

ZTEST(solar_parameters, test_transaction_is_all_or_none_and_hooks_see_complete_state)
{
    auto baseline = solar::parameters::set_all(solar::parameters::assign<fixture::DriveKp>(6.0F),
                                               solar::parameters::assign<fixture::DriveKi>(0.6F));
    zassert_true(baseline.has_value());

    auto rejected = solar::parameters::set_all(solar::parameters::assign<fixture::DriveKp>(4.0F),
                                               solar::parameters::assign<fixture::DriveKi>(-1.0F));
    zassert_false(rejected.has_value());
    zassert_equal(rejected.error().reason, solar::parameters::Reason::ValidationRejected);

    auto unchanged = solar::parameters::snapshot<fixture::DriveKp, fixture::DriveKi>();
    zassert_true(unchanged.has_value());
    zassert_equal(unchanged->get<fixture::DriveKp>(), 6.0F);
    zassert_equal(unchanged->get<fixture::DriveKi>(), 0.6F);

    fixture::transaction_snapshot_succeeded.store(false, std::memory_order_release);
    auto committed = solar::parameters::set_all(solar::parameters::assign<fixture::DriveKp>(4.0F),
                                                solar::parameters::assign<fixture::DriveKi>(0.4F));
    zassert_true(committed.has_value());
    zassert_equal(committed->changed, 2);
    zassert_true(fixture::transaction_snapshot_succeeded.load(std::memory_order_acquire));
}
