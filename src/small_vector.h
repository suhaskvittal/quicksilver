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
 * `SmallVector<T, N>` is a small-buffer-optimized array.
 *
 * Elements 0..N-1 are stored inline (no heap allocation).
 * For size > N, a heap buffer is allocated. The discriminant
 * is `size_ > N`.
 *
 * T must be trivially copyable (e.g., an integer type).
 * */
template <class T, size_t N>
class SmallVector
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
    SmallVector(size_t size);

    SmallVector(std::initializer_list<T> il);
    SmallVector(const SmallVector& other);
    SmallVector& operator=(const SmallVector& other);

    template <class Iter>
    SmallVector(Iter begin, Iter end);

    ~SmallVector();

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
