#pragma once
#include <memory>
#include <utility>
#include <boost/asio/io_context.hpp>

template <typename TStrand, typename T>
struct StrandGuard {
  template <typename... TArgs>
  explicit StrandGuard(boost::asio::io_context& io_context, TArgs&&... args)
      : strand_(io_context), obj_(std::forward<TArgs>(args)...) {}

  template <typename TCompletionHandler>
  void dispatch(TCompletionHandler&& handler) {
    strand_.dispatch(
        [ this, handler = std::forward<TCompletionHandler>(handler) ]() mutable { handler(obj_); }, std::allocator<TCompletionHandler>());
  }

  template <typename TCompletionHandler>
  void post(TCompletionHandler&& handler) {
    strand_.post(
        [ this, handler = std::forward<TCompletionHandler>(handler) ]() mutable { handler(obj_); }, std::allocator<TCompletionHandler>());
  }

  template <typename THandler>
  auto wrap(THandler&& handler) {
    return [ this, handler = std::forward<THandler>(handler) ](auto&&... args) mutable {
      dispatch([ handler = std::forward<THandler>(handler), args... ](auto&& obj) mutable {
        handler(obj, std::forward<decltype(args)>(args)...);
      });
    };
  }

  bool running_in_this_thread() const {
    return strand_.running_in_this_thread();
  }
 private:
  TStrand strand_;
  T obj_;
};
