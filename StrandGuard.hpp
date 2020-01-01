#pragma once
#include <boost/asio/io_context.hpp>

template <typename TStrand, typename T>
struct StrandGuard {
  template <typename... TArgs>
  explicit StrandGuard(boost::asio::io_context& io_context, TArgs&&... args)
      : strand_(io_context), obj_(std::forward<TArgs>(args)...) {}

  static struct {} ConstructWithStrand;
  template <typename... TArgs>
  explicit StrandGuard(boost::asio::io_context& io_context, decltype(ConstructWithStrand), TArgs&&... args)
      : strand_(io_context), obj_(strand_, std::forward<TArgs>(args)...) {}

  template <typename TCompletionHandler>
  void dispatch(TCompletionHandler&& handler) {
    boost::asio::dispatch(strand_,
        [ this, handler = std::forward<TCompletionHandler>(handler) ]() mutable { handler(obj_); });
  }

  template <typename TCompletionHandler>
  void post(TCompletionHandler&& handler) {
    boost::asio::post(strand_,
        [ this, handler = std::forward<TCompletionHandler>(handler) ]() mutable { handler(obj_); });
  }

  template <typename THandler>
  auto wrap(THandler&& handler) {
    return boost::asio::bind_executor(strand_,
      [this, handler = std::forward<THandler>(handler)](auto&&... args) mutable {
        handler(obj_, std::forward<decltype(args)>(args)...);
      });
  }

  bool running_in_this_thread() const {
    return strand_.running_in_this_thread();
  }
 private:
  TStrand strand_;
  T obj_;
};
