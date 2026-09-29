#include <coasio/runtime.hpp>

#include <print>

void coasio::worker::run() const {
  runtime::context_guard guard(runtime_);

  while (!runtime_->stop_requested_.load(std::memory_order_acquire)) {
    if (auto task = runtime_->get_next_task_from_queue()) {
      if (*task) {
        task->resume();
      }
    }
  }
}

void coasio::io_worker::run() const {
  runtime_->io_context_.run();
}

coasio::runtime::runtime() : work_guard_(asio::make_work_guard(io_context_)) {
  auto num_threads = std::thread::hardware_concurrency();
  if (num_threads == 0)
    num_threads = 1;

  worker_threads_.reserve(num_threads);
  for (unsigned int i = 0; i < num_threads; ++i) {
    worker_threads_.emplace_back([this]() {
      const worker w(this);
      w.run();
    });
  }

  unsigned int num_io_threads = 2; // TODO: configurable
  io_worker_threads_.reserve(num_io_threads);
  for (unsigned int i = 0; i < num_io_threads; ++i) {
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
      if (n->scope_raw_) n->scope_raw_->cancel();
    }
  }

  {
    std::unique_lock lock(roots_mutex_);
    roots_drained_cv_.wait(lock, [this] { return live_roots_ == 0; });
  }

  {
      std::lock_guard lock(global_tasks_queue_mutex_);
      stop_requested_.store(true, std::memory_order_release);
  }

  global_tasks_queue_cv_.notify_all();

  work_guard_.reset();
  io_context_.poll();

  io_worker_threads_.clear();
  worker_threads_.clear();
};
