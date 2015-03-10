#pragma once

#include <utility>
#include <agency/execution_categories.hpp>
#include <agency/executor_traits.hpp>
#include <agency/detail/index_tuple.hpp>
#include <agency/detail/tuple_utility.hpp>
#include <agency/detail/make_tuple_if_not_nested.hpp>
#include <agency/detail/unwrap_tuple_if_not_nested.hpp>

namespace agency
{


template<class Executor1, class Executor2>
class nested_executor
{
  public:
    using outer_executor_type = Executor1;
    using inner_executor_type = Executor2;

  private:
    using outer_traits = executor_traits<outer_executor_type>;
    using inner_traits = executor_traits<inner_executor_type>;

    using outer_execution_category = typename outer_traits::execution_category;
    using inner_execution_category = typename inner_traits::execution_category;

    using outer_index_type = typename outer_traits::index_type;
    using inner_index_type = typename inner_traits::index_type;

    // XXX move this into index_tuple.hpp?
    static auto index_cat(const outer_index_type& outer_idx, const inner_index_type& inner_idx)
      -> decltype(
           __tu::tuple_cat_apply(
             detail::index_tuple_maker{},
             detail::make_tuple_if_not_nested<outer_execution_category>(outer_idx),
             detail::make_tuple_if_not_nested<inner_execution_category>(inner_idx)
           )
         )
    {
      return __tu::tuple_cat_apply(
        detail::index_tuple_maker{},
        detail::make_tuple_if_not_nested<outer_execution_category>(outer_idx),
        detail::make_tuple_if_not_nested<inner_execution_category>(inner_idx)
      );
    }

    using outer_shape_type = typename outer_traits::shape_type;
    using inner_shape_type = typename inner_traits::shape_type;

  public:
    using execution_category = 
      nested_execution_tag<
        outer_execution_category,
        inner_execution_category
      >;

    // XXX consider adding a public static make_shape() function
    using shape_type = decltype(
      detail::tuple_cat(
        detail::make_tuple_if_not_nested<outer_execution_category>(std::declval<outer_shape_type>()),
        detail::make_tuple_if_not_nested<inner_execution_category>(std::declval<inner_shape_type>())
      )
    );

    using index_type = decltype(
      index_cat(
        std::declval<outer_index_type>(), 
        std::declval<inner_shape_type>()
      )
    );

    template<class T>
    using future = typename outer_traits::template future<T>;

    nested_executor() = default;

    nested_executor(const outer_executor_type& outer_ex,
                    const inner_executor_type& inner_ex)
      : outer_ex_(outer_ex),
        inner_ex_(inner_ex)
    {}

    // XXX executor adaptors like nested_executor need to implement execute to be sure we get the most efficient implementation

    // XXX think we can eliminate this function
    template<class Function>
    future<void> async_execute(Function f, shape_type shape)
    {
      // split the shape into the inner & outer portions
      auto outer_shape = this->outer_shape(shape);
      auto inner_shape = this->inner_shape(shape);

      return outer_traits::async_execute(outer_executor(), [=](outer_index_type outer_idx)
      {
        inner_traits::execute(inner_executor(), [=](inner_index_type inner_idx)
        {
          f(index_cat(outer_idx, inner_idx));
        },
        inner_shape
        );
      },
      outer_shape
      );
    }

  private:
    // this is the functor used by async_execute below
    // it takes the place of a nested, polymorphic lambda function
    template<class Function, class InnerSharedType>
    struct async_execute_outer_functor
    {
      nested_executor& exec;
      Function         f;
      inner_shape_type inner_shape;
      InnerSharedType  inner_shared_arg;

      template<class OuterIndex, class OuterSharedType>
      struct async_execute_inner_functor
      {
        Function f;
        OuterIndex outer_idx;
        OuterSharedType& outer_shared;

        template<class InnerIndex, class T>
        void operator()(const InnerIndex& inner_idx, T& inner_shared)
        {
          // create a 1-tuple of just a reference to the outer shared argument
          auto outer_shared_ref_tuple = detail::tie(outer_shared);

          // if the inner executor isn't nested, we need to tie the inner_shared_ref into a 1-element tuple
          auto inner_shared_ref_tuple = detail::tie_if_not_nested<inner_execution_category>(inner_shared);

          // concatenate the outer reference tuple inner tuple of references
          auto full_tuple_of_references = detail::tuple_cat(outer_shared_ref_tuple, inner_shared_ref_tuple);

          f(index_cat(outer_idx, inner_idx), full_tuple_of_references);
        }
      };

      template<class OuterIndex, class T>
      void operator()(const OuterIndex& outer_idx, T& outer_shared)
      {
        inner_traits::execute(exec.inner_executor(), async_execute_inner_functor<OuterIndex,T>{f, outer_idx, outer_shared}, inner_shape, inner_shared_arg);
      }
    };

  public:
    template<class Function, class Tuple>
    future<void> async_execute(Function f, shape_type shape, Tuple shared_arg_tuple)
    {
      static_assert(std::tuple_size<shape_type>::value == std::tuple_size<Tuple>::value, "Tuple of shared arguments must be the same size as shape_type.");

      // split the shape into the inner & outer portions
      auto outer_shape = this->outer_shape(shape);
      auto inner_shape = this->inner_shape(shape);

      // split the shared argument tuple into the inner & outer portions
      // note these are moves and not copies or references into the original tuple
      auto outer_shared_arg = std::move(__tu::tuple_head(shared_arg_tuple));
      auto inner_shared_arg_tuple = detail::move_tail(shared_arg_tuple);

      // if the inner executor isn't nested, we need to unwrap the tail arguments
      // XXX this could also create a copy -- we should move instead
      auto inner_shared_arg = detail::unwrap_tuple_if_not_nested<inner_execution_category>(inner_shared_arg_tuple);

      return outer_traits::async_execute(
        outer_executor(),
        async_execute_outer_functor<Function,decltype(inner_shared_arg)>{*this, f, inner_shape, inner_shared_arg},
        outer_shape,
        outer_shared_arg
      );

      // XXX upon c++14
      //return outer_traits::async_execute(outer_executor(), [=](const auto& outer_idx, auto& outer_shared)
      //{
      //  inner_traits::execute(inner_executor(), [=,&outer_shared_ref](const auto& inner_idx, auto& inner_shared)
      //  {
      //    // create a 1-tuple of just a reference to the outer shared argument
      //    auto outer_shared_ref_tuple = detail::tie(outer_shared);

      //    // if the inner executor isn't nested, we need to tie the inner_shared_ref into a 1-element tuple
      //    auto inner_shared_ref_tuple = detail::tie_if_not_nested<inner_execution_category>(inner_shared);

      //    // concatenate the outer reference tuple inner tuple of references
      //    auto full_tuple_of_references = detail::tuple_cat(outer_shared_ref_tuple, inner_shared_ref_tuple);

      //    f(index_cat(outer_idx, inner_idx), full_tuple_of_references);
      //  },
      //  inner_shape,
      //  inner_shared_arg
      //  );
      //},
      //outer_shape,
      //outer_shared_arg
      //);
    }

    outer_executor_type& outer_executor()
    {
      return outer_ex_;
    }

    const outer_executor_type& outer_executor() const
    {
      return outer_ex_;
    }

    inner_executor_type& inner_executor()
    {
      return inner_ex_;
    }

    const inner_executor_type& inner_executor() const
    {
      return inner_ex_;
    }

  private:
    static outer_shape_type outer_shape(const shape_type& shape)
    {
      // the outer portion is always the head of the tuple
      return __tu::tuple_head(shape);
    }

    static inner_shape_type inner_shape(const shape_type& shape)
    {
      // the inner portion is the tail of the tuple, but if the 
      // inner executor is not nested, then the tuple needs to be unwrapped
      return detail::unwrap_tuple_if_not_nested<inner_execution_category>(detail::forward_tail(shape));
    }

    outer_executor_type outer_ex_;
    inner_executor_type inner_ex_;
};


} // end agency

