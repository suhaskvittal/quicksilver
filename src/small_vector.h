/*
 *  author: Suhas Vittal
 *  date:   7 March 2026
 * */

#ifndef SMALL_VECTOR_h
#define SMALL_VECTOR_h

#include <cstddef>
#include <cstring>
#include <initializer_list>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `small_vector<T, N>` is a small-buffer-optimized array.
 *
 * Elements 0..N-1 are stored inline (no heap allocation).
 * For size > N, a heap buffer is allocated. The discriminant
 * is `size_ > N`.
 *
 * T must be trivially copyable (e.g., an integer type).
 * */
template <class T, size_t N>
class small_vector
{
private:
    size_t size_{0};
    union
    {
        T  inline_[N];
        T* heap_;
    };
public:
    /*
     * Initialize with given size. Values of container
     * are not cleared out.
     * */
    small_vector(size_t size);

    small_vector(std::initializer_list<T> il);
    small_vector(const small_vector& other);
    small_vector& operator=(const small_vector& other);

    template <class ITER>
    small_vector(ITER begin, ITER end);

    ~small_vector();

    T*       data()       { return is_heap() ? heap_ : inline_; }
    const T* data() const { return is_heap() ? heap_ : inline_; }

    T& operator[](size_t i)       { return data()[i]; }
    T  operator[](size_t i) const { return data()[i]; }

    size_t size() const { return size_; }

    T*       begin()       { return data(); }
    T*       end()         { return data() + size_; }
    const T* begin() const { return data(); }
    const T* end()   const { return data() + size_; }
private:
    bool is_heap() const { return size_ > N; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "small_vector.tpp"

#endif  // SMALL_VECTOR_h
