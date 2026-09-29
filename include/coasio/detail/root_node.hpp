#ifndef COASIO_DETAIL_ROOT_NODE_HPP
#define COASIO_DETAIL_ROOT_NODE_HPP

#include "coasio/detail/fwd.hpp"

namespace coasio {
  class cancel_scope;

  namespace detail {
    struct root_node {
      root_node* prev_ = nullptr;
      root_node* next_ = nullptr;
      cancel_scope* scope_raw_ = nullptr; // task<T>::promise_type's shared_ptr keep it alive
      runtime* owner_rt_ = nullptr;
    };

    // Defined in runtime.hpp
    void runtime_unregister_root(runtime *rt, detail::root_node *n) noexcept;
  }
}

#endif // !COASIO_DETAIL_ROOT_NODE_HPP