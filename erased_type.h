/*
MIT License

Copyright (c) 2021 Helio Nunes Santos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef ERASED_TYPE_ERASED_TYPE_H
#define ERASED_TYPE_ERASED_TYPE_H

#include <cstddef>
#include <typeinfo>
#include <cstdint>
#include <memory>
#include <exception>
#include <stdexcept>
#include <utility>

template<bool cond, typename T, typename U>
struct if_else_type {
    using type = T;
};

template<typename T, typename U>
struct if_else_type<false, T, U> {
    using type = U;
};

template<bool condition, typename A, typename B>
using if_else_t = typename if_else_type<condition, A, B>::type;

class bad_erased_type_cast : public std::exception {
public:
    bad_erased_type_cast() = default;
    ~bad_erased_type_cast() override = default;
    const char* what() const noexcept override {
        return "bad erased_type cast";
    }
};

template<size_t BUFFER_SIZE>
class erased_type {
    static_assert(BUFFER_SIZE % alignof(void*) == 0, "BUFFER SIZE % alignof(void*) has to be 0");

    struct manager_base {
        virtual bool can_T_be_memcopied() = 0;
        virtual void destroy_fn(erased_type&) = 0;
        virtual void copy_fn(const erased_type& from, erased_type& to) = 0;
        virtual void move_fn(erased_type& from, erased_type& to) = 0;
        virtual const std::type_info& T_type_info() const = 0;
        virtual void* get_pointer_to_val(erased_type& et) = 0;
        virtual const void* get_pointer_to_val(const erased_type& et) = 0;

    };

    template<typename T>
    struct manager : manager_base {
        template<typename ...Args>
        auto& create_fn(erased_type& et, Args&&... args) {
            auto aligned_ptr = static_cast<T*>(get_pointer_to_val(et));
            new(aligned_ptr)T(std::forward<Args>(args)...);
            return *aligned_ptr;
        }

        void destroy_fn(erased_type& et) override {
            static_cast<T*>(get_pointer_to_val(et))->~T();
        }

        void copy_fn(const erased_type& from, erased_type& to) override {
            const T& val = *static_cast<const T*>(get_pointer_to_val(from));
            create_fn(to, val);
        }

        void move_fn(erased_type& from, erased_type& to) override {
            T* val_from = static_cast<T*>(get_pointer_to_val(from));
            create_fn(to, std::move(*val_from));
        }

        void* get_pointer_to_val(erased_type& et) override {
            const auto& as_const = et;
            return const_cast<void*>(get_pointer_to_val(as_const));
        }

        const void* get_pointer_to_val(const erased_type& et) override {
            static_assert(sizeof(T) <= BUFFER_SIZE, "Buffer can't fit T.");
            auto space = BUFFER_SIZE;
            void* buffer = const_cast<std::decay_t<decltype(et.m_buffer)>>(et.m_buffer);
            auto aligned_ptr = std::align(alignof(T), sizeof(T), buffer, space);
            if(aligned_ptr == nullptr)
                throw std::runtime_error("Couldn't get aligned pointer to T");
            return aligned_ptr;
        }

        const std::type_info& T_type_info() const override {
            return typeid(T);
        }

        bool can_T_be_memcopied() override {
            return std::is_trivially_copyable_v<T>;
        }

        static manager* instance() {
            static manager m;
            return &m;
        }
    };

    template<typename T, typename ET, typename, typename>
    friend auto erased_type_cast(ET&& et) -> if_else_t<std::is_rvalue_reference_v<decltype(et)>, T, if_else_t<std::is_const_v<std::remove_reference_t<decltype(et)>>, const T&, T&>>;

    std::byte m_buffer[BUFFER_SIZE];
    manager_base* m_manager = nullptr;
public:
    erased_type() = default;

    template<typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, erased_type>>>
    erased_type(T&& val) {
        emplace<std::decay_t<T>>(std::forward<decltype(val)>(val));
    }

    template<typename T, typename ...Args>
    erased_type(std::in_place_type_t<T>, Args&&... args) {
        emplace<T>(std::forward<Args>(args)...);
    }

    erased_type(const erased_type& other) {
        *this = other;
    }

    erased_type(erased_type&& other) noexcept {
        *this = std::move(other);
    }

    ~erased_type() {
        reset();
    }

    void swap(erased_type& other) noexcept {
        if ((!has_value() || m_manager->can_T_be_memcopied()) && (!other.has_value() || other.m_manager->can_T_be_memcopied())) {
            std::byte temp_buffer[BUFFER_SIZE];
            std::memcpy(temp_buffer, m_buffer, BUFFER_SIZE);
            std::memcpy(m_buffer, other.m_buffer, BUFFER_SIZE);
            std::memcpy(other.m_buffer, temp_buffer, BUFFER_SIZE);
            std::swap(m_manager, other.m_manager);
        }
        else {
            erased_type temp = std::move(*this);
            *this = std::move(other);
            other = std::move(temp);
        }
    }

    template<typename T, typename ...Args>
    auto& emplace(Args&&... args) {
        reset();
        auto manager_ptr = manager<std::decay_t<T>>::instance();
        auto& val_ref = manager_ptr->create_fn(*this, std::forward<Args...>(args)...);
        m_manager = manager_ptr;
        return val_ref;
    }

    void reset() {
        if(this->has_value()) {
            m_manager->destroy_fn(*this);
            m_manager = nullptr;
        }
    };

    bool has_value() const noexcept {
        return m_manager != nullptr;
    }

    void* get_pointer_to_val() {
        const auto& as_const = *this;
        return const_cast<void*>(as_const.get_pointer_to_val());
    }

    const void* get_pointer_to_val() const {
        if(has_value())
            return m_manager->get_pointer_to_val(*this);
        return nullptr;
    }

    const std::type_info& type() const noexcept {
        if(this->has_value())
            return m_manager->T_type_info();
        else
            return typeid(void);
    }

    static constexpr size_t buffer_size() {
        return BUFFER_SIZE;
    }

    template<typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, erased_type>, void>>
    erased_type& operator=(T&& val) {
        emplace<T>(std::forward<T>(val));
        return *this;
    }

    erased_type& operator=(const erased_type& other) {
        if(this != &other) {
            this->reset();
            if(other.has_value()) {
                other.m_manager->copy_fn(other, *this);
                m_manager = other.m_manager;
            }
        }
        return *this;
    }

    erased_type& operator=(erased_type&& other) noexcept {
        if(this != &other) {
            this->reset();
            if(other.has_value()) {
                other.m_manager->move_fn(other, *this);
                this->m_manager = other.m_manager;
            }
        }
        return *this;
    }
};

template<typename T, typename ET, typename = typename std::enable_if<std::is_same_v<std::decay_t<ET>, erased_type<std::decay_t<ET>::buffer_size()>>, void>::type, typename = typename std::enable_if<std::is_same_v<T, std::decay_t<T>>, void>::type>
auto erased_type_cast(ET&& et) -> if_else_t<std::is_rvalue_reference_v<decltype(et)>, T, if_else_t<std::is_const_v<std::remove_reference_t<decltype(et)>>, const T&, T&>> {
    if (typeid(T) != et.type() || !et.has_value()) throw bad_erased_type_cast();

    if constexpr (std::is_rvalue_reference_v<decltype(et)>) {
        return std::move(*static_cast<T*>(et.get_pointer_to_val()));
    }
    else if constexpr (std::is_const_v<std::remove_reference_t<decltype(et)>>) {
        return *static_cast<const T*>(et.get_pointer_to_val());
    }
    else if constexpr(!std::is_const_v<std::remove_reference_t<decltype(et)>>){
        return *static_cast<T*>(et.get_pointer_to_val());
    }
}

template<typename T, typename ...Args>
auto make_erased_type(Args&&... args) {
    return erased_type<sizeof(T) + sizeof(T)%alignof(void*)>(std::forward<Args>(args)...);
};


#endif //ERASED_TYPE_ERASED_TYPE_H
