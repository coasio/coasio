#ifndef COASIO_COMBINATORS_HPP
#define COASIO_COMBINATORS_HPP

#include <type_traits>

#include "combinators/join.hpp"
#include "combinators/race.hpp"
#include "combinators/select.hpp"
#include "combinators/timeout.hpp"

namespace coasio {

using combinators::join;
using combinators::race;
using combinators::select;
using combinators::timeout;

namespace combinators {

template <typename T, std::size_t I>
concept is_branch_index =
    requires { requires std::remove_cvref_t<T>::index == I; };

} // namespace combinators

} // namespace coasio

#endif // !COASIO_COMBINATORS_HPP