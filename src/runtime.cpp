#include <coasio/runtime.hpp>

void coasio::worker::run() {
  runtime::context_guard guard(rt_);
  worker::context_guard w_guard(this);

  size_t pops_since_global_poll = 0;
  while (!rt_->stop_requested_.load(std::memory_order_acquire)) {
    std::coroutine_handle<> task{};

    if (pops_since_global_poll == global_poll_interval_) {
      pops_since_global_poll = 0;
      task = rt_->try_get_next_task_from_queue();
    }

    if (!task) {
      task = try_pop_local_task();
    }

    if (!task) {
      task = rt_->try_get_next_task_from_queue();
    }

    if (!task) {
      task = try_steal_from_peers();
    }

    if (!task) {
      park();
      continue;
    }

    task.resume();
    pops_since_global_poll++;
  }
}

std::coroutine_handle<> coasio::worker::try_steal_from_peers() {
  auto workers = rt_->get_workers();
  const auto total_workers = workers.size();
  if (total_workers <= 1)
    return {};

  for (size_t i = 0; i < total_workers; ++i) {
    next_victim_index_ = (next_victim_index_ + 1) % total_workers;

    if (workers[next_victim_index_].get() == this) {
      continue;
    }

    bool stolen = false;
    {
      std::lock_guard lock(local_queue_mutex_);
      if (local_queue_size() != 0)
        break;
      worker *victim = workers[next_victim_index_].get();
      stolen = victim->try_steal_into(this);
    }

    if (stolen) {
      return try_pop_local_task();
    }
  }

  return {};
}

void coasio::worker::park() {
  parked_.store(true, std::memory_order_seq_cst);
  {
    std::lock_guard lock(local_queue_mutex_);
    if (local_queue_size() > 0) {
      parked_.store(false, std::memory_order_seq_cst);
      return;
    }
  }
  {
    std::lock_guard lock(rt_->global_tasks_queue_mutex_);
    if (rt_->global_tasks_.size() > 0) {
      parked_.store(false, std::memory_order_seq_cst);
      return;
    }
  }
  std::unique_lock lock(park_mutex_);
  park_cv_.wait(lock, [this] {
    return !parked_.load(std::memory_order_acquire) ||
           rt_->stop_requested_.load(std::memory_order_acquire);
  });
  parked_.store(false, std::memory_order_seq_cst);
}

void coasio::io_worker::run() const {
  // We won't register current_runtime_ to catch
  // any callback that tries to read it from the io thread
  runtime_->io_context_.run();
}

coasio::runtime::runtime(const size_t io_worker_count, size_t worker_count,
                         const size_t blocking_pool_size)
    : work_guard_(asio::make_work_guard(io_context_)),
      blocking_pool_size_(blocking_pool_size) {
  worker_count = worker_count == 0 ? (std::thread::hardware_concurrency() == 0
                                          ? 1
                                          : std::thread::hardware_concurrency())
                                   : worker_count;
  if (worker_count == 0)
    worker_count = 1;

  worker_threads_.reserve(worker_count);
  for (size_t i = 0; i < worker_count; ++i) {
    auto i_worker = std::make_unique<worker>(this);
    worker *i_worker_ptr = i_worker.get();
    worker_threads_.emplace_back([i_worker_ptr]() { i_worker_ptr->run(); },
                                 std::move(i_worker));
  }

  io_worker_threads_.reserve(io_worker_count);
  for (size_t i = 0; i < io_worker_count; ++i) {
    io_worker_threads_.emplace_back([this]() {
      const io_worker w(this);
      w.run();
    });
  }
}

coasio::runtime::~runtime() {
  // Cancel every live root task tree
  {
    std::lock_guard lock(roots_mutex_);
    for (auto *n = roots_head_; n; n = n->next_) {
      if (n->scope_raw_)
        n->scope_raw_->cancel();
    }
  }

  {
    std::unique_lock lock(roots_mutex_);
    roots_drained_cv_.wait(lock, [this] { return live_roots_ == 0; });
  }

  blocking_pool_.reset();

  {
    std::lock_guard lock(global_tasks_queue_mutex_);
    stop_requested_.store(true, std::memory_order_release);
  }

  for (const auto &w : get_workers()) {
    w->unpark();
  }

  work_guard_.reset();
  io_context_.poll();

  for (auto &io_w_t : io_worker_threads_) {
    if (io_w_t.joinable()) {
      io_w_t.join();
    }
  }
  io_worker_threads_.clear();
  for (auto &w_t : worker_threads_ | std::views::keys) {
    if (w_t.joinable()) {
      w_t.join();
    }
  }
  worker_threads_.clear();
};
