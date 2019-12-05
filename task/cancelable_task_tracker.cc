// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/cancelable_task_tracker.h"

#include <stddef.h>

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback_helpers.h"
#include "base/feature_list.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/task_runner.h"
#include "base/threading/sequenced_task_runner_handle.h"

namespace base {

namespace {

void RunOrPostToTaskRunner(scoped_refptr<TaskRunner> task_runner,
                           OnceClosure closure) {
  if (task_runner->RunsTasksInCurrentSequence())
    std::move(closure).Run();
  else
    task_runner->PostTask(FROM_HERE, std::move(closure));
}

// TODO(https://crbug.com/1009795): Remove this once we have established whether
// off-sequence cancelation is worthwhile.
const base::Feature kAllowOffSequenceTaskCancelation{
    "AllowOffSequenceTaskCancelation", base::FEATURE_ENABLED_BY_DEFAULT};

}  // namespace

// static
const CancelableTaskTracker::TaskId CancelableTaskTracker::kBadTaskId = 0;

CancelableTaskTracker::CancelableTaskTracker() = default;

CancelableTaskTracker::~CancelableTaskTracker() {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  TryCancelAll();
}

CancelableTaskTracker::TaskId CancelableTaskTracker::PostTask(
    TaskRunner* task_runner,
    const Location& from_here,
    OnceClosure task) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  return PostTaskAndReply(task_runner, from_here, std::move(task), DoNothing());
}

CancelableTaskTracker::TaskId CancelableTaskTracker::PostTaskAndReply(
    TaskRunner* task_runner,
    const Location& from_here,
    OnceClosure task,
    OnceClosure reply) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  // We need a SequencedTaskRunnerHandle to run |reply|.
  DCHECK(SequencedTaskRunnerHandle::IsSet());

  auto flag = MakeRefCounted<TaskCancellationFlag>();

  TaskId id = next_id_;
  next_id_++;  // int64_t is big enough that we ignore the potential overflow.

  // Unretained(this) is safe because |flag| will have been set to the
  // "canceled" state after |this| is deleted.
  OnceClosure untrack_closure =
      BindOnce(&CancelableTaskTracker::Untrack, Unretained(this), id);
  bool success = task_runner->PostTaskAndReply(
      from_here,
      BindOnce(&RunIfNotCanceled, SequencedTaskRunnerHandle::Get(), flag,
               std::move(task)),
      BindOnce(&RunThenUntrackIfNotCanceled, SequencedTaskRunnerHandle::Get(),
               flag, std::move(reply), std::move(untrack_closure)));

  if (!success)
    return kBadTaskId;

  Track(id, std::move(flag));
  return id;
}

CancelableTaskTracker::TaskId CancelableTaskTracker::NewTrackedTaskId(
    IsCanceledCallback* is_canceled_cb) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  DCHECK(SequencedTaskRunnerHandle::IsSet());

  TaskId id = next_id_;
  next_id_++;  // int64_t is big enough that we ignore the potential overflow.

  auto flag = MakeRefCounted<TaskCancellationFlag>();

  // Unretained(this) is safe because |flag| will have been set to the
  // "canceled" state after |this| is deleted.
  OnceClosure untrack_closure =
      BindOnce(&CancelableTaskTracker::Untrack, Unretained(this), id);

  // Will always run |untrack_closure| on current sequence.
  ScopedClosureRunner untrack_runner(
      BindOnce(&RunOrPostToTaskRunner, SequencedTaskRunnerHandle::Get(),
               BindOnce(&RunIfNotCanceled, SequencedTaskRunnerHandle::Get(),
                        flag, std::move(untrack_closure))));

  *is_canceled_cb = BindRepeating(&IsCanceled, SequencedTaskRunnerHandle::Get(),
                                  flag, std::move(untrack_runner));

  Track(id, std::move(flag));
  return id;
}

void CancelableTaskTracker::TryCancel(TaskId id) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  const auto it = task_flags_.find(id);
  if (it == task_flags_.end()) {
    // Two possibilities:
    //
    //   1. The task has already been untracked.
    //   2. The TaskId is bad or unknown.
    //
    // Since this function is best-effort, it's OK to ignore these.
    return;
  }
  it->second->data.Set();

  // Remove |id| from |task_flags_| immediately, since we have no further
  // use for tracking it. This allows the reply closures (see
  // PostTaskAndReply()) for cancelled tasks to be skipped, since they have
  // no clean-up to perform.
  task_flags_.erase(it);
}

void CancelableTaskTracker::TryCancelAll() {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  for (const auto& it : task_flags_)
    it.second->data.Set();
  task_flags_.clear();
}

bool CancelableTaskTracker::HasTrackedTasks() const {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  return !task_flags_.empty();
}

// static
void CancelableTaskTracker::RunIfNotCanceled(
    const scoped_refptr<TaskRunner>& task_runner,
    const scoped_refptr<TaskCancellationFlag>& flag,
    OnceClosure task) {
  // TODO(https://crbug.com/1009795): Ignore off-sequence cancellation, to
  // evaluate whether it is a worthwhile optimization.
  if (flag->data.IsSet() &&
      (base::FeatureList::IsEnabled(kAllowOffSequenceTaskCancelation) ||
       task_runner->RunsTasksInCurrentSequence())) {
    return;
  }
  std::move(task).Run();
}

// static
void CancelableTaskTracker::RunThenUntrackIfNotCanceled(
    const scoped_refptr<TaskRunner>& task_runner,
    const scoped_refptr<TaskCancellationFlag>& flag,
    OnceClosure task,
    OnceClosure untrack) {
  RunIfNotCanceled(task_runner, flag, std::move(task));
  RunIfNotCanceled(task_runner, flag, std::move(untrack));
}

// static
bool CancelableTaskTracker::IsCanceled(
    const scoped_refptr<TaskRunner>& task_runner,
    const scoped_refptr<TaskCancellationFlag>& flag,
    const ScopedClosureRunner& cleanup_runner) {
  return flag->data.IsSet() &&
         (base::FeatureList::IsEnabled(kAllowOffSequenceTaskCancelation) ||
          task_runner->RunsTasksInCurrentSequence());
}

void CancelableTaskTracker::Track(TaskId id,
                                  scoped_refptr<TaskCancellationFlag> flag) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  bool success = task_flags_.insert(std::make_pair(id, std::move(flag))).second;
  DCHECK(success);
}

void CancelableTaskTracker::Untrack(TaskId id) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  size_t num = task_flags_.erase(id);
  DCHECK_EQ(1u, num);
}

}  // namespace base
