[![Build Status](https://travis-ci.org/mikezackles/piecewise.svg?branch=master)](https://travis-ci.org/mikezackles/piecewise)

Piecewise is an opinionated library for structuring code via compile-time
dependency injection and a pattern matching approach to error handling. A
C++14-capable compiler and standard library are required.

Given a generic aggregate type:
```c++
#include <mz/piecewise/arg_forwarder.hpp>

namespace mp = mz::piecewise;

struct A final : public mp::ArgForwarder<A> {
  A(std::string foo_, int thirty_three_)
    : foo{std::move(foo_)}, thirty_three{thirty_three_}
  {}

  std::string foo;
  int thirty_three;
};

struct B final {
  int forty_two;
  std::string bar;
  int seventy_seven;
};

template <typename T, typename U>
class Aggregate final {
public:
  template <typename TArgs, typename UArgs>
  Aggregate(TArgs t_args, UArgs u_args)
    : t{t_args.construct()}, u{u_args.braced_construct()}
  {}

  T t;
  U u;
};
```

an instance can be constructed like this:
```c++
Aggregate<A, B> aggregate{
  A::forward("foo", 33)
, mp::ArgForwarder<B>::forward(42, "bar", 77) // alternate syntax (no inheritance required)
};
```
