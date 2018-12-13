#include <boost/asio.hpp>

#include <optional>
#include <deque>
#include <chrono>

namespace CollabVm::Server {
template<typename TUserPtr>
class TurnController {
  boost::asio::io_context& io_context_;
	boost::asio::steady_timer turn_timer_;
  decltype(turn_timer_)::duration turn_time_;
	std::deque<TUserPtr> turn_queue_;

  void StartNextTurn()
  {
    turn_timer_.expires_after(turn_time_);
    turn_timer_.async_wait([this, current_user = *turn_queue_.begin()](auto ec)
    {
      if (!ec)
      {
        RemoveUser(current_user);
      }
    });
    OnCurrentUserChanged(turn_queue_);
  }
public:
  template<typename TDuration>
  TurnController(boost::asio::io_context& io_context, TDuration turn_time) :
    io_context_(io_context),
    turn_timer_(io_context)
  {
    SetTurnTime(turn_time);
  }

  virtual ~TurnController() = default;

  class UserTurnData
  {
    using TurnQueuePositionType = typename decltype(turn_queue_)::size_type;
    std::optional<TurnQueuePositionType> turn_queue_position_;
    friend class TurnController;

    bool HasCurrentTurn() const
    {
      return turn_queue_position_ == 0;
    }
  };

  bool RequestTurn(TUserPtr user)
  {
    if (user->turn_queue_position_.has_value())
    {
      return false;
    }
    turn_queue_.emplace_back(user);
    user->turn_queue_position_ = turn_queue_.size() - 1;

    if (user->turn_queue_position_ == 0)
    {
      StartNextTurn();
    }
    else
    {
      OnUserAdded(turn_queue_);
    }
    return true;
  }

  bool RemoveUser(TUserPtr user)
  {
    if (!user->turn_queue_position_.has_value())
    {
      return false;
    }
    const auto user_position =
      turn_queue_.begin() - user->turn_queue_position_.value();
    // Remove the user and update the queue position for all users behind them
    for (auto after_removed = turn_queue_.erase(user_position);
         after_removed != turn_queue_.end(); after_removed++)
    {
      after_removed->get()->turn_queue_position_.value()--;
    }
    const auto old_position = user->turn_queue_position_.value();
    user->turn_queue_position_.reset();
    if (old_position == 0)
    {
      if (turn_queue_.empty())
      {
        OnCurrentUserChanged(turn_queue_);
      }
      else
      {
        StartNextTurn();
      }
    }
    else
    {
      OnUserRemoved(turn_queue_);
    }
    return true;
  }

  template<typename TDuration>
  void SetTurnTime(TDuration time)
  {
    turn_time_ = time;
  }

  void Clear()
  {
    turn_timer_.cancel();
    turn_queue_.clear();
  }

protected:
  virtual void OnCurrentUserChanged(std::deque<TUserPtr>& users) = 0;
  virtual void OnUserAdded(std::deque<TUserPtr>& users) = 0;
  virtual void OnUserRemoved(std::deque<TUserPtr>& users) = 0;
};

}
