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

    auto* data(this auto& self)              { return self.is_heap() ? self.heap_ : self.inline_; }
    auto& operator[](this auto& self, size_t i) { return self.data()[i]; }

    size_t size() const { return size_; }

    auto* begin(this auto& self) { return self.data(); }
    auto* end(this auto& self)   { return self.data() + self.size_; }
private:
    bool is_heap() const { return size_ > N; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "small_vector.tpp"

#endif  // SMALL_VECTOR_h
