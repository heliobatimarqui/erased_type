//
// Created by Helio on 10/29/2021.
//

#ifndef ERASED_TYPE_ERASED_TYPE_H
#define ERASED_TYPE_ERASED_TYPE_H

#include <cstddef>
#include <typeinfo>
#include <cstdint>
#include <memory>
#include <exception>

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

template<size_t BUFFER_SIZE = 32>
class erased_type {
    struct manager_base {
        virtual const std::type_info& type() const = 0;
        virtual void destroy_fn(erased_type&) = 0;
        virtual void copy_fn(const erased_type&, erased_type&) = 0;
        virtual void* get_pointer_to_val(erased_type& et) = 0;
        virtual const void* get_pointer_to_val(const erased_type& et) = 0;
    };

    template<typename T>
    struct manager : manager_base {
        template<typename ...Args>
        auto& create_fn(erased_type& et, Args&&... args) {
            auto aligned_ptr = static_cast<T*>(get_pointer_to_val(et));
            new(aligned_ptr)T(std::forward<Args...>(args...));
            return *aligned_ptr;
        }

        void destroy_fn(erased_type& et) override {
            static_cast<T*>(get_pointer_to_val(et))->~T();
        }

        void copy_fn(const erased_type& from, erased_type& to) override {
            const T& val = *static_cast<const T*>(get_pointer_to_val(from));
            create_fn(to, val);
        }

        void* get_pointer_to_val(erased_type& et) override {
            const auto& as_const = et;
            return const_cast<void*>(get_pointer_to_val(as_const));
        }

        const void* get_pointer_to_val(const erased_type& et) override {
            auto space = BUFFER_SIZE;
            void* buffer = const_cast<std::decay_t<decltype(et.m_buffer)>>(et.m_buffer);
            auto aligned_ptr = std::align(alignof(T), sizeof(T), buffer, space);
            return aligned_ptr;
        }

        const std::type_info& type() const override {
            return typeid(T);
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
        emplace<T>(std::forward<decltype(val)>(val));
    }

    erased_type(const erased_type& other) {
        if(other.has_value()) {
            other.m_manager->copy_fn(other, *this);
            m_manager = other.m_manager;
        }
    }

    erased_type(erased_type&& other) noexcept {
        swap_values(other);
        other.reset();
    }

    ~erased_type() {
        reset();
    }

    void swap_values(erased_type& other) noexcept {
        auto temp_buffer = std::make_unique<std::byte[]>(BUFFER_SIZE);
        std::memcpy(temp_buffer, m_buffer, buffer_size);
        std::memcpy(m_buffer, other.m_buffer, buffer_size);
        std::memcpy(other.m_buffer, temp_buffer, buffer_size);
        std::swap(m_buffer, other.m_buffer);
    }

    template<typename T, typename ...Args>
    auto& emplace(Args&&... args) {
        static_assert(BUFFER_SIZE >= sizeof(T), "Buffer size is smaller than type size.");
        reset();
        auto manager_ptr = manager<std::decay_t<T>>::instance();
        auto& val_ref = manager_ptr->create_fn(*this, std::forward<Args...>(args)...);
        m_manager = manager_ptr;
        return val_ref;
    }

    void reset() {
        if(has_value()) {
            m_manager->destroy_fn(*this);
            m_manager = nullptr;
        }
    };

    bool has_value() const {
        return m_manager != nullptr;
    }

    const std::type_info& type() const noexcept {
        if(m_manager != nullptr)
            return m_manager->type();
        else
            return typeid(void);
    }

    static constexpr size_t buffer_size() {
        return BUFFER_SIZE;
    }

    template<typename T>
    std::decay_t<T>& operator=(T&& val) {
        return emplace<T>(std::forward<T>(val));
    }

};


template<typename T, typename ET, typename = typename std::enable_if<std::is_same_v<std::decay_t<ET>, erased_type<std::decay_t<ET>::buffer_size()>>, void>::type, typename = typename std::enable_if<std::is_same_v<T, std::decay_t<T>>, void>::type>
auto erased_type_cast(ET&& et) -> if_else_t<std::is_rvalue_reference_v<decltype(et)>, T, if_else_t<std::is_const_v<std::remove_reference_t<decltype(et)>>, const T&, T&>> {
    if (typeid(T) != et.type()) throw bad_erased_type_cast();

    if constexpr (std::is_rvalue_reference_v<decltype(et)>) {
        return std::move(*static_cast<T*>(et.m_manager->get_pointer_to_val(et)));
    }
    else if constexpr (std::is_const_v<std::remove_reference_t<decltype(et)>>) {
        return *static_cast<const T*>(et.m_manager->get_pointer_to_val(et));
    }
    else if constexpr(!std::is_const_v<std::remove_reference_t<decltype(et)>>){
        return *static_cast<T*>(et.m_manager->get_pointer_to_val(et));
    }
}


#endif //ERASED_TYPE_ERASED_TYPE_H
