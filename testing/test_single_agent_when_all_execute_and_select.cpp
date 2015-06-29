#include <agency/future.hpp>
#include <agency/new_executor_traits.hpp>
#include <iostream>
#include <cassert>

#include "test_executors.hpp"

template<class Executor>
void test()
{
  using executor_type = Executor;
  
  size_t n = 1;
  
  auto int_ready   = agency::detail::make_ready_future(0);
  auto void_ready  = agency::detail::make_ready_future();
  
  auto futures = std::make_tuple(std::move(void_ready), std::move(int_ready));
  
  std::mutex mut;
  executor_type exec;
  std::future<int> fut = agency::new_executor_traits<executor_type>::template when_all_execute_and_select<1>(exec, [&mut](int& x)
  {
    mut.lock();
    x += 1;
    mut.unlock();
  },
  std::move(futures));
  
  auto got = fut.get();
  
  assert(got == n);
  assert(exec.valid());
}

int main()
{
  using namespace test_executors;

  test<empty_executor>();
  test<single_agent_when_all_execute_and_select_executor>();
  test<multi_agent_when_all_execute_and_select_executor>();
  test<single_agent_then_execute_executor>();

  test<multi_agent_execute_returning_user_defined_container_executor>();
  test<multi_agent_execute_returning_default_container_executor>();
  test<multi_agent_execute_returning_void_executor>();

  test<multi_agent_async_execute_returning_user_defined_container_executor>();
  test<multi_agent_async_execute_returning_default_container_executor>();
  test<multi_agent_async_execute_returning_void_executor>();

  test<multi_agent_execute_with_shared_inits_returning_user_defined_container_executor>();
  test<multi_agent_execute_with_shared_inits_returning_default_container_executor>();

  std::cout << "OK" << std::endl;

  return 0;
}

