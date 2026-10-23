#ifndef COASIO_COMBINATORS_HPP
#define COASIO_COMBINATORS_HPP

#include <type_traits>

#include "combinators/race.hpp"
#include "combinators/select.hpp"
#include "combinators/timeout.hpp"

namespace coasio::combinators {

template <typename T, std::size_t I>
concept is_branch_index =
    requires { requires std::remove_cvref_t<T>::index == I; };

} // namespace coasio::combinators

#endif // !COASIO_COMBINATORS_HPP