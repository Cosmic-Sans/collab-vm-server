#pragma once
#include <boost/asio/io_context.hpp>

template<typename TBase>
struct StrandGuardBase : protected TBase
{
	using TBase::TBase;

  template <typename TCompletionHandler>
  void dispatch(TCompletionHandler&& handler) {
    TBase::strand_.dispatch(
        [ this, handler = std::move(handler) ]() mutable { handler(TBase::obj_); });
  }

  template <typename TCompletionHandler>
  void post(TCompletionHandler&& handler) {
    TBase::strand_.post(
        [ this, handler = std::move(handler) ]() mutable { handler(TBase::obj_); });
  }

  template <typename THandler>
  auto wrap(THandler&& handler) {
    return [ this, handler = std::move(handler) ](auto... args) mutable {
      dispatch([ handler = std::move(handler), args... ](auto&& obj) mutable {
        handler(obj, std::move(args)...);
      });
    };
  }

  bool running_in_this_thread() const {
    return TBase::strand_.running_in_this_thread();
  }
};

template <typename TStrand, typename T>
struct StrandGuard2 {
  template <typename... TArgs>
  explicit StrandGuard2(boost::asio::io_context& io_context, TArgs&&... args)
      : strand_(io_context), obj_(std::forward<TArgs>(args)...) {}

	struct SharedStrandGuard2 {
		template <typename... TArgs>
		explicit SharedStrandGuard2(StrandGuard2& strand, TArgs&&... args)
				: strand_(strand.strand_), obj_(std::forward<TArgs>(args)...) {}

		TStrand& strand_;
		T obj_;
	};
	using SharedStrandGuard = StrandGuardBase<SharedStrandGuard2>;

  TStrand strand_;
  T obj_;
};

template <typename TStrand, typename T>
using StrandGuard = StrandGuardBase<StrandGuard2<TStrand, T>>;

/*
template <typename TStrand, typename T>
struct SharedStrandGuard
{
  template <typename... TArgs>
  explicit StrandGuard(StrandGuard<& shared_guard, TArgs&&... args)
      : strand_(io_context), obj_(std::forward<TArgs>(args)...) {}

};
*/
