#pragma once

#include <boost/asio.hpp>

namespace CollabVm::Server {
struct UserVoteData
{
  enum class VoteDecision
  {
    kUndecided,
    kYes,
    kNo
  } last_vote = VoteDecision::kUndecided;
  int voted_amount = 0;
  bool voted_limit = false;
};

template<typename TBase>
class VoteController {
  boost::asio::io_context& io_context_;
  boost::asio::steady_timer vote_timer_;

  enum class VoteState
  {
    kIdle,
    kVoting,
    kCoolingdown
  } vote_state_ = VoteState::kIdle;
  std::uint32_t yes_vote_count_ = 0;
  std::uint32_t no_vote_count_ = 0;

public:
  explicit VoteController(boost::asio::io_context& io_context) :
    io_context_(io_context),
    vote_timer_(io_context)
  {
  }

  [[nodiscard]]
  bool IsCoolingDown() const
  {
    return vote_state_ == VoteState::kCoolingdown;
  }

  [[nodiscard]]
  std::chrono::milliseconds GetTimeRemaining() const
  {
    const auto expiry = vote_timer_.expiry();
    const auto now = std::chrono::steady_clock::now();
    return vote_state_ == VoteState::kVoting && expiry > now
      ? std::chrono::duration_cast<
          std::chrono::milliseconds>(expiry - now)
      : std::chrono::milliseconds::zero();
  }

  [[nodiscard]]
  std::uint32_t GetYesVoteCount() const
  {
    return yes_vote_count_;
  }

  [[nodiscard]]
  std::uint32_t GetNoVoteCount() const
  {
    return no_vote_count_;
  }

  // Returns true when the vote was counted
  bool AddVote(UserVoteData& data, bool voted_yes) {
    const auto votes_enabled = static_cast<TBase&>(*this).GetVotesEnabled();
    if (!votes_enabled) {
      return false;
    }
    switch (vote_state_)
    {
    case VoteState::kIdle:
    {
      if (!voted_yes)
      {
        // First vote must be a yes
        return false;
      }
      // Start a new vote
      vote_state_ = VoteState::kVoting;
      data.last_vote = voted_yes ? UserVoteData::VoteDecision::kYes : UserVoteData::VoteDecision::kNo;
      yes_vote_count_ = 1;
      no_vote_count_ = 0;
      const auto vote_time = static_cast<TBase&>(*this).GetVoteTime();
      vote_timer_.expires_after(vote_time);
      vote_timer_.async_wait(
        [this](const auto ec)
        {
          if (ec || !static_cast<TBase&>(*this).GetVotesEnabled()) {
            vote_state_ = VoteState::kIdle;
            return;
          }

          const auto cooldown_time = static_cast<TBase&>(*this).GetVoteCooldownTime();
          if (cooldown_time.count())
          {
            vote_state_ = VoteState::kCoolingdown;
            vote_timer_.expires_after(cooldown_time);
            vote_timer_.async_wait([this](const auto ec)
              {
                vote_state_ = VoteState::kIdle;
                static_cast<TBase&>(*this).OnVoteIdle();
              });
          }
          else
          {
            vote_state_ = VoteState::kIdle;
          }

          const auto vote_passed = yes_vote_count_ >= no_vote_count_;
          static_cast<TBase&>(*this).OnVoteEnd(vote_passed);
        });

      static_cast<TBase&>(*this).OnVoteStart();
      return true;
    }
    case VoteState::kVoting:
    {
      if (data.voted_limit || ++data.voted_amount >= Common::vote_limit) {
        data.voted_limit = true;
        return false;
      }

      const auto prev_vote = data.last_vote;
      const auto vote_decision = voted_yes ? UserVoteData::VoteDecision::kYes : UserVoteData::VoteDecision::kNo;
      if (prev_vote == vote_decision) {
        // The user's vote hasn't changed
        return false;
      }
      (voted_yes ? yes_vote_count_ : no_vote_count_)++;
      if (prev_vote != UserVoteData::VoteDecision::kUndecided) {
        (voted_yes ? no_vote_count_ : yes_vote_count_)--;
      }
      data.last_vote = vote_decision;
      return true;
    }
    case VoteState::kCoolingdown:
    default:
      break;
    }
    return false;
  }

  bool RemoveVote(UserVoteData& data) {
    if (data.last_vote == UserVoteData::VoteDecision::kUndecided) {
      return false;
    }
    --(data.last_vote == UserVoteData::VoteDecision::kYes
      ? yes_vote_count_
      : no_vote_count_);
    data.last_vote = UserVoteData::VoteDecision::kUndecided;
    return true;
  }

  void StopVote() {
    vote_timer_.cancel();
  }
};

}
