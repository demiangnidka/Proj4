#pragma once

#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <type_traits>

template <class M>
using ReadLock = std::conditional_t<std::is_same_v<M, std::shared_mutex>,
                                    std::shared_lock<M>, std::unique_lock<M>>;

template <class M>
struct alignas(64) Field {
    mutable M m;
    int v = 0;
};

template <class M0, class M1>
class FieldsData {
public:
    int get(int i) const {
        switch (i) {
            case 0: { ReadLock<M0> l(f0_.m); return f0_.v; }
            case 1: { ReadLock<M1> l(f1_.m); return f1_.v; }
        }
        throw std::out_of_range("field index");
    }

    void set(int i, int x) {
        switch (i) {
            case 0: { std::unique_lock<M0> l(f0_.m); f0_.v = x; return; }
            case 1: { std::unique_lock<M1> l(f1_.m); f1_.v = x; return; }
        }
        throw std::out_of_range("field index");
    }

    operator std::string() const {
        int a, b;
        {
            ReadLock<M0> l0(f0_.m);
            ReadLock<M1> l1(f1_.m);
            a = f0_.v;
            b = f1_.v;
        }
        return "(" + std::to_string(a) + ", " + std::to_string(b) + ")";
    }

private:
    Field<M0> f0_;
    Field<M1> f1_;
};

class GlobalData {
public:
    int get(int i) const {
        check(i);
        std::lock_guard<std::mutex> l(m_);
        return v_[i];
    }
    void set(int i, int x) {
        check(i);
        std::lock_guard<std::mutex> l(m_);
        v_[i] = x;
    }
    operator std::string() const {
        int a, b;
        {
            std::lock_guard<std::mutex> l(m_);
            a = v_[0];
            b = v_[1];
        }
        return "(" + std::to_string(a) + ", " + std::to_string(b) + ")";
    }

private:
    static void check(int i) {
        if (i < 0 || i > 1) throw std::out_of_range("field index");
    }
    mutable std::mutex m_;
    int v_[2] = {0, 0};
};
