// Copyright Take Vos 2020.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "notifier.hpp"
#include "hires_utc_clock.hpp"
#include "cast.hpp"
#include "algorithm.hpp"
#include "type_traits.hpp"
#include "concepts.hpp"
#include <memory>
#include <functional>
#include <algorithm>
#include <type_traits>
#include <vector>
#include <atomic>

namespace tt {
template<typename T>
class observable;

namespace detail {

/** Observable abstract base class.
 * Objects of the observable_base class will notify listeners through
 * callbacks of changes of its value.
 *
 * This class does not hold the value itself, concrete subclasses will
 * either hold the value or calculate the value on demand. In many cases
 * concrete subclasses may be sub-expressions of other observable objects.
 *
 * This object will also track the time when the value was last modified
 * so that the value can be animated. Usefull when displaying the value
 * as an animated graphic element. For calculating inbetween values
 * it will keep track of the previous value.
 *
 */
template<typename T>
class observable_base {
public:
    using value_type = T;
    using notifier_type = notifier<void()>;
    using callback_type = typename notifier_type::callback_type;
    using callback_ptr_type = typename notifier_type::callback_ptr_type;

    virtual ~observable_base()
    {
        replace_with(_listeners, nullptr);
    }

    observable_base(observable_base const &) = delete;
    observable_base(observable_base &&) = delete;
    observable_base &operator=(observable_base const &) = delete;
    observable_base &operator=(observable_base &&) = delete;

    /** Constructor.
     */
    observable_base(observable<value_type> *owner) noexcept : _owner(owner), _listeners() {}

    /** Get the current value.
     * The value is often calculated directly from the cached values retrieved from
     * notifications down the chain.
     */
    [[nodiscard]] virtual value_type load() const noexcept = 0;

    /** Set the value.
     * The value is often not directly stored, but instead forwarded up
     * the chain of observables. And then let the notifications flowing backward
     * updated the cached values so that loads() will be quick.
     *
     * @param new_value The new value
     * @return true if the value was different from before.
     */
    virtual bool store(value_type const &new_value) noexcept = 0;

    void notify() noexcept
    {
        _mutex.lock();
        ttlet listeners = _listeners;
        ttlet owner = _owner;
        _mutex.unlock();

        for (ttlet &listener : listeners) {
            tt_axiom(listener);
            listener->notify();
        }
        tt_axiom(owner);
        owner->notify();
    }

    /** Replace the operands.
     */
    virtual void replace_operand(observable_base *from, observable_base *to) noexcept {}

    /** Let other take over the listeners and owner.
     */
    void replace_with(observable_base *other) noexcept
    {
        _mutex.lock();
        ttlet listeners = _listeners;
        _mutex.unlock();
        return replace_with(std::move(listeners), other);
    }

    void add_listener(observable_base *listener)
    {
        tt_axiom(listener);
        ttlet lock = std::scoped_lock(_mutex);
        _listeners.push_back(listener);
    }

    void remove_listener(observable_base *listener)
    {
        tt_axiom(listener);
        ttlet lock = std::scoped_lock(_mutex);
        std::erase(_listeners, listener);
    }

protected:
    mutable unfair_mutex _mutex;
    observable<value_type> *_owner;
    std::vector<observable_base *> _listeners;

private:
    void replace_with(std::vector<observable_base *> listeners, observable_base *other) noexcept
    {
        if (other) {
            other->_owner = std::exchange(_owner, nullptr);
        }
        for (auto listener : listeners) {
            listener->replace_operand(this, other);
        }
    }
};

template<typename T>
class observable_value final : public observable_base<T> {
public:
    using super = observable_base<T>;
    using value_type = typename super::value_type;

    static constexpr bool is_atomic = may_be_atomic_v<value_type>;
    using atomic_type = std::conditional_t<is_atomic, std::atomic<value_type>, value_type>;

    observable_value(observable<value_type> *owner) noexcept : super(owner), _value() {}

    observable_value(observable<value_type> *owner, value_type const &value) noexcept : super(owner), _value(value) {}

    value_type load() const noexcept override
    {
        return _load();
    }

    bool store(value_type const &new_value) noexcept override
    {
        ttlet changed = _store(new_value);
        if (changed) {
            this->notify();
        }
        return changed;
    }

private:
    atomic_type _value;

    value_type _load() const noexcept requires(is_atomic)
    {
        return _value.load();
    }

    bool _store(value_type const &new_value) noexcept requires(is_atomic)
    {
        if constexpr (std::equality_comparable<T>) {
            ttlet old_value = _value.exchange(new_value);
            if (old_value != new_value) {
                return true;
            } else {
                return false;
            }

        } else {
            _value.store(new_value);
            return true;
        }
    }

    value_type _load() const noexcept requires(not is_atomic)
    {
        ttlet lock = std::scoped_lock(this->_mutex);
        return _value;
    }

    bool _store(value_type const &new_value) noexcept requires(not is_atomic)
    {
        ttlet lock = std::scoped_lock(this->_mutex);
        if constexpr (std::equality_comparable<T>) {
            return std::exchange(_value, new_value) != new_value;

        } else {
            _value = new_value;
            return true;
        }
    }
};

template<typename T>
class observable_chain final : public observable_base<T> {
public:
    using super = observable_base<T>;
    using base = observable_base<T>;
    using value_type = typename super::value_type;

    ~observable_chain()
    {
        if (auto operand = _operand.load()) {
            operand->remove_listener(this);
        }
    }

    observable_chain(observable<value_type> *owner, base *operand) noexcept : super(owner), _operand(operand)
    {
        if (auto operand_ = _operand.load()) {
            operand_->add_listener(this);
        } else {
            tt_no_default();
        }
    }

    virtual value_type load() const noexcept override
    {
        if (auto operand = _operand.load()) {
            return operand->load();
        } else {
            tt_no_default();
        }
    }

    virtual bool store(value_type const &new_value) noexcept override
    {
        if (auto operand = _operand.load()) {
            return operand->store(new_value);
        } else {
            tt_no_default();
        }
    }

    /** Replace the operand.
     * @param to The observer to replace the operand with. Maybe be nullptr.
     */
    void replace_operand(base *from, base *to) noexcept override
    {
        tt_axiom(from);
        if (_operand.compare_exchange_strong(from, to)) {
            from->remove_listener(this);
            if (to) {
                to->add_listener(this);
                this->notify();
            }
        }
    }

private:
    std::atomic<base *> _operand;
};

} // namespace detail

/** An observable value.
 *
 * An observable-value will notify listeners when a value changes. An
 * observable-value can also observe another observable-value.
 *
 * For widgets this allows changes to values to be reflected on the screen in
 * multiple places. Or values to be written automatically to a configuration
 * file.
 */
template<typename T>
class observable {
public:
    using value_type = T;
    using notifier_type = notifier<void()>;
    using callback_type = typename notifier_type::callback_type;
    using callback_ptr_type = typename notifier_type::callback_ptr_type;

    ~observable()
    {
        tt_axiom(pimpl);
    }

    observable(observable &&other) noexcept : pimpl(std::make_unique<detail::observable_value<value_type>>(this))
    {
        tt_axiom(&other != this);
        tt_axiom(other.pimpl);
        pimpl->replace_with(other.pimpl.get());
        std::swap(pimpl, other.pimpl);
    }

    observable &operator=(observable &&other) noexcept
    {
        tt_axiom(pimpl);
        tt_axiom(other.pimpl);
        pimpl->replace_with(other.pimpl.get());
        std::swap(pimpl, other.pimpl);
        // Do not replace the notifier.
        pimpl->notify();
        return *this;
    }

    observable(observable const &other) noexcept :
        pimpl(std::make_unique<detail::observable_chain<value_type>>(this, other.pimpl.get()))
    {
    }

    observable &operator=(observable const &other) noexcept
    {
        tt_return_on_self_assignment(other);
        tt_axiom(other.pimpl);
        auto new_pimpl = std::make_unique<detail::observable_chain<value_type>>(this, other.pimpl.get());
        tt_axiom(pimpl);
        pimpl->replace_with(new_pimpl.get());
        pimpl = std::move(new_pimpl);
        pimpl->notify();
        return *this;
    }

    /** Default construct a observable holding a default constructed value.
     */
    observable() noexcept : pimpl(std::make_unique<detail::observable_value<value_type>>(this))
    {
        tt_axiom(pimpl);
    }

    /** Construct a observable holding value.
     * @param value The value to set the internal value to.
     */
    observable(value_type const &value) noexcept : pimpl(std::make_unique<detail::observable_value<value_type>>(this, value))
    {
        tt_axiom(pimpl);
    }

    /** Is the internal value true.
     */
    explicit operator bool() const noexcept
    {
        return static_cast<bool>(load());
    }

    /** Assign a new value.
     *
     * Updates the internal value or the value that is being observed.
     *
     * @param value The value to assign.
     * @return *this.
     * @post Listeners will be notified.
     */
    observable &operator=(value_type const &value) noexcept
    {
        store(value);
        return *this;
    }

    /** Inplace add a value.
     *
     * Updates the internal value or the value that is being observed non-atomically.
     *
     * @param value The value to add.
     * @return *this.
     * @post Listeners will be notified.
     */
    observable &operator+=(value_type const &value) noexcept
    {
        store(load() + value);
        return *this;
    }

    /** Inplace subtract a value.
     *
     * Updates the internal value or the value that is being observed non-atomically.
     *
     * @param value The value to subtract.
     * @return *this.
     * @post Listeners will be notified.
     */
    observable &operator-=(value_type const &value) noexcept
    {
        store(load() - value);
        return *this;
    }

    /** Load the value.
     *
     * Loads the internal or observed value.
     *
     * @return The internal or observed value.
     */
    [[nodiscard]] value_type load() const noexcept
    {
        tt_axiom(pimpl);
        return pimpl->load();
    }

    /** Load the value.
     *
     * Loads the internal or observed value.
     *
     * @return The internal or observed value.
     */
    [[nodiscard]] value_type operator*() const noexcept
    {
        tt_axiom(pimpl);
        return pimpl->load();
    }

    /** Assign a new value.
     *
     * Updates the internal value or the value that is being observed.
     *
     * @param value The value to assign.
     * @return *this.
     * @post Listeners will be notified.
     */
    bool store(value_type const &new_value) noexcept
    {
        tt_axiom(pimpl);
        return pimpl->store(new_value);
    }

    /** Subscribe a callback function.
     *
     * The subscribed callback function will be called when the value is
     * modified.
     *
     * @tparam Callback A callback-type of the form `void()`.
     * @param callback The callback function to subscribe.
     * @return A `std::shared_ptr` to the callback function.
     * @post A `std::weak_ptr` to the callback function is retained by the observable.
     *       When this pointer is expired the callback will be automatically unsubscribed.
     */
    template<typename Callback>
    requires(std::is_invocable_v<Callback>) [[nodiscard]] callback_ptr_type subscribe(Callback &&callback) noexcept
    {
        auto callback_ptr = notifier.subscribe(std::forward<Callback>(callback));
        (*callback_ptr)();
        return callback_ptr;
    }

    /** Subscribe a callback function.
     *
     * The subscribed callback function will be called when the value is
     * modified.
     *
     * @param callback The callback function to subscribe.
     * @return A `std::shared_ptr` to the callback function.
     * @post A `std::weak_ptr` to the callback function is retained by the observable.
     *       When this pointer is expired the callback will be automatically unsubscribed.
     */
    callback_ptr_type subscribe(callback_ptr_type const &callback) noexcept
    {
        return notifier.subscribe(callback);
    }

    /** Unsubscribe a callback function.
     *
     * @param callback The callback function to subscribe.
     */
    void unsubscribe(callback_ptr_type const &callback_ptr) noexcept
    {
        return notifier.unsubscribe(callback_ptr);
    }

    /** Negate and return the value.
     *
     * @return The negative of the internal or observed value.
     */
    [[nodiscard]] value_type operator-() const noexcept
    {
        return -*(*this);
    }

    /** Compare the value of two observables.
     *
     * @param lhs An observable-value.
     * @param rhs An observable-value.
     * @return True if the internal or observed value of both lhs and rhs are equal.
     */
    [[nodiscard]] friend bool operator==(observable const &lhs, observable const &rhs) noexcept
    {
        return *lhs == *rhs;
    }

    /** Compare and observable with a value.
     *
     * @param lhs An observable-value.
     * @param rhs A value.
     * @return True if the internal or observed value of lhs is equal to rhs.
     */
    [[nodiscard]] friend bool operator==(observable const &lhs, value_type const &rhs) noexcept
    {
        return *lhs == rhs;
    }

    /** Compare and observable with a value.
     *
     * @param lhs A value.
     * @param rhs An observable-value.
     * @return True if lhs is equal to the internal or observed value of rhs.
     */
    [[nodiscard]] friend bool operator==(value_type const &lhs, observable const &rhs) noexcept
    {
        return lhs == *rhs;
    }

    /** Compare the value of two observables.
     *
     * @param lhs An observable-value.
     * @param rhs An observable-value.
     * @return True the comparison result of the internal or observed value of both lhs and rhs.
     */
    [[nodiscard]] friend auto operator<=>(observable const &lhs, observable const &rhs) noexcept
    {
        return *lhs <=> *rhs;
    }

    /** Compare and observable with a value.
     *
     * @param lhs An observable-value.
     * @param rhs A value.
     * @return The comparison result between the internal or observed value of lhs and the rhs.
     */
    [[nodiscard]] friend auto operator<=>(observable const &lhs, value_type const &rhs) noexcept
    {
        return *lhs <=> rhs;
    }

    /** Compare and observable with a value.
     *
     * @param lhs A value.
     * @param rhs An observable-value.
     * @return The comparison result between the lhs and to the internal or observed value of rhs.
     */
    [[nodiscard]] friend auto operator<=>(value_type const &lhs, observable const &rhs) noexcept
    {
        return lhs <=> *rhs;
    }

    /** Add the value of two observables.
     *
     * @param lhs An observable-value
     * @param rhs An observable-value
     * @return The sum of the internal or observed value of lhs and rhs.
     */
    [[nodiscard]] friend auto operator+(observable const &lhs, observable const &rhs) noexcept
    {
        return *lhs + *rhs;
    }

    /** Add the value of observables to a value.
     *
     * @param lhs An observable-value
     * @param rhs A value
     * @return The sum of the internal or observed value of lhs and rhs.
     */
    [[nodiscard]] friend auto operator+(observable const &lhs, value_type const &rhs) noexcept
    {
        return *lhs + rhs;
    }

    /** Add the value of observables to a value.
     *
     * @param lhs A value
     * @param rhs An observable-value
     * @return The sum of lhs with the internal or observed value of rhs.
     */
    [[nodiscard]] friend auto operator+(value_type const &lhs, observable const &rhs) noexcept
    {
        return lhs + *rhs;
    }

    /** Subtract the value of two observables.
     *
     * @param lhs An observable-value
     * @param rhs An observable-value
     * @return The difference between of the internal or observed value of lhs and rhs.
     */
    [[nodiscard]] friend auto operator-(observable const &lhs, observable const &rhs) noexcept
    {
        return *lhs - *rhs;
    }

    /** Subtract a value from an observable to a value.
     *
     * @param lhs An observable-value
     * @param rhs A value
     * @return The different between the internal or observed value of lhs and rhs.
     */
    [[nodiscard]] friend auto operator-(observable const &lhs, value_type const &rhs) noexcept
    {
        return *lhs - rhs;
    }

    /** Subtract an observable value from a value.
     *
     * @param lhs A value
     * @param rhs An observable-value
     * @return The difference between the lhs and the internal or observed value of rhs.
     */
    [[nodiscard]] friend auto operator-(value_type const &lhs, observable const &rhs) noexcept
    {
        return lhs - *rhs;
    }

protected:
    void notify() const noexcept
    {
        notifier();
    }

private:
    using pimpl_type = detail::observable_base<value_type>;

    notifier_type notifier;
    std::unique_ptr<pimpl_type> pimpl;

    friend class detail::observable_base<value_type>;
};

/** The value_type of an observable from the constructor argument type.
 * The argument type of an observable is the value_type or an observable<value_type>.
 * This is mostly used for creating CTAD guides.
 */
template<typename T>
struct observable_argument {
    using type = T;
};

template<typename T>
struct observable_argument<observable<T>> {
    using type = typename observable<T>::value_type;
};

template<typename T>
using observable_argument_t = typename observable_argument<T>::type;

} // namespace tt
