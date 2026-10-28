#include <array>
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <iterator>
#include <concepts>
#include <type_traits>

template <typename T>
struct circular_buffer_64
{   constexpr circular_buffer_64() noexcept = default;

    [[nodiscard]]
    constexpr bool empty() const noexcept
    {   return size_ == 0;
    }
    [[nodiscard]]
    constexpr bool full() const noexcept
    {   return size_ == CAPACITY;
    }
    [[nodiscard]]
    constexpr std::size_t size() const noexcept
    {   return size_;
    }
    [[nodiscard]]
    constexpr std::size_t capacity() const noexcept
    {   return CAPACITY;
    }

    constexpr void push(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {   buffer_[tail_] = std::move(value);
        advance_tail();
    }

    constexpr void push(const T& value)
    {   buffer_[tail_] = value;
        advance_tail();
    }

    [[nodiscard]]
    constexpr T pop()
    {   if (empty()) throw std::underflow_error("Buffer empty");
        T value = std::move(buffer_[head_]);
        head_ = (head_ + 1) & MASK;
        --size_;
        return value;
    }

    // Direct element access with internal wrapping logic
    [[nodiscard]]
    constexpr T& operator[](std::size_t index) noexcept
    {   return buffer_[(head_ + index) & MASK];
    }

    [[nodiscard]]
    constexpr const T& operator[](std::size_t index) const noexcept
    {   return buffer_[(head_ + index) & MASK];
    }

    // --- Iterator Implementation ---
    template <bool IsConst>
    struct Iterator
    {   using iterator_category = std::random_access_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using pointer           = std::conditional_t<IsConst, const T*, T*>;
        using reference         = std::conditional_t<IsConst, const T&, T&>;
        using ContainerPtr      = std::conditional_t<IsConst, const circular_buffer_64*, circular_buffer_64*>;

    public:
        constexpr Iterator() noexcept = default;

        constexpr Iterator(ContainerPtr container, std::size_t logical_index) noexcept
        :   container_(container)
        ,   logical_index_(logical_index)
        {}

        // Allow conversion from non-const to const iterator
        template <bool OtherConst>
            requires (IsConst && !OtherConst)
        constexpr Iterator(const Iterator<OtherConst>& other) noexcept
        :   container_(other.container_)
        ,   logical_index_(other.logical_index_)
        {}

        constexpr reference operator*() const noexcept
        {   return (*container_)[logical_index_];
        }

        constexpr pointer operator->() const noexcept
        {   return &((*container_)[logical_index_]);
        }

        constexpr reference operator[](difference_type n) const noexcept
        {   return *(*this + n);
        }

        // Increment / Decrement
        constexpr Iterator& operator++() noexcept
        {   ++logical_index_; return *this;
        }
        constexpr Iterator operator++(int) noexcept
        {   Iterator tmp = *this; ++logical_index_; return tmp;
        }
        constexpr Iterator& operator--() noexcept
        {   --logical_index_; return *this;
        }
        constexpr Iterator operator--(int) noexcept
        {   Iterator tmp = *this; --logical_index_; return tmp;
        }

        // Arithmetic
        constexpr Iterator& operator+=(difference_type n) noexcept
        {   logical_index_ += n; return *this;
        }
        constexpr Iterator& operator-=(difference_type n) noexcept
        {   logical_index_ -= n; return *this;
        }

        constexpr friend
        Iterator operator+(Iterator it, difference_type n) noexcept
        {   return it += n;
        }
        constexpr friend
        Iterator operator+(difference_type n, Iterator it) noexcept
        {   return it += n;
        }
        constexpr friend
        Iterator operator-(Iterator it, difference_type n) noexcept
        {   return it -= n;
        }
        
        constexpr friend
        difference_type operator-
        (   const Iterator& lhs
        ,   const Iterator& lhs_other
        ) noexcept
        {   return static_cast<difference_type>(lhs.logical_index_)
            -   static_cast<difference_type>(lhs_other.logical_index_);
        }

        // Comparison operators (Fully support modern C++ spaceship operator semantics)
        constexpr friend bool operator==
        (   const Iterator& lhs
        ,   const Iterator& rhs
        ) noexcept
        {   return lhs.logical_index_ == rhs.logical_index_;
        }
        constexpr friend auto operator<=>
        (   const Iterator& lhs
        ,   const Iterator& rhs
        ) noexcept
        {   return lhs.logical_index_ <=> rhs.logical_index_;
        }

        friend class Iterator<!IsConst>; // Grant friendship to counterpart

    private:
        ContainerPtr container_ = nullptr;
        std::size_t logical_index_ = 0; // 0 to size()
    };

    using iterator       = Iterator<false>;
    using const_iterator = Iterator<true>;

    [[nodiscard]]
    constexpr iterator begin() noexcept
    {   return iterator(this, 0);
    }
    [[nodiscard]]
    constexpr iterator end() noexcept
    {   return iterator(this, size_);
    }

    [[nodiscard]]
    constexpr const_iterator begin() const noexcept
    {   return const_iterator(this, 0);
    }
    [[nodiscard]]
    constexpr const_iterator end() const noexcept
    {   return const_iterator(this, size_);
    }
    [[nodiscard]]
    constexpr const_iterator cbegin() const noexcept
    {   return const_iterator(this, 0);
    }
    [[nodiscard]]
    constexpr const_iterator cend() const noexcept
    {   return const_iterator(this, size_);
    }

private:
    static constexpr std::size_t CAPACITY = 64;
    static constexpr std::size_t MASK = CAPACITY - 1;

    std::array<T, CAPACITY> buffer_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;

    constexpr void advance_tail() noexcept
    {   tail_ = (tail_ + 1) & MASK;
        if (full()) {
            head_ = tail_; 
        } else {
            ++size_;
        }
    }
};
