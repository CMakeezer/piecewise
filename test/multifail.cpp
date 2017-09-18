#if defined(_MSC_VER)
  #pragma warning( push )
  #pragma warning( disable : 4244 )
#endif
#include <catch.hpp>
#if defined(_MSC_VER)
  #pragma warning( pop )
#endif
#include <mz/piecewise/arg_forwarder.hpp>
#include <mz/piecewise/constructors.hpp>
#include <mz/piecewise/lambda_overload.hpp>
#include <mz/piecewise/multifail.hpp>
#include <mz/piecewise/factory.hpp>
#include <mz/piecewise/aggregate.hpp>

#include <string>

namespace mp = mz::piecewise;

#if __cplusplus >= 201703L
  #define CONSTEXPR_LAMBDA constexpr
#else
  #define CONSTEXPR_LAMBDA
#endif

namespace {
  // `A` simulates a type that could fail during creation.
  class A final : private mp::Constructors<A> {
  public:
    // Two error types are used to distinguish separate error conditions. Below
    // there are examples of handling both errors generically and of handling
    // them individually.
    struct StringEmptyError {
      // It's a good idea to give errors a static description so that generic
      // error handlers can print it.
      static constexpr auto description = "String is empty";
    };

    struct IntNegativeError {
      static constexpr auto description = "Int is negative";
    };

    std::string const &get_a_string() const { return a_string; }
    int get_an_int() const { return an_int; }

    // The true logic for construction of `A` lives here. Error cases in this
    // function result in calls to the `on_fail` callback, and successful
    // construction results in the forwarding of what we will call a "builder"
    // to the `on_success` callback via `mz::piecewise::forward`. A builder is
    // basically a construction callback paired with a group of perfectly
    // forwarded references to arguments.
    static CONSTEXPR_LAMBDA auto factory() {
      return [](
        auto&& on_success, auto&& on_fail
      , std::string a_string, int an_int
      ) {
        // Validate arguments
        if (a_string.empty()) return on_fail(A::StringEmptyError{});
        if (an_int < 0) return on_fail(A::IntNegativeError{});
        // Now the success callback gets a builder which creates a *valid*
        // instance of `A`. We can now always assume that every instance of `A`
        // satisfies these preconditions. (This doesn't necessarily hold for a
        // moved-from instance of `A`, so it is up to the programmer to avoid
        // such usage.)
        return on_success(
          // Create a builder
          mp::builder(
            // This is the actual creation callback. It just calls `A`'s
            // constructor in the normal way
            constructor()
          , // The arguments to be passed to `A`'s constructor
            std::move(a_string), an_int
          )
        );
      };
    }

  private:
    friend class mp::Constructors<A>;
    // The private constructor is the final step of construction an object of
    // type `A`, and it is only called if `A`'s factory function has succeeded.
    A(std::string a_string_, int an_int_)
      : a_string{std::move(a_string_)}, an_int{an_int_}
    {}

    std::string a_string;
    int an_int;
  };

  // `B` can be constructed normally, so it needs no explicit factory function
  // to be compatible with `mz::piecewise::factory`.
  struct B {
    int int_a;
    int int_b;
  };

  // `Aggregate` demonstrates an aggregate type whose private members can all be
  // injected as template parameters. If any of these members fail to be
  // created, the failure callback will be called, and the aggregate will not be
  // created.
  template <typename T, typename U, typename V>
  class Aggregate final {
  public:
    T const &get_t() const { return t; }
    U const &get_u() const { return u; }
    V const &get_v() const { return v; }

  private:
    // This gives piecewise the ability to call the private constructor.
    friend struct mp::Aggregate<Aggregate<T, U, V>>;
    template <typename TBuilder, typename UBuilder, typename VBuilder>
    Aggregate(TBuilder t_builder, UBuilder u_builder, VBuilder v_builder)
      : t{t_builder.construct()}
      , u{u_builder.construct()}
      , v{v_builder.construct()}
    {}

    T t;
    U u;
    V v;
  };
}

SCENARIO("multifail") {
  GIVEN("a multifail aggregate") {
    bool success = false;
    bool failure1 = false;
    bool failure2 = false;

    WHEN("the first nested construction fails") {
      // Here we specify all the information necessary to construct an
      // `Aggregate<A, A, B>`.
      mp::builder(
        mp::multifactory<Aggregate<A, A, B>>
      , // Here we specify the arguments to construct each of the aggregate's
        // nested types. Notice that in this case, the first call should fail
        // validation.
        mp::builder(A::factory(), "abc", -42)
      , mp::builder(A::factory(), "def", 123)
      , mp::builder(mp::factory<B>, 5, 6)
      )
      // Here we pass one lambda to be invoked if the instance is successfully
      // created and one lambda to be invoked if instantiation fails. In this
      // case, the failure callback will receive an error type as an argument.
      .construct(
        [&](auto) { success = true; }
      , // Here we construct a failure callback out of lambdas that pattern
        // matches based on error type. Notice that order makes no difference.
        mp::handler(
          [&](A::IntNegativeError) { failure2 = true; }
        , [&](A::StringEmptyError) { failure1 = true; }
        )
      );

      THEN("the failure callback is called") {
        REQUIRE(!success);
        REQUIRE(!failure1);
        REQUIRE(failure2);
      }
    }

    WHEN("the second nested construction fails") {
      mp::builder(
        mp::multifactory<Aggregate<A, A, B>>
      , mp::builder(A::factory(), "abc", 42)
      , // Should fail validation
        mp::builder(A::factory(), "", 123)
      , mp::builder(mp::factory<B>, 5, 6)
      ).construct(
        [&](auto) { success = true; }
      , mp::handler(
          [&](A::StringEmptyError) { failure1 = true; }
        , [&](A::IntNegativeError) { failure2 = true; }
        )
      );

      THEN("the failure callback is called") {
        REQUIRE(!success);
        REQUIRE(failure1);
        REQUIRE(!failure2);
      }
    }

    WHEN("construction succeeds") {
      mp::builder(
        mp::multifactory<Aggregate<A, A, B>>
      , mp::builder(A::factory(), "abc", 42)
      , mp::builder(A::factory(), "def", 123)
      , mp::builder(mp::factory<B>, 5, 6)
      ).construct(
        [&](auto builder) {
          success = true;
          auto res = builder.construct();
          THEN("the nested types contain the correct values") {
            REQUIRE(res.get_t().get_a_string() == "abc");
            REQUIRE(res.get_t().get_an_int() == 42);
            REQUIRE(res.get_u().get_a_string() == "def");
            REQUIRE(res.get_u().get_an_int() == 123);
            REQUIRE(res.get_v().int_a == 5);
            REQUIRE(res.get_v().int_b == 6);
          }
        }
      , [&](auto /* e */) {
          // We could print the error generically here:
          // ```
          // std::cerr << "Error: " << decltype(e)::description << std::endl;
          // ```
        }
      );

      THEN("the success callback is called") {
        REQUIRE(success);
      }
    }
  }
}

#undef CONSTEXPR_LAMBDA
