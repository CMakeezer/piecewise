[![Travis Status](https://travis-ci.org/mikezackles/piecewise.svg?branch=master)](https://travis-ci.org/mikezackles/piecewise)
[![AppVeyor Status](https://ci.appveyor.com/api/projects/status/github/mikezackles/piecewise?svg=true&branch=master)](https://ci.appveyor.com/project/mikezackles/piecewise)

**Disclaimer**: This project is still in its infancy. It may not work as advertised.

Overview
--

Piecewise is a library for structuring code via compile-time dependency
injection and a pattern matching approach to error handling. In particular, it
aims to facilitate type composition hierarchies with strong invariants. It does
not use exceptions. A C++14-capable compiler and standard library are required.

```c++
Foo::builder(
  Bar::builder("abc", 42)
, Baz::builder("xyzzy")
).construct(
  [](auto builder) {
    // We now know we can construct a valid Foo! Do so using arguments perfectly
    // forwarded to the nested types Bar and Baz
    Foo result = builder.construct();
    // Use it here!
    std::cout << result.bar().get_string(); // "abc"
    std::cout << result.bar().get_int();    // 42
    std::cout << result.baz().get_string(); // "xyzzy"
  }
, [](auto error) {
    std::cerr << "Validation failed: " << decltype(error)::description << std::endl;
  }
);

// Or save it for later as a std::variant
auto saved = Foo::variant<Error1, Error2>(
  Bar::builder("abc", 42)
, Baz::builder("xyzzy")
);
```

Test Matrix
--

This table attempts to document the configurations that are currently tested by
CI. Assume that compiler versions are the latest offered in the listed
environment. Untested configurations may still work.

| Environment | Compiler | Standard Library | C++14 | C++17 | Address Sanitizer |
| --- | --- | --- | --- | --- | --- |
| [Arch Travis](https://github.com/mikkeloscar/arch-travis) | gcc | libstdc++ | yes | yes | no |
| [Arch Travis](https://github.com/mikkeloscar/arch-travis) | clang | libstdc++ | yes | disabled due to [bug](https://bugs.llvm.org//show_bug.cgi?id=33222) | disabled due to [bug1](https://github.com/google/sanitizers/issues/856), [bug2](https://github.com/google/sanitizers/issues/837) |
| Travis OS X | clang | libc++ | yes | no | no |
| AppVeyor Windows | msvc | ms | yes | no | no |

Build
--

Piecewise is header only, so you don't have to build it to use it. That said, if
you're a [meson](http://mesonbuild.com) user you
can
[use piecewise as a subproject](http://mesonbuild.com/Wrap-dependency-system-manual.html) (currently
untested).

If you'd like to build the tests:

* [Install meson](http://mesonbuild.com/Getting-meson.html) with pip or your preferred package manager
* Install the [ninja build system](https://ninja-build.org/)
* `meson build` generates a ninja build inside the build directory (use the `CXX` env var to control the compiler)
* `meson test -C build` will build and run the test suite
* `meson configure -C build` will list all the knobs you can tweak

Design
--

*Builders* are piecewise's fundamental construct. They're essentially callbacks
paired with references to arguments. When the `construct` member function is
called on a `Builder` instance, the captured references are perfectly forwarded
to the callback, along with any arguments to `construct`.
```c++
// This is a pre-factory builder. It's factory callable has not yet run.
Foo::builder(arg1, arg2)
// This construct call invokes the builder's factory callable
.construct(
  // This callback receives a post-factory builder. It's factory callable has
  // succeeded.
  [](auto builder) {
    // ...
  }
, // This callback gets called if the factory callable fails
  [](auto error) {
    // ...
  }
);
```

Since builders are entirely composed of references, you should treat them as
such! If you try to use the builders outside the scope of the original
expression, you're likely to have a bad time. **Don't do this:**
```c++
auto builder = Foo::builder("this is temporary", 2)
// NO! The temporaries are already gone.
builder.construct([](auto) {}, [](auto) {});
```

Builders are designed so that it is impossible to construct a `Foo` instance
without it being valid. But it *is* possible to return values from the success
and error callbacks, as long as their types match.
```c++
int res = Foo::builder(42).construct(
  [](auto) -> int { return 1; }
, [](auto) -> int { return 2; }
);
```

And for completeness, here's an example of a retry pattern:
```c++
bool connected = false;
while (!connected) {
  Connection::builder("https://github.com").construct(
    [&connected] (auto) { connected = true; }
  , [] (auto) { std::cerr << "Connection failed!" << std::endl; }
  );
}
```

*Helpers* help you design a piecewise-enabled type. They are mix-ins implemented
using
[CRTP](https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern). If
you want to keep your implementation details private, they require friendship
with your derived class:

```c++
using mp = mz::piecewise;
class Foo final
  : private mp::ConstructorHelper<Foo>
  , public mp::BuilderHelper<Foo>
  , public mp::VariantHelper<Foo>
{
  friend mp::ConstructorHelper<Foo>;
  friend mp::BuilderHelper<Foo>;
  friend mp::VariantHelper<Foo>;
```

`ConstructorHelper` gives access to the helper functions `Foo::constructor` and
`Foo::braced_constructor`. These return callables that do what you'd think.
Notice that this helper uses private inheritance to avoid exposing `Foo`'s
constructor to client code.

`BuilderHelper` creates the `Foo::builder` function. It requires a user-defined
`Foo::factory` that returns a callable containing the type's creation logic (see
below).

If you're using C++17, `VariantHelper` creates the templated `Foo::variant`
function, which creates a `std::variant` containing either a `Foo` instance or
one of the enumerated error types. Use it normally with `std::visit`.

In piecewise, the heavy lifting for construction moves to the static
`Foo::factory` function. It accepts two callbacks, one for success, and one for
failure, along with the other arguments passed to `Foo::builder`.

```c++
private:
  // constexpr lambdas require C++17 support
  static CONSTEXPR_LAMBDA auto factory() {
    return [](
      auto&& on_success, auto&& on_fail
    , std::string a_string, int an_int
    ) {
      // Validate arguments
      if (a_string.empty()) return on_fail(A::StringEmptyError{});
      if (an_int < 0) return on_fail(A::IntNegativeError{});

      // Now the success callback gets a builder which creates a *valid*
      // instance of `Foo`.
      return on_success(
        // Create a builder
        mp::builder(
          // This is the actual creation callback. It just calls Foo's
          // constructor in the normal way
          constructor()
        , // The arguments to be passed to Foo's constructor
          std::move(a_string), an_int
        )
      );
    };
  }
```

Piecewise also contains a helper function called `multifail`. This important
function handles invoking pre-factory builders in succession until it either
successfully collects all required post-factory builders or the failure callback
has been called. If successful, it invokes the requested constructor. Note that
due to an implementation detail it will pass regular arguments to the
constructor *before* it passes builders. Use this helper if your type contains
nested types that are piecewise-enabled.
```c++
  static CONSTEXPR_LAMBDA auto factory() {
    return [](
      auto&& on_success, auto&& on_fail
    , auto builder1, auto builder2
    , int arg1, std::string arg2
    ) {
      // Error handling specific to this type would happen here
      return mp::multifail(
        Foo::braced_constructor()
      , on_success
      , on_fail
      , mp::builders(
          std::move(builder1), std::move(builder2)
        )
      , mp::arguments(arg1, std::move(arg2))
      );
    };
  }
```

The private constructor is the final step for constructing an object of type
`Foo`, and it is only called if `Foo`'s factory function has succeeded. Notice
that variadic arguments are *not* supported at this phase of construction.
Again, notice that regular arguments are passed before builders.
```c++
  template <typename Builder1, typename Builder2>
  Foo(int arg1, std::string arg2, Builder1 builder1, Builder2 builder2)
    : nested_type1{builder1.construct()}
    , nested_type2{builder2.construct()}
    , an_int{arg1}
    , a_string{std::move(arg2)}
  {}
```

Non-Piecewise Types
--

A compatibility wrapper is provided for passing arguments to nested types that
must coexist with piecewise types.

```
struct B {
  int int_a;
  int int_b;
};

Aggregate<A, A, B>::builder(
  A::builder("abc", -42)
, A::builder("def", 123)
, mp::wrapper<B>(5, 6)
, 3
)
.construct(
// ...
);
```

Dependency Injection
--

You may have noticed that piecewise doesn't need to know anything about how to
construct nested types. This means that you can create templated aggregate
types and use compile-time dependency injection at no extra penalty.

Coverage
--

This report is not automated and almost certain to lag behind the latest
development. But it should at least give you an idea :)
![Coverage Report](coverage.png)

See [here](test/multifail.cpp) for a more complete example.
