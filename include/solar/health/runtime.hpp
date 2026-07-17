#pragma once

#include <type_traits>

#include "solar/execution/runtime.hpp"
#include "solar/health/facility.hpp"
#include "solar/kernel/diagnostics.hpp"
#include "solar/lifecycle/engine.hpp"
#if defined(CONFIG_SOLAR_REMOTE)
#include "solar/remote/types.hpp"
#endif

namespace solar::health::detail
{

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_HEALTH)

template <typename Component>
concept HasHealthContract = requires { typename Component::Health; };

template <typename Component>
concept HasAssess = HasHealthContract<Component> && requires { Component::Health::assess(); };

template <typename Component>
concept ValidAssess =
    HasAssess<Component> &&
    (std::same_as<decltype(Component::Health::assess()), Result<Assessment>> ||
     std::same_as<decltype(Component::Health::assess()), Result<Assessment, Error>>);

template <typename Component>
concept HasRecover = HasHealthContract<Component> && requires { Component::Health::recover(); };

template <typename Checker>
concept ValidChecker = requires { Checker::check(); } &&
                       (std::same_as<decltype(Checker::check()), Result<Observation>> ||
                        std::same_as<decltype(Checker::check()), Result<Observation, Error>>);

template <typename System, typename Component>
void commit_source(EvidenceState SubjectState::* member, Assessment assessment,
                   EvidenceQuality quality, SourceAvailability availability, bool required,
                   Operation operation, SourceKind source_kind, std::uint32_t source_id = 0,
                   Tick observed_at = kernel::now_ticks()) noexcept
{
    constexpr auto index = subject_index<System, Component>();
    constexpr auto subject =
        System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id;
    (void)with_storage<System>(
        [&](auto& state) {
            auto& source = state.subject_state[index].*member;
            source = {.assessment = assessment,
                      .quality = quality,
                      .availability = availability,
                      .observed_at = observed_at,
                      .stale_after = default_stale_ticks(),
                      .source_kind = source_kind,
                      .source_id = source_id,
                      .required = required,
                      .present = true};
            if (assessment.last_error) {
                assessment.last_error->operation = operation;
                assessment.last_error->subject = subject;
                source.assessment.last_error = assessment.last_error;
            }
            recompute_subject_locked(state, index, observed_at);
            recompute_system_locked(state, observed_at);
        },
        operation);
}

template <typename System, typename Component> void refresh_lifecycle() noexcept
{
    auto source = lifecycle::Engine<System>::template record<Component>();
    if (!source) {
        Assessment assessment{};
        assessment.last_error = normalize_error(source.error(), Operation::Refresh);
        commit_source<System, Component>(&SubjectState::lifecycle, assessment,
                                         EvidenceQuality::Direct, SourceAvailability::Unavailable,
                                         true, Operation::Refresh, SourceKind::Lifecycle);
        return;
    }

    Assessment assessment{.freshness = Freshness::Current};
    switch (source->state) {
    case lifecycle::ComponentState::Running:
        assessment.condition = Condition::Nominal;
        assessment.readiness = Readiness::Ready;
        break;
    case lifecycle::ComponentState::Failed:
        assessment.condition = Condition::Faulted;
        assessment.readiness = Readiness::NotReady;
        assessment.safety = Safety::AtRisk;
        assessment.last_error = normalize_error(source->last_status, Operation::Refresh);
        break;
    case lifecycle::ComponentState::Stopping:
    case lifecycle::ComponentState::Stopped:
    case lifecycle::ComponentState::Deinitializing:
    case lifecycle::ComponentState::Deinitialized:
        assessment.condition = Condition::Nominal;
        assessment.readiness = Readiness::NotReady;
        break;
    case lifecycle::ComponentState::Registered:
    case lifecycle::ComponentState::Initializing:
    case lifecycle::ComponentState::Initialized:
    case lifecycle::ComponentState::Starting:
        assessment.condition = Condition::Unknown;
        assessment.readiness = Readiness::NotReady;
        break;
    }
    commit_source<System, Component>(&SubjectState::lifecycle, assessment, EvidenceQuality::Direct,
                                     SourceAvailability::Available, true, Operation::Refresh,
                                     SourceKind::Lifecycle);
}

template <typename System, typename Component> void refresh_execution() noexcept
{
    using Category = typename System::graph::template Category<Component>;
    if constexpr (std::is_same_v<Category, category::Service>) {
        const auto source = execution::detail::service_state<System, Component>().copy();
        Assessment assessment{.freshness = Freshness::Current};
        if (source.timed_out || source.containment == execution::ContainmentState::Uncontained ||
            (source.exited && !source.exited_after_stop)) {
            assessment.condition = Condition::Faulted;
            assessment.liveness = Liveness::Exited;
            assessment.readiness = Readiness::NotReady;
            assessment.last_error = normalize_error(
                source.run_status == Status::Ok ? Status::UnexpectedExit : source.run_status,
                Operation::Refresh);
        } else if (source.running) {
            assessment.condition = Condition::Nominal;
            assessment.liveness = Liveness::Live;
            assessment.readiness = Readiness::Ready;
        } else if (source.exited_after_stop) {
            assessment.condition = Condition::Nominal;
            assessment.liveness = Liveness::Exited;
            assessment.readiness = Readiness::NotReady;
        }
        commit_source<System, Component>(&SubjectState::execution, assessment,
                                         EvidenceQuality::Direct, SourceAvailability::Available,
                                         true, Operation::Refresh, SourceKind::Execution);
    } else if constexpr (std::is_same_v<Category, category::Executor>) {
        const auto source = execution::detail::executor_state<System, Component>().copy();
        Assessment assessment{.freshness = Freshness::Current};
        if (source.timed_out || source.unexpected_exit ||
            source.containment == execution::ContainmentState::Uncontained) {
            assessment.condition = Condition::Faulted;
            assessment.liveness = source.unexpected_exit ? Liveness::Exited : Liveness::Stalled;
            assessment.readiness = Readiness::NotReady;
            assessment.last_error = normalize_error(
                source.last_status == Status::Ok ? Status::UnexpectedExit : source.last_status,
                Operation::Refresh);
        } else if (source.started && !source.stopped) {
            assessment.condition = Condition::Nominal;
            assessment.liveness = Liveness::Live;
            assessment.readiness = source.accepting ? Readiness::Ready : Readiness::NotReady;
        } else if (source.stopped) {
            assessment.condition = Condition::Nominal;
            assessment.liveness = Liveness::Exited;
            assessment.readiness = Readiness::NotReady;
        }
        commit_source<System, Component>(&SubjectState::execution, assessment,
                                         EvidenceQuality::Direct, SourceAvailability::Available,
                                         true, Operation::Refresh, SourceKind::Execution);
    }
}

#if defined(CONFIG_SOLAR_REMOTE)
template <typename System> void refresh_remote() noexcept
{
    if constexpr (System::RemoteArchitecture::demanded &&
                  contains_v<typename System::RemoteService, typename System::Components>) {
        using Component = typename System::RemoteService;
        const auto service = Component::record();
        Assessment assessment{.condition = service.ready ? Condition::Nominal : Condition::Unknown,
                              .readiness =
                                  service.accepting ? Readiness::Ready : Readiness::NotReady,
                              .freshness = Freshness::Current};
        if (service.dropped_events > 0) {
            assessment.condition = Condition::Degraded;
            assessment.detail = service.dropped_events;
        }

        for_each_type<typename System::RemoteArchitecture::Links>([&]<typename Link> {
            constexpr auto index = static_cast<std::uint16_t>(
                System::RemoteLinkCatalog::template Entry<Link>::local_id.value);
            const auto link = Component::template link_record<Link, index>();
            std::uint32_t dropped{};
            for (const auto& lane : link.lanes) {
                dropped += lane.dropped;
            }
            if (link.session == remote::SessionState::Faulted) {
                assessment.condition = Condition::Faulted;
                assessment.readiness = Readiness::NotReady;
                assessment.detail = index;
            } else if (assessment.condition != Condition::Faulted &&
                       (link.protocol_errors > 0 || link.rejected_frames > 0 || dropped > 0)) {
                assessment.condition = Condition::Degraded;
                assessment.detail = link.protocol_errors + link.rejected_frames + dropped;
            }
        });

        commit_source<System, Component>(&SubjectState::remote, assessment, EvidenceQuality::Direct,
                                         SourceAvailability::Available, true, Operation::Refresh,
                                         SourceKind::Remote);
    }
}
#endif

template <typename System, typename Component>
[[nodiscard]] Result<Receipt, Error> assess_component() noexcept
{
    constexpr auto subject =
        System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id;
    if constexpr (!HasAssess<Component>) {
        return fail<Error>(
            make_error(Status::NotSupported, Reason::Unsupported, Operation::Assess, subject));
    } else {
        static_assert(ValidAssess<Component>,
                      "SOLAR_DIAGNOSTIC_HEALTH_ASSESS_RETURN: Health::assess() must return "
                      "solar::Result<solar::health::Assessment>");
        auto assessed = Component::Health::assess();
        if (!assessed) {
            Assessment unavailable{};
            unavailable.last_error = normalize_error(assessed.error(), Operation::Assess, subject);
            commit_source<System, Component>(&SubjectState::assessment, unavailable,
                                             EvidenceQuality::Reported,
                                             SourceAvailability::Unavailable, true,
                                             Operation::Assess, SourceKind::ComponentAssessment);
            return fail<Error>(*unavailable.last_error);
        }
        const auto before = subject_record<System, Component>();
        commit_source<System, Component>(&SubjectState::assessment, *assessed,
                                         EvidenceQuality::Reported, SourceAvailability::Available,
                                         true, Operation::Assess, SourceKind::ComponentAssessment);
        const auto after = subject_record<System, Component>();
        if (!after) {
            return fail<Error>(after.error());
        }
        return Receipt{.subject = subject,
                       .generation = after->assessment_generation,
                       .transitioned =
                           before && before->transition_count != after->transition_count,
                       .repeated = false};
    }
}

template <typename System, typename Component> void refresh_assessment(Tick now) noexcept
{
    if constexpr (HasAssess<Component>) {
        Tick period{};
        if constexpr (requires { Component::Health::period; }) {
            period = kernel::to_ticks_ceil(Component::Health::period);
        }
        Tick last{};
        (void)with_storage<System>([&](const auto& state) {
            last = state.subject_state[subject_index<System, Component>()].assessment.observed_at;
        });
        if (period == 0 || last == 0 || now - last >= period) {
            (void)assess_component<System, Component>();
        }
    }
}

template <typename System, typename Monitor> [[nodiscard]] Result<void, Error> run_check() noexcept
{
    using Declaration = typename Monitor::DeclarationType;
    using Checker = checker_for_t<Declaration>;
    constexpr auto monitor = System::HealthMonitorCatalog::template Entry<Monitor>::local_id;
    constexpr auto subject = System::HealthMonitorCatalog::template Entry<Monitor>::owner_id;
    if constexpr (!ValidChecker<Checker>) {
        static_assert(ValidChecker<Checker>,
                      "SOLAR_DIAGNOSTIC_HEALTH_CHECK_RETURN: named Health checks must implement "
                      "static solar::Result<solar::health::Observation> check()");
        return fail<Error>(
            make_error(Status::Invalid, Reason::SourceFailed, Operation::Check, subject, monitor));
    } else {
        auto observation = Checker::check();
        if (!observation) {
            Observation unavailable{
                .assessment =
                    Assessment{.last_error = normalize_error(observation.error(), Operation::Check,
                                                             subject, monitor)},
                .quality = EvidenceQuality::Direct,
                .availability = SourceAvailability::Unavailable,
                .required = System::HealthMonitorCatalog::template Entry<Monitor>::local_id.valid(),
            };
            (void)commit_monitor<System, Monitor>(unavailable);
            return fail<Error>(*unavailable.assessment.last_error);
        }
        return commit_monitor<System, Monitor>(*observation);
    }
}

template <typename System, typename Monitor> void refresh_progress(Tick now) noexcept
{
    using Component = typename Monitor::Subject;
    constexpr auto index = subject_index<System, Component>();
    constexpr auto descriptor =
        System::HealthMonitorCatalog::template Entry<Monitor>::local_id.index();
    const auto period = kernel::to_ticks_ceil(std::chrono::nanoseconds{
        System::HealthMonitorCatalog::descriptors()[descriptor].descriptor.period_ns});
    auto& state = storage<System>();
    const auto generation = state.progress[index].generation.load(std::memory_order_acquire);
    const auto observed = state.progress[index].observed_at.load(std::memory_order_acquire);
    Assessment assessment{.freshness = Freshness::Current};
    if (generation == 0) {
        const auto grace =
            kernel::to_ticks_ceil(std::chrono::milliseconds{CONFIG_SOLAR_HEALTH_PROGRESS_GRACE_MS});
        if (state.first_refresh > 0 && now - state.first_refresh > period + grace) {
            assessment.condition = Condition::Faulted;
            assessment.liveness = Liveness::Stalled;
        }
    } else if (now - observed > period * 2) {
        assessment.condition = Condition::Faulted;
        assessment.liveness = Liveness::Stalled;
    } else if (now - observed > period) {
        assessment.condition = Condition::Degraded;
        assessment.liveness = Liveness::Late;
    } else {
        assessment.condition = Condition::Nominal;
        assessment.liveness = Liveness::Live;
    }
    Observation observation{.assessment = assessment,
                            .quality = EvidenceQuality::Inferred,
                            .availability = SourceAvailability::Available,
                            .observed_at = now,
                            .stale_after = period * 2,
                            .required = true};
    (void)commit_monitor<System, Monitor>(observation);
}

template <typename System, typename Monitor> void refresh_stack(Tick now) noexcept
{
    using Component = typename Monitor::Subject;
    using Category = typename System::graph::template Category<Component>;
    constexpr auto descriptor =
        System::HealthMonitorCatalog::template Entry<Monitor>::local_id.index();
    constexpr auto margin =
        System::HealthMonitorCatalog::descriptors()[descriptor].descriptor.stack_margin_bytes;
    kernel::ThreadId thread{};
    std::size_t stack_bytes{};
    if constexpr (std::is_same_v<Category, category::Service>) {
        static_assert(margin <= execution::component_service_policy<Component>::stack_bytes,
                      "SOLAR_DIAGNOSTIC_HEALTH_STACK_MARGIN_EXCEEDS_STACK: StackMargin must not "
                      "exceed the service's configured stack size");
        const auto record = execution::detail::service_state<System, Component>().copy();
        thread = static_cast<kernel::ThreadId>(record.thread);
        stack_bytes = record.stack_bytes;
    } else if constexpr (std::is_same_v<Category, category::Executor>) {
        static_assert(margin <= Component::Policy::stack_bytes,
                      "SOLAR_DIAGNOSTIC_HEALTH_STACK_MARGIN_EXCEEDS_STACK: StackMargin must not "
                      "exceed the executor's configured stack size");
        const auto record = execution::detail::executor_state<System, Component>().copy();
        thread = static_cast<kernel::ThreadId>(record.thread);
        stack_bytes = record.stack_bytes;
    }

    Observation observation{
        .quality = EvidenceQuality::Direct, .observed_at = now, .required = false};
    if (thread == nullptr) {
        observation.availability = SourceAvailability::Unavailable;
        observation.assessment.freshness = Freshness::Unknown;
    } else if (auto usage = kernel::stack_usage(thread, stack_bytes); usage) {
        observation.availability = SourceAvailability::Available;
        observation.assessment.freshness = Freshness::Current;
        observation.assessment.detail = static_cast<std::uint32_t>(usage->unused);
        if (usage->unused < margin) {
            observation.assessment.condition = Condition::Degraded;
            observation.assessment.safety = Safety::AtRisk;
        } else {
            observation.assessment.condition = Condition::Nominal;
            observation.assessment.safety = Safety::Acceptable;
        }
    } else {
        observation.availability = status_of(usage.error()) == Status::NotSupported
                                       ? SourceAvailability::Unsupported
                                       : SourceAvailability::Unavailable;
        observation.assessment.last_error = normalize_error(usage.error(), Operation::Check);
    }
    (void)commit_monitor<System, Monitor>(observation);
}

template <typename System, typename Monitor> void refresh_monitor(Tick now) noexcept
{
    using Declaration = typename Monitor::DeclarationType;
    constexpr auto index = monitor_index<System, Monitor>();
    const auto descriptor = System::HealthMonitorCatalog::descriptors()[index].descriptor;
    MonitorRecord current{};
    (void)with_storage<System>([&](const auto& state) { current = state.monitors[index]; });
    const auto period = descriptor.period_ns > 0
                            ? kernel::to_ticks_ceil(std::chrono::nanoseconds{descriptor.period_ns})
                            : Tick{0};
    if (current.has_observation && period > 0 && now - current.observation.observed_at < period) {
        return;
    }

    if constexpr (IsProgress<Declaration>::value) {
        refresh_progress<System, Monitor>(now);
    } else if constexpr (IsStackMargin<Declaration>::value) {
        refresh_stack<System, Monitor>(now);
    } else if constexpr (std::is_same_v<Declaration, Execution>) {
        using Component = typename Monitor::Subject;
        refresh_execution<System, Component>();
    } else if constexpr (!std::is_same_v<Declaration, Signal>) {
        (void)run_check<System, Monitor>();
    }
}

template <typename System> [[nodiscard]] Result<void, Error> refresh() noexcept
{
    (void)drain_isr<System>();
    const auto now = kernel::now_ticks();
    {
        auto result = with_storage<System>(
            [&](auto& state) {
                if (state.first_refresh == 0) {
                    state.first_refresh = now;
                }
            },
            Operation::Refresh);
        if (!result) {
            return result;
        }
    }

    for_each_type<typename System::Components>([&]<typename Component> {
        refresh_lifecycle<System, Component>();
        refresh_execution<System, Component>();
        refresh_assessment<System, Component>(now);
    });
    for_each_type<typename System::HealthMonitorCatalog::EntryTypes>(
        [&]<typename Entry> { refresh_monitor<System, typename Entry::Declaration>(now); });
#if defined(CONFIG_SOLAR_REMOTE)
    refresh_remote<System>();
#endif

    return with_storage<System>(
        [&](auto& state) {
            for (std::size_t index = 0; index < state.subjects.size(); ++index) {
                recompute_subject_locked(state, index, now);
            }
            recompute_system_locked(state, now);
        },
        Operation::Refresh);
}

#else

template <typename System, typename Component>
[[nodiscard]] Result<Receipt, Error> assess_component() noexcept
{
    return fail<Error>(make_error(Status::NotSupported, Reason::Disabled, Operation::Assess));
}

template <typename System, typename Monitor> [[nodiscard]] Result<void, Error> run_check() noexcept
{
    return fail<Error>(make_error(Status::NotSupported, Reason::Disabled, Operation::Check));
}

template <typename System> [[nodiscard]] Result<void, Error> refresh() noexcept
{
    return fail<Error>(make_error(Status::NotSupported, Reason::Disabled, Operation::Refresh));
}

#endif

} // namespace solar::health::detail
