/*
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#ifndef SRSLTE_TASK_H
#define SRSLTE_TASK_H

#include <cstddef>
#include <functional>
#include <type_traits>

namespace srslte {

constexpr size_t default_buffer_size = 256;

template <class Signature, size_t Capacity = default_buffer_size, size_t Alignment = alignof(std::max_align_t)>
class inplace_task;

namespace task_details {

template <typename R, typename... Args>
struct oper_table_t {
  using call_oper_t = R (*)(void* src, Args&&... args);
  using move_oper_t = void (*)(void* src, void* dest);
  //  using copy_oper_t = void (*)(void* src, void* dest);
  using dtor_oper_t = void (*)(void* src);

  static oper_table_t* get_empty() noexcept
  {
    static oper_table_t t;
    t.call = [](void* src, Args&&... args) -> R { throw std::bad_function_call(); };
    t.move = [](void*, void*) {};
    //    t.copy = [](void*, void*) {};
    t.dtor = [](void*) {};
    return &t;
  }

  template <typename Func>
  static oper_table_t* get() noexcept
  {
    static oper_table_t t{};
    t.call = [](void* src, Args&&... args) -> R { return (*static_cast<Func*>(src))(std::forward<Args>(args)...); };
    t.move = [](void* src, void* dest) -> void {
      ::new (dest) Func{std::move(*static_cast<Func*>(src))};
      static_cast<Func*>(src)->~Func();
    };
    //    t.copy = [](void* src, void* dest) -> void { ::new (dest) Func{*static_cast<Func*>(src)}; };
    t.dtor = [](void* src) -> void { static_cast<Func*>(src)->~Func(); };
    return &t;
  }

  oper_table_t(const oper_table_t&) = delete;
  oper_table_t(oper_table_t&&)      = delete;
  oper_table_t& operator=(const oper_table_t&) = delete;
  oper_table_t& operator=(oper_table_t&&) = delete;
  ~oper_table_t()                         = default;

  call_oper_t call;
  move_oper_t move;
  //  copy_oper_t copy;
  dtor_oper_t dtor;

  static oper_table_t<R, Args...>* empty_oper;

private:
  oper_table_t() = default;
};

template <class>
struct is_inplace_task : std::false_type {};
template <class Sig, size_t Cap, size_t Align>
struct is_inplace_task<inplace_task<Sig, Cap, Align> > : std::true_type {};

template <typename R, typename... Args>
oper_table_t<R, Args...>* oper_table_t<R, Args...>::empty_oper = oper_table_t<R, Args...>::get_empty();

} // namespace task_details

template <class R, class... Args, size_t Capacity, size_t Alignment>
class inplace_task<R(Args...), Capacity, Alignment>
{
  using storage_t    = typename std::aligned_storage<Capacity, Alignment>::type;
  using oper_table_t = task_details::oper_table_t<R, Args...>;

public:
  inplace_task() noexcept { oper_ptr = oper_table_t::empty_oper; }

  template <typename T,
            typename FunT = typename std::decay<T>::type,
            typename      = typename std::enable_if<not task_details::is_inplace_task<FunT>::value>::type>
  inplace_task(T&& function)
  {
    static_assert(sizeof(FunT) <= sizeof(buffer), "inplace_task cannot store object with given size.\n");
    static_assert(Alignment % alignof(FunT) == 0, "inplace_task cannot store object with given alignment.\n");

    ::new (&buffer) FunT{std::forward<T>(function)};
    oper_ptr = oper_table_t::template get<T>();
  }

  inplace_task(inplace_task&& other) noexcept
  {
    oper_ptr       = other.oper_ptr;
    other.oper_ptr = oper_table_t::empty_oper;
    oper_ptr->move(&other.buffer, &buffer);
  }

  //  inplace_task(const inplace_task& other) noexcept
  //  {
  //    oper_ptr = other.oper_ptr;
  //    oper_ptr->copy(&other.buffer, &buffer);
  //  }

  ~inplace_task() { oper_ptr->dtor(&buffer); }

  inplace_task& operator=(inplace_task&& other) noexcept
  {
    oper_ptr->dtor(&buffer);
    oper_ptr       = other.oper_ptr;
    other.oper_ptr = oper_table_t::empty_oper;
    oper_ptr->move(&other.buffer, &buffer);
    return *this;
  }

  //  inplace_task& operator=(const inplace_task& other) noexcept
  //  {
  //    if (this != &other) {
  //      oper_ptr->dtor(&buffer);
  //      oper_ptr = other.oper_ptr;
  //      oper_ptr->copy(&other.buffer, &buffer);
  //    }
  //    return *this;
  //  }

  R operator()(Args&&... args) { return oper_ptr->call(&buffer, std::forward<Args>(args)...); }

  bool is_empty() const { return oper_ptr == oper_table_t::empty_oper; }

  void swap(inplace_task& other) noexcept
  {
    if (this == &other)
      return;

    storage_t tmp;
    oper_ptr->move(&buffer, &tmp);
    other.oper_ptr->move(&other.buffer, &buffer);
    oper_ptr->move(&tmp, &other.buffer);
    std::swap(oper_ptr, other.oper_ptr);
  }

private:
  storage_t     buffer;
  oper_table_t* oper_ptr;
};

} // namespace srslte

#endif // SRSLTE_TASK_H