# Lifecycle Engine

The lifecycle engine traverses the validated component dependency graph. It
retains one bounded record per component and one retained report for the last
boot and stop attempt.

Boot initializes in topological order, prepares execution, starts components,
commits the running state, and releases execution activation. Failure initiates
reverse cleanup while preserving the primary failure separately from cleanup
failures.

Stop first quiesces execution and services, then calls component stop/deinit in
reverse dependency order. Timeouts and forced containment are recorded rather
than hidden by destructors.
