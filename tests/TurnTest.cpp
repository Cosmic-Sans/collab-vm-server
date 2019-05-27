#include <memory>
#include <string>
#include <boost/asio.hpp>
#include <utility>
#include "TurnController.hpp"

class TestUser;
using TestUserPtr = std::shared_ptr<TestUser>;
using UserTurnController = CollabVm::Server::TurnController<TestUserPtr>;
class TestUser final : public UserTurnController::UserTurnData
{
  std::string name_;
public:
  TestUser(std::string&& name) : name_(std::move(name)){}
};

class TestUserTurnController final : public UserTurnController {
  using UserTurnController::UserTurnController;
  void OnCurrentUserChanged(
    std::deque<TestUserPtr>& users,
    std::chrono::milliseconds time_remaining) override
  {
  }
  void OnUserAdded(
    std::deque<TestUserPtr>& users,
    std::chrono::milliseconds time_remaining) override
  {
  }
  void OnUserRemoved(
    std::deque<TestUserPtr>& users,
    std::chrono::milliseconds time_remaining) override
  {
  }
};

int main(int argc, char** args)
{
  auto io_context = boost::asio::io_context();
  auto turn_controller = TestUserTurnController(io_context);
  turn_controller.SetTurnTime(std::chrono::seconds(5));

  const auto user1 = std::make_shared<TestUser>("user1");
  turn_controller.RequestTurn(user1);

  const auto current_user = turn_controller.GetCurrentUser();

  const auto user2 = std::make_shared<TestUser>("user2");
  turn_controller.RequestTurn(user2);

  const auto user3 = std::make_shared<TestUser>("user3");
  turn_controller.RequestTurn(user3);

  turn_controller.RemoveUser(user1);
  turn_controller.Clear();

  io_context.run();

  return 0;
}
