// Copyright Take Vos 2020-2021.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <ranges>
#include <concepts>
#include <coroutine>
#include <optional>

namespace tt {

/** A return value for a generator-function.
 * A generator-function is a coroutine which co_yields zero or more values.
 *
 * The generator object returned from the generator-function is used to retrieve
 * the yielded values through an forward-iterator returned by the
 * `begin()` and `end()` member functions.
 *
 * The incrementing the iterator will resume the generator-function until
 * the generator-function co_yields another value.
 */
template<typename T>
class generator {
public:
    using value_type = T;

    class promise_type {
    public:
        generator<value_type> get_return_object()
        {
            return generator{handle_type::from_promise(*this)};
        }

        value_type const &value() {
            return *_value;
        }

        static std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        static std::suspend_always final_suspend() noexcept
        {
            return {};
        }

        std::suspend_always yield_value(value_type const &value) noexcept
        {
            _value = value;
            return {};
        }

        std::suspend_always yield_value(value_type &&value) noexcept
        {
            _value = std::move(value);
            return {};
        }

        void return_void() noexcept {}

        // Disallow co_await in generator coroutines.
        void await_transform() = delete;

        [[noreturn]] static void unhandled_exception()
        {
            throw;
        }

    private:
        std::optional<value_type> _value;
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit generator(handle_type coroutine) : _coroutine(coroutine) {}

    generator() = default;
    ~generator()
    {
        if (_coroutine) {
            _coroutine.destroy();
        }
    }

    generator(const generator &) = delete;
    generator &operator=(const generator &) = delete;

    generator(generator &&other) noexcept : _coroutine{other._coroutine}
    {
        tt_axiom(&other != this);
        other._coroutine = {};
    }

    generator &operator=(generator &&other) noexcept
    {
        tt_return_on_self_assignment(other);
        if (_coroutine) {
            _coroutine.destroy();
        }
        _coroutine = other._coroutine;
        other._coroutine = {};
        return *this;
    }

    /** A forward iterator which iterates through values co_yieled by the generator-function.
     */
    class iterator {
    public:
        explicit iterator(handle_type coroutine) : _coroutine{coroutine} {}

        /** Resume the generator-function.
         */
        iterator &operator++()
        {
            _coroutine.resume();
            return *this;
        }

        /** Retrieve the value co_yielded by the generator-function.
         */
        value_type const &operator*() const
        {
            return _coroutine.promise().value();
        }

        /** Check if the generator-function has finished.
         */
        [[nodiscard]] bool operator==(std::default_sentinel_t) const
        {
            return !_coroutine || _coroutine.done();
        }


    private:
        handle_type _coroutine;
    };

    /** Start the generator-function and return an iterator.
     */
    iterator begin()
    {
        if (_coroutine) {
            _coroutine.resume();
        }
        return iterator{_coroutine};
    }

    /** Return a sentinal for the iterator.
     */
    std::default_sentinel_t end()
    {
        return {};
    }

private:
    handle_type _coroutine;
};

} // namespace tt

