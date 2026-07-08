# OTA Manager Shutdown and Lifecycle Fix Plan

This document defines the technical implementation plan for fixing the worker lifecycle and shutdown behavior in `ota_manager`.

The goal is to eliminate unsafe task/resource teardown, make shutdown deterministic, and prepare the component for longer-running transport operations such as HTTPS.

Status: implemented in the current branch.

## Scope

This plan covers only worker lifecycle and shutdown coordination.

It does not cover:

- HTTPS transport changes
- API redesign for certificate handling
- transport policy validation

## Result

The branch now implements the final shutdown contract:

- `cancel_ota()` aborts the active OTA session, notifies the worker with `OTA_CANCEL_BIT`, and preserves worker infrastructure.
- `deinit()` aborts the active session, requests worker stop with `OTA_STOP_BIT`, waits for shutdown completion, and returns `bool`.
- The worker clears `ota_task_handle_`, signals `shutdown_done_`, and self-deletes.
- `deinit()` no longer force-deletes the worker task handle.
- `deinit()` no longer destroys lifecycle resources on timeout.

## Problem Summary

The current implementation mixes two incompatible shutdown models:

1. cooperative worker shutdown through notification bits
2. external forced destruction from `deinit()`

That combination creates race conditions:

- `deinit()` may delete the worker task handle while the worker is still running
- `deinit()` may delete synchronization primitives still visible to the worker
- the worker still touches shared state during its exit path
- a timeout currently degrades into forced cleanup rather than safe failure handling

These issues have been addressed in the current branch. The rest of the document is kept as implementation history and traceability.

## Design Goals

The fix should achieve the following:

1. only one side owns final task deletion
2. no synchronization primitive is deleted before worker exit is confirmed
3. `deinit()` behaves like a coordinated shutdown request plus wait
4. repeated `deinit()` calls remain safe
5. `cancel_ota()` remains lightweight and does not perform teardown
6. the component remains host-testable

## Target Ownership Model

Use a single ownership rule:

- the worker task owns its own final `delete_task(nullptr)` call
- `deinit()` never deletes a live worker task

This keeps termination local to the running task and removes the main race.

## Proposed Behavioral Model

### `init()`

- creates synchronization primitives once
- sets initial state to `IDLE`
- leaves worker absent until first `start_ota()`

### `start_ota()`

- creates the worker only if it does not already exist
- notifies the worker to start OTA
- does not recreate synchronization primitives

### `cancel_ota()`

- aborts the active OTA session
- notifies the worker with `OTA_CANCEL_BIT`
- does not destroy task or synchronization resources
- returns the manager to `IDLE`

### `deinit()`

- aborts the active OTA session
- requests worker stop if a worker exists
- waits for an explicit worker exit signal
- only after confirmed worker exit deletes synchronization primitives
- returns `bool`
- if exit confirmation times out, returns failure and preserves safe state

### worker exit

Before self-deletion, the worker must:

1. stop using OTA session resources
2. clear `ota_task_handle_` while holding the state mutex
3. signal shutdown completion
4. self-delete

## Recommended API Adjustment

The current `void deinit()` contract hides shutdown failure.

Recommended change:

```cpp
bool deinit() override;
```

Reason:

- safe shutdown is now a real operation with success/failure semantics
- timeout should not silently degrade into unsafe cleanup

If API change is deferred, the implementation can still log and preserve safe state on timeout, but returning `bool` is the cleaner model.

## Incremental Implementation Plan

## Step 1: Define the Final Shutdown Contract

### Action

Document and implement the rule that `deinit()` is a stop-and-wait operation, not a force-delete operation.

### Files

- `include/interfaces/i_ota_manager.hpp`
- `include/ota_manager.hpp`
- `README.md` later, after implementation

### Functions

- `IOtaManager::deinit()`
- `OtaManager::deinit()`

### Change

- decide whether `deinit()` returns `bool`
- update API comments to state:
  - it requests worker shutdown
  - it waits for worker completion
  - it only releases resources after worker exit confirmation

### Impact

- possible public API breakage if return type changes
- no runtime behavior change yet

Implemented.

### Build State

- may temporarily break builds if header and implementation are changed in separate commits
- best done in one commit

Implemented.

## Step 2: Remove External Task Deletion From `deinit()`

### Action

Delete the logic path where `deinit()` calls `delete_task(ota_task_handle_)`.

### Files

- `src/ota_manager.cpp`

### Functions

- `OtaManager::deinit()`

### Current problematic logic

- reads `ota_task_handle_`
- sends stop notification
- waits with timeout
- still calls `delete_task(ota_task_handle_)`

### Change

- after stop request, `deinit()` must only wait for worker confirmation
- it must not call `delete_task()` on the worker handle
- it must not clear `ota_task_handle_` proactively

### Impact

- removes the most dangerous race
- requires later steps to guarantee confirmed worker exit before cleanup

Implemented.

### Build State

- should remain buildable if worker exit signaling is already preserved

Implemented.

## Step 3: Make Worker Exit the Single Point of Task Handle Finalization

### Action

Keep all final worker cleanup in the worker exit path.

### Files

- `src/ota_manager.cpp`

### Functions

- `OtaManager::ota_task()`
- `OtaManager::signal_shutdown_done()`

### Change

At worker exit:

1. take `state_mutex_`
2. set `ota_task_handle_ = nullptr`
3. release `state_mutex_`
4. signal shutdown completion
5. self-delete with `delete_task(nullptr)`

### Additional rule

After the worker decides to exit, it must not access shared members that may be deleted by `deinit()` after the shutdown signal.

### Impact

- centralizes task lifecycle finalization
- defines a deterministic handoff point to `deinit()`

Implemented.

### Build State

- buildable

Implemented.

## Step 4: Tighten Resource Deletion Ordering in `deinit()`

### Action

Delete synchronization primitives only after confirmed worker exit.

### Files

- `src/ota_manager.cpp`

### Functions

- `OtaManager::deinit()`

### Change

New order:

1. abort OTA session
2. read whether a worker exists
3. if worker exists, send `OTA_STOP_BIT`
4. wait for shutdown completion
5. verify worker handle is now null
6. delete `shutdown_done_`
7. delete `state_mutex_`
8. set status to `IDLE`

### Important rule

If shutdown completion is not observed, do not delete `shutdown_done_` or `state_mutex_`.

### Impact

- prevents use-after-free on synchronization objects
- requires an explicit timeout policy

Implemented.

### Build State

- buildable

Implemented.

## Step 5: Define Timeout Behavior as Safe Failure, Not Forced Cleanup

### Action

Replace the current timeout behavior with one of the following safe models.

### Preferred model

- `deinit()` returns `false`
- component keeps internal resources allocated
- caller may retry later or escalate

### Acceptable fallback model if API cannot change immediately

- log error
- leave manager in a known non-deinitialized state
- do not delete worker-visible resources

### Files

- `include/interfaces/i_ota_manager.hpp`
- `include/ota_manager.hpp`
- `src/ota_manager.cpp`

### Functions

- `OtaManager::deinit()`

### Impact

- introduces explicit shutdown failure semantics
- improves diagnosability under network stalls

Implemented.

### Build State

- buildable if return type changes are propagated consistently

Implemented.

## Step 6: Clarify `cancel_ota()` vs `deinit()` Responsibilities

### Action

Make cancellation and shutdown distinct lifecycle operations.

### Files

- `src/ota_manager.cpp`
- `include/ota_manager.hpp`
- `include/interfaces/i_ota_manager.hpp`

### Functions

- `OtaManager::cancel_ota()`
- `OtaManager::deinit()`

### Intended roles

- `cancel_ota()`:
  - abort current transfer
  - request state transition back to `IDLE`
  - keep worker and synchronization infrastructure alive

- `deinit()`:
  - terminate the worker
  - release lifecycle resources

### Impact

- reduces semantic overlap
- makes tests easier to reason about

Implemented.

### Build State

- buildable

Implemented.

## Step 7: Make Repeated `deinit()` Calls Explicitly Idempotent

### Action

Define and implement behavior for:

- `deinit()` when no worker exists
- `deinit()` after a successful previous `deinit()`
- `deinit()` while worker shutdown is already in progress

### Files

- `src/ota_manager.cpp`
- `include/ota_manager.hpp`

### Functions

- `OtaManager::deinit()`
- optionally small internal helpers if introduced

### Recommended semantics

- if already deinitialized, return success immediately
- if worker is already gone, release remaining resources safely
- if shutdown is already in progress, do not duplicate teardown

### Impact

- avoids double-cleanup regressions
- simplifies application usage

Partially implemented. The current branch is already safe enough for the next migration step, but further idempotency hardening can still be done later if needed.

### Build State

- buildable

Implemented.

## Step 8: Optional Internal Refactor for Clarity

### Action

Introduce small internal helpers if needed to keep lifecycle code readable.

### Candidate helpers

- `TaskHandle_t get_worker_handle_locked()`
- `bool request_worker_stop_and_wait()`
- `void clear_worker_handle()`
- `bool is_initialized() const`

### Files

- `include/ota_manager.hpp`
- `src/ota_manager.cpp`

### Impact

- no API requirement
- easier reasoning and testing

Implemented.

### Build State

- buildable

Implemented.

## File and Function Change Map

## `include/interfaces/i_ota_manager.hpp`

Potential changes:

- change `deinit()` return type from `void` to `bool`
- update lifecycle contract comments

## `include/ota_manager.hpp`

Potential changes:

- mirror `deinit()` signature change
- update lifecycle comments
- optionally add small private helpers for shutdown flow

## `src/ota_manager.cpp`

Primary implementation changes:

- `OtaManager::deinit()`
  - remove external task deletion
  - enforce safe timeout behavior
  - enforce resource deletion ordering

- `OtaManager::ota_task()`
  - keep worker handle clearing and shutdown signaling as the only final task exit path
  - ensure no shared resource access after shutdown signal

- `OtaManager::cancel_ota()`
  - preserve lightweight cancellation-only semantics

- `OtaManager::signal_shutdown_done()`
  - keep as the explicit worker-to-manager completion signal

## `host_test/test_ota_manager/main/test_ota_manager.cpp`

Tests that must change:

- any test expecting `deinit()` to call `delete_task(fake_task)`
- any test expecting forced cleanup after shutdown timeout

New tests needed:

- `deinit()` does not call external `delete_task(fake_task)`
- worker timeout does not delete mutex/semaphore resources
- repeated `deinit()` remains safe
- `deinit()` with no worker still releases resources

## `host_test/test_ota_manager/main/test_ota_manager_task.cpp`

Tests to add or refine:

- worker receives stop notification and exits cleanly
- shutdown signal is emitted before self-delete
- `cancel_ota()` returns OTA state to `IDLE` without worker destruction

## Intermediate-State Strategy

To avoid long periods with broken tests, implement in this order:

1. update tests to reflect the new intended behavior
2. change `deinit()` behavior
3. refine worker exit path if needed
4. clean up comments and API docs

If changing `deinit()` return type, do header and implementation updates in the same commit as the first test update.

## Test Plan

Minimum host test matrix:

1. `deinit()` without active worker
2. `deinit()` with active worker and successful shutdown signal
3. `deinit()` with worker timeout
4. repeated `deinit()`
5. `cancel_ota()` during active download
6. `start_ota()` after successful re-initialization

Recommended on-target check:

- run a real OTA start and call `deinit()` during an active session to verify no deadlock or crash

## Risks and Tradeoffs

### Returning `bool` from `deinit()`

Pros:

- explicit shutdown success/failure
- no silent unsafe cleanup

Cons:

- API breakage
- examples and tests must be updated

Recommendation:

- take the break now while the component is still evolving

### Keeping timeout support

Pros:

- avoids indefinite blocking

Cons:

- requires a defined failure state

Recommendation:

- keep timeout support, but timeout must never trigger forced resource deletion

## Recommended Implementation Order

1. change the shutdown contract and comments
2. update tests to reject external task deletion
3. remove forced `delete_task(ota_task_handle_)` from `deinit()`
4. enforce safe timeout behavior
5. verify worker exit path is the only finalizer
6. clean up idempotency and documentation

## Approval Boundary

This document is a technical implementation plan only.

It does not apply code changes.

The implementation in this branch has already advanced beyond the original plan.
