#pragma once

#include <boost/asio.hpp>

#include <optional>
#include <deque>

namespace CollabVm::Server {
template<typename TUserPtr>
class TurnController {
  boost::asio::steady_timer turn_timer_;
  typename decltype(turn_timer_)::duration turn_time_;
  std::deque<TUserPtr> turn_queue_;
  std::optional<std::chrono::milliseconds> paused_time_;

  void UpdateCurrentTurn(typename decltype(turn_timer_)::duration time_remaining)
  {
    if (turn_queue_.empty())
    {
      OnCurrentUserChanged(turn_queue_, std::chrono::milliseconds(0));
      return;
    }

    if (!IsPaused())
    {
      turn_timer_.expires_after(time_remaining);
      turn_timer_.async_wait(
        [this, current_user = *turn_queue_.begin()](auto ec)
        {
          if (!ec)
          {
            RemoveUser(current_user);
          }
        });
    }
    OnCurrentUserChanged(
      turn_queue_,
      std::chrono::duration_cast<std::chrono::milliseconds>(time_remaining));
  }

  std::chrono::milliseconds GetTimeRemaining() const
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      turn_timer_.expiry() - std::chrono::steady_clock::now());
  }
public:
  template<typename TExecutionContext>
  explicit TurnController(TExecutionContext& context) :
    turn_timer_(context),
    turn_time_(0)
  {
  }

  class UserTurnData
  {
    using TurnQueuePositionType = typename decltype(turn_queue_)::size_type;
    std::optional<TurnQueuePositionType> turn_queue_position_;
    friend class TurnController;
  };

  auto GetCurrentUser() const
  {
    return turn_queue_.empty()
             ? std::optional<TUserPtr>()
             : turn_queue_.front();
  }

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
      UpdateCurrentTurn(turn_time_);
    }
    else
    {
      OnUserAdded(turn_queue_, GetTimeRemaining());
    }
    return true;
  }

  bool RemoveUser(const TUserPtr& user)
  {
    if (!user->turn_queue_position_.has_value())
    {
      return false;
    }
    const auto user_position =
      turn_queue_.begin() + user->turn_queue_position_.value();
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
      UpdateCurrentTurn(turn_time_);
    }
    else
    {
      OnUserRemoved(turn_queue_, GetTimeRemaining());
    }
    return true;
  }

  template<typename TDuration>
  void SetTurnTime(TDuration time)
  {
    turn_time_ = time;
  }

  void PauseTurnTimer()
  {
    const auto time_remaining = GetTimeRemaining();
    const auto was_running = turn_timer_.cancel();
    paused_time_ = was_running && time_remaining.count() > 0
        ? time_remaining
        : std::chrono::duration_cast<std::chrono::milliseconds>(turn_time_);
    UpdateCurrentTurn(paused_time_.value());
  }

  bool IsPaused() const
  {
    return paused_time_.has_value();
  }

  void ResumeTurnTimer()
  {
    if (IsPaused())
    {
      const auto time_remaining = paused_time_.value();
      paused_time_.reset();
      UpdateCurrentTurn(time_remaining);
    }
  }

  void EndCurrentTurn()
  {
    if (turn_queue_.empty())
    {
      return;
    }
    RemoveUser(turn_queue_.front());
  }

  void Clear()
  {
    turn_timer_.cancel();

    if (!turn_queue_.empty())
    {
      for (auto& user : turn_queue_)
      {
        user->turn_queue_position_.reset();
      }
      turn_queue_.clear();
      OnCurrentUserChanged(turn_queue_, std::chrono::milliseconds(0));
    }
  }

protected:
  virtual void OnCurrentUserChanged(
    const std::deque<TUserPtr>& users, std::chrono::milliseconds time_remaining) = 0;
  virtual void OnUserAdded(
    const std::deque<TUserPtr>& users, std::chrono::milliseconds time_remaining) = 0;
  virtual void OnUserRemoved(
    const std::deque<TUserPtr>& users, std::chrono::milliseconds time_remaining) = 0;
};

}
