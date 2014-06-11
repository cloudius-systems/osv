/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DEFER_HH_
#define DEFER_HH_

template <typename Func>
class deferred_operation {
public:
    deferred_operation(const deferred_operation&) = default;
    deferred_operation(const Func& func) : _func(func) {}
    ~deferred_operation() {
        if (!_cancelled) {
            _func();
        }
    }
    void cancel() { _cancelled = true; }
private:
    Func _func;
    bool _cancelled = false;
};

template <typename Func>
deferred_operation<Func>
defer(Func func)
{
    return deferred_operation<Func>(func);
}

#endif /* DEFER_HH_ */
